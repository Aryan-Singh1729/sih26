#include "raksh/depth_sampler.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace raksh {
namespace {

float median(std::vector<float> values) {
    if (values.empty()) {
        return 0.0F;
    }

    std::sort(values.begin(), values.end());
    const auto middle = values.size() / 2;
    if ((values.size() % 2U) != 0U) {
        return values[middle];
    }
    return (values[middle - 1] + values[middle]) * 0.5F;
}

void validate_config(const DepthSampleConfig& config) {
    if (!std::isfinite(config.min_distance_m) ||
        !std::isfinite(config.max_distance_m) ||
        config.min_distance_m < 0.0F ||
        config.max_distance_m <= config.min_distance_m) {
        throw std::invalid_argument("invalid depth distance range");
    }
    if (!std::isfinite(config.min_valid_ratio) ||
        config.min_valid_ratio < 0.0F || config.min_valid_ratio > 1.0F) {
        throw std::invalid_argument("min_valid_ratio must be between 0 and 1");
    }
    if (!std::isfinite(config.mad_multiplier) || config.mad_multiplier < 0.0F ||
        !std::isfinite(config.min_outlier_window_m) ||
        config.min_outlier_window_m < 0.0F) {
        throw std::invalid_argument("invalid outlier rejection configuration");
    }
}

}  // namespace

DepthSample robust_depth_sample(const std::vector<float>& depths_m,
                                const DepthSampleConfig& config) {
    validate_config(config);

    DepthSample result;
    result.total_count = depths_m.size();
    if (depths_m.empty()) {
        return result;
    }

    std::vector<float> valid;
    valid.reserve(depths_m.size());
    for (const float depth : depths_m) {
        if (std::isfinite(depth) && depth >= config.min_distance_m &&
            depth <= config.max_distance_m) {
            valid.push_back(depth);
        }
    }

    result.valid_count = valid.size();
    const float center = median(valid);

    std::vector<float> deviations;
    deviations.reserve(valid.size());
    for (const float depth : valid) {
        deviations.push_back(std::fabs(depth - center));
    }

    const float mad = median(deviations);
    const float window =
        std::max(config.min_outlier_window_m, config.mad_multiplier * mad);

    std::vector<float> inliers;
    inliers.reserve(valid.size());
    for (const float depth : valid) {
        if (std::fabs(depth - center) <= window) {
            inliers.push_back(depth);
        }
    }

    result.inlier_count = inliers.size();
    result.confidence = static_cast<float>(result.inlier_count) /
                        static_cast<float>(result.total_count);
    result.valid = !inliers.empty() && result.confidence >= config.min_valid_ratio;
    if (result.valid) {
        result.distance_m = median(inliers);
    }
    return result;
}

}  // namespace raksh
