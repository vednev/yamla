#pragma once

#include <functional>
#include "../analysis/analyzer.hpp"
#include "log_view.hpp"

// ------------------------------------------------------------
//  FilterView
//
//  Renders two checkbox lists embedded inside the left panel:
//    1. Connection IDs  — unique conn IDs from the loaded cluster
//    2. Driver types    — unique driver name+version strings
//
//  Inclusion model:
//    - Empty include set  → filter inactive, all entries shown.
//    - Non-empty set      → show only entries whose value is in
//                           the set.
//
//  Default on load: all checkboxes unchecked (empty sets).
//  Checking any box narrows the log view to only those items.
//  "All" button fills the set (selects everything).
//  "None" button empties the set (deselects everything = show all).
// ------------------------------------------------------------
class FilterView {
public:
    using FilterChangedCb = std::function<void()>;

    FilterView() = default;

    void set_analysis(const AnalysisResult* analysis,
                      const StringTable* strings);
    void set_filter(FilterState* filter);
    void set_on_filter_changed(FilterChangedCb cb);

    // Render the contents directly — call inside an existing child window.
    void render_inner();

private:
    void render_conn_section();
    void render_driver_section();

    void select_all_conn();
    void clear_all_conn();
    void select_all_driver();
    void clear_all_driver();

    const AnalysisResult* analysis_ = nullptr;
    const StringTable*    strings_  = nullptr;
    FilterState*          filter_   = nullptr;
    FilterChangedCb       on_filter_changed_;

    char conn_search_[128]   = {};
    char driver_search_[128] = {};
};
