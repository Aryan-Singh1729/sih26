#include "raksh/perception.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace raksh {
namespace {
float percentile(std::vector<float> values, float fraction) {
    if (values.empty()) return 0.0F;
    const auto index = static_cast<std::size_t>(
        std::floor(fraction * static_cast<float>(values.size() - 1)));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(index),
                     values.end());
    return values[index];
}

std::vector<float> region(const DepthImage& image, int x0, int y0, int x1,
                          int y1, const PerceptionConfig& config) {
    std::vector<float> result;
    for (int y = std::max(0, y0); y < std::min(image.height, y1); ++y) {
        for (int x = std::max(0, x0); x < std::min(image.width, x1); ++x) {
            const auto raw = image.values[static_cast<std::size_t>(y * image.width + x)];
            const float metres = static_cast<float>(raw) * image.depth_scale_m;
            if (raw != 0 && std::isfinite(metres) &&
                metres >= config.minimum_distance_m &&
                metres <= config.maximum_distance_m) result.push_back(metres);
        }
    }
    return result;
}
}  // namespace

const char* to_string(RiskBand band) noexcept {
    switch (band) {
        case RiskBand::invalid: return "invalid";
        case RiskBand::critical: return "critical";
        case RiskBand::close: return "close";
        case RiskBand::intermediate: return "intermediate";
        case RiskBand::clear: return "clear";
    }
    return "invalid";
}

RiskBand classify_risk(float distance, bool valid, const RiskThresholds& t) {
    if (!valid || !std::isfinite(distance) || distance <= 0) return RiskBand::invalid;
    if (distance <= t.critical_m) return RiskBand::critical;
    if (distance <= t.close_m) return RiskBand::close;
    if (distance <= t.intermediate_m) return RiskBand::intermediate;
    return RiskBand::clear;
}

std::vector<DepthCell> build_depth_cone(const DepthImage& image,
                                        std::uint64_t timestamp,
                                        const PerceptionConfig& config) {
    if (image.width <= 0 || image.height <= 0 || image.depth_scale_m <= 0 ||
        image.values.size() != static_cast<std::size_t>(image.width * image.height))
        throw std::invalid_argument("invalid depth image");
    std::vector<DepthCell> cells;
    cells.reserve(static_cast<std::size_t>(config.grid_columns * config.grid_rows));
    for (int row = 0; row < config.grid_rows; ++row) {
        const int y0 = row * image.height / config.grid_rows;
        const int y1 = (row + 1) * image.height / config.grid_rows;
        for (int column = 0; column < config.grid_columns; ++column) {
            const int x0 = column * image.width / config.grid_columns;
            const int x1 = (column + 1) * image.width / config.grid_columns;
            auto values = region(image, x0, y0, x1, y1, config);
            const auto total = static_cast<float>((x1 - x0) * (y1 - y0));
            DepthCell cell;
            cell.column = column; cell.row = row; cell.monotonic_ms = timestamp;
            cell.confidence = total > 0 ? static_cast<float>(values.size()) / total : 0;
            cell.valid = !values.empty() && cell.confidence >= config.minimum_valid_ratio;
            if (cell.valid) cell.distance_m = percentile(std::move(values), config.near_percentile);
            cell.risk = classify_risk(cell.distance_m, cell.valid, config.risk);
            cells.push_back(cell);
        }
    }
    return cells;
}

std::vector<Ray> build_rays(const DepthImage& image, std::uint64_t timestamp,
                            const PerceptionConfig& config,
                            const Deprojector& deproject,
                            const CameraMount& mount,
                            bool reject_floor) {
    std::vector<Ray> rays;
    rays.reserve(static_cast<std::size_t>(config.ray_count));
    const int y = image.height / 2;
    for (int index = 0; index < config.ray_count; ++index) {
        const float xf = (static_cast<float>(index) + 0.5F) * image.width /
                         static_cast<float>(config.ray_count);
        const int x = std::clamp(static_cast<int>(xf), 0, image.width - 1);
        auto values = region(image, x - config.sample_radius, y - config.sample_radius,
                             x + config.sample_radius + 1,
                             y + config.sample_radius + 1, config);
        const float total = static_cast<float>((2 * config.sample_radius + 1) *
                                               (2 * config.sample_radius + 1));
        Ray ray;
        ray.index = index; ray.monotonic_ms = timestamp;
        ray.confidence = total > 0 ? static_cast<float>(values.size()) / total : 0;
        ray.valid = !values.empty() && ray.confidence >= config.minimum_valid_ratio;
        if (ray.valid) {
            const float distance = percentile(std::move(values), 0.5F);
            const auto point = deproject(static_cast<float>(x), static_cast<float>(y), distance);
            ray.bearing_rad = std::atan2(point.x, point.z);
            ray.ground_range_m = std::sqrt(point.x * point.x + point.z * point.z);
            if (reject_floor &&
                is_floor_return(point, mount, config.floor_tolerance_m)) {
                ray.valid = false;
                ray.floor_filtered = true;
                ray.ground_range_m = 0.0F;
            }
        }
        ray.risk = classify_risk(ray.ground_range_m, ray.valid, config.risk);
        rays.push_back(ray);
    }
    return rays;
}

bool is_floor_return(const Point3D& point, const CameraMount& mount, float tolerance) {
    const float down = point.y * std::cos(mount.pitch_down_rad) +
                       point.z * std::sin(mount.pitch_down_rad);
    return std::fabs(mount.height_m - down) <= tolerance;
}

TemporalRayFilter::TemporalRayFilter(int count, PerceptionConfig config)
    : config_(std::move(config)), previous_(static_cast<std::size_t>(count)),
      initialized_(static_cast<std::size_t>(count), false) {}

std::vector<Ray> TemporalRayFilter::update(const std::vector<Ray>& rays) {
    if (rays.size() != previous_.size()) throw std::invalid_argument("ray count changed");
    auto result = rays;
    for (std::size_t i = 0; i < rays.size(); ++i) {
        if (!rays[i].valid) { initialized_[i] = false; continue; }
        if (!initialized_[i]) { previous_[i] = rays[i]; initialized_[i] = true; continue; }
        const auto old = previous_[i];
        if (rays[i].ground_range_m > old.ground_range_m) {
            result[i].ground_range_m = old.ground_range_m + config_.smoothing_alpha *
                (rays[i].ground_range_m - old.ground_range_m);
            result[i].risk = classify_risk(result[i].ground_range_m, true, config_.risk);
            if (static_cast<int>(rays[i].risk) > static_cast<int>(old.risk) &&
                rays[i].ground_range_m <= old.ground_range_m + config_.risk_hysteresis_m)
                result[i].risk = old.risk;
        }
        previous_[i] = result[i];
    }
    return result;
}

std::vector<ObstacleCluster> cluster_rays(const std::vector<Ray>& rays,
                                          const PerceptionConfig& config) {
    std::vector<ObstacleCluster> clusters;
    ObstacleCluster current;
    bool active = false;
    int gaps = 0;
    for (std::size_t i = 0; i < rays.size(); ++i) {
        if (!rays[i].valid) { if (active) ++gaps; continue; }
        const bool compatible = active && gaps <= config.maximum_cluster_gap_rays &&
            std::fabs(rays[i].ground_range_m - current.nearest_distance_m) <=
                config.cluster_range_tolerance_m;
        if (!compatible) {
            if (active) clusters.push_back(current);
            current = {static_cast<int>(i), static_cast<int>(i), 1,
                       rays[i].ground_range_m, rays[i].bearing_rad, 0};
            active = true;
        } else {
            current.last_ray = static_cast<int>(i); ++current.ray_count;
            current.nearest_distance_m = std::min(current.nearest_distance_m,
                                                  rays[i].ground_range_m);
            current.center_bearing_rad =
                (rays[static_cast<std::size_t>(current.first_ray)].bearing_rad +
                 rays[i].bearing_rad) * 0.5F;
            current.angular_width_rad = rays[i].bearing_rad -
                rays[static_cast<std::size_t>(current.first_ray)].bearing_rad;
        }
        gaps = 0;
    }
    if (active) clusters.push_back(current);
    return clusters;
}

}  // namespace raksh
