#include <catch2/catch_all.hpp>
#include "parser/log_entry.hpp"
#include "parser/log_parser.hpp"
#include "core/arena_chain.hpp"
#include "core/chunk_vector.hpp"
#include "core/mmap_file.hpp"

#include <cstdio>
#include <cstring>
#include <string>

// ============================================================
//  Mirrored from src/parser/log_parser.cpp for direct testing
// ============================================================
namespace {

static int64_t utc_to_epoch(int y, int mon, int d,
                              int h, int mi, int sec) noexcept
{
    y   -= (mon <= 2);
    int era  = y / 400;
    int yoe  = y - era * 400;
    int doy  = (153*(mon + (mon<=2 ? 9 : -3)) + 2)/5 + d - 1;
    int doe  = yoe*365 + yoe/4 - yoe/100 + doy;
    int64_t days = static_cast<int64_t>(era)*146097 + doe - 719468;
    return days*86400 + h*3600 + mi*60 + sec;
}

static int64_t parse_timestamp(std::string_view ts) noexcept {
    if (ts.size() < 23) return -1;
    const char* s = ts.data();
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

// Helper: write content to a temp file, return path
struct TempFile {
    std::string path;
    TempFile(const std::string& content) {
        path = "/tmp/yamla_test_" + std::to_string(reinterpret_cast<uintptr_t>(this)) + ".log";
        FILE* f = std::fopen(path.c_str(), "w");
        std::fwrite(content.data(), 1, content.size(), f);
        std::fclose(f);
    }
    ~TempFile() { std::remove(path.c_str()); }
};

} // namespace

// ============================================================
//  utc_to_epoch tests
// ============================================================

TEST_CASE("utc_to_epoch: epoch", "[log_parser]") {
    REQUIRE(utc_to_epoch(1970, 1, 1, 0, 0, 0) == 0);
}

TEST_CASE("utc_to_epoch: known date", "[log_parser]") {
    REQUIRE(utc_to_epoch(2024, 1, 15, 10, 30, 45) == 1705314645);
}

TEST_CASE("utc_to_epoch: leap year", "[log_parser]") {
    REQUIRE(utc_to_epoch(2024, 2, 29, 0, 0, 0) == 1709164800);
}

TEST_CASE("utc_to_epoch: end of day", "[log_parser]") {
    REQUIRE(utc_to_epoch(2024, 1, 1, 23, 59, 59) ==
            utc_to_epoch(2024, 1, 1, 0, 0, 0) + 86399);
}

TEST_CASE("utc_to_epoch: year 2000", "[log_parser]") {
    REQUIRE(utc_to_epoch(2000, 1, 1, 0, 0, 0) == 946684800);
}

// ============================================================
//  parse_timestamp tests
// ============================================================

TEST_CASE("parse_timestamp: valid", "[log_parser]") {
    REQUIRE(parse_timestamp("2024-01-15T10:30:45.123+00:00") == 1705314645123);
}

TEST_CASE("parse_timestamp: midnight", "[log_parser]") {
    REQUIRE(parse_timestamp("2024-01-01T00:00:00.000+00:00") == 1704067200000);
}

TEST_CASE("parse_timestamp: max ms", "[log_parser]") {
    int64_t ts = parse_timestamp("2024-01-15T10:30:45.999+00:00");
    REQUIRE(ts % 1000 == 999);
}

TEST_CASE("parse_timestamp: too short", "[log_parser]") {
    REQUIRE(parse_timestamp("2024-01-15T10:30") == -1);
}

TEST_CASE("parse_timestamp: empty", "[log_parser]") {
    REQUIRE(parse_timestamp("") == -1);
}

// ============================================================
//  Full parser integration tests
// ============================================================

TEST_CASE("LogParser: single valid line", "[log_parser]") {
    std::string line =
        R"({"t":{"$date":"2024-01-15T10:30:45.123+00:00"},"s":"I","c":"COMMAND","ctx":"conn1","msg":"Slow query","attr":{"ns":"test.coll","durationMillis":500,"command":{"find":"coll","filter":{"x":1}}}})"
        "\n";
    TempFile tf(line);
    ArenaChain chain;
    StringTable strings(chain);
    ChunkVector<LogEntry> entries(chain);
    LogParser parser(strings);
    MmapFile file(tf.path);
    parser.parse_file(file, 0, entries);

    REQUIRE(entries.size() == 1);
    const auto& e = entries[0];
    REQUIRE(e.severity == Severity::Info);
    REQUIRE(e.timestamp_ms == 1705314645123);
    REQUIRE(strings.get(e.component_idx) == "COMMAND");
    REQUIRE(strings.get(e.msg_idx) == "Slow query");
    REQUIRE(strings.get(e.ns_idx) == "test.coll");
    REQUIRE(e.duration_ms == 500);
    REQUIRE(e.conn_id == 1);
}

TEST_CASE("LogParser: multiple lines", "[log_parser]") {
    std::string content;
    for (int i = 0; i < 3; ++i) {
        content += R"({"t":{"$date":"2024-01-15T10:30:45.)" + std::to_string(100+i) +
                   R"(+00:00"},"s":"I","c":"NET","ctx":"conn1","msg":"test"})" "\n";
    }
    TempFile tf(content);
    ArenaChain chain;
    StringTable strings(chain);
    ChunkVector<LogEntry> entries(chain);
    LogParser parser(strings);
    MmapFile file(tf.path);
    parser.parse_file(file, 0, entries);

    REQUIRE(entries.size() == 3);
}

TEST_CASE("LogParser: malformed JSON", "[log_parser]") {
    // simdjson parse_many may handle invalid lines differently depending
    // on where they occur in the stream. The key invariant is that at
    // least one valid entry is parsed, and the failed-lines counter is
    // incremented for the bad line.
    std::string content;
    content += R"({"t":{"$date":"2024-01-15T10:30:45.123+00:00"},"s":"I","c":"NET","ctx":"conn1","msg":"ok"})" "\n";
    content += "NOT VALID JSON\n";
    content += R"({"t":{"$date":"2024-01-15T10:30:45.456+00:00"},"s":"W","c":"NET","ctx":"conn2","msg":"ok2"})" "\n";
    TempFile tf(content);
    ArenaChain chain;
    StringTable strings(chain);
    ChunkVector<LogEntry> entries(chain);
    LogParser parser(strings);
    MmapFile file(tf.path);
    parser.parse_file(file, 0, entries);

    // At least 1 valid entry parsed; not all 3 lines succeeded
    REQUIRE(entries.size() >= 1);
    REQUIRE(entries.size() <= 2);
    REQUIRE(parser.total_lines_failed() >= 1);
}

TEST_CASE("LogParser: all severities", "[log_parser]") {
    const char* sevs[] = {"F", "E", "W", "I", "D"};
    Severity expected[] = {Severity::Fatal, Severity::Error, Severity::Warning, Severity::Info, Severity::Debug};
    std::string content;
    for (int i = 0; i < 5; ++i) {
        content += std::string(R"({"t":{"$date":"2024-01-15T10:30:45.)") +
                   std::to_string(100+i) + R"(+00:00"},"s":")" + sevs[i] +
                   R"(","c":"NET","ctx":"conn1","msg":"m"})" + "\n";
    }
    TempFile tf(content);
    ArenaChain chain;
    StringTable strings(chain);
    ChunkVector<LogEntry> entries(chain);
    LogParser parser(strings);
    MmapFile file(tf.path);
    parser.parse_file(file, 0, entries);

    REQUIRE(entries.size() == 5);
    for (int i = 0; i < 5; ++i) {
        REQUIRE(entries[i].severity == expected[i]);
    }
}

TEST_CASE("LogParser: empty file", "[log_parser]") {
    TempFile tf("");
    ArenaChain chain;
    StringTable strings(chain);
    ChunkVector<LogEntry> entries(chain);
    LogParser parser(strings);
    MmapFile file(tf.path);
    parser.parse_file(file, 0, entries);

    REQUIRE(entries.size() == 0);
}
