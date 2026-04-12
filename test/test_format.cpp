#include <catch2/catch_all.hpp>
#include "core/format.hpp"

#include <string>
#include <cstdint>
#include <climits>

// ============================================================
//  fmt_count tests
// ============================================================

TEST_CASE("fmt_count: zero", "[format]") {
    REQUIRE(fmt_count(0) == "0");
}

TEST_CASE("fmt_count: small number", "[format]") {
    REQUIRE(fmt_count(42) == "42");
}

TEST_CASE("fmt_count: 999", "[format]") {
    REQUIRE(fmt_count(999) == "999");
}

TEST_CASE("fmt_count: 1000", "[format]") {
    REQUIRE(fmt_count(1000) == "1,000");
}

TEST_CASE("fmt_count: 1234567", "[format]") {
    REQUIRE(fmt_count(1234567) == "1,234,567");
}

TEST_CASE("fmt_count: UINT64_MAX", "[format]") {
    std::string result = fmt_count(UINT64_MAX);
    // UINT64_MAX = 18,446,744,073,709,551,615
    REQUIRE(result == "18,446,744,073,709,551,615");
}

// ============================================================
//  fmt_compact tests
// ============================================================

TEST_CASE("fmt_compact: zero", "[format]") {
    char buf[16];
    REQUIRE(std::string(fmt_compact(0, buf, sizeof(buf))) == "0");
}

TEST_CASE("fmt_compact: 999", "[format]") {
    char buf[16];
    REQUIRE(std::string(fmt_compact(999, buf, sizeof(buf))) == "999");
}

TEST_CASE("fmt_compact: 1000", "[format]") {
    char buf[16];
    REQUIRE(std::string(fmt_compact(1000, buf, sizeof(buf))) == "1K");
}

TEST_CASE("fmt_compact: 1200", "[format]") {
    char buf[16];
    REQUIRE(std::string(fmt_compact(1200, buf, sizeof(buf))) == "1.2K");
}

TEST_CASE("fmt_compact: 2000", "[format]") {
    char buf[16];
    REQUIRE(std::string(fmt_compact(2000, buf, sizeof(buf))) == "2K");
}

TEST_CASE("fmt_compact: 10000", "[format]") {
    char buf[16];
    REQUIRE(std::string(fmt_compact(10000, buf, sizeof(buf))) == "10K");
}

TEST_CASE("fmt_compact: 1000000", "[format]") {
    char buf[16];
    REQUIRE(std::string(fmt_compact(1000000, buf, sizeof(buf))) == "1M");
}

TEST_CASE("fmt_compact: 1000000000", "[format]") {
    char buf[16];
    REQUIRE(std::string(fmt_compact(1000000000, buf, sizeof(buf))) == "1B");
}
