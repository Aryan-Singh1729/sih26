#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace raksh {

enum class RiskBand { invalid, critical, close, intermediate, clear };

const char* to_string(RiskBand band) noexcept;

struct RiskThresholds {
    float critical_m{0.45F};
    float close_m{0.90F};
    float intermediate_m{1.80F};
};

struct PerceptionConfig {
    int grid_columns{16};
    int grid_rows{6};
    int ray_count{48};
    int sample_radius{5};
    float minimum_valid_ratio{0.20F};
    float near_percentile{0.20F};
    float minimum_distance_m{0.05F};
    float maximum_distance_m{5.0F};
    RiskThresholds risk;
    float floor_tolerance_m{0.04F};
    float smoothing_alpha{0.25F};
    float risk_hysteresis_m{0.05F};
    float cluster_range_tolerance_m{0.25F};
    int maximum_cluster_gap_rays{1};
};

struct DepthImage {
    int width{0};
    int height{0};
    float depth_scale_m{0.0F};
    std::vector<std::uint16_t> values;
};

struct DepthCell {
    int column{0};
    int row{0};
    bool valid{false};
    float distance_m{0.0F};
    float confidence{0.0F};
    RiskBand risk{RiskBand::invalid};
    std::uint64_t monotonic_ms{0};
};

struct Point3D { float x{0}; float y{0}; float z{0}; };
struct CameraIntrinsics {
    int width{0}; int height{0};
    float fx{0}; float fy{0}; float ppx{0}; float ppy{0};
};
struct CameraMount { float height_m{0.40F}; float pitch_down_rad{0.0F}; };

struct Ray {
    int index{0};
    bool valid{false};
    bool floor_filtered{false};
    float bearing_rad{0.0F};
    float ground_range_m{0.0F};
    float confidence{0.0F};
    RiskBand risk{RiskBand::invalid};
    std::uint64_t monotonic_ms{0};
};

struct ObstacleCluster {
    int first_ray{0}; int last_ray{0}; int ray_count{0};
    float nearest_distance_m{0};
    float center_bearing_rad{0};
    float angular_width_rad{0};
};

using Deprojector = std::function<Point3D(float, float, float)>;

RiskBand classify_risk(float distance_m, bool valid,
                       const RiskThresholds& thresholds);
std::vector<DepthCell> build_depth_cone(const DepthImage& image,
                                        std::uint64_t monotonic_ms,
                                        const PerceptionConfig& config);
std::vector<Ray> build_rays(const DepthImage& image,
                            std::uint64_t monotonic_ms,
                            const PerceptionConfig& config,
                            const Deprojector& deproject,
                            const CameraMount& mount = {},
                            bool reject_floor = true);
bool is_floor_return(const Point3D& point, const CameraMount& mount,
                     float tolerance_m);
std::vector<ObstacleCluster> cluster_rays(const std::vector<Ray>& rays,
                                          const PerceptionConfig& config);

class TemporalRayFilter {
public:
    TemporalRayFilter(int ray_count, PerceptionConfig config);
    std::vector<Ray> update(const std::vector<Ray>& rays);
private:
    PerceptionConfig config_;
    std::vector<Ray> previous_;
    std::vector<bool> initialized_;
};

}  // namespace raksh
