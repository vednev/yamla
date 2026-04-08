#pragma once

#include <string>
#include <string_view>

// ------------------------------------------------------------
//  QueryShapeNormalizer
//
//  Converts a parsed MongoDB query document (as a raw JSON
//  string slice) into a normalized "shape" string where all
//  values are replaced by type placeholders:
//
//    {str}   — string
//    {int}   — integer (int64 / uint64)
//    {num}   — double / decimal
//    {bool}  — boolean
//    {null}  — null
//    {oid}   — ObjectId  {"$oid": "..."}
//    {date}  — Date      {"$date": ...}
//    {regex} — RegEx     {"$regex": ...}
//    {bin}   — Binary    {"$binary": ...}
//    {ts}    — Timestamp {"$timestamp": ...}
//    [...]   — array (element types recursively normalized)
//    {...}   — object (keys preserved, values replaced)
//
//  Object keys are sorted so that {a:1,b:2} and {b:2,a:1}
//  produce the same shape string.
//
//  The returned string is meant to be interned by the caller
//  into a StringTable.
// ------------------------------------------------------------

class QueryShapeNormalizer {
public:
    // Normalize a raw JSON string slice (need not be null-terminated).
    // Returns the shape string, or the raw input if it cannot be parsed.
    static std::string normalize(std::string_view json_doc);
};
