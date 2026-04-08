#pragma once

#include <string>
#include <string_view>
#include <simdjson.h>

// ------------------------------------------------------------
//  QueryShapeNormalizer
//
//  Converts a MongoDB query/filter document into a normalized
//  "shape" string where all values are replaced by type
//  placeholders: {str}, {int}, {num}, {bool}, {null}, {oid},
//  {date}, {regex}, {bin}, {ts}, [...], {...}
//
//  Object keys are sorted so that key order doesn't matter.
//
//  Two entry points:
//    normalize_element — takes an already-parsed simdjson element
//                        (fast path — no re-parse)
//    normalize         — takes a raw JSON string (used by detail view)
// ------------------------------------------------------------

class QueryShapeNormalizer {
public:
    // Fast path: normalise a simdjson element directly (no re-parse).
    static std::string normalize_element(const simdjson::dom::element& el);

    // Slow path: parse then normalise (kept for detail_view re-use).
    static std::string normalize(std::string_view json_doc);

    // Recursive helpers — called from normalize_element
    static void normalize_value(const simdjson::dom::element& el, std::string& out);
    static void normalize_object(const simdjson::dom::element& el, std::string& out);
    static void normalize_array(const simdjson::dom::element& el,  std::string& out);
};
