#include "raksh/depth_state.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;
int assertions = 0;

void expect(bool condition, const std::string& message) {
    ++assertions;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

raksh::DepthFrameDiagnostics frame(std::uint64_t number,
                                   std::uint64_t monotonic_ms) {
    raksh::DepthFrameDiagnostics value;
    value.frame_number = number;
    value.monotonic_ms = monotonic_ms;
    value.width = 640;
    value.height = 480;
    value.depth_scale_m = 0.000125F;
    return value;
}

void only_the_latest_frame_is_retained() {
    raksh::LatestDepthState state(500);
    state.record_frame(frame(40, 1000));
    state.record_frame(frame(41, 1033));

    const auto snapshot = state.snapshot(1040);
    expect(snapshot.state == raksh::AcquisitionState::live,
           "fresh frame must report live");
    expect(snapshot.latest_frame.has_value(), "latest frame must be present");
    expect(snapshot.latest_frame->frame_number == 41,
           "new frame must overwrite old frame rather than queue it");
    expect(snapshot.frame_age_ms == 7, "frame age must use monotonic time");
}

void old_frames_become_stale() {
    raksh::LatestDepthState state(500);
    state.record_frame(frame(8, 1000));

    const auto snapshot = state.snapshot(1501);
    expect(snapshot.state == raksh::AcquisitionState::stale,
           "frame older than the configured threshold must be stale");
    expect(snapshot.frame_age_ms == 501, "stale snapshot must report age");
}

void explicit_errors_are_preserved() {
    raksh::LatestDepthState state(500);
    state.record_error(raksh::AcquisitionState::camera_missing,
                       "SR300 617205001375 not found");
    auto snapshot = state.snapshot(10);
    expect(snapshot.state == raksh::AcquisitionState::camera_missing,
           "missing camera must be distinguishable");
    expect(snapshot.detail == "SR300 617205001375 not found",
           "missing-camera detail must survive");

    state.record_error(raksh::AcquisitionState::frame_timeout,
                       "no frame for 1000 ms");
    snapshot = state.snapshot(20);
    expect(snapshot.state == raksh::AcquisitionState::frame_timeout,
           "frame timeout must be distinguishable");

    state.record_error(raksh::AcquisitionState::start_failed,
                       "pipeline start failed");
    snapshot = state.snapshot(30);
    expect(snapshot.state == raksh::AcquisitionState::start_failed,
           "pipeline start failure must be distinguishable");
}

void state_names_are_stable_for_diagnostics() {
    expect(raksh::to_string(raksh::AcquisitionState::starting) == "starting",
           "starting state name must be stable");
    expect(raksh::to_string(raksh::AcquisitionState::live) == "live",
           "live state name must be stable");
    expect(raksh::to_string(raksh::AcquisitionState::stale) == "stale",
           "stale state name must be stable");
    expect(raksh::to_string(raksh::AcquisitionState::camera_missing) ==
               "camera_missing",
           "camera missing state name must be stable");
    expect(raksh::to_string(raksh::AcquisitionState::start_failed) ==
               "start_failed",
           "start failure state name must be stable");
    expect(raksh::to_string(raksh::AcquisitionState::frame_timeout) ==
               "frame_timeout",
           "timeout state name must be stable");
    expect(raksh::to_string(raksh::AcquisitionState::runtime_error) ==
               "runtime_error",
           "runtime error state name must be stable");
    expect(raksh::to_string(raksh::AcquisitionState::stopped) == "stopped",
           "stopped state name must be stable");
}

}  // namespace

int main() {
    only_the_latest_frame_is_retained();
    old_frames_become_stale();
    explicit_errors_are_preserved();
    state_names_are_stable_for_diagnostics();

    if (failures != 0) {
        std::cerr << failures << " of " << assertions << " assertions failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: " << assertions << " depth-state assertions\n";
    return EXIT_SUCCESS;
}
