#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "../core/arena.hpp"
#include "../core/arena_vector.hpp"
#include "../core/mmap_file.hpp"
#include "log_entry.hpp"

// ------------------------------------------------------------
//  ParseBatch — a contiguous range of lines from one file
//  assigned to a single worker thread.
// ------------------------------------------------------------
struct ParseBatch {
    const char* start      = nullptr;  // pointer into mmap'd data
    size_t      length     = 0;        // byte length of this slice
    size_t      file_base  = 0;        // byte offset of start within the file
    uint16_t    node_idx   = 0;        // which cluster node this file belongs to
};

// ------------------------------------------------------------
//  ParseResult — collected by the main thread from workers
// ------------------------------------------------------------
struct ParseResult {
    std::vector<LogEntry> entries;  // parsed entries from this batch
    size_t lines_ok     = 0;
    size_t lines_failed = 0;
};

// ------------------------------------------------------------
//  LogParser
//
//  Splits an mmap'd log file into line-aligned batches and
//  dispatches them to worker threads. Each worker uses a
//  thread-local simdjson parser so there is no lock contention
//  on the parse path.
//
//  After parse() returns, all LogEntry objects are appended
//  to `out` (in timestamp order — re-sorting is the caller's
//  responsibility if multiple nodes are parsed concurrently).
//
//  String interning is serialised through a mutex because
//  StringTable is not thread-safe; the hot parse path is
//  purely local to each worker.
// ------------------------------------------------------------

class LogParser {
public:
    // progress_cb is called from the main thread with
    // (lines_parsed, total_lines_estimate).
    using ProgressCb = std::function<void(size_t, size_t)>;

    struct Config {
        size_t   batch_size_bytes;
        unsigned num_threads;
        Config() : batch_size_bytes(4 * 1024 * 1024), num_threads(0) {}
    };

    LogParser(StringTable& strings, Config cfg = {});

    // Parse one file, appending results to `out`.
    // `node_idx` identifies which cluster node this file belongs to.
    // Blocks until all batches are processed.
    void parse_file(const MmapFile& file, uint16_t node_idx,
                    ArenaVector<LogEntry>& out,
                    ProgressCb progress_cb = nullptr);

    size_t total_lines_ok()     const { return lines_ok_; }
    size_t total_lines_failed() const { return lines_failed_; }

private:
    // Split [data, data+size) into line-aligned batches of ~batch_size_bytes
    static std::vector<ParseBatch> split_batches(const char* data, size_t size,
                                                 size_t batch_size,
                                                 uint16_t node_idx);

    // Parse a single batch — called on worker threads.
    // Returns a ParseResult (no shared-state writes except via mutex).
    ParseResult parse_batch(const ParseBatch& batch);

    // Parse one JSON log line. Returns false if not a valid MongoDB log line.
    bool parse_line(const char* line, size_t len, size_t file_offset,
                    uint16_t node_idx, LogEntry& out_entry);

    StringTable&    strings_;
    Config          cfg_;
    std::mutex      strings_mutex_;  // protects StringTable during intern

    std::atomic<size_t> lines_ok_     {0};
    std::atomic<size_t> lines_failed_ {0};
};
