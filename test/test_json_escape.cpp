#include <catch2/catch_all.hpp>
#include <string>
#include <cstdio>

// ============================================================
//  Mirrored from src/llm/llm_client.cpp for direct testing
// ============================================================
namespace test_mirror {

static std::string json_esc(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

} // namespace test_mirror

using test_mirror::json_esc;

TEST_CASE("json_esc: empty", "[json_escape]") {
    REQUIRE(json_esc("") == "");
}

TEST_CASE("json_esc: no special chars", "[json_escape]") {
    REQUIRE(json_esc("hello world") == "hello world");
}

TEST_CASE("json_esc: double quote", "[json_escape]") {
    REQUIRE(json_esc("a\"b") == "a\\\"b");
}

TEST_CASE("json_esc: backslash", "[json_escape]") {
    REQUIRE(json_esc("a\\b") == "a\\\\b");
}

TEST_CASE("json_esc: newline", "[json_escape]") {
    REQUIRE(json_esc("a\nb") == "a\\nb");
}

TEST_CASE("json_esc: carriage return", "[json_escape]") {
    REQUIRE(json_esc("a\rb") == "a\\rb");
}

TEST_CASE("json_esc: tab", "[json_escape]") {
    REQUIRE(json_esc("a\tb") == "a\\tb");
}

TEST_CASE("json_esc: NUL char", "[json_escape]") {
    std::string input(3, '\0');
    input[0] = 'a';
    input[2] = 'b';
    REQUIRE(json_esc(input) == "a\\u0000b");
}

TEST_CASE("json_esc: control char 0x07", "[json_escape]") {
    std::string input = "a";
    input += static_cast<char>(0x07);
    input += "b";
    REQUIRE(json_esc(input) == "a\\u0007b");
}

TEST_CASE("json_esc: mixed special chars", "[json_escape]") {
    REQUIRE(json_esc("\"hello\"\n\\world\t") == "\\\"hello\\\"\\n\\\\world\\t");
}

TEST_CASE("json_esc: UTF-8 passthrough", "[json_escape]") {
    // UTF-8 bytes >= 0x80 should pass through unchanged
    std::string input = "\xc3\xa9"; // é in UTF-8
    REQUIRE(json_esc(input) == input);
}

TEST_CASE("json_esc: all control chars 0x00-0x1F", "[json_escape]") {
    for (int i = 0; i < 0x20; ++i) {
        std::string input(1, static_cast<char>(i));
        std::string result = json_esc(input);
        // All control chars should be escaped (not left as raw bytes)
        if (i == '\n') {
            REQUIRE(result == "\\n");
        } else if (i == '\r') {
            REQUIRE(result == "\\r");
        } else if (i == '\t') {
            REQUIRE(result == "\\t");
        } else {
            char expected[8];
            std::snprintf(expected, sizeof(expected), "\\u%04x", i);
            REQUIRE(result == std::string(expected));
        }
    }
}
