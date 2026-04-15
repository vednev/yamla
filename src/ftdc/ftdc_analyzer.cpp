#include "ftdc_analyzer.hpp"

#include <algorithm>
#include <numeric>
#include <cmath>

// ============================================================
//  compute_rate
// ============================================================
std::vector<double> FtdcAnalyzer::compute_rate(
    const std::vector<int64_t>& timestamps_ms,
    const std::vector<double>&  values)
{
    size_t n = timestamps_ms.size();
    if (n < 2) return {};

    std::vector<double> rate;
    rate.reserve(n - 1);

    for (size_t i = 1; i < n; ++i) {
        double dt_s = static_cast<double>(timestamps_ms[i] - timestamps_ms[i - 1]) / 1000.0;
        if (dt_s <= 0.0) {
            rate.push_back(0.0);
        } else {
            double dv = values[i] - values[i - 1];
            // Clamp negative deltas to zero — cumulative counter wraps / server restarts
            if (dv < 0.0) dv = 0.0;
            rate.push_back(dv / dt_s);
        }
    }
    return rate;
}

// ============================================================
//  compute_all_rates
// ============================================================
void FtdcAnalyzer::compute_all_rates(MetricStore& store) {
    for (auto& kv : store.series) {
        MetricSeries& ms = kv.second;
        if (!ms.is_cumulative || ms.size() < 2) continue;
        ms.rate = compute_rate(ms.timestamps_ms, ms.values);
    }
}

// ============================================================
//  lttb_downsample
//
//  Largest-Triangle-Three-Buckets algorithm.
//  Reference: Sveinn Steinarsson, 2013.
// ============================================================
std::vector<size_t> FtdcAnalyzer::lttb_downsample(
    const std::vector<double>& values,
    size_t                     max_points)
{
    size_t n = values.size();
    if (n <= max_points || max_points < 3) {
        std::vector<size_t> all(n);
        for (size_t i = 0; i < n; ++i) all[i] = i;
        return all;
    }

    std::vector<size_t> sampled;
    sampled.reserve(max_points);

    // Always include first and last
    sampled.push_back(0);

    double bucket_size = static_cast<double>(n - 2) / static_cast<double>(max_points - 2);

    size_t a = 0; // previous selected point

    for (size_t i = 0; i < max_points - 2; ++i) {
        // Bucket boundaries for current bucket
        size_t b_start = static_cast<size_t>(static_cast<double>(i    ) * bucket_size) + 1;
        size_t b_end   = static_cast<size_t>(static_cast<double>(i + 1) * bucket_size) + 1;
        if (b_end >= n) b_end = n - 1;

        // Compute average point of NEXT bucket (used as "C")
        size_t c_start = b_end;
        size_t c_end   = static_cast<size_t>(static_cast<double>(i + 2) * bucket_size) + 1;
        if (c_end >= n) c_end = n - 1;

        double avg_x = 0.0, avg_y = 0.0;
        size_t c_count = c_end - c_start + 1;
        for (size_t k = c_start; k <= c_end && k < n; ++k) {
            avg_x += static_cast<double>(k);
            avg_y += values[k];
        }
        avg_x /= static_cast<double>(c_count);
        avg_y /= static_cast<double>(c_count);

        // Find point in current bucket that forms largest triangle with A and avg-C
        double max_area = -1.0;
        size_t best     = b_start;
        double ax = static_cast<double>(a);
        double ay = values[a];

        for (size_t k = b_start; k < b_end && k < n; ++k) {
            double bx = static_cast<double>(k);
            double by = values[k];
            // Triangle area = 0.5 * |det|
            double area = std::abs((ax - avg_x) * (by - ay) -
                                   (ax - bx)    * (avg_y - ay));
            if (area > max_area) {
                max_area = area;
                best     = k;
            }
        }

        sampled.push_back(best);
        a = best;
    }

    sampled.push_back(n - 1);
    return sampled;
}

// ============================================================
//  compute_window_stats (4-arg — delegates to 5-arg with scratch)
// ============================================================
WindowStats FtdcAnalyzer::compute_window_stats(
    const std::vector<int64_t>& timestamps_ms,
    const std::vector<double>&  values,
    int64_t                     t_start_ms,
    int64_t                     t_end_ms)
{
    std::vector<double> scratch;
    return compute_window_stats(timestamps_ms, values, t_start_ms, t_end_ms, scratch);
}

// ============================================================
//  compute_window_stats (5-arg — reusable scratch buffer, D-05)
// ============================================================
WindowStats FtdcAnalyzer::compute_window_stats(
    const std::vector<int64_t>& timestamps_ms,
    const std::vector<double>&  values,
    int64_t                     t_start_ms,
    int64_t                     t_end_ms,
    std::vector<double>&        scratch)
{
    WindowStats ws;
    if (timestamps_ms.empty() || values.empty()) return ws;

    // Binary search for start
    auto it_start = std::lower_bound(timestamps_ms.begin(), timestamps_ms.end(), t_start_ms);
    auto it_end   = std::upper_bound(timestamps_ms.begin(), timestamps_ms.end(), t_end_ms);

    size_t i_start = static_cast<size_t>(it_start - timestamps_ms.begin());
    size_t i_end   = static_cast<size_t>(it_end   - timestamps_ms.begin());

    if (i_start >= i_end) return ws;

    ws.count = i_end - i_start;
    ws.min   = values[i_start];
    ws.max   = values[i_start];
    double sum = 0.0;

    scratch.clear();
    scratch.reserve(ws.count);

    for (size_t i = i_start; i < i_end; ++i) {
        double v = values[i];
        if (v < ws.min) ws.min = v;
        if (v > ws.max) ws.max = v;
        sum += v;
        scratch.push_back(v);
    }

    ws.avg = sum / static_cast<double>(ws.count);

    // p99
    std::sort(scratch.begin(), scratch.end());
    size_t p99_idx = static_cast<size_t>(0.99 * static_cast<double>(ws.count - 1));
    ws.p99 = scratch[p99_idx];

    ws.valid = true;
    return ws;
}

// ============================================================
//  find_sample_at
// ============================================================
size_t FtdcAnalyzer::find_sample_at(
    const std::vector<int64_t>& timestamps_ms,
    int64_t                     t_ms)
{
    if (timestamps_ms.empty()) return static_cast<size_t>(-1);

    auto it = std::lower_bound(timestamps_ms.begin(), timestamps_ms.end(), t_ms);
    if (it == timestamps_ms.end()) return timestamps_ms.size() - 1;
    if (it == timestamps_ms.begin()) return 0;

    // Pick the closer of it-1 and it
    auto prev = std::prev(it);
    int64_t d1 = t_ms - *prev;
    int64_t d2 = *it   - t_ms;
    return static_cast<size_t>(d1 <= d2 ? prev - timestamps_ms.begin()
                                        : it   - timestamps_ms.begin());
}
