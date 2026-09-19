#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace raksh {

enum class AcquisitionState {
    starting,
    live,
    stale,
    camera_missing,
    start_failed,
    frame_timeout,
    runtime_error,
    stopped,
};

const char* to_string(AcquisitionState state) noexcept;

struct DepthFrameDiagnostics {
    std::uint64_t frame_number{0};
    std::uint64_t monotonic_ms{0};
    int width{0};
    int height{0};
    float depth_scale_m{0.0F};
};

struct DepthStateSnapshot {
    AcquisitionState state{AcquisitionState::starting};
    std::optional<DepthFrameDiagnostics> latest_frame;
    std::uint64_t frame_age_ms{0};
    std::string detail;
};

class LatestDepthState {
public:
    explicit LatestDepthState(std::uint64_t stale_after_ms);

    void record_frame(const DepthFrameDiagnostics& frame);
    void record_error(AcquisitionState state, std::string detail);
    void record_stopped(std::string detail = "stopped");
    DepthStateSnapshot snapshot(std::uint64_t now_monotonic_ms) const;

private:
    std::uint64_t stale_after_ms_;
    mutable std::mutex mutex_;
    AcquisitionState state_{AcquisitionState::starting};
    std::optional<DepthFrameDiagnostics> latest_frame_;
    std::string detail_{"waiting for first depth frame"};
};

}  // namespace raksh
