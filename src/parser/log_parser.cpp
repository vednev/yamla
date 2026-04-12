#include "log_parser.hpp"
#include "query_shape.hpp"

#include <simdjson.h>
#include <mutex>
#include <thread>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <atomic>
#include <unordered_map>
#include <string_view>
#include <string>
#include <vector>
#include <deque>

// ------------------------------------------------------------
//  Thread-local simdjson parser
// ------------------------------------------------------------
static thread_local simdjson::dom::parser tl_parser;

// ------------------------------------------------------------
//  Thread-local string batch
//
//  Workers accumulate all strings for a batch without locks.
//  After the batch, one lock commits everything into the global
//  StringTable in a single critical section.
//
//  owned: deque<string> — push_back never invalidates existing
//         elements, so string_view keys never dangle.
// ------------------------------------------------------------
struct StringBatch {
    std::deque<std::string>                                owned;
    std::unordered_map<std::string_view, uint32_t, SvHash> cache;

    void clear() { owned.clear(); cache.clear(); }

    // Stage a string by value — copies into `owned` so the key is stable
    // regardless of where the source string_view points (simdjson tape,
    // mmap, or stack).  Returns the stable string_view into owned storage.
    std::string_view stage(std::string_view sv) {
        if (sv.empty()) return {};
        // Check if already staged (dedup by value)
        auto it = cache.find(sv);
        if (it != cache.end()) return it->first; // return existing stable view
        // Copy into owned for stable key
        owned.emplace_back(sv.data(), sv.size());
        std::string_view stable(owned.back());
        cache.emplace(stable, 0u);
        return stable;
    }

    std::string_view stage_owned(std::string&& s) {
        if (s.empty()) return {};
        // Check dedup before moving
        auto it = cache.find(std::string_view(s));
        if (it != cache.end()) return it->first;
        owned.push_back(std::move(s));
        std::string_view stable(owned.back());
        cache.emplace(stable, 0u);
        return stable;
    }

    uint32_t get(std::string_view sv) const {
        if (sv.empty()) return 0;
        auto it = cache.find(sv);
        return (it != cache.end()) ? it->second : 0;
    }
};

// Per-thread string batch (persistent — survives across batches)
static thread_local StringBatch tl_batch;

// Per-thread staging vectors — persistent across batches.
// NOTE: SVS holds string_views into tl_batch.owned (stable deque storage),
// NOT into simdjson's internal tape (which is invalidated per document).
struct SVS { std::string_view comp, msg, ns, op, shape, drv; };
static thread_local std::vector<LogEntry> tl_entries;
static thread_local std::vector<SVS>      tl_svs;

// ------------------------------------------------------------
//  Helpers
// ------------------------------------------------------------

// Pure-arithmetic UTC calendar → Unix epoch conversion.
// No system calls, no timezone locks. Valid for 1970–2099.
// On macOS, timegm() acquires a global timezone lock making it
// serialise across threads — this is the lock-free alternative.
static int64_t utc_to_epoch(int y, int mon, int d,
                              int h, int mi, int sec) noexcept
{
    // Rata Die algorithm: reduce Jan/Feb to months 13/14 of prior year
    y   -= (mon <= 2);
    int era  = y / 400;
    int yoe  = y - era * 400;                              // [0, 399]
    int doy  = (153*(mon + (mon<=2 ? 9 : -3)) + 2)/5 + d - 1; // [0, 365]
    int doe  = yoe*365 + yoe/4 - yoe/100 + doy;           // [0, 146096]
    int64_t days = static_cast<int64_t>(era)*146097 + doe - 719468;
    return days*86400 + h*3600 + mi*60 + sec;
}

static int64_t parse_timestamp(std::string_view ts) noexcept {
    if (ts.size() < 23) return -1;
    const char* s = ts.data();
    // Validate separators
    if (s[4]!='-'||s[7]!='-'||s[10]!='T'||
        s[13]!=':'||s[16]!=':'||s[19]!='.') return -1;

    auto d2=[s](int i) noexcept { return (s[i]-'0')*10+(s[i+1]-'0'); };
    auto d4=[s](int i) noexcept {
        return (s[i]-'0')*1000+(s[i+1]-'0')*100+
               (s[i+2]-'0')*10+(s[i+3]-'0');
    };

    int y=d4(0), mo=d2(5), day=d2(8);
    int h=d2(11), mi=d2(14), sec=d2(17);
    int ms=d2(20)*10+(s[22]-'0');

    return utc_to_epoch(y, mo, day, h, mi, sec) * 1000 + ms;
}

static uint32_t parse_conn_id(std::string_view ctx) {
    for (size_t i=0; i+4<=ctx.size(); ++i) {
        if (ctx[i]=='c'&&ctx[i+1]=='o'&&ctx[i+2]=='n'&&ctx[i+3]=='n') {
            uint32_t id=0;
            for (size_t p=i+4; p<ctx.size()&&ctx[p]>='0'&&ctx[p]<='9'; ++p)
                id=id*10+(uint32_t)(ctx[p]-'0');
            return id;
        }
    }
    return 0;
}

static std::string_view normalize_op(std::string_view cmd) {
    if (cmd=="find")            return "find";
    if (cmd=="insert")          return "insert";
    if (cmd=="update")          return "update";
    if (cmd=="delete")          return "delete";
    if (cmd=="aggregate")       return "aggregate";
    if (cmd=="count")           return "count";
    if (cmd=="distinct")        return "distinct";
    if (cmd=="getMore")         return "getMore";
    if (cmd=="findAndModify"||cmd=="findandmodify") return "findAndModify";
    if (cmd=="createIndexes")   return "createIndexes";
    if (cmd=="drop")            return "drop";
    if (cmd=="listCollections") return "listCollections";
    if (cmd=="ping")            return "ping";
    if (cmd=="hello"||cmd=="isMaster"||cmd=="ismaster") return "hello";
    if (cmd=="replSetGetStatus")return "replSetGetStatus";
    return cmd;
}

// ------------------------------------------------------------
//  LogParser
// ------------------------------------------------------------
LogParser::LogParser(StringTable& strings, Config cfg)
    : strings_(strings), cfg_(cfg)
{
    if (cfg_.num_threads == 0)
        cfg_.num_threads = std::max(1u, std::thread::hardware_concurrency());
}

std::vector<ParseBatch> LogParser::split_batches(const char* data, size_t size,
                                                  size_t batch_size, uint16_t node_idx)
{
    std::vector<ParseBatch> batches;
    if (size == 0) return batches;
    size_t pos = 0;
    while (pos < size) {
        size_t end = std::min(pos + batch_size, size);
        if (end < size) {
            while (end < size && data[end] != '\n') ++end;
            if (end < size) ++end;
        }
        batches.push_back({data + pos, end - pos, pos, node_idx});
        pos = end;
    }
    return batches;
}

// Kept for header compatibility — unused in new impl
void LogParser::reconcile_cache(LocalInternCache&) {}

// ------------------------------------------------------------
//  parse_batch
//
//  Uses thread-local persistent buffers to avoid per-batch
//  heap allocation.  Flow:
//    1. parse_many: iterate all docs, extract fields into
//       tl_entries + tl_svs (no locks, no alloc beyond
//       initial growth to high-water-mark)
//    2. ONE mutex acquire: commit all unique strings to the
//       global StringTable
//    3. Fill LogEntry indices in-place from local cache
//    4. Swap tl_entries contents into result (move, O(1))
// ------------------------------------------------------------
ParseResult LogParser::parse_batch(const ParseBatch& batch) {
    ParseResult result;

    tl_batch.clear();
    tl_entries.clear();
    tl_svs.clear();

    simdjson::dom::document_stream stream;
    if (tl_parser.parse_many(batch.start, batch.length,
                              batch.length + 1).get(stream) != simdjson::SUCCESS) {
        ++result.lines_failed;
        return result;
    }

    // Sampling: accept every sample_step-th document.
    // sample_step = 1 means keep everything (no sampling).
    const uint32_t sample_step = (cfg_.sample_ratio >= 1.0f || cfg_.sample_ratio <= 0.0f)
        ? 1u
        : static_cast<uint32_t>(1.0f / cfg_.sample_ratio + 0.5f);
    uint32_t sample_counter = 0;

    for (auto it = stream.begin(); it != stream.end(); ++it) {
        simdjson::dom::element doc;
        if ((*it).get(doc) != simdjson::SUCCESS) { ++result.lines_failed; continue; }

        // Apply sampling — skip entries that don't fall on a keep step
        if (sample_step > 1) {
            if (sample_counter++ % sample_step != 0) continue;
        } else {
            ++sample_counter;
        }

        LogEntry e{};
        SVS      sv{};

        e.node_idx   = batch.node_idx;
        e.node_mask  = (batch.node_idx < 32) ? (1u << batch.node_idx) : 0u;
        e.raw_offset = static_cast<uint64_t>(batch.file_base + it.current_index());

        // timestamp
        simdjson::dom::element t_field;
        if (doc["t"].get(t_field) != simdjson::SUCCESS) { ++result.lines_failed; continue; }
        std::string_view ts;
        simdjson::dom::element date_el;
        if (t_field["$date"].get(date_el) == simdjson::SUCCESS)
            (void)date_el.get_string().get(ts);
        else
            (void)t_field.get_string().get(ts);
        e.timestamp_ms = parse_timestamp(ts);

        // severity — read immediately from the tape and convert to enum;
        // we only need the first character so no string copy is needed.
        {
            std::string_view sev;
            if (doc["s"].get_string().get(sev) == simdjson::SUCCESS && !sev.empty())
                e.severity = severity_from_char(sev[0]);
        }

        // All remaining string fields are staged via tl_batch.stage() which
        // copies bytes into the deque for stability across iterator advances.
        std::string_view v;
        if (doc["c"].get_string().get(v)  == simdjson::SUCCESS) sv.comp = tl_batch.stage(v);
        if (doc["ctx"].get_string().get(v) == simdjson::SUCCESS) e.conn_id = parse_conn_id(v);
        if (doc["msg"].get_string().get(v) == simdjson::SUCCESS) sv.msg  = tl_batch.stage(v);

        simdjson::dom::element attr;
        if (doc["attr"].get(attr) == simdjson::SUCCESS) {
            if (attr["ns"].get_string().get(v) == simdjson::SUCCESS)
                sv.ns = tl_batch.stage(v);

            int64_t dur = 0;
            if (attr["durationMillis"].get_int64().get(dur) == simdjson::SUCCESS)
                e.duration_ms = static_cast<int32_t>(dur);

            simdjson::dom::element cmd_obj;
            if (attr["command"].get(cmd_obj) == simdjson::SUCCESS &&
                cmd_obj.type() == simdjson::dom::element_type::OBJECT)
            {
                for (auto [k, val] : cmd_obj.get_object().value()) {
                    if (k.empty() || k[0]=='$') continue;
                    // normalize_op returns string_view into static storage
                    // (for known ops) or into the simdjson tape (for unknown).
                    // Stage it so we always get a stable view back.
                    sv.op = tl_batch.stage(normalize_op(k));
                    break;
                }
                simdjson::dom::element filter;
                if (attr["command"]["filter"].get(filter) == simdjson::SUCCESS ||
                    attr["command"]["query"].get(filter)  == simdjson::SUCCESS)
                {
                    std::string shape = QueryShapeNormalizer::normalize_element(filter);
                    sv.shape = tl_batch.stage_owned(std::move(shape));
                }
            }

            simdjson::dom::element client;
            if (attr["client"].get(client) == simdjson::SUCCESS) {
                simdjson::dom::element driver;
                if (client["driver"].get(driver) == simdjson::SUCCESS) {
                    std::string_view dn, dv;
                    (void)driver["name"].get_string().get(dn);
                    (void)driver["version"].get_string().get(dv);
                    if (!dn.empty()) {
                        std::string full;
                        full.reserve(dn.size()+1+dv.size());
                        full += dn;
                        if (!dv.empty()) { full += ' '; full += dv; }
                        sv.drv = tl_batch.stage_owned(std::move(full));
                    }
                }
            }
        }

        // raw_len
        const char* p  = batch.start + (e.raw_offset - batch.file_base);
        const char* nl = static_cast<const char*>(
            std::memchr(p, '\n', batch.length - (e.raw_offset - batch.file_base)));
        e.raw_len = nl ? static_cast<uint32_t>(nl - p)
                       : static_cast<uint32_t>(batch.length-(e.raw_offset-batch.file_base));

        tl_entries.push_back(e);
        tl_svs.push_back(sv);
        ++result.lines_ok;
    }

    // ---- ONE lock: commit all unique strings ----
    if (!tl_batch.cache.empty()) {
        std::lock_guard<std::mutex> lk(strings_mutex_);
        for (auto& [sv2, idx] : tl_batch.cache)
            idx = strings_.intern(sv2);
    }

    // ---- Fill indices in-place ----
    for (size_t i = 0; i < tl_entries.size(); ++i) {
        tl_entries[i].component_idx = tl_batch.get(tl_svs[i].comp);
        tl_entries[i].msg_idx       = tl_batch.get(tl_svs[i].msg);
        tl_entries[i].ns_idx        = tl_batch.get(tl_svs[i].ns);
        tl_entries[i].op_type_idx   = tl_batch.get(tl_svs[i].op);
        tl_entries[i].shape_idx     = tl_batch.get(tl_svs[i].shape);
        tl_entries[i].driver_idx    = tl_batch.get(tl_svs[i].drv);
    }

    // Move tl_entries into result — O(1) swap, no copy
    result.entries = std::move(tl_entries);
    tl_batch.clear();

    // Restore tl_entries capacity for next batch (move left it empty but
    // capacity intact; re-assign an empty vector with pre-reserved capacity)
    // Actually std::move leaves capacity in result.entries now; tl_entries
    // is valid but empty with 0 capacity. Reserve for next batch:
    tl_entries.reserve(result.entries.size());

    return result;
}

// ------------------------------------------------------------
//  parse_file — output goes into a ChunkVector (unbounded)
// ------------------------------------------------------------
void LogParser::parse_file(const MmapFile& file, uint16_t node_idx,
                            ChunkVector<LogEntry>& out,
                            ProgressCb progress_cb)
{
    if (file.size() == 0) return;

    auto batches = split_batches(file.data(), file.size(),
                                 cfg_.batch_size_bytes, node_idx);

    size_t estimated_lines = std::max<size_t>(1, file.size() / 160);
    std::vector<ParseResult> results(batches.size());
    std::atomic<size_t> next_batch{0}, done_lines{0};

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
    std::vector<std::thread> workers;
    workers.reserve(nthreads);
    for (size_t i = 0; i < nthreads; ++i) workers.emplace_back(worker_fn);

    if (progress_cb) {
        while (next_batch.load(std::memory_order_relaxed) < batches.size()) {
            progress_cb(done_lines.load(std::memory_order_relaxed), estimated_lines);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    for (auto& t : workers) t.join();

    for (auto& r : results) {
        lines_ok_.fetch_add(r.lines_ok,     std::memory_order_relaxed);
        lines_failed_.fetch_add(r.lines_failed, std::memory_order_relaxed);
        for (auto& e : r.entries) out.push_back(e);
    }
}
