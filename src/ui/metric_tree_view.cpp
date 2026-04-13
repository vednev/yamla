#include "metric_tree_view.hpp"

#include <imgui.h>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <cmath>

#include "../ftdc/metric_store.hpp"
#include "../ftdc/metric_defs.hpp"

// ---- Colors ----
static constexpr ImVec4 COL_CARD_ACTIVE         = ImVec4(0.15f, 0.35f, 0.55f, 1.0f);
static constexpr ImVec4 COL_CARD_ACTIVE_HOVERED = ImVec4(0.12f, 0.28f, 0.45f, 1.0f); // dimmed blue
static constexpr ImVec4 COL_CARD_INACTIVE       = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);
static constexpr ImVec4 COL_CARD_INACTIVE_HOVER = ImVec4(0.18f, 0.18f, 0.22f, 1.0f);
static constexpr ImVec4 COL_BADGE_RED      = ImVec4(1.00f, 0.30f, 0.30f, 1.0f);
static constexpr ImVec4 COL_BADGE_GREEN    = ImVec4(0.30f, 0.70f, 0.30f, 0.6f);
static constexpr ImVec4 COL_SEARCH_MATCH   = ImVec4(0.60f, 0.80f, 1.00f, 1.0f);
static constexpr ImVec4 COL_CUSTOM_TAG     = ImVec4(0.70f, 0.50f, 1.00f, 1.0f);

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
    // Initialize dashboard toggle state
    const auto& presets = preset_dashboards();
    dashboard_active_.assign(presets.size(), false);
    custom_metrics_.clear();
    search_buf_[0] = '\0';
    // Auto-select Overview (per D-21)
    if (!presets.empty()) {
        dashboard_active_[0] = true;
    }
    rebuild_active_dashboards();
    rebuild_selected();
}

// ============================================================
//  clear_selection / set_selection
// ============================================================
void MetricTreeView::clear_selection() {
    dashboard_active_.assign(dashboard_active_.size(), false);
    custom_metrics_.clear();
    rebuild_active_dashboards();
    rebuild_selected();
    if (on_selection_changed_) on_selection_changed_();
}

void MetricTreeView::set_selection(const std::vector<std::string>& paths) {
    dashboard_active_.assign(dashboard_active_.size(), false);
    custom_metrics_.clear();
    for (const auto& p : paths) custom_metrics_.insert(p);
    rebuild_active_dashboards();
    rebuild_selected();
    if (on_selection_changed_) on_selection_changed_();
}

// ============================================================
//  resolve_disk_paths — scan store for disk metrics
// ============================================================
std::vector<std::string> MetricTreeView::resolve_disk_paths() const {
    std::vector<std::string> result;
    if (!store_) return result;
    for (const auto& key : store_->ordered_keys) {
        if (is_disk_metric(key))
            result.push_back(key);
    }
    std::sort(result.begin(), result.end());
    return result;
}

// ============================================================
//  rebuild_active_dashboards
// ============================================================
void MetricTreeView::rebuild_active_dashboards() {
    active_dashboards_.clear();
    const auto& presets = preset_dashboards();
    for (size_t i = 0; i < presets.size(); ++i) {
        if (i >= dashboard_active_.size() || !dashboard_active_[i]) continue;
        std::vector<std::string> paths;
        if (std::string(presets[i].name) == DISK_IO_DASHBOARD_NAME) {
            // Dynamic resolution from store
            paths = resolve_disk_paths();
        } else {
            for (const char* p : presets[i].metric_paths)
                paths.emplace_back(p);
        }
        active_dashboards_.emplace_back(presets[i].name, std::move(paths));
    }
}

// ============================================================
//  rebuild_selected
// ============================================================
void MetricTreeView::rebuild_selected() {
    selected_metrics_.clear();
    for (const auto& [name, paths] : active_dashboards_) {
        for (const auto& p : paths)
            selected_metrics_.insert(p);
    }
    for (const auto& p : custom_metrics_)
        selected_metrics_.insert(p);
}

// ============================================================
//  check_anomaly — does any metric in dashboard[i] exceed threshold?
// ============================================================
bool MetricTreeView::check_anomaly(size_t dashboard_idx) const {
    if (!store_) return false;
    const auto& presets = preset_dashboards();
    if (dashboard_idx >= presets.size()) return false;

    // Resolve the metric paths for this dashboard
    std::vector<std::string> paths;
    if (std::string(presets[dashboard_idx].name) == DISK_IO_DASHBOARD_NAME) {
        paths = resolve_disk_paths();
    } else {
        for (const char* p : presets[dashboard_idx].metric_paths)
            paths.emplace_back(p);
    }

    for (const auto& path : paths) {
        double thresh = metric_threshold(path);
        if (std::isnan(thresh)) continue;

        const MetricSeries* s = store_->get(path);
        if (!s || s->empty()) continue;

        bool is_cumul = metric_is_cumulative(path);
        if (is_cumul) {
            // Check last rate value against threshold
            if (!s->rate.empty()) {
                double last_rate = s->rate.back();
                // threshold 0.0 means any non-zero rate is anomalous
                if (thresh == 0.0) {
                    if (last_rate > 0.0) return true;
                } else {
                    if (last_rate > thresh) return true;
                }
            }
        } else {
            // Gauge: check last raw value
            double last_val = s->values.back();
            if (last_val > thresh) return true;
        }
    }
    return false;
}

// ============================================================
//  render_dashboard_cards
// ============================================================
void MetricTreeView::render_dashboard_cards() {
    ImGui::TextDisabled("Dashboards");
    ImGui::Spacing();

    const auto& presets = preset_dashboards();
    float avail_w = ImGui::GetContentRegionAvail().x;
    // 2-column layout for dashboard cards
    float card_w = (avail_w - ImGui::GetStyle().ItemSpacing.x) / 2.0f;
    if (card_w < 80.0f) card_w = 80.0f;

    for (size_t i = 0; i < presets.size(); ++i) {
        // Layout: 2 columns
        if (i > 0 && (i % 2) != 0) ImGui::SameLine();

        ImGui::PushID(static_cast<int>(i));

        bool active = (i < dashboard_active_.size()) && dashboard_active_[i];
        bool has_anomaly = check_anomaly(i);

        // Card styling: active=blue bg, inactive=dark bg
        ImGui::PushStyleColor(ImGuiCol_Button,
            active ? COL_CARD_ACTIVE : COL_CARD_INACTIVE);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            active ? COL_CARD_ACTIVE_HOVERED : COL_CARD_INACTIVE_HOVER);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, COL_CARD_ACTIVE);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, active ? 1.5f : 0.5f);
        ImGui::PushStyleColor(ImGuiCol_Border,
            active ? ImVec4(0.3f, 0.5f, 0.8f, 1.0f) : ImVec4(0.25f, 0.25f, 0.25f, 1.0f));

        float card_h = ImGui::GetTextLineHeightWithSpacing() + 8.0f;

        // Draw the button (full card area)
        if (ImGui::Button("##card", ImVec2(card_w, card_h))) {
            dashboard_active_[i] = !dashboard_active_[i];
            rebuild_active_dashboards();
            rebuild_selected();
            if (on_selection_changed_) on_selection_changed_();
        }

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(4);

        // Overlay text + badge on the button
        ImVec2 btn_min = ImGui::GetItemRectMin();
        ImVec2 btn_max = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Dashboard name (left-aligned, vertically centered)
        float text_y = btn_min.y + (btn_max.y - btn_min.y - ImGui::GetTextLineHeight()) * 0.5f;
        dl->AddText(ImVec2(btn_min.x + 8.0f, text_y),
            ImGui::GetColorU32(ImVec4(0.9f, 0.9f, 0.9f, 1.0f)),
            presets[i].name);

        // Anomaly badge (right side — small colored circle)
        float badge_r = 4.0f;
        float badge_cx = btn_max.x - 12.0f;
        float badge_cy = btn_min.y + (btn_max.y - btn_min.y) * 0.5f;
        if (has_anomaly) {
            dl->AddCircleFilled(ImVec2(badge_cx, badge_cy), badge_r,
                ImGui::GetColorU32(COL_BADGE_RED));
        } else {
            dl->AddCircleFilled(ImVec2(badge_cx, badge_cy), badge_r,
                ImGui::GetColorU32(COL_BADGE_GREEN));
        }

        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::Separator();
}

// ============================================================
//  render_search_overlay
// ============================================================
void MetricTreeView::render_search_overlay() {
    ImGui::Spacing();
    ImGui::TextDisabled("Search Metrics");
    ImGui::Spacing();

    // Search input
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::InputText("##metric_search", search_buf_, sizeof(search_buf_));

    std::string query = to_lower_str(search_buf_);
    if (query.empty()) return;

    // Show matching metrics in a scrollable list (max 15 results)
    ImGui::Spacing();
    int shown = 0;
    for (const auto& path : store_->ordered_keys) {
        if (shown >= 15) break;
        std::string path_lower = to_lower_str(path);
        // Also match against display name
        std::string display = to_lower_str(metric_display_name(path));
        if (path_lower.find(query) == std::string::npos &&
            display.find(query) == std::string::npos) continue;

        ImGui::PushID(path.c_str());

        bool is_custom = custom_metrics_.count(path) > 0;
        bool in_dashboard = false;
        // Check if this metric is already in an active dashboard
        for (const auto& [name, paths] : active_dashboards_) {
            for (const auto& dp : paths) {
                if (dp == path) { in_dashboard = true; break; }
            }
            if (in_dashboard) break;
        }

        // Show checkbox for toggling custom selection
        bool chk_state = is_custom || in_dashboard;
        if (in_dashboard) {
            // Already in an active dashboard — show as disabled checked
            ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            ImGui::Checkbox("##chk", &chk_state);
            ImGui::PopStyleColor();
        } else {
            if (ImGui::Checkbox("##chk", &chk_state)) {
                if (chk_state) custom_metrics_.insert(path);
                else           custom_metrics_.erase(path);
                rebuild_selected();
                if (on_selection_changed_) on_selection_changed_();
            }
        }
        ImGui::SameLine();

        // Display name
        ImGui::PushStyleColor(ImGuiCol_Text,
            is_custom ? COL_CUSTOM_TAG :
            in_dashboard ? ImVec4(0.5f, 0.5f, 0.5f, 1.0f) :
            COL_SEARCH_MATCH);
        ImGui::TextUnformatted(metric_display_name(path).c_str());
        ImGui::PopStyleColor();

        // Tooltip with full path
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(path.c_str());
            ImGui::EndTooltip();
        }

        ImGui::PopID();
        ++shown;
    }

    if (shown == 0) {
        ImGui::TextDisabled("No metrics match '%s'", search_buf_);
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

    render_dashboard_cards();

    // Show count of active selections
    if (!selected_metrics_.empty()) {
        ImGui::TextDisabled("%zu metrics selected", selected_metrics_.size());
        ImGui::Spacing();
    }

    // Show custom metric tags (if any) with remove buttons
    if (!custom_metrics_.empty()) {
        ImGui::TextDisabled("Custom:");
        // Copy keys to avoid iterator invalidation on erase
        std::vector<std::string> custom_copy(custom_metrics_.begin(),
                                             custom_metrics_.end());
        for (const auto& path : custom_copy) {
            ImGui::PushID(path.c_str());
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.15f, 0.35f, 1.0f));
            std::string label = metric_display_name(path) + " x";
            if (ImGui::SmallButton(label.c_str())) {
                custom_metrics_.erase(path);
                rebuild_selected();
                if (on_selection_changed_) on_selection_changed_();
            }
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
        ImGui::Spacing();
        ImGui::Separator();
    }

    render_search_overlay();
}
