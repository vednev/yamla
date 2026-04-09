#include "analyzer.hpp"
#include "../core/chunk_vector.hpp"
#include <algorithm>
#include <climits>

// ------------------------------------------------------------
//  sort_map — convert raw index→count map to sorted CountMap
// ------------------------------------------------------------
CountMap Analyzer::sort_map(std::unordered_map<uint32_t, uint64_t>& raw,
                             const StringTable& strings)
{
    CountMap out;
    out.reserve(raw.size());
    for (auto& [idx, cnt] : raw) {
        std::string_view sv = strings.get(idx);
        if (!sv.empty())
            out.push_back({ std::string(sv), cnt });
    }
    std::sort(out.begin(), out.end(),
              [](const CountEntry& a, const CountEntry& b) {
                  return a.count > b.count;
              });
    return out;
}

// ------------------------------------------------------------
//  analyze — single pass O(N)
// ------------------------------------------------------------
AnalysisResult Analyzer::analyze(const ChunkVector<LogEntry>& entries,
                                  const StringTable& strings)
{
    std::unordered_map<uint32_t, uint64_t> sev_map, comp_map, op_map,
                                            drv_map, ns_map, shape_map,
                                            conn_map;
    AnalysisResult r;

    for (size_t i = 0; i < entries.size(); ++i) {
        const LogEntry& e = entries[i];

        ++r.total_entries;

        // Severity — use enum as key (cast to uint32_t)
        sev_map[static_cast<uint32_t>(e.severity)]++;

        if (e.component_idx) comp_map[e.component_idx]++;
        if (e.op_type_idx)   op_map[e.op_type_idx]++;
        if (e.driver_idx)    drv_map[e.driver_idx]++;
        if (e.conn_id)       conn_map[e.conn_id]++;
        if (e.ns_idx) {
            ns_map[e.ns_idx]++;
            ++r.entries_with_ns;
        }
        if (e.shape_idx) shape_map[e.shape_idx]++;

        // Count as a slow query only when MongoDB itself flagged it:
        // the msg field starts with "Slow" (e.g. "Slow query",
        // "Slow write operation"). Entries that merely have a high
        // durationMillis but were not explicitly tagged are excluded.
        if (e.msg_idx != 0) {
            std::string_view msg = strings.get(e.msg_idx);
            if (msg.size() >= 4 &&
                (msg[0]=='S'||msg[0]=='s') &&
                (msg[1]=='l') && (msg[2]=='o') && (msg[3]=='w'))
                ++r.slow_queries;
        }

        if (e.timestamp_ms < r.earliest_ms) r.earliest_ms = e.timestamp_ms;
        if (e.timestamp_ms > r.latest_ms)   r.latest_ms   = e.timestamp_ms;
    }

    // Build severity map using string labels rather than enum indices
    // so the UI gets human-readable labels.
    r.by_severity.reserve(6);
    auto sev_label = [](uint32_t v) -> std::string {
        return severity_string(static_cast<Severity>(v));
    };
    for (auto& [k, v] : sev_map)
        r.by_severity.push_back({ sev_label(k), v });
    std::sort(r.by_severity.begin(), r.by_severity.end(),
              [](const CountEntry& a, const CountEntry& b) {
                  return a.count > b.count;
              });

    r.by_component = sort_map(comp_map, strings);
    r.by_op_type   = sort_map(op_map,   strings);
    r.by_driver    = sort_map(drv_map,  strings);
    r.by_namespace = sort_map(ns_map,   strings);
    r.by_shape     = sort_map(shape_map, strings);

    // Build conn ID list sorted descending by count
    r.by_conn_id.reserve(conn_map.size());
    for (auto& [id, cnt] : conn_map)
        r.by_conn_id.push_back({ id, cnt });
    std::sort(r.by_conn_id.begin(), r.by_conn_id.end(),
              [](const ConnEntry& a, const ConnEntry& b) {
                  return a.count > b.count;
              });

    return r;
}
