#include <catch2/catch_all.hpp>
#include "analysis/cluster.hpp"

#include <fstream>
#include <cstdio>
#include <string>
#include <vector>

// ============================================================
//  Helpers
// ============================================================
namespace {

static int g_append_counter = 0;

std::string write_temp_log(const std::string& content) {
    std::string path = "/tmp/yamla_append_test_" +
                       std::to_string(++g_append_counter) + ".log";
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

} // namespace

// ============================================================
//  Tests
// ============================================================

TEST_CASE("Cluster append: add files to existing", "[cluster][append]") {
    std::string content_a;
    content_a += log_line("2024-01-15T10:00:00.000+00:00", "I", "COMMAND", "conn1", "entry A1");
    content_a += log_line("2024-01-15T10:00:01.000+00:00", "W", "REPL",    "conn2", "entry A2");

    std::string content_b;
    content_b += log_line("2024-01-15T10:00:02.000+00:00", "E", "NETWORK", "conn3", "entry B1");
    content_b += log_line("2024-01-15T10:00:03.000+00:00", "I", "COMMAND", "conn4", "entry B2");

    auto path_a = write_temp_log(content_a);
    auto path_b = write_temp_log(content_b);

    Cluster c;
    c.add_file(path_a);
    c.load();

    REQUIRE(c.entries().size() == 2);
    REQUIRE(c.nodes().size() == 1);

    c.append_files({path_b});

    REQUIRE(c.entries().size() == 4);
    REQUIRE(c.nodes().size() == 2);
    REQUIRE(c.state() == LoadState::Ready);

    cleanup_temp(path_a);
    cleanup_temp(path_b);
}

TEST_CASE("Cluster append: cross-file dedup", "[cluster][append]") {
    // Both files share one identical entry
    std::string shared = log_line("2024-01-15T10:30:45.123+00:00", "I", "COMMAND", "conn1", "shared msg");
    std::string unique_a = log_line("2024-01-15T10:00:00.000+00:00", "W", "REPL", "conn2", "only A");
    std::string unique_b = log_line("2024-01-15T10:00:01.000+00:00", "E", "NETWORK", "conn3", "only B");

    auto path_a = write_temp_log(unique_a + shared);
    auto path_b = write_temp_log(unique_b + shared);

    Cluster c;
    c.set_dedup_enabled(true);
    c.add_file(path_a);
    c.load();

    // Before append: 2 entries on node 0
    REQUIRE(c.entries().size() == 2);

    c.append_files({path_b});

    // After append+dedup: unique_a + unique_b + shared(stacked) = 3
    REQUIRE(c.entries().size() == 3);

    // Find the stacked entry — both node bits should be set
    bool found_stacked = false;
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if ((c.entries()[i].node_mask & 0x3) == 0x3) {
            found_stacked = true;
            break;
        }
    }
    REQUIRE(found_stacked);

    cleanup_temp(path_a);
    cleanup_temp(path_b);
}

TEST_CASE("Cluster append: preserves old entries", "[cluster][append]") {
    std::string content_a = log_line("2024-01-15T10:00:00.000+00:00", "I", "COMMAND", "conn1", "old entry");
    std::string content_b = log_line("2024-01-15T11:00:00.000+00:00", "I", "COMMAND", "conn2", "new entry");

    auto path_a = write_temp_log(content_a);
    auto path_b = write_temp_log(content_b);

    Cluster c;
    c.add_file(path_a);
    c.load();
    c.append_files({path_b});

    REQUIRE(c.entries().size() == 2);

    // Verify both timestamps are present
    bool found_10 = false, found_11 = false;
    for (size_t i = 0; i < c.entries().size(); ++i) {
        int64_t ts = c.entries()[i].timestamp_ms;
        // 10:00:00 => some ts, 11:00:00 => ts + 3600000
        if (ts < c.analysis().earliest_ms + 1800000) found_10 = true;
        else found_11 = true;
    }
    REQUIRE(found_10);
    REQUIRE(found_11);

    cleanup_temp(path_a);
    cleanup_temp(path_b);
}

TEST_CASE("Cluster append: re-analyzes", "[cluster][append]") {
    std::string content_a = log_line("2024-01-15T10:00:00.000+00:00", "E", "COMMAND", "conn1", "error msg");
    std::string content_b = log_line("2024-01-15T10:00:01.000+00:00", "W", "REPL",    "conn2", "warn msg");

    auto path_a = write_temp_log(content_a);
    auto path_b = write_temp_log(content_b);

    Cluster c;
    c.add_file(path_a);
    c.load();

    // After initial load: 1 ERROR
    REQUIRE(c.analysis().total_entries == 1);

    c.append_files({path_b});

    // After append: should have both ERROR and WARN in by_severity
    REQUIRE(c.analysis().total_entries == 2);

    bool has_error = false, has_warn = false;
    for (const auto& ce : c.analysis().by_severity) {
        if (ce.label == "ERROR") has_error = true;
        if (ce.label == "WARN")  has_warn  = true;
    }
    REQUIRE(has_error);
    REQUIRE(has_warn);

    cleanup_temp(path_a);
    cleanup_temp(path_b);
}

TEST_CASE("Cluster append: nodes get colors", "[cluster][append]") {
    std::string content_a = log_line("2024-01-15T10:00:00.000+00:00", "I", "COMMAND", "conn1", "a");
    std::string content_b = log_line("2024-01-15T10:00:01.000+00:00", "I", "COMMAND", "conn2", "b");

    auto path_a = write_temp_log(content_a);
    auto path_b = write_temp_log(content_b);

    Cluster c;
    c.add_file(path_a);
    c.load();
    c.append_files({path_b});

    REQUIRE(c.nodes().size() == 2);
    // Both nodes should have non-zero alpha (i.e., valid color)
    REQUIRE(c.nodes()[0].color.a > 0.0f);
    REQUIRE(c.nodes()[1].color.a > 0.0f);

    cleanup_temp(path_a);
    cleanup_temp(path_b);
}
