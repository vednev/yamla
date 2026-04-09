#pragma once

#include <functional>
#include <vector>
#include "../analysis/analyzer.hpp"
#include "../analysis/cluster.hpp"
#include "log_view.hpp"

// ------------------------------------------------------------
//  FilterView — connection IDs, driver types, and node filter
//
//  Connections are integrated as a collapsible section matching
//  the style of the breakdown panel filters (no dedicated header,
//  "Clear" button inside the body when active).
// ------------------------------------------------------------
class FilterView {
public:
    using FilterChangedCb = std::function<void()>;

    FilterView() = default;

    void set_analysis(const AnalysisResult* analysis,
                      const StringTable* strings);
    void set_nodes(const std::vector<NodeInfo>* nodes);
    void set_filter(FilterState* filter);
    void set_on_filter_changed(FilterChangedCb cb);

    void render_inner();

private:
    void render_node_section();
    void render_conn_section();
    void render_driver_section();

    void select_all_driver();
    void clear_all_driver();

    const AnalysisResult*        analysis_ = nullptr;
    const StringTable*           strings_  = nullptr;
    const std::vector<NodeInfo>* nodes_    = nullptr;
    FilterState*                 filter_   = nullptr;
    FilterChangedCb              on_filter_changed_;

    char conn_search_[128]   = {};
    char driver_search_[128] = {};
};
