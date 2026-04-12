#pragma once

#include <string>
#include <memory>
#include <thread>
#include <functional>

#include "../ftdc/ftdc_cluster.hpp"
#include "../ftdc/ftdc_analyzer.hpp"
#include "metric_tree_view.hpp"
#include "chart_panel_view.hpp"

struct FilterState;

// ------------------------------------------------------------
//  FtdcView
//
//  Top-level FTDC tab layout. Owns:
//    - FtdcCluster (background load of diagnostic.data dir)
//    - MetricTreeView (left panel)
//    - ChartPanelView (center panel)
//
//  Called by App::render_dockspace() when the FTDC tab is active.
// ------------------------------------------------------------
class FtdcView {
public:
    FtdcView() = default;
    ~FtdcView();

    // Non-copyable
    FtdcView(const FtdcView&)            = delete;
    FtdcView& operator=(const FtdcView&) = delete;

    // Wire up shared FilterState (for cross-view linking)
    void set_filter(FilterState* filter);

    // Wire up log data for annotation markers (may be null)
    void set_log_data(const std::vector<const LogEntry*>* entries,
                      const StringTable*                  strings);

    // Load a diagnostic.data directory (or single metrics.* file) async
    void start_load(const std::string& path);

    // Poll cluster state — call every frame regardless of active tab,
    // so the Loading→Ready transition is never missed.
    void poll_state();

    // Render the full FTDC layout (two-column: left tree + right charts).
    // Uses internal left_w_ for splitter. Only call when FTDC tab is visible.
    void render(float total_h);

    // Loading popup — call every frame regardless of active tab.
    void render_loading_popup();

    FtdcLoadState load_state() const {
        return cluster_ ? cluster_->state() : FtdcLoadState::Idle;
    }

private:
    void on_selection_changed();

    std::unique_ptr<FtdcCluster> cluster_;
    std::thread                  load_thread_;
    FtdcLoadState                last_state_ = FtdcLoadState::Idle;

    MetricTreeView tree_view_;
    ChartPanelView chart_panel_;

    FilterState*                        filter_      = nullptr;
    const std::vector<const LogEntry*>* log_entries_ = nullptr;
    const StringTable*                  log_strings_ = nullptr;

    // Splitter between tree and chart panels
    float left_w_ = 280.0f;
};
