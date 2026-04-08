#include "query_shape.hpp"

#include <simdjson.h>
#include <algorithm>
#include <vector>
#include <utility>

// Thread-local parser for the slow-path (raw JSON string) entry point
static thread_local simdjson::dom::parser tl_parser;

// ------------------------------------------------------------
//  Extended JSON type detection
// ------------------------------------------------------------
static bool try_extended_json(const simdjson::dom::element& el, std::string& out) {
    if (el.type() != simdjson::dom::element_type::OBJECT) return false;
    auto obj = el.get_object().value();
    size_t count = 0;
    std::string_view first_key;
    for (auto [k, _] : obj) {
        if (count == 0) first_key = k;
        if (++count > 2) return false;
    }
    if (count == 0) return false;

    if (first_key == "$oid")   { out += "{oid}";   return true; }
    if (first_key == "$date")  { out += "{date}";  return true; }
    if (first_key == "$regex") { out += "{regex}"; return true; }
    if (first_key == "$numberDecimal" ||
        first_key == "$numberDouble"  ||
        first_key == "$numberLong"    ||
        first_key == "$numberInt")   { out += "{num}"; return true; }
    if (first_key == "$binary")      { out += "{bin}"; return true; }
    if (first_key == "$timestamp")   { out += "{ts}";  return true; }
    return false;
}

// ------------------------------------------------------------
//  Core recursive normalizers — now public statics on the class
// ------------------------------------------------------------
void QueryShapeNormalizer::normalize_value(const simdjson::dom::element& el,
                                            std::string& out)
{
    using T = simdjson::dom::element_type;
    switch (el.type()) {
        case T::STRING:     out += "{str}";  break;
        case T::INT64:
        case T::UINT64:     out += "{int}";  break;
        case T::DOUBLE:     out += "{num}";  break;
        case T::BOOL:       out += "{bool}"; break;
        case T::NULL_VALUE: out += "{null}"; break;
        case T::ARRAY:      normalize_array(el, out); break;
        case T::OBJECT:
            if (!try_extended_json(el, out))
                normalize_object(el, out);
            break;
    }
}

void QueryShapeNormalizer::normalize_array(const simdjson::dom::element& el,
                                            std::string& out)
{
    out += '[';
    bool first = true;
    for (auto elem : el.get_array().value()) {
        if (!first) out += ',';
        normalize_value(elem, out);
        first = false;
    }
    out += ']';
}

void QueryShapeNormalizer::normalize_object(const simdjson::dom::element& el,
                                             std::string& out)
{
    auto obj = el.get_object().value();

    // Collect and sort keys for deterministic shape
    std::vector<std::pair<std::string_view, simdjson::dom::element>> pairs;
    for (auto [k, v] : obj)
        pairs.emplace_back(k, v);
    std::sort(pairs.begin(), pairs.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    out += '{';
    bool first = true;
    for (auto& [k, v] : pairs) {
        if (!first) out += ',';
        out += k;
        out += ':';
        normalize_value(v, out);
        first = false;
    }
    out += '}';
}

// ------------------------------------------------------------
//  Fast path — element already parsed by the caller
// ------------------------------------------------------------
std::string QueryShapeNormalizer::normalize_element(
    const simdjson::dom::element& el)
{
    std::string result;
    result.reserve(64);
    normalize_value(el, result);
    return result;
}

// ------------------------------------------------------------
//  Slow path — parse from raw JSON string then normalize
// ------------------------------------------------------------
std::string QueryShapeNormalizer::normalize(std::string_view json_doc) {
    if (json_doc.empty()) return "";
    simdjson::dom::element el;
    auto err = tl_parser.parse(json_doc.data(), json_doc.size()).get(el);
    if (err) return std::string(json_doc);
    return normalize_element(el);
}
