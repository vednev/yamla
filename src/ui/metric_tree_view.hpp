#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include <functional>

// Forward declarations
struct MetricStore;

// ------------------------------------------------------------
//  MetricTreeView
//
//  Left panel of the FTDC view. Renders:
//    1. Preset dashboard buttons (Overview, CPU, Memory, etc.)
//    2. A searchable hierarchical tree of all available metrics
//       with per-metric checkboxes.
//
//  The selected_metrics set is modified by user interaction.
//  on_selection_changed() is called whenever the set changes.
// ------------------------------------------------------------
class MetricTreeView {
public:
    using SelectionChangedCb = std::function<void()>;

    MetricTreeView() = default;

    void set_store(const MetricStore* store);
    void set_on_selection_changed(SelectionChangedCb cb) {
        on_selection_changed_ = std::move(cb);
    }

    // Render inside an existing child window (no Begin/End).
    void render_inner();

    // Currently selected metric paths
    const std::unordered_set<std::string>& selected() const {
        return selected_metrics_;
    }

    void clear_selection();
    void set_selection(const std::vector<std::string>& paths);

private:
    void render_presets();
    void render_search_tree();
    void render_tree_node(const std::string& prefix,
                          const std::vector<std::string>& children,
                          int depth);

    const MetricStore* store_ = nullptr;
    SelectionChangedCb on_selection_changed_;

    std::unordered_set<std::string> selected_metrics_;

    // Search input
    char search_buf_[256] = {};
};
