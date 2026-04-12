#include <catch2/catch_all.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "ftdc/ftdc_analyzer.hpp"
#include "ftdc/metric_store.hpp"
#include "parser/log_entry.hpp"  // Severity enum for annotation test

#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>

using Catch::Matchers::WithinAbs;

// ============================================================
//  compute_rate tests
// ============================================================

TEST_CASE("FtdcAnalyzer: compute_rate positive deltas", "[ftdc_analyzer][rate]") {
    // timestamps: 0, 1000, 2000, 3000 ms
    // values:    100, 200, 350, 500
    // expected rates: (200-100)/1.0=100, (350-200)/1.0=150, (500-350)/1.0=150
    std::vector<int64_t> ts  = {0, 1000, 2000, 3000};
    std::vector<double>  val = {100.0, 200.0, 350.0, 500.0};

    auto rate = FtdcAnalyzer::compute_rate(ts, val);
    REQUIRE(rate.size() == 3);
    REQUIRE_THAT(rate[0], WithinAbs(100.0, 0.001));
    REQUIRE_THAT(rate[1], WithinAbs(150.0, 0.001));
    REQUIRE_THAT(rate[2], WithinAbs(150.0, 0.001));
}

TEST_CASE("FtdcAnalyzer: compute_rate clamps negative deltas", "[ftdc_analyzer][rate]") {
    // Counter wrap scenario: values go 100 -> 200 -> 50 -> 100
    // Expected: 100/s, 0/s (clamped), 50/s
    std::vector<int64_t> ts  = {0, 1000, 2000, 3000};
    std::vector<double>  val = {100.0, 200.0, 50.0, 100.0};

    auto rate = FtdcAnalyzer::compute_rate(ts, val);
    REQUIRE(rate.size() == 3);
    REQUIRE_THAT(rate[0], WithinAbs(100.0, 0.001));
    REQUIRE_THAT(rate[1], WithinAbs(0.0, 0.001));   // clamped negative
    REQUIRE_THAT(rate[2], WithinAbs(50.0, 0.001));
}

TEST_CASE("FtdcAnalyzer: compute_rate handles zero dt", "[ftdc_analyzer][rate]") {
    // Two samples with identical timestamps — should produce rate 0.0
    std::vector<int64_t> ts  = {0, 0, 1000};
    std::vector<double>  val = {100.0, 200.0, 300.0};

    auto rate = FtdcAnalyzer::compute_rate(ts, val);
    REQUIRE(rate.size() == 2);
    REQUIRE_THAT(rate[0], WithinAbs(0.0, 0.001)); // dt=0 → rate=0
    REQUIRE_THAT(rate[1], WithinAbs(100.0, 0.001));
}

TEST_CASE("FtdcAnalyzer: compute_rate returns empty for small input", "[ftdc_analyzer][rate]") {
    // Less than 2 samples → empty rate vector
    std::vector<int64_t> ts0 = {};
    std::vector<double>  val0 = {};
    REQUIRE(FtdcAnalyzer::compute_rate(ts0, val0).empty());

    std::vector<int64_t> ts1 = {1000};
    std::vector<double>  val1 = {42.0};
    REQUIRE(FtdcAnalyzer::compute_rate(ts1, val1).empty());
}

TEST_CASE("FtdcAnalyzer: compute_all_rates populates only cumulative", "[ftdc_analyzer][rate]") {
    MetricStore store;

    // Cumulative metric — should get rates
    MetricSeries& cum = store.get_or_create("test.cumulative");
    cum.is_cumulative = true;
    cum.timestamps_ms = {0, 1000, 2000};
    cum.values = {10.0, 20.0, 40.0};

    // Non-cumulative (gauge) metric — should NOT get rates
    MetricSeries& gauge = store.get_or_create("test.gauge");
    gauge.is_cumulative = false;
    gauge.timestamps_ms = {0, 1000, 2000};
    gauge.values = {5.0, 10.0, 15.0};

    FtdcAnalyzer::compute_all_rates(store);

    REQUIRE(cum.rate.size() == 2);
    REQUIRE_THAT(cum.rate[0], WithinAbs(10.0, 0.001));
    REQUIRE_THAT(cum.rate[1], WithinAbs(20.0, 0.001));

    REQUIRE(gauge.rate.empty()); // gauge — no rates computed
}

// ============================================================
//  lttb_downsample tests
// ============================================================

TEST_CASE("FtdcAnalyzer: lttb identity when input <= max_points", "[ftdc_analyzer][lttb]") {
    // Small input — all indices returned
    std::vector<double> values = {1.0, 2.0, 3.0, 4.0, 5.0};
    auto indices = FtdcAnalyzer::lttb_downsample(values, 10);

    REQUIRE(indices.size() == values.size());
    for (size_t i = 0; i < indices.size(); ++i) {
        REQUIRE(indices[i] == i);
    }
}

TEST_CASE("FtdcAnalyzer: lttb reduces to max_points for large input", "[ftdc_analyzer][lttb]") {
    // Generate 1000 points
    std::vector<double> values(1000);
    for (size_t i = 0; i < 1000; ++i) {
        values[i] = static_cast<double>(i);
    }

    size_t max_pts = 50;
    auto indices = FtdcAnalyzer::lttb_downsample(values, max_pts);
    REQUIRE(indices.size() == max_pts);
}

TEST_CASE("FtdcAnalyzer: lttb always includes first and last", "[ftdc_analyzer][lttb]") {
    // Generate 500 points
    std::vector<double> values(500);
    for (size_t i = 0; i < 500; ++i) {
        values[i] = std::sin(static_cast<double>(i) * 0.02);
    }

    auto indices = FtdcAnalyzer::lttb_downsample(values, 30);
    REQUIRE(indices.size() == 30);
    REQUIRE(indices.front() == 0);
    REQUIRE(indices.back() == 499);
}

TEST_CASE("FtdcAnalyzer: lttb preserves peaks in triangle wave", "[ftdc_analyzer][lttb]") {
    // Create a triangle wave: 0→100→0→100→0 over 1000 points
    // Peaks at 250, 750; valleys at 0, 500, 999
    std::vector<double> values(1000);
    for (size_t i = 0; i < 1000; ++i) {
        // 4 segments of 250 points each: up, down, up, down
        size_t phase = i % 500;
        if (phase < 250)
            values[i] = static_cast<double>(phase) * (100.0 / 250.0);
        else
            values[i] = 100.0 - static_cast<double>(phase - 250) * (100.0 / 250.0);
    }

    auto indices = FtdcAnalyzer::lttb_downsample(values, 50);
    REQUIRE(indices.size() == 50);

    // Check that peaks (near indices 250, 750) and valleys (near 0, 500, 999)
    // are represented in the downsampled output.
    // We look for indices within ±15 of the expected peaks/valleys.
    auto has_near = [&](size_t target, size_t tolerance) -> bool {
        for (size_t idx : indices) {
            if (idx >= target - std::min(target, tolerance) &&
                idx <= target + tolerance)
                return true;
        }
        return false;
    };

    // First and last are always included
    REQUIRE(has_near(0, 0));
    REQUIRE(has_near(999, 0));

    // Peaks at ~250 and ~750 should be captured
    REQUIRE(has_near(250, 15));
    REQUIRE(has_near(750, 15));

    // Valley at ~500 should be captured
    REQUIRE(has_near(500, 15));
}

// ============================================================
//  compute_window_stats tests
// ============================================================

TEST_CASE("FtdcAnalyzer: compute_window_stats correct values", "[ftdc_analyzer][stats]") {
    std::vector<int64_t> ts  = {1000, 2000, 3000, 4000, 5000};
    std::vector<double>  val = {10.0, 20.0, 30.0, 40.0, 50.0};

    // Full window [1000, 5000]
    WindowStats ws = FtdcAnalyzer::compute_window_stats(ts, val, 1000, 5000);
    REQUIRE(ws.valid);
    REQUIRE(ws.count == 5);
    REQUIRE_THAT(ws.min, WithinAbs(10.0, 0.001));
    REQUIRE_THAT(ws.max, WithinAbs(50.0, 0.001));
    REQUIRE_THAT(ws.avg, WithinAbs(30.0, 0.001));
    // p99 for 5 values: index 0.99 * 4 = 3.96 → index 3 → value 40.0
    REQUIRE_THAT(ws.p99, WithinAbs(40.0, 0.001));

    // Partial window [2000, 4000]
    WindowStats ws2 = FtdcAnalyzer::compute_window_stats(ts, val, 2000, 4000);
    REQUIRE(ws2.valid);
    REQUIRE(ws2.count == 3);
    REQUIRE_THAT(ws2.min, WithinAbs(20.0, 0.001));
    REQUIRE_THAT(ws2.max, WithinAbs(40.0, 0.001));
    REQUIRE_THAT(ws2.avg, WithinAbs(30.0, 0.001));
}

TEST_CASE("FtdcAnalyzer: compute_window_stats empty range", "[ftdc_analyzer][stats]") {
    std::vector<int64_t> ts  = {1000, 2000, 3000};
    std::vector<double>  val = {10.0, 20.0, 30.0};

    // Window that covers no samples
    WindowStats ws = FtdcAnalyzer::compute_window_stats(ts, val, 5000, 6000);
    REQUIRE_FALSE(ws.valid);
    REQUIRE(ws.count == 0);

    // Empty input vectors
    WindowStats ws2 = FtdcAnalyzer::compute_window_stats({}, {}, 0, 1000);
    REQUIRE_FALSE(ws2.valid);
}

// ============================================================
//  find_sample_at tests
// ============================================================

TEST_CASE("FtdcAnalyzer: find_sample_at returns closest index", "[ftdc_analyzer][search]") {
    std::vector<int64_t> ts = {1000, 2000, 3000, 4000, 5000};

    // Exact match
    REQUIRE(FtdcAnalyzer::find_sample_at(ts, 3000) == 2);

    // Between samples — closer to 2000 (idx 1)
    REQUIRE(FtdcAnalyzer::find_sample_at(ts, 2200) == 1);

    // Between samples — closer to 3000 (idx 2)
    REQUIRE(FtdcAnalyzer::find_sample_at(ts, 2800) == 2);

    // Exactly midpoint — should pick the earlier one (d1 <= d2)
    REQUIRE(FtdcAnalyzer::find_sample_at(ts, 2500) == 1);
}

TEST_CASE("FtdcAnalyzer: find_sample_at before all data returns 0", "[ftdc_analyzer][search]") {
    std::vector<int64_t> ts = {1000, 2000, 3000};
    REQUIRE(FtdcAnalyzer::find_sample_at(ts, 0) == 0);
    REQUIRE(FtdcAnalyzer::find_sample_at(ts, 500) == 0);
}

TEST_CASE("FtdcAnalyzer: find_sample_at after all data returns last", "[ftdc_analyzer][search]") {
    std::vector<int64_t> ts = {1000, 2000, 3000};
    REQUIRE(FtdcAnalyzer::find_sample_at(ts, 9999) == 2);
    REQUIRE(FtdcAnalyzer::find_sample_at(ts, 3001) == 2);
}

TEST_CASE("FtdcAnalyzer: find_sample_at empty returns npos", "[ftdc_analyzer][search]") {
    std::vector<int64_t> ts = {};
    REQUIRE(FtdcAnalyzer::find_sample_at(ts, 1000) == static_cast<size_t>(-1));
}

// ============================================================
//  Annotation severity verification (D-10)
//
//  From chart_panel_view.cpp (render_annotation_markers):
//    if (e->severity > Severity::Warning) continue;    // skip Info/Debug
//    if (e->severity <= Severity::Error) err_xs.push_back(ex);  // Fatal+Error
//    else warn_xs.push_back(ex);                        // Warning
//
//  This test verifies the Severity enum ordering guarantees
//  that the above logic is correct. If someone reorders the
//  enum, these tests will catch it.
// ============================================================

TEST_CASE("Annotation severity enum values are correctly ordered", "[ftdc_analyzer][severity]") {
    // Verify raw enum values match expected ordering
    // Lower numeric value = higher severity
    REQUIRE(static_cast<uint8_t>(Severity::Fatal)   == 0);
    REQUIRE(static_cast<uint8_t>(Severity::Error)   == 1);
    REQUIRE(static_cast<uint8_t>(Severity::Warning) == 2);
    REQUIRE(static_cast<uint8_t>(Severity::Info)    == 3);
    REQUIRE(static_cast<uint8_t>(Severity::Debug)   == 4);
    REQUIRE(static_cast<uint8_t>(Severity::Unknown) == 5);

    // Verify the annotation marker filter logic:
    // "if (severity > Warning) continue" skips Info and Debug
    REQUIRE(Severity::Info    > Severity::Warning);
    REQUIRE(Severity::Debug   > Severity::Warning);
    REQUIRE(Severity::Unknown > Severity::Warning);

    // Fatal, Error, Warning pass the filter
    REQUIRE_FALSE(Severity::Fatal   > Severity::Warning);
    REQUIRE_FALSE(Severity::Error   > Severity::Warning);
    REQUIRE_FALSE(Severity::Warning > Severity::Warning);

    // "if (severity <= Error)" captures Fatal and Error as error markers
    REQUIRE(Severity::Fatal <= Severity::Error);
    REQUIRE(Severity::Error <= Severity::Error);

    // Warning does NOT match "severity <= Error" — it becomes a warning marker
    REQUIRE_FALSE(Severity::Warning <= Severity::Error);
}
