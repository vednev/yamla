#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <functional>
#include <utility>

// Forward declarations
struct MetricStore;
struct MetricSeries;

// ------------------------------------------------------------
//  MetricTreeView — Dashboard-First Navigation Panel
//
//  Left panel of the FTDC view. Renders:
//    1. Dashboard category cards (toggle on/off) with anomaly badges
//    2. A search overlay for finding individual metrics by name
//
//  Multiple dashboards can be active simultaneously. Toggling a
//  dashboard adds/removes its metrics from the selected set.
//  Individual metrics from search appear in a "Custom" group.
//
//  The selected_metrics_ set is the union of all active dashboard
//  metrics plus any individually-selected search metrics.
// ------------------------------------------------------------
class MetricTreeView {
public:
    using SelectionChangedCb = std::function<void()>;

    // Dashboard activation info — name + resolved metric paths
    using DashboardInfo = std::pair<std::string, std::vector<std::string>>;

    MetricTreeView() = default;

    void set_store(const MetricStore* store);
    void set_on_selection_changed(SelectionChangedCb cb) {
        on_selection_changed_ = std::move(cb);
    }

    // Render inside an existing child window (no Begin/End).
    void render_inner();

    // Currently selected metric paths (union of all active dashboards + custom)
    const std::unordered_set<std::string>& selected() const {
        return selected_metrics_;
    }

    // Active dashboards with their resolved metric paths.
    // Used by ChartPanelView for category grouping.
    const std::vector<DashboardInfo>& active_dashboards() const {
        return active_dashboards_;
    }

    // Custom (search-selected) metrics not belonging to any active dashboard.
    const std::unordered_set<std::string>& custom_metrics() const {
        return custom_metrics_;
    }

    void clear_selection();
    void set_selection(const std::vector<std::string>& paths);

private:
    void rebuild_selected();          // Recompute selected_metrics_ from active state
    void rebuild_active_dashboards(); // Recompute active_dashboards_ from toggle state
    void render_dashboard_cards();
    void render_search_overlay();
    bool check_anomaly(size_t dashboard_idx) const;

    // Resolve disk I/O dashboard paths dynamically from the store
    std::vector<std::string> resolve_disk_paths() const;

    const MetricStore* store_ = nullptr;
    SelectionChangedCb on_selection_changed_;

    // Dashboard toggle state — indexed by preset_dashboards() index
    std::vector<bool> dashboard_active_;

    // Cached resolved dashboard info for active dashboards
    std::vector<DashboardInfo> active_dashboards_;

    // Union of all active dashboard metrics + custom metrics
    std::unordered_set<std::string> selected_metrics_;

    // Individually selected metrics from search overlay
    std::unordered_set<std::string> custom_metrics_;

    // Search overlay state
    char search_buf_[256] = {};
    bool search_focused_ = false;
};
