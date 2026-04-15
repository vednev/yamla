#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <functional>

#include "metric_store.hpp"

// ------------------------------------------------------------
//  FtdcParser
//
//  Decodes MongoDB FTDC binary files (the metrics.* files found
//  in a mongod/mongos diagnostic.data directory).
//
//  FTDC format:
//    File = sequence of BSON documents (each prefixed with int32 size).
//    Each BSON document has a field "type":
//      type 0 — metadata chunk: contains the reference BSON document
//               that defines the metric schema (paths) and the first
//               set of values.
//      type 1 — data chunk: contains a zlib-compressed payload.
//               The compressed data is a packed binary array of
//               delta-encoded int64 values, one row per sample,
//               one column per metric (schema from last type-0 chunk).
//               Also contains "nSamples" (int32) and "start" (date).
//
//  After decoding, all metric values are stored in MetricStore.
//  String paths are dot-joined (e.g. "serverStatus.opcounters.insert").
//
//  Usage:
//    FtdcParser parser;
//    parser.parse_file("/path/to/metrics.2024-01-01", store);
// ------------------------------------------------------------
class FtdcParser {
public:
    FtdcParser() = default;

    // Parse a single FTDC file and append results to store.
    // Returns true on success.
    bool parse_file(const std::string& path, MetricStore& store,
                    std::string& error_out);

    // Optional progress callback — called with (bytes_processed, total_bytes)
    using ProgressCb = std::function<void(size_t, size_t)>;
    void set_progress_cb(ProgressCb cb) { progress_cb_ = std::move(cb); }

private:
    ProgressCb progress_cb_;
    // D-08: persistent buffer reused across parse_file() calls via resize(),
    // eliminating per-chunk heap allocation.
    std::vector<uint8_t> doc_buf_;
};
