#include "raksh/perception.hpp"

#include <librealsense2/rs.hpp>
#include <librealsense2/rsutil.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
std::atomic<bool> stop_requested{false};
void stop_handler(int) { stop_requested.store(true); }

std::uint64_t monotonic_ms() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

struct Options {
    std::string serial{"617205001375"};
    // Loopback is the safe default. Use --bind with the UNO Q's LAN address
    // when a laptop must reach the dashboard.
    std::string bind_address{"127.0.0.1"};
    unsigned port{8080};
    unsigned duration_seconds{0};
    std::string web_root{"web"};
};

Options parse_options(int argc, char** argv) {
    Options result;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        auto value = [&]() -> std::string {
            if (++i >= argc) throw std::invalid_argument("missing value for " + argument);
            return argv[i];
        };
        if (argument == "--serial") result.serial = value();
        else if (argument == "--bind") result.bind_address = value();
        else if (argument == "--port") result.port = std::stoul(value());
        else if (argument == "--duration-seconds") result.duration_seconds = std::stoul(value());
        else if (argument == "--web-root") result.web_root = value();
        else throw std::invalid_argument("unknown option: " + argument);
    }
    if (result.port == 0 || result.port > 65535) throw std::invalid_argument("invalid port");
    return result;
}

struct Snapshot {
    std::string state{"starting"};
    std::string detail{"waiting for first frame"};
    std::string telemetry;
    std::uint64_t generation{0};
    std::uint64_t frame{0};
    std::uint64_t timestamp_ms{0};
    double fps{0.0};
};

class LatestState {
public:
    void update(Snapshot value) {
        std::lock_guard<std::mutex> lock(mutex_);
        value.generation = value_.generation + 1;
        value_ = std::move(value);
        condition_.notify_all();
    }

    Snapshot get() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return value_;
    }

    Snapshot wait_after(std::uint64_t generation) {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait_for(lock, std::chrono::seconds(1), [&] {
            return value_.generation != generation || stop_requested.load();
        });
        return value_;
    }

    void wake() { condition_.notify_all(); }

private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    Snapshot value_;
};

std::string telemetry_json(std::uint64_t frame, std::uint64_t timestamp,
                           float display_range, float fov,
                           const std::vector<raksh::DepthCell>& cells,
                           const std::vector<raksh::Ray>& rays,
                           const std::vector<raksh::ObstacleCluster>& clusters) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(4)
        << "{\"schema_version\":1,\"frame_number\":" << frame
        << ",\"monotonic_ms\":" << timestamp
        << ",\"sensor_status\":\"live\",\"stale\":false"
        << ",\"display_range_m\":" << display_range
        << ",\"horizontal_fov_rad\":" << fov << ",\"cells\":[";
    for (std::size_t i = 0; i < cells.size(); ++i) {
        if (i) out << ',';
        const auto& cell = cells[i];
        out << "{\"column\":" << cell.column << ",\"row\":" << cell.row
            << ",\"valid\":" << (cell.valid ? "true" : "false")
            << ",\"distance_m\":";
        if (cell.valid) out << cell.distance_m; else out << "null";
        out << ",\"confidence\":" << cell.confidence
            << ",\"risk\":\"" << raksh::to_string(cell.risk) << "\"}";
    }
    out << "],\"rays\":[";
    for (std::size_t i = 0; i < rays.size(); ++i) {
        if (i) out << ',';
        const auto& ray = rays[i];
        out << "{\"index\":" << ray.index
            << ",\"bearing_rad\":" << ray.bearing_rad
            << ",\"range_m\":";
        if (ray.valid) out << ray.ground_range_m; else out << "null";
        out << ",\"valid\":" << (ray.valid ? "true" : "false")
            << ",\"confidence\":" << ray.confidence
            << ",\"risk\":\"" << raksh::to_string(ray.risk) << "\"}";
    }
    out << "],\"clusters\":[";
    for (std::size_t i = 0; i < clusters.size(); ++i) {
        if (i) out << ',';
        const auto& cluster = clusters[i];
        out << "{\"nearest_m\":" << cluster.nearest_distance_m
            << ",\"bearing_rad\":" << cluster.center_bearing_rad
            << ",\"angular_width_rad\":" << cluster.angular_width_rad << '}';
    }
    out << "]}";
    return out.str();
}

std::string error_telemetry(const Snapshot& snapshot) {
    std::ostringstream out;
    out << "{\"schema_version\":1,\"frame_number\":" << snapshot.frame
        << ",\"monotonic_ms\":" << monotonic_ms()
        << ",\"sensor_status\":\"" << snapshot.state
        << "\",\"stale\":true,\"display_range_m\":5.0"
        << ",\"horizontal_fov_rad\":0,\"cells\":[],\"rays\":[],\"clusters\":[]}";
    return out.str();
}

std::string health_json(const Snapshot& snapshot) {
    const auto now = monotonic_ms();
    const auto age = snapshot.timestamp_ms && now >= snapshot.timestamp_ms
                         ? now - snapshot.timestamp_ms : 0;
    const bool stale = snapshot.state == "live" && age > 1000;
    std::ostringstream out;
    out << std::fixed << std::setprecision(2)
        << "{\"service_state\":\"" << (stale ? "stale" : snapshot.state)
        << "\",\"sr300_state\":\"" << (stale ? "stale" : snapshot.state)
        << "\",\"last_frame_number\":" << snapshot.frame
        << ",\"last_frame_age_ms\":" << age
        << ",\"depth_fps\":" << snapshot.fps
        << ",\"emeet_state\":\"unavailable_until_milestone_6\"}";
    return out.str();
}

bool send_all(int socket, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const auto result = ::send(socket, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (result <= 0) return false;
        sent += static_cast<std::size_t>(result);
    }
    return true;
}

void respond(int socket, const char* status, const char* type, const std::string& body) {
    std::ostringstream response;
    response << "HTTP/1.1 " << status << "\r\nContent-Type: " << type
             << "\r\nAccess-Control-Allow-Origin: *\r\nCache-Control: no-store\r\n"
             << "Content-Length: " << body.size() << "\r\nConnection: close\r\n\r\n"
             << body;
    send_all(socket, response.str());
}

bool read_file(const std::string& path, std::string& content) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::ostringstream buffer;
    buffer << file.rdbuf();
    content = buffer.str();
    return true;
}

bool serve_dashboard_asset(int socket, const std::string& request,
                           const std::string& web_root) {
    struct Asset { const char* route; const char* file; const char* type; };
    static constexpr Asset assets[] = {
        {"GET / ", "index.html", "text/html; charset=utf-8"},
        {"GET /index.html ", "index.html", "text/html; charset=utf-8"},
        {"GET /dashboard.css ", "dashboard.css", "text/css; charset=utf-8"},
        {"GET /dashboard.js ", "dashboard.js", "text/javascript; charset=utf-8"},
    };
    for (const auto& asset : assets) {
        if (request.rfind(asset.route, 0) != 0) continue;
        std::string content;
        const std::string separator = web_root.empty() || web_root.back() == '/' ? "" : "/";
        if (!read_file(web_root + separator + asset.file, content)) {
            respond(socket, "503 Service Unavailable", "application/json",
                    "{\"error\":\"dashboard_asset_unavailable\"}");
        } else {
            respond(socket, "200 OK", asset.type, content);
        }
        return true;
    }
    return false;
}

void handle_client(int socket, std::shared_ptr<LatestState> state,
                   std::string web_root) {
    timeval timeout{2, 0};
    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    char buffer[4096]{};
    const auto count = recv(socket, buffer, sizeof(buffer) - 1, 0);
    if (count <= 0) { close(socket); return; }
    const std::string request(buffer, static_cast<std::size_t>(count));
    if (serve_dashboard_asset(socket, request, web_root)) {
        // Response already sent.
    } else if (request.rfind("GET /health ", 0) == 0) {
        respond(socket, "200 OK", "application/json", health_json(state->get()));
    } else if (request.rfind("GET /events ", 0) == 0) {
        if (!send_all(socket,
            "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
            "Cache-Control: no-cache\r\nConnection: keep-alive\r\n"
            "Access-Control-Allow-Origin: *\r\nX-Accel-Buffering: no\r\n\r\n")) {
            close(socket); return;
        }
        std::uint64_t generation = 0;
        while (!stop_requested.load()) {
            const auto snapshot = state->wait_after(generation);
            if (snapshot.generation == generation) {
                if (!send_all(socket, ": keepalive\n\n")) break;
                continue;
            }
            generation = snapshot.generation;
            const auto& data = snapshot.telemetry.empty()
                                   ? error_telemetry(snapshot) : snapshot.telemetry;
            if (!send_all(socket, "event: telemetry\ndata: " + data + "\n\n")) break;
        }
    } else {
        respond(socket, "404 Not Found", "application/json", "{\"error\":\"not_found\"}");
    }
    close(socket);
}

void capture(std::shared_ptr<LatestState> state, const Options& options) {
    rs2::pipeline pipeline;
    bool started = false;
    try {
        rs2::config requested;
        requested.enable_device(options.serial);
        requested.enable_stream(RS2_STREAM_DEPTH, 640, 480, RS2_FORMAT_Z16, 30);
        const auto profile = pipeline.start(requested);
        started = true;
        const auto stream = profile.get_stream(RS2_STREAM_DEPTH).as<rs2::video_stream_profile>();
        const auto intrinsics = stream.get_intrinsics();
        const float scale = profile.get_device().first<rs2::depth_sensor>().get_depth_scale();
        const float fov = 2.0F * std::atan(static_cast<float>(stream.width()) /
                                           (2.0F * intrinsics.fx));
        raksh::PerceptionConfig config;
        config.ray_count = 48;
        raksh::TemporalRayFilter filter(config.ray_count, config);
        raksh::CameraMount mount;
        std::uint64_t frame_count = 0;
        auto fps_time = std::chrono::steady_clock::now();
        auto next_publish = fps_time;
        double fps = 0.0;

        while (!stop_requested.load()) {
            rs2::frameset frames;
            if (!pipeline.try_wait_for_frames(&frames, 1000))
                throw std::runtime_error("depth frame timeout");
            const auto depth = frames.get_depth_frame();
            if (!depth) continue;
            ++frame_count;
            const auto now = std::chrono::steady_clock::now();
            const auto fps_elapsed = std::chrono::duration<double>(now - fps_time).count();
            if (fps_elapsed >= 1.0) {
                fps = static_cast<double>(frame_count) / fps_elapsed;
                frame_count = 0; fps_time = now;
            }
            if (now < next_publish) continue;
            next_publish = now + std::chrono::milliseconds(100);

            raksh::DepthImage image;
            image.width = depth.get_width(); image.height = depth.get_height();
            image.depth_scale_m = scale;
            image.values.resize(static_cast<std::size_t>(image.width * image.height));
            std::memcpy(image.values.data(), depth.get_data(),
                        image.values.size() * sizeof(std::uint16_t));
            const auto timestamp = monotonic_ms();
            const auto deproject = [&intrinsics](float x, float y, float distance) {
                const float pixel[2]{x, y}; float point[3]{};
                rs2_deproject_pixel_to_point(point, &intrinsics, pixel, distance);
                return raksh::Point3D{point[0], point[1], point[2]};
            };
            const auto cells = raksh::build_depth_cone(image, timestamp, config);
            auto rays = raksh::build_rays(image, timestamp, config, deproject, mount, true);
            rays = filter.update(rays);
            const auto clusters = raksh::cluster_rays(rays, config);
            Snapshot snapshot;
            snapshot.state = "live"; snapshot.detail.clear();
            snapshot.frame = depth.get_frame_number(); snapshot.timestamp_ms = timestamp;
            snapshot.fps = fps;
            snapshot.telemetry = telemetry_json(snapshot.frame, timestamp,
                config.maximum_distance_m, fov, cells, rays, clusters);
            state->update(std::move(snapshot));
        }
    } catch (const std::exception& error) {
        auto snapshot = state->get();
        snapshot.state = started ? "disconnected" : "camera_missing";
        snapshot.detail = error.what(); snapshot.telemetry.clear();
        state->update(std::move(snapshot));
    }
    if (started) { try { pipeline.stop(); } catch (...) {} }
}
}  // namespace

int main(int argc, char** argv) {
    try {
        const auto options = parse_options(argc, argv);
        std::signal(SIGINT, stop_handler);
        std::signal(SIGTERM, stop_handler);
        const int server = socket(AF_INET, SOCK_STREAM, 0);
        if (server < 0) throw std::runtime_error("socket creation failed");
        int reuse = 1;
        setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        sockaddr_in address{};
        address.sin_family = AF_INET; address.sin_port = htons(options.port);
        if (inet_pton(AF_INET, options.bind_address.c_str(), &address.sin_addr) != 1)
            throw std::runtime_error("bind address must be IPv4");
        if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0)
            throw std::runtime_error("bind failed");
        if (listen(server, 16) < 0) throw std::runtime_error("listen failed");
        auto state = std::make_shared<LatestState>();
        std::thread capture_thread(capture, state, std::cref(options));
        std::cerr << "state=serving bind=" << options.bind_address
                  << " port=" << options.port << "\n";
        const auto started = std::chrono::steady_clock::now();
        while (!stop_requested.load()) {
            if (options.duration_seconds &&
                std::chrono::steady_clock::now() - started >=
                    std::chrono::seconds(options.duration_seconds)) break;
            timeval timeout{0, 200000};
            fd_set set; FD_ZERO(&set); FD_SET(server, &set);
            const auto ready = select(server + 1, &set, nullptr, nullptr, &timeout);
            if (ready <= 0) continue;
            const int client = accept(server, nullptr, nullptr);
            if (client >= 0)
                std::thread(handle_client, client, state, options.web_root).detach();
        }
        stop_requested.store(true); state->wake(); close(server);
        capture_thread.join();
        std::cerr << "state=stopped\n";
        return 0;
    } catch (const std::exception& error) {
        stop_requested.store(true);
        std::cerr << "state=runtime_error detail=\"" << error.what() << "\"\n";
        return 1;
    }
}
