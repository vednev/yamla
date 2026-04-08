#include "log_parser.hpp"
#include "query_shape.hpp"

#include <simdjson.h>
#include <mutex>
#include <thread>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cassert>
#include <algorithm>
#include <chrono>
#include <atomic>

// ------------------------------------------------------------
//  Thread-local simdjson parser — no allocation contention
// ------------------------------------------------------------
static thread_local simdjson::dom::parser tl_parser;

// ------------------------------------------------------------
//  Helpers
// ------------------------------------------------------------

// Parse ISO-8601 timestamp to milliseconds since Unix epoch.
// MongoDB 4.4+ format: "2024-01-15T10:23:45.123+00:00"
// Returns -1 on parse failure.
static int64_t parse_timestamp(std::string_view ts) {
    if (ts.size() < 24) return -1;

    struct tm t{};
    int ms = 0;
    // Parse: YYYY-MM-DDTHH:MM:SS.mmm
    if (std::sscanf(ts.data(),
                    "%4d-%2d-%2dT%2d:%2d:%2d.%3d",
                    &t.tm_year, &t.tm_mon, &t.tm_mday,
                    &t.tm_hour, &t.tm_min, &t.tm_sec, &ms) != 7) {
        return -1;
    }
    t.tm_year -= 1900;
    t.tm_mon  -= 1;
    t.tm_isdst = 0;

#if defined(_WIN32)
    time_t epoch = _mkgmtime(&t);
#else
    time_t epoch = timegm(&t);
#endif
    if (epoch == (time_t)-1) return -1;
    return static_cast<int64_t>(epoch) * 1000 + ms;
}

// Extract a uint32_t connection number from a string like "conn123"
static uint32_t parse_conn_id(std::string_view ctx) {
    // ctx looks like "conn1234" or "conn1234[...]"
    size_t pos = ctx.find("conn");
    if (pos == std::string_view::npos) return 0;
    pos += 4;
    uint32_t id = 0;
    while (pos < ctx.size() && ctx[pos] >= '0' && ctx[pos] <= '9') {
        id = id * 10 + static_cast<uint32_t>(ctx[pos] - '0');
        ++pos;
    }
    return id;
}

// Map MongoDB op strings to canonical names
static std::string_view normalize_op(std::string_view cmd) {
    if (cmd == "find")        return "find";
    if (cmd == "insert")      return "insert";
    if (cmd == "update")      return "update";
    if (cmd == "delete")      return "delete";
    if (cmd == "aggregate")   return "aggregate";
    if (cmd == "count")       return "count";
    if (cmd == "distinct")    return "distinct";
    if (cmd == "getMore")     return "getMore";
    if (cmd == "findAndModify" ||
        cmd == "findandmodify") return "findAndModify";
    if (cmd == "createIndexes") return "createIndexes";
    if (cmd == "drop")        return "drop";
    if (cmd == "listCollections") return "listCollections";
    if (cmd == "ping")        return "ping";
    if (cmd == "hello" ||
        cmd == "isMaster" ||
        cmd == "ismaster")   return "hello";
    if (cmd == "replSetGetStatus") return "replSetGetStatus";
    return cmd; // unknown — keep as-is
}

// ------------------------------------------------------------
//  LogParser constructor
// ------------------------------------------------------------

LogParser::LogParser(StringTable& strings, Config cfg)
    : strings_(strings), cfg_(cfg)
{
    if (cfg_.num_threads == 0)
        cfg_.num_threads = std::max(1u, std::thread::hardware_concurrency());
}

// ------------------------------------------------------------
//  Batch splitting
// ------------------------------------------------------------

std::vector<ParseBatch> LogParser::split_batches(const char* data, size_t size,
                                                  size_t batch_size,
                                                  uint16_t node_idx)
{
    std::vector<ParseBatch> batches;
    if (size == 0) return batches;

    size_t pos = 0;
    while (pos < size) {
        size_t end = std::min(pos + batch_size, size);
        // Advance to the next newline so we don't split a JSON line
        if (end < size) {
            while (end < size && data[end] != '\n') ++end;
            if (end < size) ++end; // include the newline
        }
        ParseBatch b;
        b.start     = data + pos;
        b.length    = end - pos;
        b.file_base = pos;
        b.node_idx  = node_idx;
        batches.push_back(b);
        pos = end;
    }
    return batches;
}

// ------------------------------------------------------------
//  Single-line parse
// ------------------------------------------------------------

bool LogParser::parse_line(const char* line, size_t len, size_t file_offset,
                            uint16_t node_idx, LogEntry& entry)
{
    if (len == 0 || line[0] != '{') return false;

    simdjson::dom::element doc;
    if (tl_parser.parse(line, len).get(doc) != simdjson::SUCCESS) return false;

    // -- t (timestamp) --
    simdjson::dom::element t_field;
    if (doc["t"].get(t_field) != simdjson::SUCCESS) return false;
    std::string_view ts_str;
    simdjson::dom::element date_el;
    if (t_field["$date"].get(date_el) == simdjson::SUCCESS) {
        // {"$date": "2024-..."}
        if (date_el.get_string().get(ts_str) != simdjson::SUCCESS) return false;
    } else {
        // plain string
        if (t_field.get_string().get(ts_str) != simdjson::SUCCESS) return false;
    }
    entry.timestamp_ms = parse_timestamp(ts_str);

    // -- s (severity) --
    std::string_view sev_str;
    if (doc["s"].get_string().get(sev_str) == simdjson::SUCCESS && !sev_str.empty())
        entry.severity = severity_from_char(sev_str[0]);

    // -- c (component) --
    {
        std::string_view comp;
        if (doc["c"].get_string().get(comp) == simdjson::SUCCESS) {
            std::lock_guard<std::mutex> lk(strings_mutex_);
            entry.component_idx = strings_.intern(comp);
        }
    }

    // -- ctx (context, contains conn ID) --
    {
        std::string_view ctx;
        if (doc["ctx"].get_string().get(ctx) == simdjson::SUCCESS) {
            entry.conn_id = parse_conn_id(ctx);
        }
    }

    // -- msg --
    {
        std::string_view msg;
        if (doc["msg"].get_string().get(msg) == simdjson::SUCCESS) {
            std::lock_guard<std::mutex> lk(strings_mutex_);
            entry.msg_idx = strings_.intern(msg);
        }
    }

    // -- attr (attributes) --
    simdjson::dom::element attr;
    if (doc["attr"].get(attr) == simdjson::SUCCESS) {

        // namespace
        std::string_view ns;
        if (attr["ns"].get_string().get(ns) == simdjson::SUCCESS) {
            std::lock_guard<std::mutex> lk(strings_mutex_);
            entry.ns_idx = strings_.intern(ns);
        }

        // durationMillis
        int64_t dur = 0;
        if (attr["durationMillis"].get_int64().get(dur) == simdjson::SUCCESS)
            entry.duration_ms = static_cast<int32_t>(dur);

        // command name — first key of the "command" sub-object
        simdjson::dom::element cmd_obj;
        if (attr["command"].get(cmd_obj) == simdjson::SUCCESS &&
            cmd_obj.type() == simdjson::dom::element_type::OBJECT)
        {
            for (auto [k, v] : cmd_obj.get_object().value()) {
                if (k.empty() || k[0] == '$') continue;
                auto op = normalize_op(k);
                std::lock_guard<std::mutex> lk(strings_mutex_);
                entry.op_type_idx = strings_.intern(op);
                break;
            }

            // Query shape — normalize the "filter" or "query" sub-object
            simdjson::dom::element filter;
            bool has_filter =
                attr["command"]["filter"].get(filter) == simdjson::SUCCESS ||
                attr["command"]["query"].get(filter) == simdjson::SUCCESS;

            if (has_filter) {
                // Serialize filter to JSON string, then normalize
                std::string filter_json = simdjson::to_string(filter);
                std::string shape = QueryShapeNormalizer::normalize(filter_json);
                if (!shape.empty()) {
                    std::lock_guard<std::mutex> lk(strings_mutex_);
                    entry.shape_idx = strings_.intern(shape);
                }
            }
        }

        // Driver info (inside client metadata on "hello" / handshake messages)
        simdjson::dom::element client;
        if (attr["client"].get(client) == simdjson::SUCCESS) {
            simdjson::dom::element driver;
            if (client["driver"].get(driver) == simdjson::SUCCESS) {
                std::string_view drv_name, drv_ver;
                (void)driver["name"].get_string().get(drv_name);
                (void)driver["version"].get_string().get(drv_ver);
                if (!drv_name.empty()) {
                    std::string full;
                    full.reserve(drv_name.size() + 1 + drv_ver.size());
                    full += drv_name;
                    if (!drv_ver.empty()) { full += ' '; full += drv_ver; }
                    std::lock_guard<std::mutex> lk(strings_mutex_);
                    entry.driver_idx = strings_.intern(full);
                }
            }
        }
    }

    entry.raw_offset = static_cast<uint32_t>(file_offset);
    entry.raw_len    = static_cast<uint32_t>(len);
    entry.node_idx   = node_idx;
    entry.node_mask  = 1u << node_idx;

    return true;
}

// ------------------------------------------------------------
//  Batch parse — runs on a worker thread
// ------------------------------------------------------------

ParseResult LogParser::parse_batch(const ParseBatch& batch) {
    ParseResult result;
    result.entries.reserve(4096);

    const char* p   = batch.start;
    const char* end = batch.start + batch.length;

    while (p < end) {
        // Find end of line
        const char* nl = static_cast<const char*>(
            std::memchr(p, '\n', static_cast<size_t>(end - p)));
        const char* line_end = nl ? nl : end;
        size_t      line_len = static_cast<size_t>(line_end - p);
        size_t      offset   = static_cast<size_t>(p - batch.start) + batch.file_base;

        // Trim trailing \r
        if (line_len > 0 && p[line_len - 1] == '\r') --line_len;

        LogEntry entry{};
        if (line_len > 0) {
            if (parse_line(p, line_len, offset, batch.node_idx, entry)) {
                result.entries.push_back(entry);
                ++result.lines_ok;
            } else {
                ++result.lines_failed;
            }
        }

        p = nl ? nl + 1 : end;
    }

    return result;
}

// ------------------------------------------------------------
//  parse_file — public entry point
// ------------------------------------------------------------

void LogParser::parse_file(const MmapFile& file, uint16_t node_idx,
                            ArenaVector<LogEntry>& out,
                            ProgressCb progress_cb)
{
    if (file.size() == 0) return;

    auto batches = split_batches(file.data(), file.size(),
                                 cfg_.batch_size_bytes, node_idx);

    // Estimate total lines for progress reporting (~80 chars/line average)
    size_t estimated_lines = std::max<size_t>(1, file.size() / 80);

    // Results collected in order
    std::vector<ParseResult> results(batches.size());

    // Dispatch batches to threads
    std::vector<std::thread> workers;
    workers.reserve(cfg_.num_threads);

    // Use an atomic index to let threads pick up batches dynamically
    std::atomic<size_t> next_batch{0};
    std::atomic<size_t> done_lines{0};

    auto worker_fn = [&] {
        while (true) {
            size_t idx = next_batch.fetch_add(1, std::memory_order_relaxed);
            if (idx >= batches.size()) break;

            results[idx] = parse_batch(batches[idx]);
            done_lines.fetch_add(results[idx].lines_ok + results[idx].lines_failed,
                                 std::memory_order_relaxed);
        }
    };

    size_t nthreads = std::min<size_t>(cfg_.num_threads, batches.size());
    for (size_t i = 0; i < nthreads; ++i)
        workers.emplace_back(worker_fn);

    // Poll progress while workers run
    if (progress_cb) {
        while (true) {
            size_t done = done_lines.load(std::memory_order_relaxed);
            progress_cb(done, estimated_lines);
            bool all_done = (next_batch.load() >= batches.size());
            // Small sleep — this is the UI thread polling
            if (all_done) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    for (auto& t : workers) t.join();

    // Accumulate stats and append entries to output
    for (auto& r : results) {
        lines_ok_.fetch_add(r.lines_ok, std::memory_order_relaxed);
        lines_failed_.fetch_add(r.lines_failed, std::memory_order_relaxed);
        for (auto& e : r.entries) out.push_back(e);
    }
}
