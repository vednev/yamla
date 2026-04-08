#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <functional>
#include <unordered_set>

// Forward declarations
struct LogEntry;
class StringTable;
struct NodeInfo;
class DetailView;

// ------------------------------------------------------------
//  FilterState — shared between LogView and BreakdownView
//
//  A filter is active when any field is non-zero / non-empty.
//  The log view only shows entries that match ALL active filters.
// ------------------------------------------------------------
struct FilterState {
    // Text search (case-insensitive match against msg)
    std::string text_search;

    // Single-value category filters (0 = no filter)
    uint32_t severity_filter   = 0;  // (Severity enum value + 1), 0 = all
    uint32_t op_type_idx       = 0;
    uint32_t driver_idx        = 0;  // single driver from breakdown panel
    uint32_t ns_idx            = 0;
    uint32_t shape_idx         = 0;

    // Multi-select component filter — empty = no filter (show all)
    std::unordered_set<uint32_t> component_idx_include;

    // Slow query filter — when true only entries with duration_ms > 100 pass
    bool slow_query_only = false;

    // Set-based inclusion filters from the Filter panel.
    // Empty set = no filter active (show all).
    std::unordered_set<uint32_t>  conn_id_include;    // raw conn_id values
    std::unordered_set<uint32_t>  driver_idx_include; // StringTable indices
    std::unordered_set<uint16_t>  node_idx_include;   // node indices (NodeInfo::idx)

    bool active() const {
        return !text_search.empty()              ||
               severity_filter != 0             ||
               !component_idx_include.empty()   ||
               op_type_idx     != 0             ||
               driver_idx      != 0             ||
               ns_idx          != 0             ||
               shape_idx       != 0             ||
               slow_query_only                  ||
               !conn_id_include.empty()         ||
               !driver_idx_include.empty()      ||
               !node_idx_include.empty();
    }

    void clear() { *this = FilterState{}; }
};

// ------------------------------------------------------------
//  LogView
//
//  Renders the main scrolling log list using ImGuiListClipper
//  for virtual scrolling. Only visible rows are rendered.
//
//  Calls on_select(index) when the user clicks a row.
// ------------------------------------------------------------
class LogView {
public:
    using SelectCallback = std::function<void(size_t)>;

    LogView() = default;

    void set_entries(const LogEntry* entries, size_t count,
                     const StringTable* strings,
                     const std::vector<NodeInfo>* nodes);

    void set_filter(FilterState* filter);
    void set_on_select(SelectCallback cb);

    // Rebuild the filtered index list — call when filter changes.
    void rebuild_filter_index();

    // Render as a standalone ImGui window.
    void render();

    // Render only the contents (no Begin/End) — use inside a child window.
    void render_inner();

    size_t visible_count() const { return filtered_indices_.size(); }

private:
    bool entry_matches(const LogEntry& e) const;

    const LogEntry*              entries_    = nullptr;
    size_t                       count_      = 0;
    const StringTable*           strings_    = nullptr;
    const std::vector<NodeInfo>* nodes_      = nullptr;
    FilterState*                 filter_     = nullptr;
    SelectCallback               on_select_;

    std::vector<size_t>          filtered_indices_;
    int                          selected_row_ = -1;

    // Cache for text-search lowercase conversion
    mutable std::string          search_lower_;

    // Debounce: rebuild only when user pauses typing for DEBOUNCE_MS
    static constexpr double      DEBOUNCE_MS = 150.0;
    bool                         search_dirty_     = false;
    double                       search_dirty_time_ = 0.0; // ImGui time at last keystroke
};
