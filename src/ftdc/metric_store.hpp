#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

// ------------------------------------------------------------
//  MetricSeries
//
//  Holds one decoded FTDC time series.
//  timestamps_ms: epoch milliseconds, parallel to values.
//  rate:          computed delta/s for cumulative metrics.
// ------------------------------------------------------------
struct MetricSeries {
    std::string path;          // e.g. "serverStatus.opcounters.insert"
    std::string display_name;  // human-readable, from metric_defs
    std::string unit;          // "bytes", "ms", "count", "ratio", "ops/s"
    bool        is_cumulative = false;  // from metric_defs

    std::vector<int64_t> timestamps_ms; // epoch ms, sorted ascending
    std::vector<double>  values;        // raw decoded FTDC values
    std::vector<double>  rate;          // delta/s — populated if is_cumulative

    bool empty() const { return timestamps_ms.empty(); }
    size_t size() const { return timestamps_ms.size(); }
};

// ------------------------------------------------------------
//  MetricStore
//
//  Owns all decoded time series for one FTDC diagnostic.data
//  directory load. Keyed by metric path string.
// ------------------------------------------------------------
struct MetricStore {
    // path → series (insertion order preserved via separate key list)
    std::unordered_map<std::string, MetricSeries> series;
    std::vector<std::string> ordered_keys; // paths in decode order

    int64_t time_start_ms = 0;
    int64_t time_end_ms   = 0;

    bool empty() const { return series.empty(); }

    MetricSeries* get(const std::string& path) {
        auto it = series.find(path);
        return it != series.end() ? &it->second : nullptr;
    }
    const MetricSeries* get(const std::string& path) const {
        auto it = series.find(path);
        return it != series.end() ? &it->second : nullptr;
    }

    // Return or create a series for path
    MetricSeries& get_or_create(const std::string& path) {
        auto it = series.find(path);
        if (it == series.end()) {
            ordered_keys.push_back(path);
            it = series.emplace(path, MetricSeries{}).first;
            it->second.path = path;
        }
        return it->second;
    }

    void update_time_range() {
        time_start_ms = INT64_MAX;
        time_end_ms   = INT64_MIN;
        for (const auto& kv : series) {
            if (kv.second.empty()) continue;
            int64_t s = kv.second.timestamps_ms.front();
            int64_t e = kv.second.timestamps_ms.back();
            if (s < time_start_ms) time_start_ms = s;
            if (e > time_end_ms)   time_end_ms   = e;
        }
        if (time_start_ms == INT64_MAX) time_start_ms = time_end_ms = 0;
    }
};

// ------------------------------------------------------------
//  FtdcLoadState — mirrors LogView's LoadState
// ------------------------------------------------------------
enum class FtdcLoadState { Idle, Loading, Ready, Error };
