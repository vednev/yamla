#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>
#include <cmath>

#include "metric_store.hpp"

// ------------------------------------------------------------
//  WindowStats — min/avg/max/p99 over a time window
// ------------------------------------------------------------
struct WindowStats {
    double min   = 0.0;
    double max   = 0.0;
    double avg   = 0.0;
    double p99   = 0.0;
    size_t count = 0;
    bool   valid = false;
};

// ------------------------------------------------------------
//  LttbPoint — downsampled (timestamp_ms, value) pair
// ------------------------------------------------------------
struct LttbPoint {
    double ts_ms;
    double value;
};

// ------------------------------------------------------------
//  FtdcAnalyzer — stateless utility class
//
//  All methods are static; no state is stored.
// ------------------------------------------------------------
class FtdcAnalyzer {
public:
    // ----------------------------------------------------------
    //  compute_rate_series
    //
    //  For a cumulative metric, compute delta/second series.
    //  Result has size = input.size() - 1.
    //  timestamps_ms and values must be parallel and sorted asc.
    // ----------------------------------------------------------
    static std::vector<double> compute_rate(
        const std::vector<int64_t>& timestamps_ms,
        const std::vector<double>&  values);

    // ----------------------------------------------------------
    //  compute_all_rates
    //
    //  Walk every series in the store that has is_cumulative=true
    //  and populate its .rate vector.
    // ----------------------------------------------------------
    static void compute_all_rates(MetricStore& store);

    // ----------------------------------------------------------
    //  lttb_downsample
    //
    //  Largest-Triangle-Three-Buckets downsampling.
    //  Reduces input to at most max_points representative points
    //  while preserving visual shape (peaks and valleys are kept).
    //
    //  Returns indices into the original arrays.
    //  If input.size() <= max_points, returns all indices 0..n-1.
    // ----------------------------------------------------------
    static std::vector<size_t> lttb_downsample(
        const std::vector<double>& values,
        size_t                     max_points);

    // ----------------------------------------------------------
    //  compute_window_stats
    //
    //  Compute min/avg/max/p99 for values in [t_start_ms, t_end_ms].
    //  Uses the provided values array (either raw or rate).
    //  timestamps_ms and values must be parallel.
    // ----------------------------------------------------------
    static WindowStats compute_window_stats(
        const std::vector<int64_t>& timestamps_ms,
        const std::vector<double>&  values,
        int64_t                     t_start_ms,
        int64_t                     t_end_ms);

    // ----------------------------------------------------------
    //  find_sample_at
    //
    //  Binary search: return index of the sample closest to t_ms.
    //  Returns size_t(-1) if series is empty.
    // ----------------------------------------------------------
    static size_t find_sample_at(
        const std::vector<int64_t>& timestamps_ms,
        int64_t                     t_ms);
};
