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
    // BSON traversal — build a flat dot-path list of all int32/int64/double/date
    // leaf values, and extract those values into a parallel int64 vector.
    struct MetricLeaf {
        std::string path;
        int64_t     value = 0; // raw int64 representation
    };

    // Walk a BSON document recursively, populating flat leaf list.
    // prefix = current path prefix (dot-separated)
    bool extract_metrics(const uint8_t* doc, size_t doc_len,
                         const std::string& prefix,
                         std::vector<MetricLeaf>& leaves);

    // Decompress a zlib blob; returns uncompressed bytes
    bool zlib_decompress(const uint8_t* src, size_t src_len,
                         std::vector<uint8_t>& out, std::string& err);

    // Read a packed delta-encoded chunk after decompression.
    // Format: nMetrics * nSamples int64 values, written column-major
    // (all samples for metric 0, then all for metric 1, etc.),
    // each value stored as a zigzag-encoded varint.
    bool decode_data_chunk(const uint8_t* data, size_t data_len,
                           int32_t n_metrics, int32_t n_samples,
                           const std::vector<std::string>& schema_paths,
                           const std::vector<int64_t>& ref_values,
                           int64_t start_ms,
                           MetricStore& store);

    ProgressCb progress_cb_;
};
