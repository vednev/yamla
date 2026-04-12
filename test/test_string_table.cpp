#include <catch2/catch_all.hpp>
#include "parser/log_entry.hpp"
#include "core/arena_chain.hpp"

#include <string>
#include <string_view>

// ============================================================
//  StringTable tests
// ============================================================

TEST_CASE("StringTable: UNKNOWN sentinel", "[string_table]") {
    ArenaChain chain;
    StringTable st(chain);
    REQUIRE(st.get(0) == std::string_view(""));
}

TEST_CASE("StringTable: initial size", "[string_table]") {
    ArenaChain chain;
    StringTable st(chain);
    REQUIRE(st.size() == 1);
}

TEST_CASE("StringTable: intern new string", "[string_table]") {
    ArenaChain chain;
    StringTable st(chain);
    uint32_t idx = st.intern("hello");
    REQUIRE(idx != 0);
    REQUIRE(st.get(idx) == "hello");
}

TEST_CASE("StringTable: dedup", "[string_table]") {
    ArenaChain chain;
    StringTable st(chain);
    uint32_t idx1 = st.intern("hello");
    uint32_t idx2 = st.intern("hello");
    REQUIRE(idx1 == idx2);
    REQUIRE(st.size() == 2); // UNKNOWN + "hello"
}

TEST_CASE("StringTable: different strings", "[string_table]") {
    ArenaChain chain;
    StringTable st(chain);
    uint32_t a = st.intern("aaa");
    uint32_t b = st.intern("bbb");
    REQUIRE(a != b);
}

TEST_CASE("StringTable: empty string returns UNKNOWN", "[string_table]") {
    ArenaChain chain;
    StringTable st(chain);
    uint32_t idx = st.intern("");
    REQUIRE(idx == StringTable::UNKNOWN);
}

TEST_CASE("StringTable: out of bounds", "[string_table]") {
    ArenaChain chain;
    StringTable st(chain);
    REQUIRE(st.get(999999) == std::string_view(""));
}

TEST_CASE("StringTable: stability after many interns", "[string_table]") {
    ArenaChain chain;
    StringTable st(chain);
    std::vector<uint32_t> indices;
    for (int i = 0; i < 1000; ++i) {
        std::string s = "str_" + std::to_string(i);
        indices.push_back(st.intern(s));
    }
    for (int i = 0; i < 1000; ++i) {
        std::string expected = "str_" + std::to_string(i);
        REQUIRE(st.get(indices[i]) == expected);
    }
}

TEST_CASE("SvHash: consistency", "[string_table]") {
    SvHash h;
    size_t h1 = h(std::string_view("test"));
    size_t h2 = h(std::string_view("test"));
    REQUIRE(h1 == h2);
    size_t h3 = h(std::string_view("tset"));
    REQUIRE(h1 != h3);
}

TEST_CASE("Severity: round-trip", "[string_table]") {
    REQUIRE(severity_from_char('F') == Severity::Fatal);
    REQUIRE(severity_from_char('E') == Severity::Error);
    REQUIRE(severity_from_char('W') == Severity::Warning);
    REQUIRE(severity_from_char('I') == Severity::Info);
    REQUIRE(severity_from_char('D') == Severity::Debug);
    REQUIRE(severity_from_char('X') == Severity::Unknown);

    REQUIRE(std::string(severity_string(Severity::Fatal))   == "FATAL");
    REQUIRE(std::string(severity_string(Severity::Error))   == "ERROR");
    REQUIRE(std::string(severity_string(Severity::Warning)) == "WARN");
    REQUIRE(std::string(severity_string(Severity::Info))    == "INFO");
    REQUIRE(std::string(severity_string(Severity::Debug))   == "DEBUG");
    REQUIRE(std::string(severity_string(Severity::Unknown)) == "?");
}

TEST_CASE("severity_from_string: all severities", "[string_table]") {
    REQUIRE(severity_from_string("FATAL") == Severity::Fatal);
    REQUIRE(severity_from_string("ERROR") == Severity::Error);
    REQUIRE(severity_from_string("WARN")  == Severity::Warning);
    REQUIRE(severity_from_string("INFO")  == Severity::Info);
    REQUIRE(severity_from_string("DEBUG") == Severity::Debug);
    REQUIRE(severity_from_string("???")   == Severity::Unknown);
    REQUIRE(severity_from_string(nullptr) == Severity::Unknown);
}
