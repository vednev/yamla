#include <catch2/catch_all.hpp>
#include "parser/log_entry.hpp"
#include "core/arena_chain.hpp"
#include "core/chunk_vector.hpp"
#include "ui/log_view.hpp"   // FilterState

#include <string>
#include <cctype>

// ============================================================
//  Mirror of entry_matches logic from log_view.cpp
//
//  LogView::entry_matches is private and depends on ImGui,
//  so we replicate the matching logic here for standalone
//  testing. This must be kept in sync with the real impl.
// ============================================================
namespace {

bool entry_matches(const LogEntry& e,
                   const FilterState& filter,
                   const StringTable& strings)
{
    if (!filter.active()) return true;

    if (filter.severity_filter &&
        static_cast<uint32_t>(e.severity) != filter.severity_filter - 1)
        return false;

    if (!filter.component_idx_include.empty() &&
        !filter.component_idx_include.count(e.component_idx))
        return false;
    if (filter.op_type_idx && e.op_type_idx != filter.op_type_idx) return false;
    if (filter.driver_idx  && e.driver_idx  != filter.driver_idx)  return false;
    if (filter.ns_idx      && e.ns_idx      != filter.ns_idx)      return false;
    if (filter.shape_idx   && e.shape_idx   != filter.shape_idx)   return false;

    // Slow query filter
    if (filter.slow_query_only) {
        bool is_slow = false;
        if (e.msg_idx != 0) {
            std::string_view msg = strings.get(e.msg_idx);
            is_slow = (msg.size() >= 4 &&
                       (msg[0] == 'S' || msg[0] == 's') &&
                       msg[1] == 'l' && msg[2] == 'o' && msg[3] == 'w');
        }
        if (!is_slow) return false;
    }

    // Set-based inclusion filters
    if (!filter.conn_id_include.empty() &&
        !filter.conn_id_include.count(e.conn_id)) return false;
    if (!filter.driver_idx_include.empty() &&
        !filter.driver_idx_include.count(e.driver_idx)) return false;
    if (!filter.node_idx_include.empty() &&
        !filter.node_idx_include.count(e.node_idx)) return false;

    // Text search (case-insensitive)
    if (!filter.text_search.empty()) {
        std::string_view msg = strings.get(e.msg_idx);
        std::string search_lower;
        search_lower.reserve(filter.text_search.size());
        for (char c : filter.text_search)
            search_lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        bool found = false;
        if (msg.size() >= search_lower.size()) {
            for (size_t i = 0; i <= msg.size() - search_lower.size(); ++i) {
                bool match = true;
                for (size_t j = 0; j < search_lower.size() && match; ++j)
                    match = (std::tolower(static_cast<unsigned char>(msg[i + j]))
                             == search_lower[j]);
                if (match) { found = true; break; }
            }
        }
        if (!found) return false;
    }

    return true;
}

LogEntry make_entry(StringTable& st, Severity sev,
                    const char* comp, const char* ns,
                    const char* msg, const char* op_type,
                    int32_t duration, uint32_t conn_id,
                    int64_t ts_ms, uint16_t node_idx = 0)
{
    LogEntry e{};
    e.severity      = sev;
    e.component_idx = st.intern(comp);
    e.ns_idx        = st.intern(ns);
    e.msg_idx       = st.intern(msg);
    e.op_type_idx   = st.intern(op_type);
    e.duration_ms   = duration;
    e.conn_id       = conn_id;
    e.timestamp_ms  = ts_ms;
    e.node_idx      = node_idx;
    e.node_mask     = 1u << node_idx;
    return e;
}

} // namespace

// ============================================================
//  Tests
// ============================================================

TEST_CASE("Filter: no filter matches all", "[filter]") {
    ArenaChain chain;
    StringTable st(chain);
    FilterState filter;

    auto e1 = make_entry(st, Severity::Error,   "COMMAND", "db.users", "err msg",  "find", 100, 1, 1000);
    auto e2 = make_entry(st, Severity::Info,    "REPL",    "db.orders","info msg", "insert", -1, 2, 2000);
    auto e3 = make_entry(st, Severity::Warning, "NETWORK", "",         "warn msg", "", -1, 0, 3000);

    REQUIRE(entry_matches(e1, filter, st));
    REQUIRE(entry_matches(e2, filter, st));
    REQUIRE(entry_matches(e3, filter, st));
}

TEST_CASE("Filter: severity filter", "[filter]") {
    ArenaChain chain;
    StringTable st(chain);
    FilterState filter;
    // severity_filter = (Severity enum value + 1)
    // Error = 1, so severity_filter = 2
    filter.severity_filter = static_cast<uint32_t>(Severity::Error) + 1;

    auto err  = make_entry(st, Severity::Error,   "C", "", "e", "", -1, 0, 1000);
    auto info = make_entry(st, Severity::Info,    "C", "", "i", "", -1, 0, 2000);
    auto warn = make_entry(st, Severity::Warning, "C", "", "w", "", -1, 0, 3000);

    REQUIRE(entry_matches(err, filter, st));
    REQUIRE_FALSE(entry_matches(info, filter, st));
    REQUIRE_FALSE(entry_matches(warn, filter, st));
}

TEST_CASE("Filter: component include", "[filter]") {
    ArenaChain chain;
    StringTable st(chain);
    FilterState filter;

    auto cmd_entry = make_entry(st, Severity::Info, "COMMAND", "", "m", "", -1, 0, 1000);
    auto net_entry = make_entry(st, Severity::Info, "NETWORK", "", "m", "", -1, 0, 2000);

    uint32_t cmd_idx = st.intern("COMMAND");
    filter.component_idx_include.insert(cmd_idx);

    REQUIRE(entry_matches(cmd_entry, filter, st));
    REQUIRE_FALSE(entry_matches(net_entry, filter, st));
}

TEST_CASE("Filter: namespace filter", "[filter]") {
    ArenaChain chain;
    StringTable st(chain);
    FilterState filter;

    auto users  = make_entry(st, Severity::Info, "C", "db.users",  "m", "", -1, 0, 1000);
    auto orders = make_entry(st, Severity::Info, "C", "db.orders", "m", "", -1, 0, 2000);

    filter.ns_idx = st.intern("db.users");

    REQUIRE(entry_matches(users, filter, st));
    REQUIRE_FALSE(entry_matches(orders, filter, st));
}

TEST_CASE("Filter: text search", "[filter]") {
    ArenaChain chain;
    StringTable st(chain);
    FilterState filter;
    filter.text_search = "slow";

    auto slow_entry   = make_entry(st, Severity::Info, "C", "", "Slow query",    "", -1, 0, 1000);
    auto normal_entry = make_entry(st, Severity::Info, "C", "", "Normal message", "", -1, 0, 2000);

    // Case-insensitive: "slow" should match "Slow query"
    REQUIRE(entry_matches(slow_entry, filter, st));
    REQUIRE_FALSE(entry_matches(normal_entry, filter, st));
}

TEST_CASE("Filter: slow_query_only", "[filter]") {
    ArenaChain chain;
    StringTable st(chain);
    FilterState filter;
    filter.slow_query_only = true;

    auto slow  = make_entry(st, Severity::Info, "C", "", "Slow query", "find", 500, 0, 1000);
    auto slow2 = make_entry(st, Severity::Info, "C", "", "slow write", "insert", 300, 0, 2000);
    auto fast  = make_entry(st, Severity::Info, "C", "", "Normal msg", "find", 500, 0, 3000);

    REQUIRE(entry_matches(slow, filter, st));
    REQUIRE(entry_matches(slow2, filter, st));
    REQUIRE_FALSE(entry_matches(fast, filter, st));
}

TEST_CASE("Filter: node filter", "[filter]") {
    ArenaChain chain;
    StringTable st(chain);
    FilterState filter;
    filter.node_idx_include.insert(0);

    auto node0 = make_entry(st, Severity::Info, "C", "", "m", "", -1, 0, 1000, 0);
    auto node1 = make_entry(st, Severity::Info, "C", "", "m", "", -1, 0, 2000, 1);

    REQUIRE(entry_matches(node0, filter, st));
    REQUIRE_FALSE(entry_matches(node1, filter, st));
}

TEST_CASE("Filter: combined filters", "[filter]") {
    ArenaChain chain;
    StringTable st(chain);
    FilterState filter;

    // Severity = Error AND component = COMMAND
    filter.severity_filter = static_cast<uint32_t>(Severity::Error) + 1;
    uint32_t cmd_idx = st.intern("COMMAND");
    filter.component_idx_include.insert(cmd_idx);

    auto err_cmd = make_entry(st, Severity::Error, "COMMAND", "", "m", "", -1, 0, 1000);
    auto err_net = make_entry(st, Severity::Error, "NETWORK", "", "m", "", -1, 0, 2000);
    auto info_cmd = make_entry(st, Severity::Info, "COMMAND", "", "m", "", -1, 0, 3000);

    REQUIRE(entry_matches(err_cmd, filter, st));
    REQUIRE_FALSE(entry_matches(err_net, filter, st));
    REQUIRE_FALSE(entry_matches(info_cmd, filter, st));
}

TEST_CASE("Filter: conn_id include", "[filter]") {
    ArenaChain chain;
    StringTable st(chain);
    FilterState filter;
    filter.conn_id_include.insert(100);

    auto conn100 = make_entry(st, Severity::Info, "C", "", "m", "", -1, 100, 1000);
    auto conn200 = make_entry(st, Severity::Info, "C", "", "m", "", -1, 200, 2000);
    auto conn0   = make_entry(st, Severity::Info, "C", "", "m", "", -1, 0,   3000);

    REQUIRE(entry_matches(conn100, filter, st));
    REQUIRE_FALSE(entry_matches(conn200, filter, st));
    REQUIRE_FALSE(entry_matches(conn0, filter, st));
}

TEST_CASE("Filter: active() detection", "[filter]") {
    FilterState filter;

    // Default: no filter active
    REQUIRE_FALSE(filter.active());

    // Set one field: should become active
    filter.severity_filter = 1;
    REQUIRE(filter.active());

    filter.clear();
    REQUIRE_FALSE(filter.active());

    filter.text_search = "hello";
    REQUIRE(filter.active());

    filter.clear();
    filter.slow_query_only = true;
    REQUIRE(filter.active());

    filter.clear();
    filter.conn_id_include.insert(42);
    REQUIRE(filter.active());

    filter.clear();
    filter.node_idx_include.insert(0);
    REQUIRE(filter.active());

    filter.clear();
    filter.component_idx_include.insert(1);
    REQUIRE(filter.active());
}
