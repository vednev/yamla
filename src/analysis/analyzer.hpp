#pragma once

#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <string>

#include "../parser/log_entry.hpp"
#include "../core/chunk_vector.hpp"

// ------------------------------------------------------------
//  CountMap — a sorted-by-count snapshot used by the UI
// ------------------------------------------------------------
struct CountEntry {
    std::string label;
    uint64_t    count = 0;
};
using CountMap = std::vector<CountEntry>; // sorted descending by count

// ------------------------------------------------------------
//  ConnEntry — one unique connection ID with its entry count
// ------------------------------------------------------------
struct ConnEntry {
    uint32_t conn_id = 0;
    uint64_t count   = 0;
};

// ------------------------------------------------------------
//  AnalysisResult
//
//  Produced by a single pass over all LogEntry objects.
//  All string labels come from the shared StringTable.
// ------------------------------------------------------------
struct AnalysisResult {
    CountMap  by_severity;    // INFO / WARN / ERROR / ...
    CountMap  by_component;   // COMMAND / REPL / NETWORK / ...
    CountMap  by_op_type;     // find / insert / update / ...
    CountMap  by_driver;      // "pymongo 4.1.0" / ...
    CountMap  by_namespace;   // "db.collection"
    CountMap  by_shape;       // normalized query shapes

    // Unique connection IDs, sorted descending by entry count
    std::vector<ConnEntry> by_conn_id;

    uint64_t total_entries   = 0;
    uint64_t entries_with_ns = 0;
    uint64_t slow_queries    = 0;  // duration_ms > 100
    int64_t  earliest_ms     = INT64_MAX;
    int64_t  latest_ms       = INT64_MIN;
};

// ------------------------------------------------------------
//  Analyzer
//
//  Single-pass O(N) aggregation over an ArenaVector<LogEntry>.
//  Thread-safe to call from any thread as long as the entries
//  and string table are not being modified concurrently.
// ------------------------------------------------------------
class Analyzer {
public:
    // Perform analysis over all entries. The StringTable is used
    // to resolve index → label strings.
    static AnalysisResult analyze(const ChunkVector<LogEntry>& entries,
                                  const StringTable& strings);

private:
    static CountMap sort_map(std::unordered_map<uint32_t, uint64_t>& raw,
                             const StringTable& strings);
};
