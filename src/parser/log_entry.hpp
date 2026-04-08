#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <string>
#include <functional>
#include "../core/arena.hpp"

// ------------------------------------------------------------
//  String intern table
//
//  Maps string -> uint32_t index. The actual string bytes are
//  stored in the arena so lookups return stable string_views.
//  Index 0 is reserved for "unknown" / empty.
//
//  Uses a heterogeneous hash/equal that accepts string_view
//  directly — intern() never constructs a std::string heap
//  object for a lookup that hits an existing entry.
// ------------------------------------------------------------

// Heterogeneous hash: accepts both std::string and std::string_view
struct SvHash {
    using is_transparent = void;
    size_t operator()(std::string_view sv) const noexcept {
        // FNV-1a — fast, good distribution, no alloc
        size_t h = 14695981039346656037ULL;
        for (unsigned char c : sv)
            h = (h ^ c) * 1099511628211ULL;
        return h;
    }
    size_t operator()(const std::string& s) const noexcept {
        return (*this)(std::string_view(s));
    }
};

struct SvEqual {
    using is_transparent = void;
    bool operator()(std::string_view a, std::string_view b) const noexcept {
        return a == b;
    }
    bool operator()(const std::string& a, std::string_view b) const noexcept {
        return std::string_view(a) == b;
    }
    bool operator()(std::string_view a, const std::string& b) const noexcept {
        return a == std::string_view(b);
    }
    bool operator()(const std::string& a, const std::string& b) const noexcept {
        return a == b;
    }
};

class StringTable {
public:
    static constexpr uint32_t UNKNOWN = 0;

    explicit StringTable(ArenaAllocator& arena) : arena_(&arena) {
        // index 0 = empty/unknown — store a stable empty string_view
        strings_.push_back(std::string_view("", 0));
        // key is string_view pointing to static empty string — stable forever
        index_map_.emplace(std::string_view("", 0), 0);
    }

    // Intern a string_view; returns its stable index.
    // Hot path: lookup is string_view → no heap allocation for existing strings.
    // Insert path: copy to arena first, then use arena pointer as stable key.
    uint32_t intern(std::string_view sv) {
        auto it = index_map_.find(sv);
        if (it != index_map_.end()) return it->second;

        // New: copy bytes into the arena to get a stable address
        const char* stored = arena_->intern_string(sv.data(), sv.size());
        std::string_view stable(stored, sv.size());

        uint32_t idx = static_cast<uint32_t>(strings_.size());
        strings_.push_back(stable);
        index_map_.emplace(stable, idx); // key points into arena — no heap alloc
        return idx;
    }

    std::string_view get(uint32_t idx) const {
        if (idx >= strings_.size()) return "";
        return strings_[idx];
    }

    size_t size() const { return strings_.size(); }

private:
    ArenaAllocator*                                          arena_;
    std::vector<std::string_view>                            strings_;
    // Keys are string_views into arena memory — stable, zero-copy lookup
    std::unordered_map<std::string_view, uint32_t, SvHash>   index_map_;
};

// ------------------------------------------------------------
//  Severity — MongoDB log levels
// ------------------------------------------------------------
enum class Severity : uint8_t {
    Fatal   = 0,
    Error   = 1,
    Warning = 2,
    Info    = 3,
    Debug   = 4,
    Unknown = 5,
};

inline Severity severity_from_char(char c) {
    switch (c) {
        case 'F': return Severity::Fatal;
        case 'E': return Severity::Error;
        case 'W': return Severity::Warning;
        case 'I': return Severity::Info;
        case 'D': return Severity::Debug;
        default:  return Severity::Unknown;
    }
}

inline const char* severity_string(Severity s) {
    switch (s) {
        case Severity::Fatal:   return "FATAL";
        case Severity::Error:   return "ERROR";
        case Severity::Warning: return "WARN";
        case Severity::Info:    return "INFO";
        case Severity::Debug:   return "DEBUG";
        default:                return "?";
    }
}

// Parse a severity label string back to the enum.
// Used by the UI to map bar-chart labels back to filter values.
inline Severity severity_from_string(const char* s) {
    if (!s) return Severity::Unknown;
    if (s[0] == 'F') return Severity::Fatal;
    if (s[0] == 'E') return Severity::Error;
    if (s[0] == 'W') return Severity::Warning;
    if (s[0] == 'I') return Severity::Info;
    if (s[0] == 'D') return Severity::Debug;
    return Severity::Unknown;
}

// ------------------------------------------------------------
//  LogEntry — packed struct stored in ArenaVector
//
//  All variable-length data is represented as indices into
//  shared StringTables so the struct stays small and cache-
//  friendly. The raw JSON line is recoverable via raw_offset /
//  raw_len into the original mmap'd file.
// ------------------------------------------------------------

struct LogEntry {
    // Raw JSON line position in the mmap'd file
    uint32_t raw_offset  = 0;
    uint32_t raw_len     = 0;

    // Parsed fields
    int64_t  timestamp_ms = 0;    // Unix ms

    // String table indices
    uint32_t ns_idx        = 0;   // namespace (db.collection)
    uint32_t op_type_idx   = 0;   // find/insert/update/delete/command/...
    uint32_t component_idx = 0;   // COMMAND, REPL, NETWORK, ...
    uint32_t driver_idx    = 0;   // driver name (intern: "pymongo 4.1.0")
    uint32_t shape_idx     = 0;   // normalized query shape
    uint32_t msg_idx       = 0;   // msg field

    // Numeric fields
    int32_t  duration_ms   = -1;  // -1 = not present
    uint32_t conn_id       = 0;   // connection number (0 = none)

    // Node + severity in compact form
    uint16_t node_idx      = 0;   // which cluster node
    Severity severity      = Severity::Unknown;

    // Bitmask of node indices that share this deduplicated entry
    // Supports up to 32 nodes per cluster.
    uint32_t node_mask     = 0;
};

// Sanity check — keep LogEntry reasonably small
static_assert(sizeof(LogEntry) <= 64, "LogEntry too large — review fields");
