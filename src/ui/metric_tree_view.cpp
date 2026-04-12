#include "metric_tree_view.hpp"

#include <imgui.h>
#include <cstring>
#include <cctype>
#include <map>
#include <algorithm>

#include "../ftdc/metric_store.hpp"
#include "../ftdc/metric_defs.hpp"

// ---- Colors ----
static constexpr ImVec4 COL_THRESHOLD_RED   = ImVec4(1.00f, 0.40f, 0.40f, 1.0f);
static constexpr ImVec4 COL_SELECTED        = ImVec4(0.60f, 0.80f, 1.00f, 1.0f);

static std::string to_lower_str(const std::string& s) {
    std::string out(s.size(), ' ');
    for (size_t i = 0; i < s.size(); ++i)
        out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
    return out;
}

// ============================================================
//  MetricTreeView::set_store
// ============================================================
void MetricTreeView::set_store(const MetricStore* store) {
    store_ = store;
    selected_metrics_.clear();
    // Default to Overview preset on first load
    const auto& presets = preset_dashboards();
    if (!presets.empty()) {
        for (const char* p : presets[0].metric_paths)
            selected_metrics_.insert(p);
    }
}

void MetricTreeView::clear_selection() {
    selected_metrics_.clear();
    if (on_selection_changed_) on_selection_changed_();
}

void MetricTreeView::set_selection(const std::vector<std::string>& paths) {
    selected_metrics_.clear();
    for (const auto& p : paths) selected_metrics_.insert(p);
    if (on_selection_changed_) on_selection_changed_();
}

// ============================================================
//  render_presets
// ============================================================
void MetricTreeView::render_presets() {
    const auto& presets = preset_dashboards();
    ImGui::TextDisabled("Dashboards");
    ImGui::Spacing();

    float avail_w = ImGui::GetContentRegionAvail().x;
    float btn_w   = (avail_w - ImGui::GetStyle().ItemSpacing.x * 2) / 3.0f;
    if (btn_w < 60.0f) btn_w = 60.0f;

    for (size_t i = 0; i < presets.size(); ++i) {
        if (i > 0 && (i % 3) != 0) ImGui::SameLine();
        else if (i > 0) {}

        // Detect if this preset is currently active (all its paths selected)
        bool preset_active = true;
        for (const char* p : presets[i].metric_paths) {
            if (!selected_metrics_.count(p)) { preset_active = false; break; }
        }

        if (preset_active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.30f, 0.50f, 1.0f));
        if (ImGui::Button(presets[i].name, ImVec2(btn_w, 0))) {
            selected_metrics_.clear();
            for (const char* p : presets[i].metric_paths)
                selected_metrics_.insert(p);
            if (on_selection_changed_) on_selection_changed_();
        }
        if (preset_active) ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
}

// ============================================================
//  render_search_tree
// ============================================================
void MetricTreeView::render_search_tree() {
    if (!store_) return;

    // Search box
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::InputText("##search", search_buf_, sizeof(search_buf_));

    std::string search_lower = to_lower_str(search_buf_);
    bool searching = search_lower.size() > 0;

    ImGui::Spacing();

    // Build a prefix tree (map of top-level key -> children paths)
    // Traverse store_->ordered_keys grouped by first path component
    std::map<std::string, std::vector<std::string>> top_groups;
    for (const auto& path : store_->ordered_keys) {
        // Filter by search
        if (searching) {
            std::string path_lower = to_lower_str(path);
            if (path_lower.find(search_lower) == std::string::npos) continue;
        }
        size_t dot = path.find('.');
        std::string top = (dot != std::string::npos) ? path.substr(0, dot) : path;
        top_groups[top].push_back(path);
    }

    for (auto& [group_name, paths] : top_groups) {
        // Push a unique ID for the group
        ImGui::PushID(group_name.c_str());

        bool group_open = ImGui::CollapsingHeader(group_name.c_str(),
            searching ? ImGuiTreeNodeFlags_DefaultOpen : 0);

        if (group_open) {
            ImGui::Indent(12.0f);

            // Further group by second component
            std::map<std::string, std::vector<std::string>> sub_groups;
            for (const auto& path : paths) {
                size_t d1 = path.find('.');
                size_t d2 = (d1 != std::string::npos) ? path.find('.', d1 + 1) : std::string::npos;
                std::string subkey;
                if (d2 != std::string::npos)
                    subkey = path.substr(d1 + 1, d2 - d1 - 1);
                else if (d1 != std::string::npos)
                    subkey = path.substr(d1 + 1);
                else
                    subkey = path;
                sub_groups[subkey].push_back(path);
            }

            for (auto& [sub_name, sub_paths] : sub_groups) {
                if (sub_paths.size() == 1 && sub_paths[0].find('.') == sub_paths[0].rfind('.')) {
                    // Leaf with no further nesting
                    render_tree_node("", sub_paths, 0);
                } else {
                    ImGui::PushID(sub_name.c_str());
                    bool sub_open = ImGui::CollapsingHeader(sub_name.c_str(),
                        searching ? ImGuiTreeNodeFlags_DefaultOpen : 0);
                    if (sub_open) {
                        ImGui::Indent(12.0f);
                        render_tree_node("", sub_paths, 1);
                        ImGui::Unindent(12.0f);
                    }
                    ImGui::PopID();
                }
            }

            ImGui::Unindent(12.0f);
        }

        ImGui::PopID();
    }
}

// ============================================================
//  render_tree_node — leaf list
// ============================================================
void MetricTreeView::render_tree_node(const std::string& /*prefix*/,
                                       const std::vector<std::string>& paths,
                                       int /*depth*/)
{
    for (const auto& path : paths) {
        ImGui::PushID(path.c_str());

        bool selected = selected_metrics_.count(path) > 0;

        // Threshold indicator dot
        double thresh = metric_threshold(path);
        bool has_threshold = !std::isnan(thresh);

        if (has_threshold) {
            ImGui::PushStyleColor(ImGuiCol_Text, COL_THRESHOLD_RED);
            ImGui::TextUnformatted("!");
            ImGui::PopStyleColor();
            ImGui::SameLine();
        }

        // Checkbox
        if (ImGui::Checkbox("##chk", &selected)) {
            if (selected) selected_metrics_.insert(path);
            else          selected_metrics_.erase(path);
            if (on_selection_changed_) on_selection_changed_();
        }
        ImGui::SameLine();

        // Display name (colored if selected)
        std::string label = metric_display_name(path);
        std::string unit  = metric_unit(path);
        if (!unit.empty() && unit != "count") label += " (" + unit + ")";

        if (selected) ImGui::PushStyleColor(ImGuiCol_Text, COL_SELECTED);
        else          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
        ImGui::TextUnformatted(label.c_str());
        ImGui::PopStyleColor();

        ImGui::PopID();
    }
}

// ============================================================
//  render_inner
// ============================================================
void MetricTreeView::render_inner() {
    if (!store_ || store_->empty()) {
        ImGui::TextDisabled("Drop a diagnostic.data directory here.");
        return;
    }

    render_presets();
    render_search_tree();
}
