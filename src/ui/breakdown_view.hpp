#pragma once

#include <functional>
#include <vector>
#include "../analysis/analyzer.hpp"
#include "../analysis/cluster.hpp"
#include "../core/prefs.hpp"
#include "log_view.hpp"

// ------------------------------------------------------------
//  BreakdownView
//
//  Single unified filter + breakdown panel. Renders all filter
//  categories in one scrollable column:
//    Severity, Operation Type, Component (charts/tables)
//    Driver, Namespace, Query Shape    (tables)
//    Nodes, Connections               (checkbox lists)
// ------------------------------------------------------------
class BreakdownView {
public:
    using FilterChangedCb = std::function<void()>;

    BreakdownView() = default;

    void set_analysis(const AnalysisResult* analysis,
                      const StringTable* strings);
    void set_nodes(const std::vector<NodeInfo>* nodes);
    void set_filter(FilterState* filter);
    void set_on_filter_changed(FilterChangedCb cb);
    void set_prefs(const Prefs* prefs);

    void render();

private:
    // Chart/table renders
    void render_bar_chart(const char* label, const CountMap& data,
                          uint32_t FilterState::*field, bool is_severity = false);
    // Checkbox-list equivalent of render_bar_chart (used when prefer_checkboxes)
    void render_bar_as_checkboxes(const char* label, const CountMap& data,
                                  uint32_t FilterState::*field, bool is_severity = false);
    void render_table(const char* label, const CountMap& data,
                      uint32_t FilterState::*field);
    void render_table_multi(const char* label, const CountMap& data,
                            std::unordered_set<uint32_t> FilterState::*set_field);

    // Checkbox-list renders (moved from FilterView)
    void render_nodes_section();
    void render_connections_section();

    void render_reset_button();

    const AnalysisResult*        analysis_ = nullptr;
    const StringTable*           strings_  = nullptr;
    const std::vector<NodeInfo>* nodes_    = nullptr;
    FilterState*                 filter_   = nullptr;
    const Prefs*                 prefs_    = nullptr;
    FilterChangedCb              on_filter_changed_;

    // Per-section search buffers
    char conn_search_[128] = {};
};
