#include "llm_tools.hpp"
#include "../analysis/cluster.hpp"
#include "../analysis/analyzer.hpp"
#include "../parser/log_entry.hpp"
#include "../core/chunk_vector.hpp"
#include "../core/json_escape.hpp"

#include <simdjson.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>
#include <sstream>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <unistd.h>
#endif

// Thread-local parser for on-demand pread of entry detail
static thread_local simdjson::dom::parser tl_detail_parser;

// ------------------------------------------------------------
//  Helpers — minimal JSON building without a library
//  json_escape(const std::string&) is provided by json_escape.hpp
// ------------------------------------------------------------
static std::string json_escape(std::string_view sv) {
    return json_escape(std::string(sv));
}

static void append_kv_str(std::string& out, const char* key, const std::string& val) {
    out += "\"";
    out += key;
    out += "\":\"";
    out += json_escape(val);
    out += "\"";
}

static void append_kv_str(std::string& out, const char* key, std::string_view val) {
    out += "\"";
    out += key;
    out += "\":\"";
    out += json_escape(val);
    out += "\"";
}

static void append_kv_str(std::string& out, const char* key, const char* val) {
    append_kv_str(out, key, std::string_view(val));
}

static void append_kv_int(std::string& out, const char* key, int64_t val) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lld", (long long)val);
    out += "\"";
    out += key;
    out += "\":";
    out += buf;
}

static void append_kv_uint(std::string& out, const char* key, uint64_t val) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%llu", (unsigned long long)val);
    out += "\"";
    out += key;
    out += "\":";
    out += buf;
}

// Format timestamp_ms to ISO 8601 string
static std::string format_ts(int64_t ms) {
    time_t sec = static_cast<time_t>(ms / 1000);
    int millis = static_cast<int>(ms % 1000);
    struct tm t;
#if defined(_WIN32)
    gmtime_s(&t, &sec);
#else
    gmtime_r(&sec, &t);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                  t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                  t.tm_hour, t.tm_min, t.tm_sec, millis);
    return buf;
}

// ------------------------------------------------------------
//  set_cluster / set_selected_entry
// ------------------------------------------------------------
void LlmTools::set_cluster(const Cluster* cluster) {
    cluster_ = cluster;
}

void LlmTools::set_selected_entry(const LogEntry* entry, const std::string& file_path) {
    selected_entry_ = entry;
    selected_file_  = file_path;
}

// ------------------------------------------------------------
//  tools_json — Anthropic tool definitions
// ------------------------------------------------------------
std::string LlmTools::tools_json() {
    return R"TOOLS([
  {
    "name": "get_analysis_summary",
    "description": "Get the full analysis summary of the loaded log data: counts by severity, component, operation type, driver, namespace, query shape, connection ID, slow query count, and time range.",
    "input_schema": {
      "type": "object",
      "properties": {},
      "required": []
    }
  },
  {
    "name": "search_logs",
    "description": "Search log entries by severity, component, namespace, text pattern, time range, or duration. Returns matching entries up to limit. Use this to find specific log events.",
    "input_schema": {
      "type": "object",
      "properties": {
        "severity": {
          "type": "string",
          "description": "Filter by severity level: FATAL, ERROR, WARN, INFO, DEBUG",
          "enum": ["FATAL", "ERROR", "WARN", "INFO", "DEBUG"]
        },
        "component": {
          "type": "string",
          "description": "Filter by component, e.g. COMMAND, REPL, NETWORK, STORAGE, ACCESS"
        },
        "namespace": {
          "type": "string",
          "description": "Filter by namespace, e.g. mydb.mycollection"
        },
        "text": {
          "type": "string",
          "description": "Case-insensitive text search in the message field"
        },
        "time_start_ms": {
          "type": "integer",
          "description": "Start of time range in Unix milliseconds"
        },
        "time_end_ms": {
          "type": "integer",
          "description": "End of time range in Unix milliseconds"
        },
        "min_duration_ms": {
          "type": "integer",
          "description": "Minimum operation duration in milliseconds"
        },
        "limit": {
          "type": "integer",
          "description": "Maximum number of entries to return, default 50, max 200"
        }
      },
      "required": []
    }
  },
  {
    "name": "get_entry_detail",
    "description": "Get the full raw JSON of a specific log entry by its index. Use search_logs first to find entries, then use this to see full details.",
    "input_schema": {
      "type": "object",
      "properties": {
        "entry_index": {
          "type": "integer",
          "description": "The entry index from search_logs results"
        }
      },
      "required": ["entry_index"]
    }
  },
  {
    "name": "get_slow_queries",
    "description": "Get slow query analysis: top query shapes by count, duration statistics, and affected namespaces.",
    "input_schema": {
      "type": "object",
      "properties": {
        "limit": {
          "type": "integer",
          "description": "Number of top shapes/namespaces to return, default 20"
        },
        "min_duration_ms": {
          "type": "integer",
          "description": "Minimum duration to include, default 0"
        }
      },
      "required": []
    }
  },
  {
    "name": "get_connections",
    "description": "Get connection statistics: per-driver counts, top connection IDs by activity count.",
    "input_schema": {
      "type": "object",
      "properties": {
        "limit": {
          "type": "integer",
          "description": "Number of top connections to return, default 20"
        }
      },
      "required": []
    }
  },
  {
    "name": "get_error_details",
    "description": "Get error and warning details: top error messages with counts and sample entries for each.",
    "input_schema": {
      "type": "object",
      "properties": {
        "limit": {
          "type": "integer",
          "description": "Number of top error types to return, default 20"
        },
        "severity": {
          "type": "string",
          "description": "Filter: ERROR, WARN, or BOTH. Default is BOTH",
          "enum": ["ERROR", "WARN", "BOTH"]
        }
      },
      "required": []
    }
  }
])TOOLS";
}

// ------------------------------------------------------------
//  execute — dispatch to the right tool
// ------------------------------------------------------------
std::string LlmTools::execute(const std::string& name,
                               const std::string& input_json) const
{
    if (!cluster_) return "{\"error\":\"No data loaded\"}";

    if (name == "get_analysis_summary") return exec_get_analysis_summary();
    if (name == "search_logs")          return exec_search_logs(input_json);
    if (name == "get_entry_detail")     return exec_get_entry_detail(input_json);
    if (name == "get_slow_queries")     return exec_get_slow_queries(input_json);
    if (name == "get_connections")       return exec_get_connections(input_json);
    if (name == "get_error_details")    return exec_get_error_details(input_json);

    return "{\"error\":\"Unknown tool: " + json_escape(name) + "\"}";
}

// ------------------------------------------------------------
//  get_analysis_summary
// ------------------------------------------------------------
std::string LlmTools::exec_get_analysis_summary() const {
    const auto& a = cluster_->analysis();
    const auto& nodes = cluster_->nodes();

    std::string out = "{";
    append_kv_uint(out, "total_entries", a.total_entries); out += ",";
    append_kv_uint(out, "entries_with_namespace", a.entries_with_ns); out += ",";
    append_kv_uint(out, "slow_queries", a.slow_queries); out += ",";
    append_kv_str(out, "earliest", format_ts(a.earliest_ms)); out += ",";
    append_kv_str(out, "latest", format_ts(a.latest_ms)); out += ",";

    // Nodes
    out += "\"nodes\":[";
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (i) out += ",";
        out += "{";
        append_kv_str(out, "hostname", nodes[i].hostname); out += ",";
        append_kv_str(out, "path", nodes[i].path);
        out += "}";
    }
    out += "],";

    // Helper: serialize a CountMap
    auto add_count_map = [&](const char* key, const CountMap& cm, size_t max_n = 30) {
        out += "\"";
        out += key;
        out += "\":[";
        size_t n = std::min(cm.size(), max_n);
        for (size_t i = 0; i < n; ++i) {
            if (i) out += ",";
            out += "{";
            append_kv_str(out, "label", cm[i].label); out += ",";
            append_kv_uint(out, "count", cm[i].count);
            out += "}";
        }
        out += "]";
    };

    add_count_map("by_severity", a.by_severity);    out += ",";
    add_count_map("by_component", a.by_component);  out += ",";
    add_count_map("by_op_type", a.by_op_type);      out += ",";
    add_count_map("by_driver", a.by_driver);         out += ",";
    add_count_map("by_namespace", a.by_namespace);   out += ",";
    add_count_map("by_query_shape", a.by_shape, 20);

    out += "}";
    return out;
}

// ------------------------------------------------------------
//  search_logs
// ------------------------------------------------------------
std::string LlmTools::exec_search_logs(const std::string& input_json) const {
    const auto& entries = cluster_->entries();
    const auto& strings = cluster_->strings();

    // Parse input
    simdjson::dom::parser parser;
    simdjson::dom::element doc;
    auto err = parser.parse(input_json).get(doc);
    if (err) return "{\"error\":\"Invalid input JSON\"}";

    // Extract optional filters
    std::string sev_filter, comp_filter, ns_filter, text_filter;
    int64_t time_start = 0, time_end = INT64_MAX;
    int64_t min_dur = -1;
    int64_t limit = 50;

    std::string_view sv;
    if (doc["severity"].get_string().get(sv) == simdjson::SUCCESS)
        sev_filter = std::string(sv);
    if (doc["component"].get_string().get(sv) == simdjson::SUCCESS)
        comp_filter = std::string(sv);
    if (doc["namespace"].get_string().get(sv) == simdjson::SUCCESS)
        ns_filter = std::string(sv);
    if (doc["text"].get_string().get(sv) == simdjson::SUCCESS)
        text_filter = std::string(sv);

    int64_t tmp;
    if (doc["time_start_ms"].get_int64().get(tmp) == simdjson::SUCCESS)
        time_start = tmp;
    if (doc["time_end_ms"].get_int64().get(tmp) == simdjson::SUCCESS)
        time_end = tmp;
    if (doc["min_duration_ms"].get_int64().get(tmp) == simdjson::SUCCESS)
        min_dur = tmp;
    if (doc["limit"].get_int64().get(tmp) == simdjson::SUCCESS)
        limit = std::min(tmp, (int64_t)200);

    // Convert text_filter to lowercase for case-insensitive matching
    std::string text_lower;
    text_lower.reserve(text_filter.size());
    for (char c : text_filter)
        text_lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    // Map severity string to enum
    Severity sev_enum = Severity::Unknown;
    bool has_sev = false;
    if (!sev_filter.empty()) {
        has_sev = true;
        sev_enum = severity_from_string(sev_filter.c_str());
    }

    // Scan entries
    std::string out = "{\"matches\":[";
    int64_t count = 0;

    for (size_t i = 0; i < entries.size() && count < limit; ++i) {
        const LogEntry& e = entries[i];

        // Time filter
        if (e.timestamp_ms < time_start || e.timestamp_ms > time_end) continue;

        // Severity filter
        if (has_sev && e.severity != sev_enum) continue;

        // Duration filter
        if (min_dur >= 0 && e.duration_ms < min_dur) continue;

        // Component filter
        if (!comp_filter.empty() && e.component_idx) {
            std::string_view comp = strings.get(e.component_idx);
            if (comp != comp_filter) continue;
        } else if (!comp_filter.empty()) {
            continue;
        }

        // Namespace filter
        if (!ns_filter.empty() && e.ns_idx) {
            std::string_view ns = strings.get(e.ns_idx);
            if (ns != ns_filter) continue;
        } else if (!ns_filter.empty()) {
            continue;
        }

        // Text filter (case-insensitive)
        if (!text_lower.empty() && e.msg_idx) {
            std::string_view msg = strings.get(e.msg_idx);
            // Convert to lowercase and search
            bool found = false;
            if (msg.size() >= text_lower.size()) {
                for (size_t j = 0; j <= msg.size() - text_lower.size(); ++j) {
                    bool match = true;
                    for (size_t k = 0; k < text_lower.size(); ++k) {
                        if (std::tolower(static_cast<unsigned char>(msg[j + k]))
                            != static_cast<unsigned char>(text_lower[k])) {
                            match = false;
                            break;
                        }
                    }
                    if (match) { found = true; break; }
                }
            }
            if (!found) continue;
        } else if (!text_lower.empty()) {
            continue;
        }

        if (count > 0) out += ",";

        out += "{";
        append_kv_uint(out, "index", i); out += ",";
        append_kv_str(out, "timestamp", format_ts(e.timestamp_ms)); out += ",";
        append_kv_str(out, "severity", severity_string(e.severity)); out += ",";
        if (e.component_idx) {
            append_kv_str(out, "component", strings.get(e.component_idx));
            out += ",";
        }
        if (e.ns_idx) {
            append_kv_str(out, "namespace", strings.get(e.ns_idx));
            out += ",";
        }
        if (e.msg_idx) {
            // Truncate long messages to 300 chars
            std::string_view msg = strings.get(e.msg_idx);
            if (msg.size() > 300) {
                std::string truncated(msg.substr(0, 300));
                truncated += "...";
                append_kv_str(out, "message", truncated);
            } else {
                append_kv_str(out, "message", msg);
            }
            out += ",";
        }
        if (e.duration_ms >= 0) {
            append_kv_int(out, "duration_ms", e.duration_ms);
            out += ",";
        }
        if (e.op_type_idx) {
            append_kv_str(out, "op_type", strings.get(e.op_type_idx));
            out += ",";
        }
        if (e.conn_id) {
            append_kv_uint(out, "conn_id", e.conn_id);
            out += ",";
        }
        // Node info
        if (e.node_idx < cluster_->nodes().size()) {
            append_kv_str(out, "node", cluster_->nodes()[e.node_idx].hostname);
        }
        out += "}";
        ++count;
    }

    out += "],";
    append_kv_uint(out, "returned", static_cast<uint64_t>(count));
    out += ",";
    append_kv_uint(out, "total_entries", entries.size());
    out += "}";
    return out;
}

// ------------------------------------------------------------
//  get_entry_detail — pread raw JSON from file
// ------------------------------------------------------------
std::string LlmTools::exec_get_entry_detail(const std::string& input_json) const {
    simdjson::dom::parser parser;
    simdjson::dom::element doc;
    auto err = parser.parse(input_json).get(doc);
    if (err) return "{\"error\":\"Invalid input JSON\"}";

    int64_t idx = 0;
    if (doc["entry_index"].get_int64().get(idx) != simdjson::SUCCESS)
        return "{\"error\":\"entry_index is required\"}";

    const auto& entries = cluster_->entries();
    if (idx < 0 || static_cast<size_t>(idx) >= entries.size())
        return "{\"error\":\"entry_index out of range\"}";

    const LogEntry& e = entries[static_cast<size_t>(idx)];
    if (e.node_idx >= cluster_->nodes().size())
        return "{\"error\":\"invalid node index\"}";

    const std::string& file_path = cluster_->nodes()[e.node_idx].path;
    uint64_t offset = e.raw_offset;
    size_t rlen = e.raw_len;

    // pread the raw JSON line
    std::vector<char> buf(rlen + simdjson::SIMDJSON_PADDING, '\0');

#if defined(_WIN32)
    HANDLE fh = CreateFileA(file_path.c_str(), GENERIC_READ,
                            FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
    if (fh == INVALID_HANDLE_VALUE)
        return "{\"error\":\"Cannot open file\"}";
    LARGE_INTEGER li; li.QuadPart = static_cast<LONGLONG>(offset);
    SetFilePointerEx(fh, li, nullptr, FILE_BEGIN);
    DWORD read_bytes = 0;
    ReadFile(fh, buf.data(), static_cast<DWORD>(rlen), &read_bytes, nullptr);
    CloseHandle(fh);
#else
    int fd = ::open(file_path.c_str(), O_RDONLY);
    if (fd < 0) return "{\"error\":\"Cannot open file\"}";
    ssize_t bytes_read = ::pread(fd, buf.data(), rlen, static_cast<off_t>(offset));
    ::close(fd);
    if (bytes_read < 0 || static_cast<size_t>(bytes_read) != rlen)
        return "{\"error\":\"File read failed or was truncated\"}";
#endif

    // Return the raw JSON line directly
    return std::string(buf.data(), rlen);
}

// ------------------------------------------------------------
//  get_slow_queries
// ------------------------------------------------------------
std::string LlmTools::exec_get_slow_queries(const std::string& input_json) const {
    const auto& entries = cluster_->entries();
    const auto& strings = cluster_->strings();

    // Parse params
    simdjson::dom::parser parser;
    simdjson::dom::element doc;
    (void)parser.parse(input_json).get(doc);

    int64_t limit = 20, min_dur = 0;
    int64_t tmp;
    if (doc["limit"].get_int64().get(tmp) == simdjson::SUCCESS)
        limit = std::min(tmp, (int64_t)100);
    if (doc["min_duration_ms"].get_int64().get(tmp) == simdjson::SUCCESS)
        min_dur = tmp;

    // Collect slow query stats: shape -> {count, total_dur, max_dur, min_dur}
    struct ShapeStats {
        std::string shape;
        uint64_t count = 0;
        int64_t total_dur = 0;
        int64_t max_dur = 0;
        int64_t min_dur = INT64_MAX;
        std::vector<int64_t> durations;  // for percentiles (sampled)
    };
    std::unordered_map<uint32_t, ShapeStats> shape_map;

    // NS -> count for slow queries
    std::unordered_map<uint32_t, uint64_t> ns_map;

    uint64_t slow_count = 0;
    int64_t total_duration = 0;
    int64_t max_duration = 0;

    for (size_t i = 0; i < entries.size(); ++i) {
        const LogEntry& e = entries[i];

        // Only count MongoDB-flagged slow queries
        if (e.msg_idx == 0) continue;
        std::string_view msg = strings.get(e.msg_idx);
        if (msg.size() < 4 || !((msg[0]=='S'||msg[0]=='s') && msg[1]=='l' && msg[2]=='o' && msg[3]=='w'))
            continue;

        if (e.duration_ms < min_dur) continue;

        ++slow_count;
        if (e.duration_ms >= 0) {
            total_duration += e.duration_ms;
            if (e.duration_ms > max_duration) max_duration = e.duration_ms;
        }

        if (e.shape_idx) {
            auto& ss = shape_map[e.shape_idx];
            if (ss.shape.empty()) ss.shape = std::string(strings.get(e.shape_idx));
            ss.count++;
            if (e.duration_ms >= 0) {
                ss.total_dur += e.duration_ms;
                if (e.duration_ms > ss.max_dur) ss.max_dur = e.duration_ms;
                if (e.duration_ms < ss.min_dur) ss.min_dur = e.duration_ms;
                if (ss.durations.size() < 1000) // cap sample size
                    ss.durations.push_back(e.duration_ms);
            }
        }
        if (e.ns_idx) ns_map[e.ns_idx]++;
    }

    // Sort shapes by count
    std::vector<ShapeStats*> sorted_shapes;
    for (auto& [k, v] : shape_map) sorted_shapes.push_back(&v);
    std::sort(sorted_shapes.begin(), sorted_shapes.end(),
              [](const ShapeStats* a, const ShapeStats* b) {
                  return a->count > b->count;
              });

    // Build output
    std::string out = "{";
    append_kv_uint(out, "total_slow_queries", slow_count); out += ",";
    append_kv_int(out, "total_duration_ms", total_duration); out += ",";
    append_kv_int(out, "max_duration_ms", max_duration); out += ",";
    if (slow_count > 0) {
        append_kv_int(out, "avg_duration_ms", total_duration / (int64_t)slow_count);
        out += ",";
    }

    out += "\"top_shapes\":[";
    size_t n = std::min(sorted_shapes.size(), static_cast<size_t>(limit));
    for (size_t i = 0; i < n; ++i) {
        if (i) out += ",";
        auto& ss = *sorted_shapes[i];
        out += "{";
        append_kv_str(out, "shape", ss.shape); out += ",";
        append_kv_uint(out, "count", ss.count); out += ",";
        if (ss.count > 0) {
            append_kv_int(out, "avg_ms", ss.total_dur / (int64_t)ss.count); out += ",";
            append_kv_int(out, "max_ms", ss.max_dur); out += ",";
            append_kv_int(out, "min_ms", ss.min_dur);
            // p50/p99 from sampled durations
            if (!ss.durations.empty()) {
                std::sort(ss.durations.begin(), ss.durations.end());
                int64_t p50 = ss.durations[ss.durations.size() / 2];
                int64_t p99 = ss.durations[std::min(ss.durations.size() - 1,
                                           (size_t)(ss.durations.size() * 0.99))];
                out += ",";
                append_kv_int(out, "p50_ms", p50); out += ",";
                append_kv_int(out, "p99_ms", p99);
            }
        }
        out += "}";
    }
    out += "],";

    // Top namespaces for slow queries
    std::vector<std::pair<uint32_t, uint64_t>> sorted_ns(ns_map.begin(), ns_map.end());
    std::sort(sorted_ns.begin(), sorted_ns.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    out += "\"top_namespaces\":[";
    n = std::min(sorted_ns.size(), static_cast<size_t>(limit));
    for (size_t i = 0; i < n; ++i) {
        if (i) out += ",";
        out += "{";
        append_kv_str(out, "namespace", strings.get(sorted_ns[i].first)); out += ",";
        append_kv_uint(out, "count", sorted_ns[i].second);
        out += "}";
    }
    out += "]";

    out += "}";
    return out;
}

// ------------------------------------------------------------
//  get_connections
// ------------------------------------------------------------
std::string LlmTools::exec_get_connections(const std::string& input_json) const {
    const auto& analysis = cluster_->analysis();

    simdjson::dom::parser parser;
    simdjson::dom::element doc;
    (void)parser.parse(input_json).get(doc);

    int64_t limit = 20;
    int64_t tmp;
    if (doc["limit"].get_int64().get(tmp) == simdjson::SUCCESS)
        limit = std::min(tmp, (int64_t)100);

    std::string out = "{";

    // Driver stats
    out += "\"by_driver\":[";
    for (size_t i = 0; i < analysis.by_driver.size(); ++i) {
        if (i) out += ",";
        out += "{";
        append_kv_str(out, "driver", analysis.by_driver[i].label); out += ",";
        append_kv_uint(out, "count", analysis.by_driver[i].count);
        out += "}";
    }
    out += "],";

    // Top connections by activity
    out += "\"top_connections\":[";
    size_t n = std::min(analysis.by_conn_id.size(), static_cast<size_t>(limit));
    for (size_t i = 0; i < n; ++i) {
        if (i) out += ",";
        out += "{";
        append_kv_uint(out, "conn_id", analysis.by_conn_id[i].conn_id); out += ",";
        append_kv_uint(out, "entry_count", analysis.by_conn_id[i].count);
        out += "}";
    }
    out += "],";

    append_kv_uint(out, "total_unique_connections", analysis.by_conn_id.size());
    out += "}";
    return out;
}

// ------------------------------------------------------------
//  get_error_details
// ------------------------------------------------------------
std::string LlmTools::exec_get_error_details(const std::string& input_json) const {
    const auto& entries = cluster_->entries();
    const auto& strings = cluster_->strings();

    simdjson::dom::parser parser;
    simdjson::dom::element doc;
    (void)parser.parse(input_json).get(doc);

    int64_t limit = 20;
    std::string sev_filter = "BOTH";
    int64_t tmp;
    std::string_view sv;
    if (doc["limit"].get_int64().get(tmp) == simdjson::SUCCESS)
        limit = std::min(tmp, (int64_t)100);
    if (doc["severity"].get_string().get(sv) == simdjson::SUCCESS)
        sev_filter = std::string(sv);

    bool include_error = (sev_filter == "ERROR" || sev_filter == "BOTH");
    bool include_warn  = (sev_filter == "WARN" || sev_filter == "BOTH");

    // Group by message -> {count, severity, sample_indices}
    struct MsgGroup {
        std::string message;
        std::string severity;
        uint64_t    count = 0;
        std::vector<size_t> samples; // up to 3 sample indices
    };
    std::unordered_map<uint32_t, MsgGroup> msg_map;

    for (size_t i = 0; i < entries.size(); ++i) {
        const LogEntry& e = entries[i];

        if (e.severity == Severity::Error && !include_error) continue;
        if (e.severity == Severity::Warning && !include_warn) continue;
        if (e.severity != Severity::Error && e.severity != Severity::Warning) continue;

        uint32_t key = e.msg_idx ? e.msg_idx : 0;
        auto& mg = msg_map[key];
        if (mg.message.empty() && e.msg_idx) {
            std::string_view msg = strings.get(e.msg_idx);
            if (msg.size() > 200)
                mg.message = std::string(msg.substr(0, 200)) + "...";
            else
                mg.message = std::string(msg);
        }
        if (mg.severity.empty())
            mg.severity = severity_string(e.severity);
        mg.count++;
        if (mg.samples.size() < 3)
            mg.samples.push_back(i);
    }

    // Sort by count
    std::vector<MsgGroup*> sorted;
    for (auto& [k, v] : msg_map) sorted.push_back(&v);
    std::sort(sorted.begin(), sorted.end(),
              [](const MsgGroup* a, const MsgGroup* b) {
                  return a->count > b->count;
              });

    std::string out = "{\"error_groups\":[";
    size_t n = std::min(sorted.size(), static_cast<size_t>(limit));
    for (size_t i = 0; i < n; ++i) {
        if (i) out += ",";
        auto& mg = *sorted[i];
        out += "{";
        append_kv_str(out, "message", mg.message); out += ",";
        append_kv_str(out, "severity", mg.severity); out += ",";
        append_kv_uint(out, "count", mg.count); out += ",";
        out += "\"sample_indices\":[";
        for (size_t j = 0; j < mg.samples.size(); ++j) {
            if (j) out += ",";
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%zu", mg.samples[j]);
            out += buf;
        }
        out += "]";
        out += "}";
    }
    out += "],";

    // Total error/warning counts
    uint64_t total_errors = 0, total_warnings = 0;
    for (const auto& ce : cluster_->analysis().by_severity) {
        if (ce.label == "ERROR") total_errors = ce.count;
        if (ce.label == "WARN")  total_warnings = ce.count;
    }
    append_kv_uint(out, "total_errors", total_errors); out += ",";
    append_kv_uint(out, "total_warnings", total_warnings);
    out += "}";
    return out;
}
