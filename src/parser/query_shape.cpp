#include "query_shape.hpp"

#include <simdjson.h>
#include <algorithm>
#include <vector>
#include <utility>

// ------------------------------------------------------------
//  Implementation details
// ------------------------------------------------------------

// Thread-local parser to avoid per-call allocation overhead
static thread_local simdjson::dom::parser tl_parser;

static void normalize_value(const simdjson::dom::element& el, std::string& out);
static void normalize_object(const simdjson::dom::element& el, std::string& out);
static void normalize_array(const simdjson::dom::element& el, std::string& out);

// Detect extended JSON types encoded as single-key objects
// e.g., {"$oid": "..."} {"$date": ...} {"$regex": ...}
static bool try_extended_json(const simdjson::dom::element& el, std::string& out) {
    if (el.type() != simdjson::dom::element_type::OBJECT) return false;
    auto obj = el.get_object().value();
    // Count keys — extended JSON types have exactly 1 or 2 keys
    size_t count = 0;
    std::string_view first_key;
    for (auto [k, _] : obj) {
        if (count == 0) first_key = k;
        ++count;
        if (count > 2) return false;
    }
    if (count == 0) return false;

    if (first_key == "$oid")   { out += "{oid}";   return true; }
    if (first_key == "$date")  { out += "{date}";  return true; }
    if (first_key == "$regex") { out += "{regex}"; return true; }
    if (first_key == "$numberDecimal" ||
        first_key == "$numberDouble" ||
        first_key == "$numberLong" ||
        first_key == "$numberInt")  { out += "{num}"; return true; }
    if (first_key == "$binary")     { out += "{bin}"; return true; }
    if (first_key == "$timestamp")  { out += "{ts}";  return true; }
    return false;
}

static void normalize_value(const simdjson::dom::element& el, std::string& out) {
    using T = simdjson::dom::element_type;
    switch (el.type()) {
        case T::STRING:  out += "{str}";  break;
        case T::INT64:
        case T::UINT64:  out += "{int}";  break;
        case T::DOUBLE:  out += "{num}";  break;
        case T::BOOL:    out += "{bool}"; break;
        case T::NULL_VALUE: out += "{null}"; break;
        case T::ARRAY:   normalize_array(el, out);  break;
        case T::OBJECT:
            if (!try_extended_json(el, out))
                normalize_object(el, out);
            break;
    }
}

static void normalize_array(const simdjson::dom::element& el, std::string& out) {
    auto arr = el.get_array().value();
    out += '[';
    bool first = true;
    for (auto elem : arr) {
        if (!first) out += ',';
        normalize_value(elem, out);
        first = false;
    }
    out += ']';
}

static void normalize_object(const simdjson::dom::element& el, std::string& out) {
    auto obj = el.get_object().value();

    // Collect and sort keys for deterministic shape regardless of insertion order
    std::vector<std::pair<std::string_view, simdjson::dom::element>> pairs;
    for (auto [k, v] : obj) {
        pairs.emplace_back(k, v);
    }
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
//  Public interface
// ------------------------------------------------------------

std::string QueryShapeNormalizer::normalize(std::string_view json_doc) {
    if (json_doc.empty()) return "";

    simdjson::dom::element el;
    auto err = tl_parser.parse(json_doc.data(), json_doc.size()).get(el);
    if (err) return std::string(json_doc); // unparseable: return as-is

    std::string result;
    result.reserve(json_doc.size()); // rough upper bound
    normalize_value(el, result);
    return result;
}

// The file-level static functions (normalize_value, normalize_object,
// normalize_array) serve as the full implementation; no class wrappers needed.
