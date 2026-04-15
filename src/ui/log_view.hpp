#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <functional>
#include <unordered_set>
#include "../core/chunk_vector.hpp"
#include "../core/bitmask_filter.hpp"

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

    // Time-window filter set by FTDC cross-link (epoch ms, 0 = inactive)
    bool    time_window_active   = false;
    int64_t time_window_start_ms = 0;
    int64_t time_window_end_ms   = 0;

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
               !node_idx_include.empty()        ||
               time_window_active;
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
    // Callback: (entry_index, selected_node_idx)
    using SelectCallback = std::function<void(size_t, uint16_t)>;

    LogView() = default;

    void set_entries(const ChunkVector<LogEntry>* entries,
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

    // Get the leftmost (lowest index) node from a node_mask bitmask.
    static uint16_t leftmost_node(uint32_t mask);

    const ChunkVector<LogEntry>* entries_    = nullptr;
    const StringTable*           strings_    = nullptr;
    const std::vector<NodeInfo>* nodes_      = nullptr;
    FilterState*                 filter_     = nullptr;
    SelectCallback               on_select_;

    std::vector<size_t>          filtered_indices_;
    int                          selected_row_ = -1;
    uint16_t                     selected_node_ = 0; // which node is active for detail view
    bool                         sort_ascending_ = true; // timestamp sort direction

    // Cache for text-search lowercase conversion
    mutable std::string          search_lower_;

    // Debounce: rebuild only when user pauses typing for DEBOUNCE_MS
    static constexpr double      DEBOUNCE_MS = 150.0;
    bool                         search_dirty_     = false;
    double                       search_dirty_time_ = 0.0; // ImGui time at last keystroke

    // D-11: Per-dimension bitmask filter index
    DimensionMask mask_severity_;
    DimensionMask mask_component_;
    DimensionMask mask_op_type_;
    DimensionMask mask_ns_;
    DimensionMask mask_shape_;
    DimensionMask mask_slow_query_;
    DimensionMask mask_conn_id_;
    DimensionMask mask_driver_;
    DimensionMask mask_node_;
    DimensionMask mask_time_window_;
    DimensionMask mask_text_;

    // D-12: Trigram index for text search
    std::vector<std::pair<uint32_t, uint32_t>> trigram_index_;  // sorted (trigram_key, entry_idx)
    bool trigram_index_built_ = false;

    // Snapshot of filter state for detecting which dimensions changed
    FilterState prev_filter_;

    // D-11: Rebuild a single dimension's bitmask
    void rebuild_all_dimension_masks();
    void rebuild_severity_mask();
    void rebuild_component_mask();
    void rebuild_op_type_mask();
    void rebuild_ns_mask();
    void rebuild_shape_mask();
    void rebuild_slow_query_mask();
    void rebuild_conn_id_mask();
    void rebuild_driver_mask();
    void rebuild_node_mask();
    void rebuild_time_window_mask();
    void rebuild_text_mask();
    void apply_combined_masks();

    // D-12: Trigram index
    void build_trigram_index();
    void search_trigram(const std::string& query, DimensionMask& mask);
};
