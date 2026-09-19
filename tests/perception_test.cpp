#include "raksh/perception.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

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

void expect_near(float actual, float expected, float tolerance,
                 const std::string& message) {
    expect(std::isfinite(actual) && std::fabs(actual - expected) <= tolerance,
           message + " expected=" + std::to_string(expected) +
               " actual=" + std::to_string(actual));
}

raksh::PerceptionConfig config() {
    raksh::PerceptionConfig value;
    value.grid_columns = 16;
    value.grid_rows = 6;
    value.ray_count = 64;
    value.sample_radius = 1;
    value.minimum_valid_ratio = 0.20F;
    value.near_percentile = 0.20F;
    value.minimum_distance_m = 0.05F;
    value.maximum_distance_m = 5.0F;
    value.risk = {0.45F, 0.90F, 1.80F};
    value.floor_tolerance_m = 0.04F;
    value.cluster_range_tolerance_m = 0.25F;
    value.maximum_cluster_gap_rays = 1;
    return value;
}

raksh::DepthImage image(int width, int height, std::uint16_t millimetres) {
    raksh::DepthImage result;
    result.width = width;
    result.height = height;
    result.depth_scale_m = 0.001F;
    result.values.assign(static_cast<std::size_t>(width * height), millimetres);
    return result;
}

void depth_cone_has_96_cells_and_preserves_a_narrow_near_obstacle() {
    auto source = image(160, 60, 3000);
    // Cell column 7, row 2 occupies x=[70,80), y=[20,30). Twenty percent
    // of it is close, which an arithmetic mean would largely erase.
    for (int y = 20; y < 30; ++y) {
        for (int x = 70; x < 72; ++x) {
            source.values[static_cast<std::size_t>(y * source.width + x)] = 500;
        }
    }

    const auto cone = raksh::build_depth_cone(source, 1234, config());

    expect(cone.size() == 96, "16x6 cone must always contain 96 cells");
    const auto& cell = cone[2 * 16 + 7];
    expect(cell.valid, "cell with valid depth must be valid");
    expect_near(cell.distance_m, 0.5F, 0.001F,
                "near percentile must retain a narrow obstacle");
    expect(cell.risk == raksh::RiskBand::close,
           "0.5 metre obstacle must be in close band");
    expect(cell.monotonic_ms == 1234, "cell must retain frame timestamp");
}

void zero_depth_cells_are_unknown_not_clear() {
    const auto cone = raksh::build_depth_cone(image(160, 60, 0), 50, config());
    for (const auto& cell : cone) {
        expect(!cell.valid, "zero-only cell must be invalid");
        expect(cell.risk == raksh::RiskBand::invalid,
               "zero-only cell must use invalid risk band");
    }
}

void rays_use_intrinsics_and_preserve_left_right_bearings() {
    const auto source = image(640, 480, 2000);
    raksh::CameraIntrinsics intrinsics;
    intrinsics.width = 640;
    intrinsics.height = 480;
    intrinsics.fx = 600.0F;
    intrinsics.fy = 600.0F;
    intrinsics.ppx = 319.5F;
    intrinsics.ppy = 239.5F;
    const auto deproject = [intrinsics](float x, float y, float depth) {
        return raksh::Point3D{(x - intrinsics.ppx) / intrinsics.fx * depth,
                              (y - intrinsics.ppy) / intrinsics.fy * depth,
                              depth};
    };

    const auto rays = raksh::build_rays(source, 77, config(), deproject);

    expect(rays.size() == 64, "configured ray count must be exact");
    expect(rays.front().bearing_rad < 0.0F, "left ray bearing must be negative");
    expect(rays.back().bearing_rad > 0.0F, "right ray bearing must be positive");
    expect(std::fabs(rays[31].bearing_rad) < 0.02F,
           "central ray bearing must be near zero");
    expect_near(rays[31].ground_range_m, 2.0F, 0.01F,
                "central ground range must remain metric");
}

void risk_bands_are_monotonic_with_distance() {
    const auto thresholds = config().risk;
    expect(raksh::classify_risk(0.30F, true, thresholds) ==
               raksh::RiskBand::critical,
           "closest distance must be critical");
    expect(raksh::classify_risk(0.70F, true, thresholds) ==
               raksh::RiskBand::close,
           "next distance must be close");
    expect(raksh::classify_risk(1.20F, true, thresholds) ==
               raksh::RiskBand::intermediate,
           "next distance must be intermediate");
    expect(raksh::classify_risk(2.50F, true, thresholds) ==
               raksh::RiskBand::clear,
           "farthest distance must be clear");
    expect(raksh::classify_risk(0.0F, false, thresholds) ==
               raksh::RiskBand::invalid,
           "invalid distance must never be clear");
}

void floor_plane_is_rejected_but_a_low_obstacle_is_retained() {
    raksh::CameraMount mount;
    mount.height_m = 0.40F;
    mount.pitch_down_rad = 0.0F;

    expect(raksh::is_floor_return({0.0F, 0.40F, 1.0F}, mount, 0.04F),
           "point on calibrated floor plane must be rejected");
    expect(!raksh::is_floor_return({0.0F, 0.30F, 1.0F}, mount, 0.04F),
           "object ten centimetres above floor must remain visible");
}

void floor_returns_are_removed_from_generated_rays() {
    const auto source = image(64, 48, 1000);
    auto perception_config = config();
    perception_config.ray_count = 4;
    raksh::CameraMount mount;
    mount.height_m = 0.40F;
    const auto floor_point = [](float, float, float depth) {
        return raksh::Point3D{0.0F, 0.40F, depth};
    };

    const auto filtered = raksh::build_rays(
        source, 88, perception_config, floor_point, mount, true);
    const auto unfiltered = raksh::build_rays(
        source, 88, perception_config, floor_point, mount, false);

    expect(!filtered[0].valid && filtered[0].floor_filtered,
           "floor return must be marked and removed from obstacle rays");
    expect(unfiltered[0].valid && !unfiltered[0].floor_filtered,
           "diagnostic unfiltered mode must preserve the same return");
}

void approaching_hazards_bypass_smoothing_and_risk_has_hysteresis() {
    auto perception_config = config();
    perception_config.smoothing_alpha = 0.25F;
    perception_config.risk_hysteresis_m = 0.05F;
    raksh::TemporalRayFilter filter(1, perception_config);

    std::vector<raksh::Ray> rays(1);
    rays[0].valid = true;
    rays[0].ground_range_m = 2.0F;
    rays[0].risk = raksh::RiskBand::clear;
    auto filtered = filter.update(rays);
    expect_near(filtered[0].ground_range_m, 2.0F, 0.001F,
                "first reading must pass through");

    rays[0].ground_range_m = 0.40F;
    rays[0].risk = raksh::RiskBand::critical;
    filtered = filter.update(rays);
    expect_near(filtered[0].ground_range_m, 0.40F, 0.001F,
                "approaching critical hazard must update immediately");
    expect(filtered[0].risk == raksh::RiskBand::critical,
           "approaching hazard must become critical immediately");

    rays[0].ground_range_m = 0.47F;
    rays[0].risk = raksh::RiskBand::close;
    filtered = filter.update(rays);
    expect(filtered[0].risk == raksh::RiskBand::critical,
           "risk must not flicker clear of threshold inside hysteresis margin");
}

raksh::Ray ray(float bearing, float range, bool valid = true) {
    raksh::Ray value;
    value.valid = valid;
    value.bearing_rad = bearing;
    value.ground_range_m = range;
    value.risk = valid ? raksh::RiskBand::close : raksh::RiskBand::invalid;
    return value;
}

void clusters_join_continuous_rays_and_separate_distinct_obstacles() {
    const std::vector<raksh::Ray> rays{
        ray(-0.30F, 1.00F), ray(-0.20F, 1.08F), ray(-0.10F, 1.05F),
        ray(0.00F, 0.0F, false),
        ray(0.10F, 2.00F), ray(0.20F, 2.10F),
    };

    const auto clusters = raksh::cluster_rays(rays, config());

    expect(clusters.size() == 2, "separated obstacles must form two clusters");
    expect(clusters[0].ray_count == 3, "continuous obstacle must join three rays");
    expect_near(clusters[0].nearest_distance_m, 1.00F, 0.001F,
                "cluster must retain nearest distance");
    expect_near(clusters[0].center_bearing_rad, -0.20F, 0.001F,
                "cluster center bearing must span its angular bounds");
    expect(clusters[1].ray_count == 2, "second obstacle must remain separate");
}

}  // namespace

int main() {
    depth_cone_has_96_cells_and_preserves_a_narrow_near_obstacle();
    zero_depth_cells_are_unknown_not_clear();
    rays_use_intrinsics_and_preserve_left_right_bearings();
    risk_bands_are_monotonic_with_distance();
    floor_plane_is_rejected_but_a_low_obstacle_is_retained();
    floor_returns_are_removed_from_generated_rays();
    approaching_hazards_bypass_smoothing_and_risk_has_hysteresis();
    clusters_join_continuous_rays_and_separate_distinct_obstacles();

    if (failures != 0) {
        std::cerr << failures << " of " << assertions << " assertions failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: " << assertions << " perception assertions\n";
    return EXIT_SUCCESS;
}
