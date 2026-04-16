#include "ftdc_parser.hpp"
#include "metric_defs.hpp"
#include "../core/thread_pool.hpp"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cassert>
#include <algorithm>
#include <limits>
#include <thread>

#if defined(__APPLE__)
#  include <mach/mach.h>
#  include <unistd.h>
#elif defined(_WIN32)
#  include <windows.h>
#else
#  include <sys/sysinfo.h>
#  include <unistd.h>
#endif

#include <zlib.h>

// BSON is little-endian; verify host byte order at compile time.
static_assert(
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
    __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
#elif defined(_WIN32)
    true, // Windows is always little-endian on supported architectures
#else
    true, // assume LE on unknown; ARM and x86 are LE
#endif
    "FTDC parser requires a little-endian platform");

// ============================================================
//  BSON wire-format constants
//  https://bsonspec.org/spec.html
// ============================================================
namespace bson_type {
    static constexpr uint8_t DOUBLE    = 0x01;
    static constexpr uint8_t UTF8      = 0x02;
    static constexpr uint8_t DOCUMENT  = 0x03;
    static constexpr uint8_t ARRAY     = 0x04;
    static constexpr uint8_t BINARY    = 0x05;
    static constexpr uint8_t BOOL      = 0x08;
    static constexpr uint8_t DATE      = 0x09;
    static constexpr uint8_t NULL_TYPE = 0x0A;
    static constexpr uint8_t INT32     = 0x10;
    static constexpr uint8_t TIMESTAMP = 0x11;
    static constexpr uint8_t INT64     = 0x12;
}

// ---- Little-endian reads ----
static inline int32_t read_i32(const uint8_t* p) {
    int32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
static inline int64_t read_i64(const uint8_t* p) {
    int64_t v;
    std::memcpy(&v, p, 8);
    return v;
}
static inline double read_f64(const uint8_t* p) {
    double v;
    std::memcpy(&v, p, 8);
    return v;
}

// ---- Zigzag varint decoding (FTDC delta encoding) ----
// FTDC uses 64-bit zigzag-encoded varints for delta compression.
// Encoding: zigzag(n) = (n << 1) ^ (n >> 63)
// Decoding: n = (v >> 1) ^ -(v & 1)
static inline int64_t zigzag_decode(uint64_t v) {
    return static_cast<int64_t>((v >> 1) ^ (-(v & 1)));
}

// Decode a varint from buffer. Returns bytes consumed (0 = error).
static size_t read_varint(const uint8_t* buf, size_t buf_len, uint64_t& out) {
    out = 0;
    size_t i = 0;
    int shift = 0;
    while (i < buf_len && i < 10) {
        uint8_t b = buf[i++];
        out |= static_cast<uint64_t>(b & 0x7F) << shift;
        if (!(b & 0x80)) return i;
        shift += 7;
    }
    return 0; // overflow or truncated
}

// ============================================================
//  BSON traversal helpers
// ============================================================

// Returns the byte length of a BSON value for a given type_byte,
// pointing at the value (i.e. after the key). Returns -1 if variable-length
// (caller must compute), -2 if unknown type.
// For BSON elements we need to know how many bytes to skip.
static ptrdiff_t bson_value_fixed_size(uint8_t type) {
    switch (type) {
        case bson_type::DOUBLE:    return 8;
        case bson_type::BOOL:      return 1;
        case bson_type::DATE:      return 8;
        case bson_type::NULL_TYPE: return 0;
        case bson_type::INT32:     return 4;
        case bson_type::TIMESTAMP: return 8;
        case bson_type::INT64:     return 8;
        default:                   return -1; // variable
    }
}

// Walk a BSON document, calling leaf_cb for each leaf numeric/date value
// and subdoc_cb for nested documents/arrays.
// doc must point to start of BSON document (4-byte int32 size).
// prefix is the current dot-path prefix.
// Returns false on parse error.
using LeafCb  = std::function<void(const std::string& path, uint8_t type, const uint8_t* val_ptr)>;
using SubdocCb= std::function<bool(const std::string& path, const uint8_t* subdoc)>;

static bool bson_walk(const uint8_t* doc, size_t doc_len,
                      const std::string& prefix,
                      const LeafCb& leaf_cb,
                      const SubdocCb& subdoc_cb)
{
    if (doc_len < 5) return false;
    int32_t total = read_i32(doc);
    if (total < 5 || static_cast<size_t>(total) > doc_len) return false;

    const uint8_t* p   = doc + 4;       // after size
    const uint8_t* end = doc + total;    // one past last valid byte (includes trailing 0x00)

    while (p < end) {
        uint8_t type = *p++;
        if (type == 0x00) break; // terminator

        // Read key (null-terminated cstring)
        const uint8_t* key_start = p;
        while (p < end && *p != 0x00) ++p;
        if (p >= end) return false;
        std::string key(reinterpret_cast<const char*>(key_start),
                        p - key_start);
        ++p; // consume null terminator

        std::string full_path = prefix.empty() ? key : (prefix + "." + key);

        // Dispatch by type
        switch (type) {
            case bson_type::DOUBLE:
            case bson_type::DATE:
            case bson_type::INT32:
            case bson_type::TIMESTAMP:
            case bson_type::INT64:
            case bson_type::BOOL:
            case bson_type::NULL_TYPE: {
                ptrdiff_t fsz = bson_value_fixed_size(type);
                if (fsz < 0) return false;
                if (p + fsz > end) return false;
                if (type != bson_type::NULL_TYPE)
                    leaf_cb(full_path, type, p);
                p += fsz;
                break;
            }
            case bson_type::UTF8: {
                if (p + 4 > end) return false;
                int32_t str_len = read_i32(p);
                if (str_len < 1 || p + 4 + str_len > end) return false;
                p += 4 + str_len;
                break;
            }
            case bson_type::BINARY: {
                if (p + 5 > end) return false; // 4 size + 1 subtype minimum
                int32_t bin_len = read_i32(p);
                if (bin_len < 0 || p + 4 + 1 + bin_len > end) return false;
                p += 4 + 1 + bin_len;
                break;
            }
            case bson_type::DOCUMENT:
            case bson_type::ARRAY: {
                if (p + 4 > end) return false;
                int32_t sub_len = read_i32(p);
                if (sub_len < 5 || p + sub_len > end) return false;
                if (!subdoc_cb(full_path, p)) return false;
                p += sub_len;
                break;
            }
            // ---- Additional BSON types — skip without counting as metric ----
            case 0x06:  // Undefined (deprecated) — 0 bytes
                break;
            case 0x07:  // ObjectId — 12 bytes
                if (p + 12 > end) return false;
                p += 12;
                break;
            case 0x0B:  // Regex — two cstrings (pattern + options)
                while (p < end && *p) ++p;
                if (p >= end) return false; ++p;
                while (p < end && *p) ++p;
                if (p >= end) return false; ++p;
                break;
            case 0x0C:  // DBPointer (deprecated) — string + 12 bytes
                if (p + 4 > end) return false;
                { int32_t sl = read_i32(p);
                  if (sl < 0 || p + 4 + sl + 12 > end) return false;
                  p += 4 + sl + 12; }
                break;
            case 0x0D:  // JavaScript code — string
            case 0x0E:  // Symbol (deprecated) — string
                if (p + 4 > end) return false;
                { int32_t sl = read_i32(p);
                  if (sl < 0 || p + 4 + sl > end) return false;
                  p += 4 + sl; }
                break;
            case 0x0F:  // Code with scope — int32 total size
                if (p + 4 > end) return false;
                { int32_t sz = read_i32(p);
                  if (sz < 0 || p + sz > end) return false;
                  p += sz; }
                break;
            case 0x13:  // Decimal128 — 16 bytes
                if (p + 16 > end) return false;
                p += 16;
                break;
            case 0xFF:  // Min key — 0 bytes
            case 0x7F:  // Max key — 0 bytes
                break;
            default:
                // Truly unknown — skip rest of this document
                p = end;
                break;
        }
    }
    return true;
}

// ============================================================
//  MetricLeaf — file-scope struct (used only in this TU)
// ============================================================
// D-08: Moved out of FtdcParser class to allow helper functions to be static
// free functions, eliminating LTO false-aliasing between doc_buf_ (member) and
// pointer parameters derived from its data().
struct MetricLeaf {
    std::string path;
    int64_t     value = 0;
};

// ============================================================
//  extract_metrics — build leaf schema from a reference BSON doc
//  Static free function: no `this`, so LTO cannot alias its pointer
//  parameters with FtdcParser::doc_buf_.
// ============================================================
static bool extract_metrics(const uint8_t* doc, size_t doc_len,
                             const std::string& prefix,
                             std::vector<MetricLeaf>& leaves)
{
    // Leaf values we care about (numeric)
    auto leaf_cb = [&](const std::string& path, uint8_t type, const uint8_t* val) {
        MetricLeaf ml;
        ml.path = path;
        switch (type) {
            case bson_type::DOUBLE: {
                double d = read_f64(val);
                ml.value = static_cast<int64_t>(d);
                break;
            }
            case bson_type::INT32:
                ml.value = read_i32(val);
                break;
            case bson_type::INT64:
            case bson_type::TIMESTAMP:
                ml.value = read_i64(val);
                break;
            case bson_type::DATE:
                ml.value = read_i64(val); // epoch ms
                break;
            case bson_type::BOOL:
                ml.value = *val ? 1 : 0;
                break;
            default:
                ml.value = 0;
        }
        leaves.push_back(std::move(ml));
    };

    // Recurse into sub-documents
    std::function<bool(const std::string&, const uint8_t*)> subdoc_cb;
    subdoc_cb = [&](const std::string& path, const uint8_t* subdoc) -> bool {
        int32_t sub_len = read_i32(subdoc);
        return extract_metrics(subdoc, static_cast<size_t>(sub_len), path, leaves);
    };

    return bson_walk(doc, doc_len, prefix, leaf_cb, subdoc_cb);
}

// ============================================================
//  zlib_decompress — static free function (no `this`)
// ============================================================
static bool zlib_decompress(const uint8_t* src, size_t src_len,
                             std::vector<uint8_t>& out,
                             std::string& err)
{
    // Initial estimate: 4x compressed size (FTDC data compresses well)
    size_t buf_sz = src_len * 4 + 1024;
    out.resize(buf_sz);

    z_stream zs{};
    zs.next_in  = const_cast<Bytef*>(src);
    zs.avail_in = static_cast<uInt>(src_len);

    if (inflateInit(&zs) != Z_OK) {
        err = "inflateInit failed";
        return false;
    }

    while (true) {
        zs.next_out  = out.data() + zs.total_out;
        zs.avail_out = static_cast<uInt>(buf_sz - zs.total_out);

        int rc = inflate(&zs, Z_NO_FLUSH);
        if (rc == Z_STREAM_END) break;
        if (rc == Z_BUF_ERROR || zs.avail_out == 0) {
            // Need more output space
            buf_sz *= 2;
            out.resize(buf_sz);
            continue;
        }
        if (rc != Z_OK) {
            err = "inflate error: ";
            err += (zs.msg ? zs.msg : "unknown");
            inflateEnd(&zs);
            return false;
        }
    }

    size_t total = zs.total_out;
    inflateEnd(&zs);
    out.resize(total);
    return true;
}

// ============================================================
//  decode_data_chunk
//
//  FTDC data chunk (after decompression) layout:
//    - n_metrics columns × n_samples rows of delta-encoded int64 varints.
//    - Data is stored COLUMN-MAJOR: all n_samples for metric[0],
//      then all n_samples for metric[1], etc.
//    - The first row (sample 0) stores absolute values (same as the
//      reference document values from the preceding metadata chunk).
//    - Subsequent rows store deltas from the previous row.
//    - Each value is zigzag-encoded then varint-packed.
//    - The timestamp column is the FIRST column (index 0), with
//      values as epoch milliseconds. Subsequent column timestamps
//      are the same timestamp (FTDC samples all metrics at one instant).
// ============================================================
// Static free function: no `this`, eliminating LTO aliasing with doc_buf_.
static bool decode_data_chunk(const uint8_t* data, size_t data_len,
                               int32_t n_metrics, int32_t n_samples,
                               const std::vector<std::string>& schema_paths,
                               const std::vector<int64_t>& ref_values,
                               int64_t start_ms,
                               MetricStore& store)
{
    if (n_metrics <= 0 || n_samples <= 0) return true;
    // schema_paths/ref_values may be shorter or longer than n_metrics due to
    // BSON type counting differences. Pad or truncate to match n_metrics.
    // (schema_paths is used for naming only; decode always uses n_metrics.)
    std::vector<std::string> paths = schema_paths;
    std::vector<int64_t> refs      = ref_values;
    while (static_cast<int32_t>(paths.size()) < n_metrics) {
        paths.push_back("__pad__." + std::to_string(paths.size()));
        refs.push_back(0);
    }
    paths.resize(static_cast<size_t>(n_metrics));
    refs.resize(static_cast<size_t>(n_metrics));

    // FTDC delta encoding (column-major, per-metric):
    //   For each metric:
    //     1. Read a varint "nZeros": number of leading zero-delta samples.
    //     2. Read (n_samples - nZeros) zigzag-encoded delta varints.
    //   The reference doc provides sample 0; deltas produce samples 1..n_samples.
    //   Total samples per metric = n_samples + 1 (ref + deltas).

    // Build matrix: [n_metrics][n_samples+1]
    // Row 0 = ref values; rows 1..n_samples = delta-reconstructed.
    size_t total_samples = static_cast<size_t>(n_samples) + 1;
    std::vector<std::vector<int64_t>> matrix(
        static_cast<size_t>(n_metrics),
        std::vector<int64_t>(total_samples, 0));

    // Row 0 = reference values
    for (int m = 0; m < n_metrics; ++m)
        matrix[static_cast<size_t>(m)][0] = refs[static_cast<size_t>(m)];

    const uint8_t* p   = data;
    const uint8_t* end = data + data_len;

    for (int m = 0; m < n_metrics; ++m) {
        if (p >= end) break; // no more data
        // Read nZeros varint
        uint64_t n_zeros_raw = 0;
        size_t consumed = read_varint(p, static_cast<size_t>(end - p), n_zeros_raw);
        if (consumed == 0) break;
        p += consumed;
        int32_t n_zeros = static_cast<int32_t>(n_zeros_raw);
        if (n_zeros < 0) n_zeros = 0;
        if (n_zeros > n_samples) n_zeros = n_samples;

        int64_t prev = refs[static_cast<size_t>(m)];

        // Fill zero-delta samples
        for (int s = 0; s < n_zeros; ++s)
            matrix[static_cast<size_t>(m)][static_cast<size_t>(s + 1)] = prev;

        // Read actual deltas for remaining samples
        bool col_ok = true;
        for (int s = n_zeros; s < n_samples; ++s) {
            if (p >= end) { col_ok = false; break; }
            uint64_t zz = 0;
            consumed = read_varint(p, static_cast<size_t>(end - p), zz);
            if (consumed == 0) { col_ok = false; break; }
            p += consumed;
            int64_t delta = zigzag_decode(zz);
            int64_t val   = prev + delta;
            prev          = val;
            matrix[static_cast<size_t>(m)][static_cast<size_t>(s + 1)] = val;
        }
        if (!col_ok) break;
    }

    // The first column (index 0) of FTDC data is typically the timestamp.
    // However, in FTDC the schema is extracted from the reference doc which
    // includes a "start" date field near the top. We use start_ms plus
    // per-sample offsets (FTDC records at ~1s intervals, but the actual
    // timestamps are stored as the first metric column if it's a Date type).
    //
    // Heuristic: the first schema path ending in "start" or containing
    // "start" is the timestamp column. If not found, generate synthetic
    // 1-second timestamps from start_ms.

    // Find timestamp column index
    int ts_col = -1;
    for (int m = 0; m < n_metrics; ++m) {
        const std::string& p2 = paths[static_cast<size_t>(m)];
        if (p2.find("start") != std::string::npos) {
            ts_col = m;
            break;
        }
    }

    // Build timestamp vector (total_samples = n_samples + 1)
    std::vector<int64_t> timestamps(total_samples);
    if (ts_col >= 0) {
        for (size_t s = 0; s < total_samples; ++s)
            timestamps[s] = matrix[static_cast<size_t>(ts_col)][s];
    } else {
        for (size_t s = 0; s < total_samples; ++s)
            timestamps[s] = start_ms + static_cast<int64_t>(s) * 1000LL;
    }

    // Append samples to MetricStore for each metric (skip timestamp col and unknowns)
    for (int m = 0; m < n_metrics; ++m) {
        if (m == ts_col) continue;
        const std::string& path = paths[static_cast<size_t>(m)];
        if (path.compare(0, 7, "__pad__") == 0) continue;
        if (path.compare(0, 12, "__unknown__.") == 0) continue;
        MetricSeries& ms = store.get_or_create(path);

        // Populate display_name/unit/is_cumulative from defs if not yet set
        if (ms.display_name.empty()) {
            ms.display_name  = metric_display_name(path);
            ms.unit          = metric_unit(path);
            ms.is_cumulative = metric_is_cumulative(path);
        }

        // Append samples (total_samples = ref + n_samples deltas)
        ms.timestamps_ms.reserve(ms.timestamps_ms.size() + total_samples);
        ms.values.reserve(ms.values.size() + total_samples);
        for (size_t s = 0; s < total_samples; ++s) {
            ms.timestamps_ms.push_back(timestamps[s]);
            ms.values.push_back(static_cast<double>(
                matrix[static_cast<size_t>(m)][s]));
        }
    }

    return true;
}

// ============================================================
//  available_memory_bytes — platform physical memory query
//  Returns an estimate of available physical RAM.
//  Falls back to SIZE_MAX (single-thread) if the query fails.
// ============================================================
static size_t available_memory_bytes() {
#if defined(__APPLE__)
    // macOS: use Mach VM statistics
    mach_port_t host = mach_host_self();
    vm_size_t page_size_v = 0;
    host_page_size(host, &page_size_v);
    vm_statistics64_data_t vm_stat;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(host, HOST_VM_INFO64,
                          reinterpret_cast<host_info64_t>(&vm_stat),
                          &count) != KERN_SUCCESS) {
        return SIZE_MAX;
    }
    uint64_t free_pages = vm_stat.free_count + vm_stat.inactive_count;
    return static_cast<size_t>(free_pages) * static_cast<size_t>(page_size_v);
#elif defined(_WIN32)
    // Windows: use GlobalMemoryStatusEx
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) return SIZE_MAX;
    return static_cast<size_t>(ms.ullAvailPhys);
#else
    // Linux/POSIX: use _SC_AVPHYS_PAGES
    long pages     = sysconf(_SC_AVPHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages <= 0 || page_size <= 0) return SIZE_MAX;
    return static_cast<size_t>(pages) * static_cast<size_t>(page_size);
#endif
}

// ============================================================
//  ChunkInfo — index entry for two-pass parallel decode
// ============================================================
struct ChunkInfo {
    size_t  file_offset;    // byte offset of this chunk in the file
    size_t  doc_size;       // total BSON document size (including 4-byte header)
    uint8_t chunk_type;     // 0 = metadata, 1 = data, 255 = unknown
    int     schema_version; // which metadata chunk's schema applies
};

// ============================================================
//  parse_file — top-level entry point
// ============================================================
bool FtdcParser::parse_file(const std::string& path,
                             MetricStore& store,
                             std::string& error_out)
{
    // Open file
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) {
        error_out = "Cannot open: " + path;
        return false;
    }

    // Get size
    std::fseek(fp, 0, SEEK_END);
    size_t file_size = static_cast<size_t>(std::ftell(fp));
    std::fseek(fp, 0, SEEK_SET);

    // Determine whether to use the parallel decode path.
    // Parallel decode only activates for files larger than half of available
    // physical memory (T-10-08: prevents memory amplification on small systems).
    bool use_parallel = (file_size > available_memory_bytes() / 2);

    // Schema from the last type-0 (metadata) chunk
    std::vector<std::string> schema_paths;
    std::vector<int64_t>     ref_values;
    int64_t                  chunk_start_ms = 0;

    size_t bytes_read = 0;
    bool   ok = true;

    // D-08: Pre-reserve persistent buffer to avoid early resize churn.
    // doc_buf_ is swapped into local_doc_buf so the parse loop works with a
    // plain local vector (no `this` member access in the hot path).
    std::vector<uint8_t> local_doc_buf;
    std::swap(local_doc_buf, doc_buf_);
    local_doc_buf.reserve(64 * 1024); // typical FTDC chunk size

    // ----------------------------------------------------------------
    //  PARALLEL PATH: two-pass decode for very large files
    // ----------------------------------------------------------------
    if (use_parallel) {
        // Pass 1: Sequential scan to build a chunk index.
        // Never decode across schema boundaries in parallel (Pitfall 4).
        std::vector<ChunkInfo> chunks;
        int schema_ver = -1; // -1 = no metadata seen yet

        size_t scan_pos = 0;
        while (scan_pos + 4 <= file_size) {
            uint8_t sz_buf[4];
            if (std::fread(sz_buf, 1, 4, fp) != 4) break;
            int32_t doc_size = read_i32(sz_buf);
            if (doc_size < 5 || doc_size > 128 * 1024 * 1024) break;

            ChunkInfo ci;
            ci.file_offset   = scan_pos;
            ci.doc_size      = static_cast<size_t>(doc_size);
            ci.schema_version = schema_ver;
            ci.chunk_type    = 255;

            // Peek at the chunk type field: read the chunk to inspect type byte.
            local_doc_buf.resize(static_cast<size_t>(doc_size));
            std::memcpy(local_doc_buf.data(), sz_buf, 4);
            size_t rem = static_cast<size_t>(doc_size) - 4;
            if (std::fread(local_doc_buf.data() + 4, 1, rem, fp) != rem) break;

            // Quick scan for "type" field in the outer BSON doc
            const uint8_t* p2  = local_doc_buf.data() + 4;
            const uint8_t* end2 = local_doc_buf.data() + doc_size;
            while (p2 < end2) {
                uint8_t ft = *p2++;
                if (ft == 0) break;
                const uint8_t* ks = p2;
                while (p2 < end2 && *p2) ++p2;
                if (p2 >= end2) break;
                std::string fk(reinterpret_cast<const char*>(ks), p2 - ks);
                ++p2;
                if (fk == "type" && ft == bson_type::INT32 && p2 + 4 <= end2) {
                    ci.chunk_type = static_cast<uint8_t>(read_i32(p2));
                    break;
                }
                // Skip value
                ptrdiff_t fs = bson_value_fixed_size(ft);
                if (fs >= 0) { p2 += fs; }
                else if (ft == bson_type::UTF8) {
                    if (p2 + 4 > end2) break;
                    int32_t sl = read_i32(p2);
                    if (sl < 0 || p2 + 4 + sl > end2) break;
                    p2 += 4 + sl;
                } else if (ft == bson_type::BINARY) {
                    if (p2 + 5 > end2) break;
                    int32_t bl = read_i32(p2);
                    if (bl < 0 || p2 + 4 + 1 + bl > end2) break;
                    p2 += 4 + 1 + bl;
                } else if (ft == bson_type::DOCUMENT || ft == bson_type::ARRAY) {
                    if (p2 + 4 > end2) break;
                    int32_t dl2 = read_i32(p2);
                    if (dl2 < 5 || p2 + dl2 > end2) break;
                    p2 += dl2;
                } else { break; }
            }

            if (ci.chunk_type == 0) {
                ++schema_ver;
                ci.schema_version = schema_ver;
            } else {
                ci.schema_version = schema_ver;
            }

            chunks.push_back(ci);
            scan_pos += static_cast<size_t>(doc_size);

            if (progress_cb_)
                progress_cb_(scan_pos / 2, file_size); // phase 1 = first half of progress
        }

        // Group data chunks by schema_version.
        // For each schema group: decode metadata first (sequential),
        // then dispatch data chunks to ThreadPool workers.
        //
        // Find distinct schema versions
        int max_schema = schema_ver;
        if (max_schema < 0) {
            // No metadata found — fall through to sequential path
            use_parallel = false;
        }

        if (use_parallel) {
        // Per-schema metadata: parsed schema_paths and ref_values
        struct SchemaData {
            std::vector<std::string> paths;
            std::vector<int64_t>     refs;
            int64_t                  start_ms = 0;
        };
        std::vector<SchemaData> schemas(static_cast<size_t>(max_schema + 1));

        // Decode each metadata chunk sequentially to extract schema
        for (const auto& ci : chunks) {
            if (ci.chunk_type != 0) continue;
            int sv = ci.schema_version;
            if (sv < 0 || sv > max_schema) continue;

            // Re-read this chunk from file
            std::fseek(fp, static_cast<long>(ci.file_offset), SEEK_SET);
            local_doc_buf.resize(ci.doc_size);
            if (std::fread(local_doc_buf.data(), 1, ci.doc_size, fp) != ci.doc_size) continue;

            // Full parse of this metadata chunk to extract schema
            const uint8_t* p3   = local_doc_buf.data() + 4;
            const uint8_t* end3 = local_doc_buf.data() + ci.doc_size;
            const uint8_t* doc_ptr = nullptr;
            int32_t        doc_sz  = 0;
            int64_t        start_v = 0;

            while (p3 < end3) {
                uint8_t ft = *p3++;
                if (ft == 0) break;
                const uint8_t* ks = p3;
                while (p3 < end3 && *p3) ++p3;
                if (p3 >= end3) break;
                std::string fk(reinterpret_cast<const char*>(ks), p3 - ks);
                ++p3;

                if (fk == "doc" && ft == bson_type::DOCUMENT && p3 + 4 <= end3) {
                    doc_ptr = p3;
                    doc_sz  = read_i32(p3);
                    if (doc_sz < 5 || p3 + doc_sz > end3) { doc_ptr = nullptr; }
                    else p3 += doc_sz;
                } else if ((fk == "_id" || fk == "start") && ft == bson_type::DATE && p3 + 8 <= end3) {
                    if (start_v == 0) start_v = read_i64(p3);
                    p3 += 8;
                } else {
                    ptrdiff_t fs = bson_value_fixed_size(ft);
                    if (fs >= 0) { if (p3 + fs > end3) break; p3 += fs; }
                    else if (ft == bson_type::UTF8) {
                        if (p3 + 4 > end3) break;
                        int32_t sl = read_i32(p3);
                        if (sl < 0 || p3 + 4 + sl > end3) break;
                        p3 += 4 + sl;
                    } else if (ft == bson_type::BINARY) {
                        if (p3 + 5 > end3) break;
                        int32_t bl = read_i32(p3);
                        if (bl < 0 || p3 + 4 + 1 + bl > end3) break;
                        p3 += 4 + 1 + bl;
                    } else if (ft == bson_type::DOCUMENT || ft == bson_type::ARRAY) {
                        if (p3 + 4 > end3) break;
                        int32_t dl2 = read_i32(p3);
                        if (dl2 < 5 || p3 + dl2 > end3) break;
                        p3 += dl2;
                    } else break;
                }
            }

            if (doc_ptr && doc_sz >= 5) {
                std::vector<MetricLeaf> leaves;
                extract_metrics(doc_ptr, static_cast<size_t>(doc_sz), "", leaves);
                SchemaData& sd = schemas[static_cast<size_t>(sv)];
                sd.paths.reserve(leaves.size());
                sd.refs.reserve(leaves.size());
                for (auto& lf : leaves) {
                    sd.paths.push_back(std::move(lf.path));
                    sd.refs.push_back(lf.value);
                }
                sd.start_ms = start_v;
            }
        }

        // Pass 2: Dispatch data chunks to ThreadPool workers by schema version.
        // Each worker decodes a contiguous range of data chunks with the same schema.
        {
            // Collect data chunks grouped by schema version
            struct DataGroup {
                int                      schema_ver;
                std::vector<ChunkInfo>   chunks;
            };

            std::vector<DataGroup> groups;
            for (const auto& ci : chunks) {
                if (ci.chunk_type != 1) continue;
                int sv = ci.schema_version;
                if (sv < 0 || sv > max_schema) continue;
                if (groups.empty() || groups.back().schema_ver != sv) {
                    groups.push_back({sv, {}});
                }
                groups.back().chunks.push_back(ci);
            }

            size_t nthreads = std::min<size_t>(
                std::thread::hardware_concurrency(),
                groups.size());
            if (nthreads == 0) nthreads = 1;

            // Per-group MetricStores (one per group, decoded by pool workers)
            std::vector<MetricStore> group_stores(groups.size());

            {
                ThreadPool pool(nthreads);
                for (size_t gi = 0; gi < groups.size(); ++gi) {
                    pool.submit([gi, &groups, &schemas, &group_stores, &path]() {
                        const DataGroup& grp = groups[gi];
                        int sv = grp.schema_ver;
                        if (sv < 0 || sv >= static_cast<int>(schemas.size())) return;
                        const SchemaData& sd = schemas[static_cast<size_t>(sv)];
                        MetricStore& ms = group_stores[gi];

                        // Open file independently per worker thread
                        FILE* wfp = std::fopen(path.c_str(), "rb");
                        if (!wfp) return;

                        std::vector<uint8_t> wbuf;
                        wbuf.reserve(64 * 1024);

                        for (const auto& ci : grp.chunks) {
                            std::fseek(wfp, static_cast<long>(ci.file_offset), SEEK_SET);
                            wbuf.resize(ci.doc_size);
                            if (std::fread(wbuf.data(), 1, ci.doc_size, wfp) != ci.doc_size) continue;

                            // Parse the outer BSON to get "data" binary field
                            const uint8_t* wp  = wbuf.data() + 4;
                            const uint8_t* we  = wbuf.data() + ci.doc_size;
                            const uint8_t* data_ptr  = nullptr;
                            int32_t        data_size  = 0;
                            int64_t        start_val  = 0;

                            while (wp < we) {
                                uint8_t ft = *wp++;
                                if (ft == 0) break;
                                const uint8_t* ks = wp;
                                while (wp < we && *wp) ++wp;
                                if (wp >= we) break;
                                std::string fk(reinterpret_cast<const char*>(ks), wp - ks);
                                ++wp;
                                if (fk == "data" && ft == bson_type::BINARY && wp + 5 <= we) {
                                    int32_t bl = read_i32(wp); wp += 4;
                                    wp += 1; // subtype
                                    if (bl >= 0 && wp + bl <= we) {
                                        data_ptr = wp;
                                        data_size = bl;
                                        wp += bl;
                                    }
                                } else if ((fk == "_id" || fk == "start") && ft == bson_type::DATE && wp + 8 <= we) {
                                    if (start_val == 0) start_val = read_i64(wp);
                                    wp += 8;
                                } else {
                                    ptrdiff_t fs = bson_value_fixed_size(ft);
                                    if (fs >= 0) { if (wp + fs > we) break; wp += fs; }
                                    else if (ft == bson_type::UTF8) {
                                        if (wp + 4 > we) break;
                                        int32_t sl = read_i32(wp);
                                        if (sl < 0 || wp + 4 + sl > we) break;
                                        wp += 4 + sl;
                                    } else if (ft == bson_type::BINARY) {
                                        if (wp + 5 > we) break;
                                        int32_t bl2 = read_i32(wp);
                                        if (bl2 < 0 || wp + 4 + 1 + bl2 > we) break;
                                        wp += 4 + 1 + bl2;
                                    } else if (ft == bson_type::DOCUMENT || ft == bson_type::ARRAY) {
                                        if (wp + 4 > we) break;
                                        int32_t dl2 = read_i32(wp);
                                        if (dl2 < 5 || wp + dl2 > we) break;
                                        wp += dl2;
                                    } else break;
                                }
                            }

                            if (!data_ptr || data_size < 5) continue;

                            // Decompress
                            std::vector<uint8_t> decompressed;
                            std::string zerr;
                            const uint8_t* zlib_data = data_ptr + 4;
                            int32_t zlib_len = data_size - 4;
                            if (zlib_len <= 0) continue;
                            if (!zlib_decompress(zlib_data, static_cast<size_t>(zlib_len), decompressed, zerr)) continue;

                            const uint8_t* dp = decompressed.data();
                            size_t         dl = decompressed.size();
                            if (dl < 13) continue;

                            // Extract per-chunk schema from embedded reference doc
                            int32_t ref_doc_size = read_i32(dp);
                            if (ref_doc_size < 5 || static_cast<size_t>(ref_doc_size) > dl) continue;

                            std::vector<MetricLeaf> leaves;
                            if (!extract_metrics(dp, static_cast<size_t>(ref_doc_size), "", leaves)) continue;

                            std::vector<std::string> chunk_paths;
                            std::vector<int64_t>     chunk_refs;
                            chunk_paths.reserve(leaves.size());
                            chunk_refs.reserve(leaves.size());
                            for (auto& lf : leaves) {
                                chunk_paths.push_back(std::move(lf.path));
                                chunk_refs.push_back(lf.value);
                            }

                            dp += ref_doc_size;
                            dl -= static_cast<size_t>(ref_doc_size);
                            if (dl < 8) continue;

                            int32_t n_metrics_chunk = read_i32(dp); dp += 4; dl -= 4;
                            int32_t n_deltas        = read_i32(dp); dp += 4; dl -= 4;
                            if (n_metrics_chunk <= 0 || n_deltas <= 0) continue;

                            int64_t ts_start = start_val;
                            if (ts_start == 0) ts_start = sd.start_ms;

                            decode_data_chunk(dp, dl,
                                              n_metrics_chunk, n_deltas,
                                              chunk_paths, chunk_refs,
                                              ts_start, ms);
                        }
                        std::fclose(wfp);
                    });
                }
                pool.wait_all();
                pool.stop();
            }

            // Merge per-group MetricStores into the final store (in order)
            for (size_t gi = 0; gi < group_stores.size(); ++gi) {
                MetricStore& src = group_stores[gi];
                for (const auto& key : src.ordered_keys) {
                    auto it = src.series.find(key);
                    if (it == src.series.end()) continue;
                    MetricSeries& src_series = it->second;
                    MetricSeries& dst_series = store.get_or_create(key);
                    if (dst_series.display_name.empty()) {
                        dst_series.display_name  = src_series.display_name;
                        dst_series.unit          = src_series.unit;
                        dst_series.is_cumulative = src_series.is_cumulative;
                    }
                    dst_series.timestamps_ms.insert(
                        dst_series.timestamps_ms.end(),
                        src_series.timestamps_ms.begin(),
                        src_series.timestamps_ms.end());
                    dst_series.values.insert(
                        dst_series.values.end(),
                        src_series.values.begin(),
                        src_series.values.end());
                }
            }
        }

        // Swap local buffer back to member
        std::swap(local_doc_buf, doc_buf_);
        std::fclose(fp);
        store.update_time_range();
        return ok;
        } // end if (use_parallel) — inner block
    } // end outer parallel branch: if (use_parallel) after index scan

    while (bytes_read + 4 <= file_size) {
        // Read BSON document size
        uint8_t size_buf[4];
        if (std::fread(size_buf, 1, 4, fp) != 4) break;
        int32_t doc_size = read_i32(size_buf);

        if (doc_size < 5 || doc_size > 128 * 1024 * 1024) {
            // Corrupt or impossibly large; stop
            error_out = "Corrupt FTDC chunk (size=" + std::to_string(doc_size) + ")";
            ok = false;
            break;
        }

        // Read full BSON doc (we already read 4 bytes of the size)
        // D-08: Reuse persistent buffer — no per-chunk heap allocation
        local_doc_buf.resize(static_cast<size_t>(doc_size));
        std::memcpy(local_doc_buf.data(), size_buf, 4);
        size_t remaining = static_cast<size_t>(doc_size) - 4;
        if (std::fread(local_doc_buf.data() + 4, 1, remaining, fp) != remaining) {
            error_out = "Truncated FTDC chunk";
            ok = false;
            break;
        }

        bytes_read += static_cast<size_t>(doc_size);

        if (progress_cb_)
            progress_cb_(bytes_read, file_size);

        // Parse the outer BSON doc to get "type", "_id"/"doc"/"data"
        uint8_t chunk_type = 255;
        const uint8_t* data_ptr  = nullptr;
        int32_t        data_size = 0;
        int64_t        start_val = 0;

        // Walk top-level doc fields with bounds checks (#15)
        const uint8_t* p   = local_doc_buf.data() + 4;
        const uint8_t* end = local_doc_buf.data() + doc_size;
        while (p < end) {
            uint8_t ftype = *p++;
            if (ftype == 0) break;

            // Key
            const uint8_t* key_start = p;
            while (p < end && *p) ++p;
            if (p >= end) break;
            std::string fkey(reinterpret_cast<const char*>(key_start), p - key_start);
            ++p;

            if (fkey == "type" && ftype == bson_type::INT32) {
                if (p + 4 > end) break;
                chunk_type = static_cast<uint8_t>(read_i32(p)); p += 4;
            } else if (fkey == "type" && ftype == bson_type::INT64) {
                if (p + 8 > end) break;
                chunk_type = static_cast<uint8_t>(read_i64(p)); p += 8;
            } else if (fkey == "_id" && ftype == bson_type::DATE) {
                if (p + 8 > end) break;
                start_val = read_i64(p); p += 8;
            } else if (fkey == "doc" && ftype == bson_type::DOCUMENT) {
                if (p + 4 > end) break;
                data_ptr  = p;
                data_size = read_i32(p);
                if (data_size < 5 || p + data_size > end) { data_ptr = nullptr; break; }
                p += data_size;
            } else if (fkey == "data" && ftype == bson_type::BINARY) {
                if (p + 5 > end) break;
                int32_t bin_len = read_i32(p); p += 4;
                p += 1; // subtype
                if (bin_len < 0 || p + bin_len > end) break;
                data_ptr  = p;
                data_size = bin_len;
                p += bin_len;
            } else if (fkey == "start" && ftype == bson_type::DATE) {
                if (p + 8 > end) break;
                if (start_val == 0) start_val = read_i64(p);
                p += 8;
            } else {
                // Skip value using known BSON sizes
                ptrdiff_t fs = bson_value_fixed_size(ftype);
                if (fs >= 0) {
                    if (p + fs > end) break;
                    p += fs;
                } else if (ftype == bson_type::UTF8) {
                    if (p + 4 > end) break;
                    int32_t slen = read_i32(p);
                    if (slen < 0 || p + 4 + slen > end) break;
                    p += 4 + slen;
                } else if (ftype == bson_type::BINARY) {
                    if (p + 5 > end) break;
                    int32_t blen = read_i32(p);
                    if (blen < 0 || p + 4 + 1 + blen > end) break;
                    p += 4 + 1 + blen;
                } else if (ftype == bson_type::DOCUMENT || ftype == bson_type::ARRAY) {
                    if (p + 4 > end) break;
                    int32_t dlen = read_i32(p);
                    if (dlen < 5 || p + dlen > end) break;
                    p += dlen;
                } else {
                    break; // unknown type — stop parsing this chunk
                }
            }
        }

        // ---- Process by chunk type ----
        if (chunk_type == 0) {
            // Metadata chunk: re-build schema from the "doc" field
            if (!data_ptr || data_size < 5) {
                continue;
            }
            schema_paths.clear();
            ref_values.clear();

            chunk_start_ms = start_val;

            std::vector<MetricLeaf> leaves;
            if (!extract_metrics(data_ptr, static_cast<size_t>(data_size),
                                  "", leaves)) {
                continue; // skip malformed chunk
            }

            schema_paths.reserve(leaves.size());
            ref_values.reserve(leaves.size());
            for (auto& leaf : leaves) {
                schema_paths.push_back(std::move(leaf.path));
                ref_values.push_back(leaf.value);
            }

        } else if (chunk_type == 1) {
            // Data chunk: decompress + delta-decode
            if (!data_ptr || data_size <= 0) {
                continue;
            }

            // FTDC binary payload layout:
            //   int32  uncompressed_size  (4 bytes, little-endian)
            //   byte[] zlib_compressed_data
            if (data_size < 5) continue;
            const uint8_t* zlib_data = data_ptr + 4;
            int32_t zlib_len = data_size - 4;

            std::vector<uint8_t> decompressed;
            std::string zerr;
            if (!zlib_decompress(zlib_data,
                                  static_cast<size_t>(zlib_len),
                                  decompressed, zerr)) {
                continue;
            }

            const uint8_t* dp = decompressed.data();
            size_t         dl = decompressed.size();
            if (dl < 5) continue;

            // Decompressed layout:
            //   1. Embedded reference BSON document (full snapshot of metrics)
            //   2. int32 nMetrics
            //   3. int32 nSamples  (number of DELTA rows; total = nSamples + 1)
            //   4. Delta-encoded varint data (nMetrics × nSamples values)
            //
            // Extract the embedded reference doc to get schema + initial values.
            int32_t ref_doc_size = read_i32(dp);
            if (ref_doc_size < 5 || static_cast<size_t>(ref_doc_size) > dl) continue;

            // Extract schema from the embedded reference doc
            std::vector<MetricLeaf> leaves;
            if (!extract_metrics(dp, static_cast<size_t>(ref_doc_size), "", leaves)) {
                continue;
            }

            // Rebuild schema from this chunk's reference doc
            schema_paths.clear();
            ref_values.clear();
            schema_paths.reserve(leaves.size());
            ref_values.reserve(leaves.size());
            for (auto& leaf : leaves) {
                schema_paths.push_back(std::move(leaf.path));
                ref_values.push_back(leaf.value);
            }

            // Advance past the reference doc
            dp += ref_doc_size;
            dl -= static_cast<size_t>(ref_doc_size);
            if (dl < 8) {
                continue;
            }

            int32_t n_metrics_chunk = read_i32(dp); dp += 4; dl -= 4;
            int32_t n_deltas        = read_i32(dp); dp += 4; dl -= 4;

            if (n_metrics_chunk <= 0 || n_deltas <= 0) continue;

            // The _id field contains the start timestamp for this chunk
            int64_t ts_start = start_val; // _id date from outer doc
            if (ts_start == 0) ts_start = chunk_start_ms;

            decode_data_chunk(dp, dl,
                              n_metrics_chunk, n_deltas,
                              schema_paths, ref_values,
                              ts_start,
                              store);
        }
        // chunk_type 2+ = reserved/metadata update, skip
    }

    // D-08: Swap back so doc_buf_ retains the allocation for next parse_file() call.
    std::swap(local_doc_buf, doc_buf_);

    std::fclose(fp);
    store.update_time_range();
    return ok;
}
