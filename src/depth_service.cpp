#include "raksh/depth_sampler.hpp"
#include "raksh/depth_state.hpp"

#include <librealsense2/rs.hpp>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

volatile std::sig_atomic_t stop_requested = 0;

void handle_signal(int) { stop_requested = 1; }

std::uint64_t monotonic_ms() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

struct Options {
    std::string serial{"617205001375"};
    std::uint64_t duration_seconds{0};
    std::uint64_t report_every_ms{1000};
    int sample_radius{5};
};

std::uint64_t parse_unsigned(const std::string& value, const char* option) {
    std::size_t consumed = 0;
    const auto parsed = std::stoull(value, &consumed);
    if (consumed != value.size()) {
        throw std::invalid_argument(std::string("invalid value for ") + option);
    }
    return parsed;
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        auto require_value = [&]() -> std::string {
            if (++index >= argc) {
                throw std::invalid_argument("missing value for " + argument);
            }
            return argv[index];
        };

        if (argument == "--serial") {
            options.serial = require_value();
        } else if (argument == "--duration-seconds") {
            options.duration_seconds =
                parse_unsigned(require_value(), "--duration-seconds");
        } else if (argument == "--report-ms") {
            options.report_every_ms = parse_unsigned(require_value(), "--report-ms");
            if (options.report_every_ms == 0) {
                throw std::invalid_argument("--report-ms must be greater than zero");
            }
        } else if (argument == "--sample-radius") {
            options.sample_radius = static_cast<int>(
                parse_unsigned(require_value(), "--sample-radius"));
            if (options.sample_radius < 1 || options.sample_radius > 50) {
                throw std::invalid_argument("--sample-radius must be between 1 and 50");
            }
        } else if (argument == "--help") {
            std::cout
                << "Usage: raksh_depth_service [options]\n"
                << "  --serial SERIAL           required SR300 serial\n"
                << "  --duration-seconds N      stop after N seconds (0 = until signal)\n"
                << "  --report-ms N             diagnostic interval (default 1000)\n"
                << "  --sample-radius N         region radius in pixels (default 5)\n";
            std::exit(EXIT_SUCCESS);
        } else {
            throw std::invalid_argument("unknown option: " + argument);
        }
    }
    return options;
}

bool device_present(const std::string& serial) {
    rs2::context context;
    const auto devices = context.query_devices();
    for (const auto& device : devices) {
        if (device.supports(RS2_CAMERA_INFO_SERIAL_NUMBER) &&
            serial == device.get_info(RS2_CAMERA_INFO_SERIAL_NUMBER)) {
            return true;
        }
    }
    return false;
}

raksh::DepthSample sample_region(const rs2::depth_frame& frame, int center_x,
                                 int center_y, int radius,
                                 const raksh::DepthSampleConfig& config) {
    std::vector<float> depths;
    const int x_min = std::max(0, center_x - radius);
    const int x_max = std::min(frame.get_width() - 1, center_x + radius);
    const int y_min = std::max(0, center_y - radius);
    const int y_max = std::min(frame.get_height() - 1, center_y + radius);
    depths.reserve(static_cast<std::size_t>((x_max - x_min + 1) *
                                            (y_max - y_min + 1)));
    for (int y = y_min; y <= y_max; ++y) {
        for (int x = x_min; x <= x_max; ++x) {
            depths.push_back(frame.get_distance(x, y));
        }
    }
    return raksh::robust_depth_sample(depths, config);
}

void print_sample(const char* name, const raksh::DepthSample& sample) {
    std::cout << ' ' << name << "_valid=" << (sample.valid ? 1 : 0)
              << ' ' << name << "_distance_m=";
    if (sample.valid) {
        std::cout << std::fixed << std::setprecision(3) << sample.distance_m;
    } else {
        std::cout << "unknown";
    }
    std::cout << ' ' << name << "_confidence=" << std::fixed
              << std::setprecision(3) << sample.confidence;
}

void print_state(const raksh::DepthStateSnapshot& snapshot, double fps,
                 const raksh::DepthSample& left,
                 const raksh::DepthSample& center,
                 const raksh::DepthSample& right) {
    std::cout << "state=" << raksh::to_string(snapshot.state)
              << " frame=" << snapshot.latest_frame->frame_number
              << " monotonic_ms=" << snapshot.latest_frame->monotonic_ms
              << " age_ms=" << snapshot.frame_age_ms
              << " width=" << snapshot.latest_frame->width
              << " height=" << snapshot.latest_frame->height
              << " depth_scale_m=" << std::fixed << std::setprecision(8)
              << snapshot.latest_frame->depth_scale_m << " fps="
              << std::setprecision(2) << fps;
    print_sample("left", left);
    print_sample("center", center);
    print_sample("right", right);
    std::cout << '\n' << std::flush;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    try {
        options = parse_options(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "state=start_failed detail=\"" << error.what() << "\"\n";
        return 2;
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    raksh::LatestDepthState state(500);
    try {
        if (!device_present(options.serial)) {
            state.record_error(raksh::AcquisitionState::camera_missing,
                               "configured SR300 serial not found");
            const auto snapshot = state.snapshot(monotonic_ms());
            std::cerr << "state=" << raksh::to_string(snapshot.state)
                      << " serial=" << options.serial
                      << " detail=\"" << snapshot.detail << "\"\n";
            return 3;
        }
    } catch (const rs2::error& error) {
        state.record_error(raksh::AcquisitionState::start_failed, error.what());
        std::cerr << "state=start_failed detail=\"" << error.what() << "\"\n";
        return 5;
    }

    rs2::pipeline pipeline;
    bool pipeline_started = false;
    try {
        rs2::config configuration;
        configuration.enable_device(options.serial);
        configuration.enable_stream(RS2_STREAM_DEPTH, 640, 480, RS2_FORMAT_Z16,
                                    30);

        const auto profile = pipeline.start(configuration);
        pipeline_started = true;
        const auto depth_profile =
            profile.get_stream(RS2_STREAM_DEPTH).as<rs2::video_stream_profile>();
        if (depth_profile.width() != 640 || depth_profile.height() != 480 ||
            depth_profile.fps() != 30 || depth_profile.format() != RS2_FORMAT_Z16) {
            throw std::runtime_error(
                "active depth profile does not match 640x480@30 Z16");
        }
        const auto intrinsics = depth_profile.get_intrinsics();
        const float depth_scale =
            profile.get_device().first<rs2::depth_sensor>().get_depth_scale();

        std::cout << "state=starting serial=" << options.serial
                  << " profile=640x480@30_Z16"
                  << " fx=" << intrinsics.fx << " fy=" << intrinsics.fy
                  << " ppx=" << intrinsics.ppx << " ppy=" << intrinsics.ppy
                  << " depth_scale_m=" << std::fixed << std::setprecision(8)
                  << depth_scale << '\n'
                  << std::flush;

        raksh::DepthSampleConfig sample_config;
        sample_config.min_distance_m = 0.05F;
        sample_config.max_distance_m = 5.0F;
        sample_config.min_valid_ratio = 0.20F;
        sample_config.mad_multiplier = 3.0F;
        sample_config.min_outlier_window_m = 0.03F;

        const auto start_time = std::chrono::steady_clock::now();
        auto last_report_time = start_time;
        std::uint64_t total_frames = 0;
        std::uint64_t last_report_frames = 0;

        while (stop_requested == 0) {
            if (options.duration_seconds > 0 &&
                std::chrono::steady_clock::now() - start_time >=
                    std::chrono::seconds(options.duration_seconds)) {
                break;
            }

            rs2::frameset frames;
            if (!pipeline.try_wait_for_frames(&frames, 1000)) {
                if (stop_requested != 0) {
                    break;
                }
                state.record_error(raksh::AcquisitionState::frame_timeout,
                                   "no depth frame for 1000 ms; restart required");
                const auto snapshot = state.snapshot(monotonic_ms());
                std::cerr << "state=" << raksh::to_string(snapshot.state)
                          << " detail=\"" << snapshot.detail << "\"\n";
                pipeline.stop();
                pipeline_started = false;
                return 4;
            }

            const auto depth = frames.get_depth_frame();
            if (!depth) {
                continue;
            }

            const auto now_ms = monotonic_ms();
            raksh::DepthFrameDiagnostics diagnostics;
            diagnostics.frame_number = depth.get_frame_number();
            diagnostics.monotonic_ms = now_ms;
            diagnostics.width = depth.get_width();
            diagnostics.height = depth.get_height();
            diagnostics.depth_scale_m = depth_scale;
            state.record_frame(diagnostics);
            ++total_frames;

            const int y = depth.get_height() / 2;
            const auto left = sample_region(depth, depth.get_width() / 4, y,
                                            options.sample_radius, sample_config);
            const auto center = sample_region(depth, depth.get_width() / 2, y,
                                              options.sample_radius, sample_config);
            const auto right = sample_region(depth, 3 * depth.get_width() / 4, y,
                                             options.sample_radius, sample_config);

            const auto now = std::chrono::steady_clock::now();
            const auto report_elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - last_report_time)
                    .count();
            if (report_elapsed >=
                static_cast<std::int64_t>(options.report_every_ms)) {
                const double fps =
                    1000.0 * static_cast<double>(total_frames - last_report_frames) /
                    static_cast<double>(report_elapsed);
                print_state(state.snapshot(now_ms), fps, left, center, right);
                last_report_time = now;
                last_report_frames = total_frames;
            }
        }

        pipeline.stop();
        pipeline_started = false;
        state.record_stopped(stop_requested != 0 ? "signal received"
                                                  : "duration completed");
        const auto snapshot = state.snapshot(monotonic_ms());
        std::cout << "state=" << raksh::to_string(snapshot.state)
                  << " frames=" << total_frames << " detail=\""
                  << snapshot.detail << "\"\n";
        return EXIT_SUCCESS;
    } catch (const rs2::error& error) {
        if (pipeline_started) {
            try {
                pipeline.stop();
            } catch (...) {
            }
        }
        const auto error_state =
            state.snapshot(monotonic_ms()).latest_frame.has_value()
                ? raksh::AcquisitionState::runtime_error
                : raksh::AcquisitionState::start_failed;
        state.record_error(error_state, error.what());
        std::cerr << "state=" << raksh::to_string(error_state)
                  << " function=\"" << error.get_failed_function()
                  << "\" detail=\"" << error.what()
                  << "\" restart_required=1\n";
        return 5;
    } catch (const std::exception& error) {
        if (pipeline_started) {
            try {
                pipeline.stop();
            } catch (...) {
            }
        }
        state.record_error(raksh::AcquisitionState::runtime_error, error.what());
        std::cerr << "state=runtime_error detail=\"" << error.what() << "\"\n";
        return 6;
    }
}
