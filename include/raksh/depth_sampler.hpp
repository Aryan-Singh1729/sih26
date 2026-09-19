#pragma once

#include <cstddef>
#include <vector>

namespace raksh {

struct DepthSampleConfig {
    float min_distance_m{0.05F};
    float max_distance_m{5.0F};
    float min_valid_ratio{0.50F};
    float mad_multiplier{3.0F};
    float min_outlier_window_m{0.03F};
};

struct DepthSample {
    bool valid{false};
    float distance_m{0.0F};
    std::size_t total_count{0};
    std::size_t valid_count{0};
    std::size_t inlier_count{0};
    float confidence{0.0F};
};

DepthSample robust_depth_sample(const std::vector<float>& depths_m,
                                const DepthSampleConfig& config);

}  // namespace raksh
