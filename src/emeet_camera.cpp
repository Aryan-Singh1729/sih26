#include "raksh/emeet_camera.hpp"

#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <system_error>

namespace raksh {
namespace {

std::uint64_t now_ms() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

int checked_ioctl(int descriptor, unsigned long request, void* argument) {
    int result;
    do { result = ioctl(descriptor, request, argument); }
    while (result < 0 && errno == EINTR);
    return result;
}

void require_ioctl(int descriptor, unsigned long request, void* argument,
                   const char* operation) {
    if (checked_ioctl(descriptor, request, argument) < 0)
        throw std::system_error(errno, std::generic_category(), operation);
}

struct MappedBuffer { void* data{MAP_FAILED}; std::size_t size{0}; };

}  // namespace

void LatestCamera::update(CameraSnapshot value) {
    std::lock_guard<std::mutex> lock(mutex_);
    value.generation = value_.generation + 1;
    value_ = std::move(value);
    condition_.notify_all();
}

CameraSnapshot LatestCamera::get() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return value_;
}

CameraSnapshot LatestCamera::wait_after(std::uint64_t generation,
                                        const std::atomic<bool>& stop) const {
    std::unique_lock<std::mutex> lock(mutex_);
    condition_.wait_for(lock, std::chrono::seconds(1), [&] {
        return value_.generation != generation || stop.load();
    });
    return value_;
}

void LatestCamera::wake() { condition_.notify_all(); }

void capture_emeet(const std::shared_ptr<LatestCamera>& state,
                   const std::string& device_path,
                   unsigned requested_width,
                   unsigned requested_height,
                   unsigned requested_fps,
                   const std::atomic<bool>& stop) {
    int descriptor = -1;
    bool streaming = false;
    std::vector<MappedBuffer> buffers;
    try {
        descriptor = open(device_path.c_str(), O_RDWR | O_NONBLOCK);
        if (descriptor < 0)
            throw std::system_error(errno, std::generic_category(), "open EMEET camera");

        v4l2_capability capability{};
        require_ioctl(descriptor, VIDIOC_QUERYCAP, &capability, "query EMEET capability");
        if (!(capability.device_caps & V4L2_CAP_VIDEO_CAPTURE) ||
            !(capability.device_caps & V4L2_CAP_STREAMING))
            throw std::runtime_error("EMEET node lacks capture/streaming capability");

        v4l2_format format{};
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        format.fmt.pix.width = requested_width;
        format.fmt.pix.height = requested_height;
        format.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
        format.fmt.pix.field = V4L2_FIELD_ANY;
        require_ioctl(descriptor, VIDIOC_S_FMT, &format, "set EMEET MJPEG format");
        if (format.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG)
            throw std::runtime_error("EMEET did not accept native MJPEG");

        v4l2_streamparm parameters{};
        parameters.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        parameters.parm.capture.timeperframe.numerator = 1;
        parameters.parm.capture.timeperframe.denominator = requested_fps;
        require_ioctl(descriptor, VIDIOC_S_PARM, &parameters, "set EMEET frame rate");

        v4l2_requestbuffers request{};
        request.count = 4;
        request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        request.memory = V4L2_MEMORY_MMAP;
        require_ioctl(descriptor, VIDIOC_REQBUFS, &request, "request EMEET buffers");
        if (request.count < 2) throw std::runtime_error("insufficient EMEET buffers");
        buffers.resize(request.count);

        for (unsigned index = 0; index < request.count; ++index) {
            v4l2_buffer buffer{};
            buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buffer.memory = V4L2_MEMORY_MMAP;
            buffer.index = index;
            require_ioctl(descriptor, VIDIOC_QUERYBUF, &buffer, "query EMEET buffer");
            buffers[index].size = buffer.length;
            buffers[index].data = mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE,
                                       MAP_SHARED, descriptor, buffer.m.offset);
            if (buffers[index].data == MAP_FAILED)
                throw std::system_error(errno, std::generic_category(), "map EMEET buffer");
            require_ioctl(descriptor, VIDIOC_QBUF, &buffer, "queue EMEET buffer");
        }

        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        require_ioctl(descriptor, VIDIOC_STREAMON, &type, "start EMEET stream");
        streaming = true;
        std::uint64_t frame_number = 0;
        std::uint64_t interval_frames = 0;
        double measured_fps = 0.0;
        auto interval_started = std::chrono::steady_clock::now();

        while (!stop.load()) {
            pollfd wait{descriptor, POLLIN, 0};
            const int ready = poll(&wait, 1, 1000);
            if (ready < 0 && errno == EINTR) continue;
            if (ready < 0) throw std::system_error(errno, std::generic_category(), "poll EMEET");
            if (ready == 0) throw std::runtime_error("EMEET frame timeout");
            if (wait.revents & (POLLERR | POLLHUP | POLLNVAL))
                throw std::runtime_error("EMEET disconnected");

            v4l2_buffer buffer{};
            buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buffer.memory = V4L2_MEMORY_MMAP;
            if (checked_ioctl(descriptor, VIDIOC_DQBUF, &buffer) < 0) {
                if (errno == EAGAIN) continue;
                throw std::system_error(errno, std::generic_category(), "dequeue EMEET frame");
            }
            if (buffer.index >= buffers.size())
                throw std::runtime_error("invalid EMEET buffer index");

            const auto* bytes = static_cast<const std::uint8_t*>(buffers[buffer.index].data);
            if (buffer.bytesused >= 4 && bytes[0] == 0xff && bytes[1] == 0xd8) {
                ++frame_number;
                ++interval_frames;
                const auto current = std::chrono::steady_clock::now();
                const double elapsed = std::chrono::duration<double>(current - interval_started).count();
                if (elapsed >= 1.0) {
                    measured_fps = static_cast<double>(interval_frames) / elapsed;
                    interval_frames = 0;
                    interval_started = current;
                }
                CameraSnapshot snapshot;
                snapshot.state = "live";
                snapshot.detail.clear();
                snapshot.jpeg = std::make_shared<const std::vector<std::uint8_t>>(
                    bytes, bytes + buffer.bytesused);
                snapshot.frame_number = frame_number;
                snapshot.timestamp_ms = now_ms();
                snapshot.fps = measured_fps;
                snapshot.width = format.fmt.pix.width;
                snapshot.height = format.fmt.pix.height;
                state->update(std::move(snapshot));
            }
            require_ioctl(descriptor, VIDIOC_QBUF, &buffer, "requeue EMEET buffer");
        }
    } catch (const std::exception& error) {
        CameraSnapshot failure;
        failure.state = streaming ? "disconnected" : "camera_missing";
        failure.detail = error.what();
        state->update(std::move(failure));
    }

    if (streaming) {
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        checked_ioctl(descriptor, VIDIOC_STREAMOFF, &type);
    }
    for (const auto& buffer : buffers)
        if (buffer.data != MAP_FAILED) munmap(buffer.data, buffer.size);
    if (descriptor >= 0) close(descriptor);
}

}  // namespace raksh
