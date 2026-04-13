#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <functional>
#include <utility>
#include <cstdint>
#include <cmath>

// Forward declarations
struct MetricStore;
struct MetricSeries;
struct FilterState;
struct LogEntry;
class StringTable;
struct NodeInfo;

// ------------------------------------------------------------
//  UnitDisplayConfig — per-unit-type Y-axis display strategy.
//  Follows Grafana/PMM conventions for MongoDB metric display.
// ------------------------------------------------------------
struct UnitDisplayConfig {
    bool   anchor_at_zero;     // true = Y min is 0 for non-negative data
    double y_pad_fraction;     // padding above max (and below min if floating)
    bool   auto_log_eligible;  // true = auto-engage log scale if range >1000x
    double hard_y_max;         // NaN = auto, 100.0 for percentages
};

// Lookup the display config for a given unit string.
UnitDisplayConfig unit_display_config(const std::string& unit);

// ------------------------------------------------------------
//  ChartState — per-metric chart render configuration.
//  show_rate and log_scale are auto-computed from data, not
//  user-toggled.  Cumulative metrics always show rate.
// ------------------------------------------------------------
struct ChartState {
    bool show_rate  = true;   // always true for cumulative metrics
    bool log_scale  = false;  // auto-computed from value range + unit
    bool initialized = false; // set once after first data scan
};

// ------------------------------------------------------------
//  ChartPanelView
//
//  Center panel of the FTDC view. Renders:
//    - A minimap overview bar at the top
//    - One ImPlot chart per selected metric, stacked vertically
//      with linked X axis
//    - Per-chart stats row (min/avg/max/p99)
//    - Synchronized crosshair across all charts
//    - Log event annotation markers (if log data is available)
//    - Anomaly threshold highlight bands
// ------------------------------------------------------------
class ChartPanelView {
public:
    using TimeClickCb = std::function<void(int64_t t_ms)>;

    // Dashboard grouping info from MetricTreeView
    using DashboardInfo = std::pair<std::string, std::vector<std::string>>;

    ChartPanelView() = default;

    void set_store(const MetricStore* store);
    void set_selected_metrics(const std::unordered_set<std::string>* sel);

    // Set active dashboard groups for category rendering.
    // Called by FtdcView whenever selection changes.
    void set_dashboard_groups(const std::vector<DashboardInfo>* groups);

    // Set custom (ungrouped) metric paths for the "Custom" group.
    void set_custom_metrics(const std::unordered_set<std::string>* custom);

    // Cross-view linking: log entries for annotation markers
    // (may be null — annotations are skipped if not set)
    void set_log_data(const std::vector<const LogEntry*>* log_entries,
                      const StringTable*                  log_strings);

    // FilterState for time-window cross-link
    void set_filter(FilterState* filter);

    // Callback: user clicked on a time point; filter_->time_window_active updated
    void set_on_time_click(TimeClickCb cb) { on_time_click_ = std::move(cb); }

    // Set column layout mode from Prefs::chart_layout_columns.
    // Caller (FtdcView) should call this with Prefs::chart_layout_columns.
    // The layout_columns_ default of 0 triggers auto-detect (D-33).
    void set_layout_columns(int cols) { layout_columns_ = (cols < 0 || cols > 4) ? 0 : cols; }

    // Render inside an existing child window (no Begin/End).
    void render_inner();

    // Shared time cursor driven by hover (epoch ms, -1 = none)
    int64_t hover_time_ms() const { return hover_time_ms_; }

private:
    void render_minimap(float width, float height);
    void render_chart(const MetricSeries& series, ChartState& state,
                      float width, float chart_h);
    void render_stats_row(const MetricSeries& series, bool use_rate,
                          int64_t t0_ms, int64_t t1_ms);
    void render_annotation_markers(double x_min, double x_max);

    // Format a double value as a human-readable string with unit
    static void fmt_metric_value(char* buf, size_t bufsz,
                                 double value, const std::string& unit);

    const MetricStore*                       store_      = nullptr;
    const std::unordered_set<std::string>*   selected_   = nullptr;
    const std::vector<const LogEntry*>*      log_entries_= nullptr;
    const StringTable*                       log_strings_= nullptr;
    FilterState*                             filter_     = nullptr;
    TimeClickCb                              on_time_click_;

    // Dashboard group data (owned by MetricTreeView, just pointing)
    const std::vector<DashboardInfo>*        dashboard_groups_ = nullptr;
    const std::unordered_set<std::string>*   custom_metrics_   = nullptr;

    // Per-group collapsed state (keyed by dashboard name)
    std::unordered_map<std::string, bool> group_collapsed_;

    // Per-metric chart state
    std::unordered_map<std::string, ChartState> chart_states_;

    // Full data time range (epoch seconds)
    double x_min_ = 0.0;
    double x_max_ = 0.0;
    // Current view window (may be zoomed in)
    double x_view_min_ = 0.0;
    double x_view_max_ = 0.0;
    bool   axis_initialized_ = false;

    // Crosshair position (epoch seconds, NaN = not hovering)
    double crosshair_x_ = std::numeric_limits<double>::quiet_NaN();

    // Hover time for external consumption
    int64_t hover_time_ms_ = -1;

    // Drag-to-zoom state
    bool   dragging_       = false;
    double drag_start_x_   = 0.0;   // plot-space X where drag began
    float  drag_start_px_x_= 0.0f;  // screen-space pixel X where drag began
    bool   drag_committed_ = false;  // set true on release inside a plot
    double drag_end_x_     = 0.0;   // plot-space X where drag ended
    float  drag_end_px_x_  = 0.0f;  // screen-space pixel X where drag ended

    int layout_columns_ = 0;  // 0=auto, 1=list, 2/3/4=grid columns

    static constexpr float CHART_HEIGHT   = 140.0f;
    static constexpr float MINIMAP_HEIGHT = 56.0f;  // taller to fit time labels
    static constexpr float STATS_HEIGHT   = 18.0f;
    static constexpr size_t MAX_PLOT_PTS  = 2000;
};
