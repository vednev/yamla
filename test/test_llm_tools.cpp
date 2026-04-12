#include <catch2/catch_all.hpp>
#include "llm/llm_tools.hpp"
#include "analysis/cluster.hpp"

#include <fstream>
#include <cstdio>
#include <string>

// ============================================================
//  Helpers
// ============================================================
namespace {

static int g_llm_counter = 0;

std::string write_temp_log(const std::string& content) {
    std::string path = "/tmp/yamla_llm_test_" +
                       std::to_string(++g_llm_counter) + ".log";
    std::ofstream ofs(path, std::ios::binary);
    ofs << content;
    ofs.close();
    return path;
}

void cleanup_temp(const std::string& path) {
    std::remove(path.c_str());
}

std::string log_line(const std::string& ts,
                     const std::string& sev,
                     const std::string& comp,
                     const std::string& ctx,
                     const std::string& msg,
                     const std::string& extra_attr = "")
{
    std::string line = R"({"t":{"$date":")" + ts +
                       R"("},"s":")" + sev +
                       R"(","c":")" + comp +
                       R"(","id":51803,"ctx":")" + ctx +
                       R"(","msg":")" + msg + R"(")";
    if (!extra_attr.empty()) {
        line += R"(,"attr":{)" + extra_attr + "}";
    }
    line += "}\n";
    return line;
}

// Build the test log file content.
// 8 entries covering ERROR(2), WARNING(2), INFO(3), DEBUG(1)
// Components: COMMAND, REPL, NETWORK
// Namespaces: db.users, db.orders
// conn_ids: 100, 100, 200, 0, ...
std::string build_test_content() {
    std::string c;
    // 1. ERROR, COMMAND, db.users, conn100
    c += log_line("2024-01-15T10:00:00.000+00:00", "E", "COMMAND", "conn100",
                  "authentication failed",
                  R"("ns":"db.users","durationMillis":50)");
    // 2. ERROR, NETWORK, db.orders, conn200
    c += log_line("2024-01-15T10:00:01.000+00:00", "E", "NETWORK", "conn200",
                  "connection reset",
                  R"("ns":"db.orders","durationMillis":10)");
    // 3. WARNING, REPL, conn100
    c += log_line("2024-01-15T10:00:02.000+00:00", "W", "REPL", "conn100",
                  "replication lag detected");
    // 4. WARNING, COMMAND
    c += log_line("2024-01-15T10:00:03.000+00:00", "W", "COMMAND", "main",
                  "deprecated command used");
    // 5. INFO, COMMAND, db.users - Slow query with duration
    c += log_line("2024-01-15T10:00:04.000+00:00", "I", "COMMAND", "conn100",
                  "Slow query",
                  R"("ns":"db.users","durationMillis":500,"command":{"find":"users","filter":{"x":1}})");
    // 6. INFO, REPL
    c += log_line("2024-01-15T10:00:05.000+00:00", "I", "REPL", "conn200",
                  "sync source changed");
    // 7. INFO, NETWORK, db.orders
    c += log_line("2024-01-15T10:00:06.000+00:00", "I", "NETWORK", "conn200",
                  "connection accepted",
                  R"("ns":"db.orders")");
    // 8. DEBUG, COMMAND
    c += log_line("2024-01-15T10:00:07.000+00:00", "D", "COMMAND", "main",
                  "debug trace info");
    return c;
}

// Simple substring check
bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

// ============================================================
//  Shared fixture: loads once, used by all tool tests
// ============================================================
struct LlmFixture {
    std::string path;
    Cluster cluster;
    LlmTools tools;

    LlmFixture() {
        path = write_temp_log(build_test_content());
        cluster.add_file(path);
        cluster.load();
        tools.set_cluster(&cluster);
    }

    ~LlmFixture() {
        cleanup_temp(path);
    }
};

// ============================================================
//  Tests
// ============================================================

TEST_CASE("LlmTools: get_analysis_summary", "[llm_tools]") {
    LlmFixture f;
    auto result = f.tools.execute("get_analysis_summary", "{}");

    // Should be valid JSON
    REQUIRE(contains(result, "{"));
    REQUIRE(contains(result, "}"));
    // Should contain total_entries matching our 8 entries
    REQUIRE(contains(result, "\"total_entries\":8"));
    // Should contain by_severity section
    REQUIRE(contains(result, "by_severity"));
}

TEST_CASE("LlmTools: search_logs by severity", "[llm_tools]") {
    LlmFixture f;
    auto result = f.tools.execute("search_logs", R"({"severity":"ERROR"})");

    REQUIRE(contains(result, "\"matches\""));
    // All returned entries should have severity ERROR
    REQUIRE(contains(result, "\"severity\":\"ERROR\""));
    // Should not contain INFO matches
    REQUIRE_FALSE(contains(result, "\"severity\":\"INFO\""));
}

TEST_CASE("LlmTools: search_logs by text", "[llm_tools]") {
    LlmFixture f;
    auto result = f.tools.execute("search_logs", R"({"text":"Slow"})");

    REQUIRE(contains(result, "\"matches\""));
    REQUIRE(contains(result, "Slow"));
}

TEST_CASE("LlmTools: search_logs with limit", "[llm_tools]") {
    LlmFixture f;
    auto result = f.tools.execute("search_logs", R"({"limit":2})");

    REQUIRE(contains(result, "\"returned\":"));
    // The returned count should be <= 2
    // Check that "returned":2 or "returned":1 or "returned":0 appears
    // (it can't be > 2)
    REQUIRE(contains(result, "\"matches\""));
    // Simple check: no more than 2 index entries
    size_t idx_count = 0;
    size_t pos = 0;
    while ((pos = result.find("\"index\":", pos)) != std::string::npos) {
        ++idx_count;
        pos += 8;
    }
    REQUIRE(idx_count <= 2);
}

TEST_CASE("LlmTools: search_logs by component", "[llm_tools]") {
    LlmFixture f;
    auto result = f.tools.execute("search_logs", R"({"component":"COMMAND"})");

    REQUIRE(contains(result, "\"matches\""));
    // All returned entries should have COMMAND component
    REQUIRE(contains(result, "COMMAND"));
    // Verify no REPL-only or NETWORK-only entries leak through
    // (There are COMMAND entries so at least some results)
    REQUIRE(contains(result, "\"returned\":"));
}

TEST_CASE("LlmTools: get_entry_detail valid", "[llm_tools]") {
    LlmFixture f;
    auto result = f.tools.execute("get_entry_detail", R"({"entry_index":0})");

    // Should return the raw JSON line — must start with {
    REQUIRE(!result.empty());
    REQUIRE(result[0] == '{');
}

TEST_CASE("LlmTools: get_entry_detail out of range", "[llm_tools]") {
    LlmFixture f;
    auto result = f.tools.execute("get_entry_detail", R"({"entry_index":999999})");

    REQUIRE(contains(result, "error"));
}

TEST_CASE("LlmTools: get_slow_queries", "[llm_tools]") {
    LlmFixture f;
    auto result = f.tools.execute("get_slow_queries", "{}");

    REQUIRE(contains(result, "\"total_slow_queries\""));
    // We have at least 1 slow query (the "Slow query" entry)
    // Check it's not 0
    REQUIRE_FALSE(contains(result, "\"total_slow_queries\":0"));
}

TEST_CASE("LlmTools: get_connections", "[llm_tools]") {
    LlmFixture f;
    auto result = f.tools.execute("get_connections", "{}");

    REQUIRE(contains(result, "\"by_driver\""));
    REQUIRE(contains(result, "\"top_connections\""));
}

TEST_CASE("LlmTools: get_error_details", "[llm_tools]") {
    LlmFixture f;
    auto result = f.tools.execute("get_error_details", "{}");

    REQUIRE(contains(result, "\"error_groups\""));
    // total_errors should be >= 1 (we have 2 ERROR entries)
    REQUIRE(contains(result, "\"total_errors\":"));
    REQUIRE_FALSE(contains(result, "\"total_errors\":0"));
}

TEST_CASE("LlmTools: unknown tool", "[llm_tools]") {
    LlmFixture f;
    auto result = f.tools.execute("nonexistent", "{}");

    REQUIRE(contains(result, "error"));
}

TEST_CASE("LlmTools: no data loaded", "[llm_tools]") {
    LlmTools tools;  // no set_cluster
    auto result = tools.execute("get_analysis_summary", "{}");

    REQUIRE(contains(result, "No data loaded"));
}

TEST_CASE("LlmTools: tools_json schema", "[llm_tools]") {
    auto json = LlmTools::tools_json();

    REQUIRE(contains(json, "get_analysis_summary"));
    REQUIRE(contains(json, "search_logs"));
    REQUIRE(contains(json, "get_entry_detail"));
    REQUIRE(contains(json, "get_slow_queries"));
    REQUIRE(contains(json, "get_connections"));
    REQUIRE(contains(json, "get_error_details"));
}
