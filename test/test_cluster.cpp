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

static int g_temp_counter = 0;

std::string write_temp_log(const std::string& content,
                           const std::string& name_hint = "")
{
    std::string path;
    if (!name_hint.empty()) {
        path = name_hint;
    } else {
        path = "/tmp/yamla_cluster_test_" + std::to_string(++g_temp_counter) + ".log";
    }
    std::ofstream ofs(path, std::ios::binary);
    ofs << content;
    ofs.close();
    return path;
}

void cleanup_temp(const std::string& path) {
    std::remove(path.c_str());
}

// Build a single MongoDB structured log line
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

TEST_CASE("Cluster: single file load", "[cluster]") {
    std::string content;
    content += log_line("2024-01-15T10:30:45.100+00:00", "I", "COMMAND", "conn1", "msg1");
    content += log_line("2024-01-15T10:30:45.200+00:00", "W", "REPL",    "conn2", "msg2");
    content += log_line("2024-01-15T10:30:45.300+00:00", "E", "NETWORK", "conn3", "msg3");

    auto path = write_temp_log(content);
    Cluster c;
    c.add_file(path);
    c.load();

    REQUIRE(c.state()           == LoadState::Ready);
    REQUIRE(c.entries().size()  == 3);
    REQUIRE(c.nodes().size()    == 1);
    REQUIRE(c.analysis().total_entries == 3);

    cleanup_temp(path);
}

TEST_CASE("Cluster: multi-file dedup", "[cluster]") {
    // Shared entry: same timestamp, severity, component, msg
    std::string shared = log_line("2024-01-15T10:30:45.123+00:00", "I", "COMMAND", "conn1", "test msg");
    std::string unique_a = log_line("2024-01-15T10:30:46.000+00:00", "W", "REPL", "conn2", "unique A");
    std::string unique_b = log_line("2024-01-15T10:30:47.000+00:00", "E", "NETWORK", "conn3", "unique B");

    auto path_a = write_temp_log(shared + unique_a);
    auto path_b = write_temp_log(shared + unique_b);

    Cluster c;
    c.add_file(path_a);
    c.add_file(path_b);
    c.load();

    REQUIRE(c.state() == LoadState::Ready);
    // After dedup: shared entry stacked, plus 2 unique = 3 entries
    REQUIRE(c.entries().size() == 3);

    // Find the stacked entry (node_mask has bits 0 and 1 set)
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

TEST_CASE("Cluster: DedupAlt preservation", "[cluster]") {
    std::string shared = log_line("2024-01-15T10:30:45.123+00:00", "I", "COMMAND", "conn1", "dup msg");
    std::string unique_a = log_line("2024-01-15T10:30:46.000+00:00", "W", "REPL", "conn2", "A only");
    std::string unique_b = log_line("2024-01-15T10:30:47.000+00:00", "E", "NETWORK", "conn3", "B only");

    auto path_a = write_temp_log(shared + unique_a);
    auto path_b = write_temp_log(shared + unique_b);

    Cluster c;
    c.add_file(path_a);
    c.add_file(path_b);
    c.load();

    // Find the stacked entry
    size_t stacked_idx = 0;
    bool found = false;
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if ((c.entries()[i].node_mask & 0x3) == 0x3) {
            stacked_idx = i;
            found = true;
            break;
        }
    }
    REQUIRE(found);

    // get_node_raw for both nodes should succeed
    uint64_t off0 = 0, off1 = 0;
    uint32_t len0 = 0, len1 = 0;
    uint16_t fidx0 = 0, fidx1 = 0;
    REQUIRE(c.get_node_raw(stacked_idx, 0, off0, len0, fidx0));
    REQUIRE(c.get_node_raw(stacked_idx, 1, off1, len1, fidx1));

    // Both should have valid offset/len
    REQUIRE(len0 > 0);
    REQUIRE(len1 > 0);

    // file_idx must correctly identify each node's source file
    REQUIRE(fidx0 == 0);
    REQUIRE(fidx1 == 1);

    cleanup_temp(path_a);
    cleanup_temp(path_b);
}

TEST_CASE("Cluster: hostname from Process Details", "[cluster]") {
    std::string pd_line = R"({"t":{"$date":"2024-01-15T10:30:44.000+00:00"},"s":"I","c":"CONTROL","id":23400,"ctx":"main","msg":"Process Details","attr":{"host":"myhost.example.com:27017","port":27017}})" "\n";
    std::string normal = log_line("2024-01-15T10:30:45.000+00:00", "I", "COMMAND", "conn1", "normal");

    auto path = write_temp_log(pd_line + normal);

    Cluster c;
    c.add_file(path);
    c.load();

    REQUIRE(c.state() == LoadState::Ready);
    REQUIRE(c.nodes().size() == 1);
    REQUIRE(c.nodes()[0].hostname == "myhost.example.com");

    cleanup_temp(path);
}

TEST_CASE("Cluster: hostname fallback to filename", "[cluster]") {
    std::string content;
    content += log_line("2024-01-15T10:30:45.100+00:00", "I", "COMMAND", "conn1", "msg1");
    content += log_line("2024-01-15T10:30:45.200+00:00", "I", "COMMAND", "conn2", "msg2");

    std::string path = "/tmp/test_node_alpha.log";
    write_temp_log(content, path);

    Cluster c;
    c.add_file(path);
    c.load();

    REQUIRE(c.state() == LoadState::Ready);
    REQUIRE(c.nodes()[0].hostname == "test_node_alpha");

    cleanup_temp(path);
}

TEST_CASE("Cluster: time range in analysis", "[cluster]") {
    std::string content;
    content += log_line("2024-01-15T10:00:00.000+00:00", "I", "COMMAND", "conn1", "first");
    content += log_line("2024-01-15T10:05:00.000+00:00", "I", "COMMAND", "conn1", "mid");
    content += log_line("2024-01-15T10:10:00.000+00:00", "I", "COMMAND", "conn1", "last");

    auto path = write_temp_log(content);
    Cluster c;
    c.add_file(path);
    c.load();

    REQUIRE(c.analysis().earliest_ms < c.analysis().latest_ms);

    cleanup_temp(path);
}

TEST_CASE("Cluster: state transitions", "[cluster]") {
    std::string content = log_line("2024-01-15T10:30:45.100+00:00", "I", "COMMAND", "conn1", "msg");
    auto path = write_temp_log(content);

    Cluster c;
    REQUIRE(c.state() == LoadState::Idle);

    c.add_file(path);
    c.load();
    REQUIRE(c.state() == LoadState::Ready);

    cleanup_temp(path);
}

TEST_CASE("Cluster: empty file", "[cluster]") {
    auto path = write_temp_log("");
    Cluster c;
    c.add_file(path);
    c.load();

    REQUIRE(c.state() == LoadState::Ready);
    REQUIRE(c.entries().size() == 0);

    cleanup_temp(path);
}
