#include <catch2/catch_all.hpp>
#include "parser/query_shape.hpp"

#include <string>

TEST_CASE("QueryShape: empty string", "[query_shape]") {
    REQUIRE(QueryShapeNormalizer::normalize("") == "");
}

TEST_CASE("QueryShape: simple string", "[query_shape]") {
    REQUIRE(QueryShapeNormalizer::normalize("\"hello\"") == "{str}");
}

TEST_CASE("QueryShape: integer", "[query_shape]") {
    REQUIRE(QueryShapeNormalizer::normalize("42") == "{int}");
}

TEST_CASE("QueryShape: float", "[query_shape]") {
    REQUIRE(QueryShapeNormalizer::normalize("3.14") == "{num}");
}

TEST_CASE("QueryShape: boolean", "[query_shape]") {
    REQUIRE(QueryShapeNormalizer::normalize("true") == "{bool}");
}

TEST_CASE("QueryShape: null", "[query_shape]") {
    REQUIRE(QueryShapeNormalizer::normalize("null") == "{null}");
}

TEST_CASE("QueryShape: simple object keys sorted", "[query_shape]") {
    auto result = QueryShapeNormalizer::normalize(R"({"b":1,"a":2})");
    REQUIRE(result == "{a:{int},b:{int}}");
}

TEST_CASE("QueryShape: key sorting z,a,m", "[query_shape]") {
    auto result = QueryShapeNormalizer::normalize(R"({"z":1,"a":2,"m":3})");
    REQUIRE(result == "{a:{int},m:{int},z:{int}}");
}

TEST_CASE("QueryShape: nested object", "[query_shape]") {
    auto result = QueryShapeNormalizer::normalize(R"({"a":{"b":1}})");
    REQUIRE(result == "{a:{b:{int}}}");
}

TEST_CASE("QueryShape: array", "[query_shape]") {
    auto result = QueryShapeNormalizer::normalize(R"([1,2,3])");
    REQUIRE(result == "[{int},{int},{int}]");
}

TEST_CASE("QueryShape: empty object", "[query_shape]") {
    REQUIRE(QueryShapeNormalizer::normalize("{}") == "{}");
}

TEST_CASE("QueryShape: empty array", "[query_shape]") {
    REQUIRE(QueryShapeNormalizer::normalize("[]") == "[]");
}

TEST_CASE("QueryShape: extended JSON $oid", "[query_shape]") {
    auto result = QueryShapeNormalizer::normalize(R"({"$oid":"507f1f77bcf86cd799439011"})");
    REQUIRE(result == "{oid}");
}

TEST_CASE("QueryShape: extended JSON $date", "[query_shape]") {
    auto result = QueryShapeNormalizer::normalize(R"({"$date":"2024-01-01T00:00:00Z"})");
    REQUIRE(result == "{date}");
}

TEST_CASE("QueryShape: extended JSON $regex", "[query_shape]") {
    auto result = QueryShapeNormalizer::normalize(R"({"$regex":"^abc","$options":"i"})");
    REQUIRE(result == "{regex}");
}

TEST_CASE("QueryShape: invalid JSON returns original", "[query_shape]") {
    std::string input = "not json at all";
    REQUIRE(QueryShapeNormalizer::normalize(input) == input);
}

TEST_CASE("QueryShape: realistic MongoDB query", "[query_shape]") {
    auto result = QueryShapeNormalizer::normalize(
        R"({"status":"active","age":{"$gt":25},"_id":{"$oid":"507f1f77bcf86cd799439011"}})"
    );
    // Keys sorted: _id, age, status
    // _id -> {oid}, age -> {$gt:{int}}, status -> {str}
    REQUIRE(result == "{_id:{oid},age:{$gt:{int}},status:{str}}");
}
