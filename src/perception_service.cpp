#include "raksh/perception.hpp"

#include <librealsense2/rs.hpp>
#include <librealsense2/rsutil.h>

#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
volatile std::sig_atomic_t stop_requested = 0;
void stop_handler(int) { stop_requested = 1; }

std::uint64_t monotonic_ms() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

struct Options {
    std::string serial{"617205001375"};
    unsigned duration_seconds{0};
    unsigned output_hz{10};
    float camera_height_m{0.40F};
    float camera_pitch_deg{0.0F};
    bool reject_floor{true};
};

Options options(int argc, char** argv) {
    Options result;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> std::string {
            if (++i >= argc) throw std::invalid_argument("missing value for " + arg);
            return argv[i];
        };
        if (arg == "--serial") result.serial = value();
        else if (arg == "--duration-seconds") result.duration_seconds = std::stoul(value());
        else if (arg == "--output-hz") result.output_hz = std::stoul(value());
        else if (arg == "--camera-height-m") result.camera_height_m = std::stof(value());
        else if (arg == "--camera-pitch-deg") result.camera_pitch_deg = std::stof(value());
        else if (arg == "--show-floor") result.reject_floor = false;
        else throw std::invalid_argument("unknown option: " + arg);
    }
    if (result.output_hz == 0 || result.output_hz > 30)
        throw std::invalid_argument("output-hz must be between 1 and 30");
    return result;
}

void write_json(std::uint64_t frame, std::uint64_t timestamp,
                const std::vector<raksh::DepthCell>& cells,
                const std::vector<raksh::Ray>& rays,
                const std::vector<raksh::ObstacleCluster>& clusters) {
    std::cout << std::fixed << std::setprecision(4)
              << "{\"schema\":1,\"state\":\"live\",\"frame\":" << frame
              << ",\"monotonic_ms\":" << timestamp << ",\"cells\":[";
    for (std::size_t i = 0; i < cells.size(); ++i) {
        if (i) std::cout << ',';
        const auto& cell = cells[i];
        std::cout << "{\"column\":" << cell.column << ",\"row\":" << cell.row
                  << ",\"valid\":" << (cell.valid ? "true" : "false")
                  << ",\"distance_m\":";
        if (cell.valid) std::cout << cell.distance_m; else std::cout << "null";
        std::cout << ",\"confidence\":" << cell.confidence
                  << ",\"risk\":\"" << raksh::to_string(cell.risk) << "\"}";
    }
    std::cout << "],\"rays\":[";
    for (std::size_t i = 0; i < rays.size(); ++i) {
        if (i) std::cout << ',';
        const auto& ray = rays[i];
        std::cout << "{\"index\":" << ray.index
                  << ",\"valid\":" << (ray.valid ? "true" : "false")
                  << ",\"floor_filtered\":" << (ray.floor_filtered ? "true" : "false")
                  << ",\"bearing_rad\":" << ray.bearing_rad
                  << ",\"range_m\":";
        if (ray.valid) std::cout << ray.ground_range_m; else std::cout << "null";
        std::cout << ",\"confidence\":" << ray.confidence
                  << ",\"risk\":\"" << raksh::to_string(ray.risk) << "\"}";
    }
    std::cout << "],\"clusters\":[";
    for (std::size_t i = 0; i < clusters.size(); ++i) {
        if (i) std::cout << ',';
        const auto& cluster = clusters[i];
        std::cout << "{\"first_ray\":" << cluster.first_ray
                  << ",\"last_ray\":" << cluster.last_ray
                  << ",\"ray_count\":" << cluster.ray_count
                  << ",\"nearest_m\":" << cluster.nearest_distance_m
                  << ",\"bearing_rad\":" << cluster.center_bearing_rad
                  << ",\"width_rad\":" << cluster.angular_width_rad << '}';
    }
    std::cout << "]}\n" << std::flush;
}
}  // namespace

int main(int argc, char** argv) {
    try {
        const auto opts = options(argc, argv);
        std::signal(SIGINT, stop_handler);
        std::signal(SIGTERM, stop_handler);

        rs2::pipeline pipeline;
        rs2::config rs_config;
        rs_config.enable_device(opts.serial);
        rs_config.enable_stream(RS2_STREAM_DEPTH, 640, 480, RS2_FORMAT_Z16, 30);
        const auto profile = pipeline.start(rs_config);
        const auto stream = profile.get_stream(RS2_STREAM_DEPTH).as<rs2::video_stream_profile>();
        const auto intrinsics = stream.get_intrinsics();
        const float scale = profile.get_device().first<rs2::depth_sensor>().get_depth_scale();

        raksh::PerceptionConfig config;
        config.ray_count = 48;
        raksh::CameraMount mount;
        mount.height_m = opts.camera_height_m;
        mount.pitch_down_rad = opts.camera_pitch_deg * 3.14159265358979323846F / 180.0F;
        raksh::TemporalRayFilter filter(config.ray_count, config);
        const auto started = std::chrono::steady_clock::now();
        const auto interval = std::chrono::milliseconds(1000 / opts.output_hz);
        auto next_output = started;

        std::cerr << "state=starting profile=640x480@30_Z16 rays=48 cells=96\n";
        while (!stop_requested) {
            if (opts.duration_seconds && std::chrono::steady_clock::now() - started >=
                    std::chrono::seconds(opts.duration_seconds)) break;
            rs2::frameset frames;
            if (!pipeline.try_wait_for_frames(&frames, 1000))
                throw std::runtime_error("depth frame timeout");
            const auto depth = frames.get_depth_frame();
            if (!depth || std::chrono::steady_clock::now() < next_output) continue;
            next_output = std::chrono::steady_clock::now() + interval;

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
            auto rays = raksh::build_rays(image, timestamp, config, deproject,
                                          mount, opts.reject_floor);
            rays = filter.update(rays);
            const auto clusters = raksh::cluster_rays(rays, config);
            write_json(depth.get_frame_number(), timestamp, cells, rays, clusters);
        }
        pipeline.stop();
        std::cerr << "state=stopped\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "state=runtime_error detail=\"" << error.what() << "\"\n";
        return EXIT_FAILURE;
    }
}
