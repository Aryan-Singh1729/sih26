#include "raksh/depth_state.hpp"

#include <stdexcept>
#include <utility>

namespace raksh {

const char* to_string(AcquisitionState state) noexcept {
    switch (state) {
        case AcquisitionState::starting:
            return "starting";
        case AcquisitionState::live:
            return "live";
        case AcquisitionState::stale:
            return "stale";
        case AcquisitionState::camera_missing:
            return "camera_missing";
        case AcquisitionState::start_failed:
            return "start_failed";
        case AcquisitionState::frame_timeout:
            return "frame_timeout";
        case AcquisitionState::runtime_error:
            return "runtime_error";
        case AcquisitionState::stopped:
            return "stopped";
    }
    return "runtime_error";
}

LatestDepthState::LatestDepthState(std::uint64_t stale_after_ms)
    : stale_after_ms_(stale_after_ms) {
    if (stale_after_ms == 0) {
        throw std::invalid_argument("stale threshold must be greater than zero");
    }
}

void LatestDepthState::record_frame(const DepthFrameDiagnostics& frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    latest_frame_ = frame;
    state_ = AcquisitionState::live;
    detail_.clear();
}

void LatestDepthState::record_error(AcquisitionState state, std::string detail) {
    if (state == AcquisitionState::live || state == AcquisitionState::stale) {
        throw std::invalid_argument("use record_frame for live/stale state");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = state;
    detail_ = std::move(detail);
}

void LatestDepthState::record_stopped(std::string detail) {
    record_error(AcquisitionState::stopped, std::move(detail));
}

DepthStateSnapshot LatestDepthState::snapshot(
    std::uint64_t now_monotonic_ms) const {
    std::lock_guard<std::mutex> lock(mutex_);
    DepthStateSnapshot result;
    result.state = state_;
    result.latest_frame = latest_frame_;
    result.detail = detail_;

    if (latest_frame_) {
        result.frame_age_ms = now_monotonic_ms >= latest_frame_->monotonic_ms
                                  ? now_monotonic_ms - latest_frame_->monotonic_ms
                                  : 0;
        if (result.state == AcquisitionState::live &&
            result.frame_age_ms > stale_after_ms_) {
            result.state = AcquisitionState::stale;
            result.detail = "latest depth frame exceeded freshness threshold";
        }
    }
    return result;
}

}  // namespace raksh
