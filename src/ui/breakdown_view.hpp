#pragma once

#include <functional>
#include "../analysis/analyzer.hpp"
#include "log_view.hpp"

// ------------------------------------------------------------
//  BreakdownView
//
//  Renders the left-hand panel with ImPlot bar charts and
//  tables for each analysis category. Clicking a bar or row
//  sets the corresponding filter in FilterState and triggers
//  a callback so the LogView can rebuild its index.
// ------------------------------------------------------------
class BreakdownView {
public:
    using FilterChangedCb = std::function<void()>;

    BreakdownView() = default;

    void set_analysis(const AnalysisResult* analysis,
                      const StringTable* strings);
    void set_filter(FilterState* filter);
    void set_on_filter_changed(FilterChangedCb cb);

    // Render inside the left panel child window.
    void render();

private:
    void render_bar_chart(const char* label, const CountMap& data,
                          uint32_t FilterState::*field, bool is_severity = false);
    void render_table(const char* label, const CountMap& data,
                      uint32_t FilterState::*field);
    // Multi-select table: clicking toggles membership in an unordered_set field
    void render_table_multi(const char* label, const CountMap& data,
                            std::unordered_set<uint32_t> FilterState::*set_field);

    void render_reset_button();

    const AnalysisResult* analysis_ = nullptr;
    const StringTable*    strings_  = nullptr;
    FilterState*          filter_   = nullptr;
    FilterChangedCb       on_filter_changed_;
};
