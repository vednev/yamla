#pragma once

#include <functional>
#include "../analysis/analyzer.hpp"
#include "log_view.hpp"

// ------------------------------------------------------------
//  FilterView
//
//  A floating ImGui window with two checkbox lists:
//    1. Connection IDs  — unique conn IDs from the loaded cluster
//    2. Driver types    — unique driver name+version strings
//
//  Each list defaults to all items checked ("show all").
//  Un-checking an item adds its value to the exclusion set in
//  FilterState, which log_view::entry_matches tests each frame.
//
//  The window is shown/hidden via show()/hide(). The App
//  exposes a menu item to toggle it.
// ------------------------------------------------------------
class FilterView {
public:
    using FilterChangedCb = std::function<void()>;

    FilterView() = default;

    void set_analysis(const AnalysisResult* analysis,
                      const StringTable* strings);
    void set_filter(FilterState* filter);
    void set_on_filter_changed(FilterChangedCb cb);

    void show() { open_ = true; }
    void hide() { open_ = false; }
    bool is_open() const { return open_; }

    // Call once per frame from App::render_frame().
    // Renders the floating window when open_.
    void render();

private:
    void render_conn_section();
    void render_driver_section();

    // "Select All" / "Clear All" helpers
    void select_all_conn();
    void clear_all_conn();
    void select_all_driver();
    void clear_all_driver();

    const AnalysisResult* analysis_ = nullptr;
    const StringTable*    strings_  = nullptr;
    FilterState*          filter_   = nullptr;
    FilterChangedCb       on_filter_changed_;
    bool                  open_     = false;

    // Local search buffers for filtering long lists
    char conn_search_[128]   = {};
    char driver_search_[128] = {};
};
