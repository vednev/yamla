#include <catch2/catch_all.hpp>
#include "analysis/analyzer.hpp"
#include "parser/log_entry.hpp"
#include "core/arena_chain.hpp"
#include "core/chunk_vector.hpp"

#include <string>
#include <climits>

// Helper: create a LogEntry with specified fields
static LogEntry make_entry(StringTable& st, Severity sev,
                           const char* comp, const char* ns,
                           const char* msg, const char* op_type,
                           int32_t duration, uint32_t conn_id,
                           int64_t ts_ms)
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
    return e;
}

TEST_CASE("Analyzer: empty entries", "[analyzer]") {
    ArenaChain chain;
    StringTable st(chain);
    ChunkVector<LogEntry> entries(chain);
    auto result = Analyzer::analyze(entries, st);
    REQUIRE(result.total_entries == 0);
}

TEST_CASE("Analyzer: single entry", "[analyzer]") {
    ArenaChain chain;
    StringTable st(chain);
    ChunkVector<LogEntry> entries(chain);
    entries.push_back(make_entry(st, Severity::Info, "COMMAND", "test.coll",
                                 "msg", "find", 100, 1, 1000));
    auto r = Analyzer::analyze(entries, st);
    REQUIRE(r.total_entries == 1);
    REQUIRE(r.by_severity.size() >= 1);
    REQUIRE(r.by_component.size() == 1);
    REQUIRE(r.by_namespace.size() == 1);
    REQUIRE(r.by_op_type.size() == 1);
}

TEST_CASE("Analyzer: multiple same severity", "[analyzer]") {
    ArenaChain chain;
    StringTable st(chain);
    ChunkVector<LogEntry> entries(chain);
    for (int i = 0; i < 3; ++i) {
        entries.push_back(make_entry(st, Severity::Warning, "NET", "",
                                     "m", "", -1, 0, 1000+i));
    }
    auto r = Analyzer::analyze(entries, st);
    REQUIRE(r.by_severity.size() == 1);
    REQUIRE(r.by_severity[0].count == 3);
}

TEST_CASE("Analyzer: slow query detection", "[analyzer]") {
    ArenaChain chain;
    StringTable st(chain);
    ChunkVector<LogEntry> entries(chain);

    // "Slow query" matches (S/s + low)
    entries.push_back(make_entry(st, Severity::Info, "COMMAND", "", "Slow query", "find", 200, 0, 1000));
    entries.push_back(make_entry(st, Severity::Info, "COMMAND", "", "slow query", "find", 300, 0, 2000));
    entries.push_back(make_entry(st, Severity::Info, "COMMAND", "", "Normal", "find", 500, 0, 3000));

    auto r = Analyzer::analyze(entries, st);
    REQUIRE(r.slow_queries == 2);
}

TEST_CASE("Analyzer: time range", "[analyzer]") {
    ArenaChain chain;
    StringTable st(chain);
    ChunkVector<LogEntry> entries(chain);
    entries.push_back(make_entry(st, Severity::Info, "C", "", "m", "", -1, 0, 5000));
    entries.push_back(make_entry(st, Severity::Info, "C", "", "m", "", -1, 0, 1000));
    entries.push_back(make_entry(st, Severity::Info, "C", "", "m", "", -1, 0, 3000));
    auto r = Analyzer::analyze(entries, st);
    REQUIRE(r.earliest_ms == 1000);
    REQUIRE(r.latest_ms == 5000);
}

TEST_CASE("Analyzer: connection IDs", "[analyzer]") {
    ArenaChain chain;
    StringTable st(chain);
    ChunkVector<LogEntry> entries(chain);
    for (int i = 0; i < 3; ++i)
        entries.push_back(make_entry(st, Severity::Info, "C", "", "m", "", -1, 100, 1000+i));
    for (int i = 0; i < 2; ++i)
        entries.push_back(make_entry(st, Severity::Info, "C", "", "m", "", -1, 200, 2000+i));
    auto r = Analyzer::analyze(entries, st);
    REQUIRE(r.by_conn_id.size() == 2);
    // Sorted descending by count: first should be conn_id=100 with count=3
    REQUIRE(r.by_conn_id[0].conn_id == 100);
    REQUIRE(r.by_conn_id[0].count == 3);
}

TEST_CASE("Analyzer: namespace counting", "[analyzer]") {
    ArenaChain chain;
    StringTable st(chain);
    ChunkVector<LogEntry> entries(chain);
    entries.push_back(make_entry(st, Severity::Info, "C", "db.coll", "m", "", -1, 0, 1000));
    entries.push_back(make_entry(st, Severity::Info, "C", "", "m", "", -1, 0, 2000));
    auto r = Analyzer::analyze(entries, st);
    REQUIRE(r.entries_with_ns == 1);
}

TEST_CASE("Analyzer: CountMap sorting", "[analyzer]") {
    ArenaChain chain;
    StringTable st(chain);
    ChunkVector<LogEntry> entries(chain);
    // 5 entries with comp=A, 3 with comp=B
    for (int i = 0; i < 5; ++i)
        entries.push_back(make_entry(st, Severity::Info, "A", "", "m", "", -1, 0, 1000));
    for (int i = 0; i < 3; ++i)
        entries.push_back(make_entry(st, Severity::Info, "B", "", "m", "", -1, 0, 2000));
    auto r = Analyzer::analyze(entries, st);
    REQUIRE(r.by_component.size() == 2);
    REQUIRE(r.by_component[0].count >= r.by_component[1].count);
}

TEST_CASE("Analyzer: entries without ns not in by_namespace", "[analyzer]") {
    ArenaChain chain;
    StringTable st(chain);
    ChunkVector<LogEntry> entries(chain);
    entries.push_back(make_entry(st, Severity::Info, "C", "", "m", "", -1, 0, 1000));
    auto r = Analyzer::analyze(entries, st);
    REQUIRE(r.by_namespace.empty());
}

TEST_CASE("Analyzer: zero conn_id excluded from by_conn_id", "[analyzer]") {
    ArenaChain chain;
    StringTable st(chain);
    ChunkVector<LogEntry> entries(chain);
    entries.push_back(make_entry(st, Severity::Info, "C", "", "m", "", -1, 0, 1000));
    auto r = Analyzer::analyze(entries, st);
    REQUIRE(r.by_conn_id.empty());
}
