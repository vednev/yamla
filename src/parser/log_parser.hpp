#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

#include "../core/mmap_file.hpp"
#include "../core/chunk_vector.hpp"
#include "log_entry.hpp"

// ------------------------------------------------------------
//  ParseBatch
// ------------------------------------------------------------
struct ParseBatch {
    const char* start      = nullptr;
    size_t      length     = 0;
    size_t      file_base  = 0;
    uint16_t    node_idx   = 0;
};

// ------------------------------------------------------------
//  ParseResult
// ------------------------------------------------------------
struct ParseResult {
    std::vector<LogEntry> entries;
    size_t lines_ok     = 0;
    size_t lines_failed = 0;
};

struct LocalInternCache;

// ------------------------------------------------------------
//  LogParser
// ------------------------------------------------------------
class LogParser {
public:
    using ProgressCb = std::function<void(size_t, size_t)>;

    struct Config {
        size_t   batch_size_bytes;
        unsigned num_threads;
        float    sample_ratio;   // 1.0 = full, <1.0 = sample every 1/ratio-th line
        Config() : batch_size_bytes(8 * 1024 * 1024), num_threads(0), sample_ratio(1.0f) {}
    };

    LogParser(StringTable& strings, Config cfg = {});

    // Output goes into a ChunkVector (unbounded capacity).
    void parse_file(const MmapFile& file, uint16_t node_idx,
                    ChunkVector<LogEntry>& out,
                    ProgressCb progress_cb = nullptr);

    size_t total_lines_ok()     const { return lines_ok_; }
    size_t total_lines_failed() const { return lines_failed_; }

private:
    static std::vector<ParseBatch> split_batches(const char* data, size_t size,
                                                  size_t batch_size,
                                                  uint16_t node_idx);

    ParseResult parse_batch(const ParseBatch& batch);
    void reconcile_cache(LocalInternCache& cache);

    StringTable&        strings_;
    Config              cfg_;
    std::mutex          strings_mutex_;

    std::atomic<size_t> lines_ok_     {0};
    std::atomic<size_t> lines_failed_ {0};
};
