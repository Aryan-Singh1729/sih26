#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace raksh {

struct CameraSnapshot {
    std::string state{"starting"};
    std::string detail{"waiting for first frame"};
    std::shared_ptr<const std::vector<std::uint8_t>> jpeg;
    std::uint64_t generation{0};
    std::uint64_t frame_number{0};
    std::uint64_t timestamp_ms{0};
    double fps{0.0};
    unsigned width{0};
    unsigned height{0};
};

class LatestCamera {
public:
    void update(CameraSnapshot value);
    CameraSnapshot get() const;
    CameraSnapshot wait_after(std::uint64_t generation,
                              const std::atomic<bool>& stop) const;
    void wake();

private:
    mutable std::mutex mutex_;
    mutable std::condition_variable condition_;
    CameraSnapshot value_;
};

void capture_emeet(const std::shared_ptr<LatestCamera>& state,
                   const std::string& device_path,
                   unsigned requested_width,
                   unsigned requested_height,
                   unsigned requested_fps,
                   const std::atomic<bool>& stop);

}  // namespace raksh
