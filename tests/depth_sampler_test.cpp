#include "raksh/depth_sampler.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

int failures = 0;
int assertions = 0;

void expect_true(bool value, const std::string& message) {
    ++assertions;
    if (!value) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void expect_false(bool value, const std::string& message) {
    expect_true(!value, message);
}

void expect_equal(std::size_t actual, std::size_t expected,
                  const std::string& message) {
    ++assertions;
    if (actual != expected) {
        ++failures;
        std::cerr << "FAIL: " << message << " (expected " << expected
                  << ", got " << actual << ")\n";
    }
}

void expect_near(float actual, float expected, float tolerance,
                 const std::string& message) {
    ++assertions;
    if (!std::isfinite(actual) || std::fabs(actual - expected) > tolerance) {
        ++failures;
        std::cerr << "FAIL: " << message << " (expected " << expected
                  << " +/- " << tolerance << ", got " << actual << ")\n";
    }
}

raksh::DepthSampleConfig default_config() {
    raksh::DepthSampleConfig config;
    config.min_distance_m = 0.05F;
    config.max_distance_m = 5.0F;
    config.min_valid_ratio = 0.50F;
    config.mad_multiplier = 3.0F;
    config.min_outlier_window_m = 0.03F;
    return config;
}

void all_invalid_values_return_unknown() {
    const std::vector<float> depths{
        0.0F,
        -1.0F,
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
    };

    const auto sample = raksh::robust_depth_sample(depths, default_config());

    expect_false(sample.valid, "all-invalid sample must remain unknown");
    expect_near(sample.distance_m, 0.0F, 0.0001F,
                "unknown sample must not contain a fake distance");
    expect_equal(sample.total_count, 4, "total count must include invalid pixels");
    expect_equal(sample.valid_count, 0, "zero/non-finite pixels must be rejected");
    expect_near(sample.confidence, 0.0F, 0.0001F,
                "all-invalid sample confidence must be zero");
}

void minimum_valid_ratio_is_enforced() {
    const std::vector<float> depths{0.0F, 0.0F, 0.0F, 1.25F};

    const auto sample = raksh::robust_depth_sample(depths, default_config());

    expect_false(sample.valid, "25 percent valid pixels must fail a 50 percent gate");
    expect_equal(sample.valid_count, 1, "valid count must still be diagnostic");
    expect_near(sample.confidence, 0.25F, 0.0001F,
                "invalid sample must report its measured confidence");
}

void zero_nonfinite_and_out_of_range_values_are_ignored() {
    const std::vector<float> depths{
        0.0F,
        std::numeric_limits<float>::quiet_NaN(),
        0.01F,
        1.00F,
        1.02F,
        8.00F,
    };
    auto config = default_config();
    config.min_valid_ratio = 0.30F;

    const auto sample = raksh::robust_depth_sample(depths, config);

    expect_true(sample.valid, "two in-range pixels must pass a 30 percent gate");
    expect_equal(sample.valid_count, 2, "only finite in-range pixels are valid");
    expect_equal(sample.inlier_count, 2, "both nearby valid pixels are inliers");
    expect_near(sample.distance_m, 1.01F, 0.0001F,
                "even-sized inlier median must average the middle pair");
    expect_near(sample.confidence, 2.0F / 6.0F, 0.0001F,
                "confidence must be inliers divided by total pixels");
}

void isolated_far_outlier_does_not_move_the_distance() {
    const std::vector<float> depths{0.99F, 1.00F, 1.01F, 5.00F};

    const auto sample = raksh::robust_depth_sample(depths, default_config());

    expect_true(sample.valid, "three coherent pixels must survive one outlier");
    expect_equal(sample.valid_count, 4, "pre-filter valid count must include the outlier");
    expect_equal(sample.inlier_count, 3, "isolated far value must be rejected");
    expect_near(sample.distance_m, 1.00F, 0.0001F,
                "outlier must not move the robust median");
    expect_near(sample.confidence, 0.75F, 0.0001F,
                "confidence must reflect post-filter inliers");
}

void outlier_filter_can_make_sample_unknown() {
    const std::vector<float> depths{0.99F, 1.00F, 1.01F, 5.00F};
    auto config = default_config();
    config.min_valid_ratio = 0.80F;

    const auto sample = raksh::robust_depth_sample(depths, config);

    expect_false(sample.valid,
                 "sample must become unknown when inlier confidence falls below gate");
    expect_near(sample.distance_m, 0.0F, 0.0001F,
                "rejected sample must not expose a misleading distance");
}

}  // namespace

int main() {
    all_invalid_values_return_unknown();
    minimum_valid_ratio_is_enforced();
    zero_nonfinite_and_out_of_range_values_are_ignored();
    isolated_far_outlier_does_not_move_the_distance();
    outlier_filter_can_make_sample_unknown();

    if (failures != 0) {
        std::cerr << failures << " of " << assertions << " assertions failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "PASS: " << assertions << " depth sampler assertions\n";
    return EXIT_SUCCESS;
}
