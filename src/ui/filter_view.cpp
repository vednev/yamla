#include "filter_view.hpp"

#include <imgui.h>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <algorithm>

#include "../core/format.hpp"

// ------------------------------------------------------------

void FilterView::set_analysis(const AnalysisResult* analysis,
                               const StringTable* strings)
{
    analysis_ = analysis;
    strings_  = strings;

    // Reset include sets to empty on every new data load.
    // Empty = no filter active = all entries visible.
    // The checkboxes will render as all-unchecked by default.
    if (filter_) {
        filter_->conn_id_include.clear();
        filter_->driver_idx_include.clear();
        // Don't call on_filter_changed_ here — the log view is being
        // set up simultaneously from render_frame; it will build its
        // index after all views are initialised.
    }
}

void FilterView::set_filter(FilterState* filter) {
    filter_ = filter;
}

void FilterView::set_on_filter_changed(FilterChangedCb cb) {
    on_filter_changed_ = std::move(cb);
}

// ------------------------------------------------------------
//  Helpers
// ------------------------------------------------------------

static bool str_contains_ci(const char* haystack, const char* needle) {
    if (!needle || needle[0] == '\0') return true;
    if (!haystack) return false;
    for (size_t i = 0; haystack[i]; ++i) {
        size_t j = 0;
        for (; needle[j] && haystack[i + j]; ++j) {
            if (std::tolower((unsigned char)haystack[i + j]) !=
                std::tolower((unsigned char)needle[j]))
                break;
        }
        if (!needle[j]) return true;
    }
    return false;
}

// ------------------------------------------------------------
//  Select / Clear All — inclusion model
// ------------------------------------------------------------

void FilterView::select_all_conn() {
    if (!filter_ || !analysis_) return;
    for (const auto& ce : analysis_->by_conn_id)
        filter_->conn_id_include.insert(ce.conn_id);
    if (on_filter_changed_) on_filter_changed_();
}

// "None" = clear the include set = filter inactive = show all
void FilterView::clear_all_conn() {
    if (!filter_) return;
    filter_->conn_id_include.clear();
    if (on_filter_changed_) on_filter_changed_();
}

void FilterView::select_all_driver() {
    if (!filter_ || !analysis_ || !strings_) return;
    for (const auto& de : analysis_->by_driver) {
        uint32_t idx = const_cast<StringTable*>(strings_)->intern(de.label);
        if (idx) filter_->driver_idx_include.insert(idx);
    }
    if (on_filter_changed_) on_filter_changed_();
}

void FilterView::clear_all_driver() {
    if (!filter_) return;
    filter_->driver_idx_include.clear();
    if (on_filter_changed_) on_filter_changed_();
}

// ------------------------------------------------------------
//  render_conn_section
// ------------------------------------------------------------
void FilterView::render_conn_section() {
    if (!analysis_ || !filter_) return;

    // Header row: title + All / None buttons on the right
    // (None already acts as the clear — no separate × needed)
    ImGui::Text("Connections (%zu)", analysis_->by_conn_id.size());
    ImGui::SameLine();
    float btn_w = ImGui::CalcTextSize("All").x + ImGui::GetStyle().FramePadding.x * 2 + 4;
    ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - btn_w * 2 - 4);
    if (ImGui::SmallButton("All##c"))  select_all_conn();
    ImGui::SameLine(0, 4);
    if (ImGui::SmallButton("None##c")) clear_all_conn();

    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##conn_search", conn_search_, sizeof(conn_search_))) {
        // search updates are instant — no action needed, just re-render
    }

    // Active filter count hint
    size_t checked = filter_->conn_id_include.size();
    if (checked > 0) {
        ImGui::SameLine(0, 6);
        ImGui::TextDisabled("(%zu)", checked);
    }

    // Scrollable checkbox list — give it most of the available height
    float avail_h = ImGui::GetContentRegionAvail().y;
    float list_h  = std::min(avail_h - 4.0f,
                             20.0f * static_cast<float>(analysis_->by_conn_id.size()) + 8.0f);
    list_h = std::max(list_h, 40.0f); // minimum visible

    ImGui::BeginChild("##conn_list", ImVec2(-1, list_h), true);

    for (const auto& ce : analysis_->by_conn_id) {
        char label[64];
        std::snprintf(label, sizeof(label), "conn%u", ce.conn_id);
        if (!str_contains_ci(label, conn_search_)) continue;

        bool checked = filter_->conn_id_include.count(ce.conn_id) > 0;
        char cnt_buf[27];
        char chk_id[80];
        std::snprintf(chk_id, sizeof(chk_id), "%s  (%s)##ci%u",
                      label,
                      fmt_count_buf(ce.count, cnt_buf, sizeof(cnt_buf)),
                      ce.conn_id);
        if (ImGui::Checkbox(chk_id, &checked)) {
            if (checked)
                filter_->conn_id_include.insert(ce.conn_id);
            else
                filter_->conn_id_include.erase(ce.conn_id);
            if (on_filter_changed_) on_filter_changed_();
        }
    }

    ImGui::EndChild();
}

// ------------------------------------------------------------
//  render_driver_section
// ------------------------------------------------------------
void FilterView::render_driver_section() {
    if (!analysis_ || !filter_ || !strings_) return;
    if (analysis_->by_driver.empty()) return;

    ImGui::Separator();

    ImGui::Text("Drivers (%zu)", analysis_->by_driver.size());
    ImGui::SameLine();

    bool drv_active = !filter_->driver_idx_include.empty();
    float btn_w   = ImGui::CalcTextSize("All").x + ImGui::GetStyle().FramePadding.x * 2 + 4;
    float reset_w = ImGui::CalcTextSize("x").x   + ImGui::GetStyle().FramePadding.x * 2 + 4;
    ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - btn_w * 2 - (drv_active ? reset_w + 4 : 0) - 4);
    if (drv_active) {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.0f,  0.0f,  0.0f,  1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.06f, 0.06f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.28f, 0.08f, 0.08f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
        if (ImGui::SmallButton("x##drvrst")) clear_all_driver();
        ImGui::PopStyleColor(4);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear driver filter");
        ImGui::SameLine(0, 4);
    }
    if (ImGui::SmallButton("All##d"))  select_all_driver();
    ImGui::SameLine(0, 4);
    if (ImGui::SmallButton("None##d")) clear_all_driver();

    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##drv_search", driver_search_, sizeof(driver_search_));

    size_t checked = filter_->driver_idx_include.size();
    if (checked > 0) {
        ImGui::SameLine(0, 6);
        ImGui::TextDisabled("(%zu)", checked);
    }

    float avail_h = ImGui::GetContentRegionAvail().y;
    float list_h  = std::min(avail_h - 4.0f,
                             20.0f * static_cast<float>(analysis_->by_driver.size()) + 8.0f);
    list_h = std::max(list_h, 40.0f);

    ImGui::BeginChild("##drv_list", ImVec2(-1, list_h), true);

    for (const auto& de : analysis_->by_driver) {
        if (!str_contains_ci(de.label.c_str(), driver_search_)) continue;

        uint32_t idx = const_cast<StringTable*>(strings_)->intern(de.label);
        bool checked = filter_->driver_idx_include.count(idx) > 0;

        char dcnt_buf[27];
        char chk_id[256];
        std::snprintf(chk_id, sizeof(chk_id), "%s  (%s)##di%u",
                      de.label.c_str(),
                      fmt_count_buf(de.count, dcnt_buf, sizeof(dcnt_buf)),
                      idx);
        if (ImGui::Checkbox(chk_id, &checked)) {
            if (checked)
                filter_->driver_idx_include.insert(idx);
            else
                filter_->driver_idx_include.erase(idx);
            if (on_filter_changed_) on_filter_changed_();
        }
    }

    ImGui::EndChild();
}

// ------------------------------------------------------------
//  render_inner — contents only, embedded in a parent child window
// ------------------------------------------------------------
void FilterView::render_inner() {
    if (!analysis_) {
        ImGui::TextDisabled("Load a cluster to see filters.");
        return;
    }
    render_conn_section();
    render_driver_section();
}
