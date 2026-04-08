#include "filter_view.hpp"

#include <imgui.h>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <algorithm>

// ------------------------------------------------------------

void FilterView::set_analysis(const AnalysisResult* analysis,
                               const StringTable* strings)
{
    analysis_ = analysis;
    strings_  = strings;
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
    // Case-insensitive substring search
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
//  Select / Clear All
// ------------------------------------------------------------

void FilterView::select_all_conn() {
    if (!filter_) return;
    filter_->conn_id_exclude.clear();
    if (on_filter_changed_) on_filter_changed_();
}

void FilterView::clear_all_conn() {
    if (!filter_ || !analysis_) return;
    for (auto& ce : analysis_->by_conn_id)
        filter_->conn_id_exclude.insert(ce.conn_id);
    if (on_filter_changed_) on_filter_changed_();
}

void FilterView::select_all_driver() {
    if (!filter_) return;
    filter_->driver_idx_exclude.clear();
    if (on_filter_changed_) on_filter_changed_();
}

void FilterView::clear_all_driver() {
    if (!filter_ || !analysis_ || !strings_) return;
    for (auto& de : analysis_->by_driver) {
        uint32_t idx = const_cast<StringTable*>(strings_)->intern(de.label);
        if (idx) filter_->driver_idx_exclude.insert(idx);
    }
    if (on_filter_changed_) on_filter_changed_();
}

// ------------------------------------------------------------
//  render_conn_section
// ------------------------------------------------------------
void FilterView::render_conn_section() {
    if (!analysis_ || !filter_) return;

    ImGui::Text("Connection IDs (%zu unique)",
                analysis_->by_conn_id.size());
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##conn_search", conn_search_, sizeof(conn_search_));
    ImGui::SameLine(0, 4);
    ImGui::SetNextItemWidth(-1);

    ImGui::BeginGroup();
    if (ImGui::SmallButton("All##c"))   select_all_conn();
    ImGui::SameLine(0, 4);
    if (ImGui::SmallButton("None##c"))  clear_all_conn();
    ImGui::EndGroup();

    // Scrollable checkbox list
    float list_h = std::min(300.0f,
        20.0f * static_cast<float>(analysis_->by_conn_id.size()) + 8.0f);
    ImGui::BeginChild("##conn_list", ImVec2(-1, list_h), true);

    for (const auto& ce : analysis_->by_conn_id) {
        // Search filter
        char label[64];
        std::snprintf(label, sizeof(label), "conn%u", ce.conn_id);
        if (!str_contains_ci(label, conn_search_)) continue;

        bool checked = filter_->conn_id_exclude.count(ce.conn_id) == 0;
        char chk_id[72];
        std::snprintf(chk_id, sizeof(chk_id), "%s (%llu)##%u",
                      label,
                      static_cast<unsigned long long>(ce.count),
                      ce.conn_id);
        if (ImGui::Checkbox(chk_id, &checked)) {
            if (checked)
                filter_->conn_id_exclude.erase(ce.conn_id);
            else
                filter_->conn_id_exclude.insert(ce.conn_id);
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

    ImGui::Separator();
    ImGui::Text("Driver Types (%zu unique)",
                analysis_->by_driver.size());
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##drv_search", driver_search_, sizeof(driver_search_));

    ImGui::BeginGroup();
    if (ImGui::SmallButton("All##d"))   select_all_driver();
    ImGui::SameLine(0, 4);
    if (ImGui::SmallButton("None##d"))  clear_all_driver();
    ImGui::EndGroup();

    float list_h = std::min(300.0f,
        20.0f * static_cast<float>(analysis_->by_driver.size()) + 8.0f);
    ImGui::BeginChild("##drv_list", ImVec2(-1, list_h), true);

    for (const auto& de : analysis_->by_driver) {
        if (!str_contains_ci(de.label.c_str(), driver_search_)) continue;

        uint32_t idx = const_cast<StringTable*>(strings_)->intern(de.label);
        bool checked = filter_->driver_idx_exclude.count(idx) == 0;

        char chk_id[256];
        std::snprintf(chk_id, sizeof(chk_id), "%s (%llu)##drv%u",
                      de.label.c_str(),
                      static_cast<unsigned long long>(de.count),
                      idx);
        if (ImGui::Checkbox(chk_id, &checked)) {
            if (checked)
                filter_->driver_idx_exclude.erase(idx);
            else
                filter_->driver_idx_exclude.insert(idx);
            if (on_filter_changed_) on_filter_changed_();
        }
    }

    ImGui::EndChild();
}

// ------------------------------------------------------------
//  render — floating window
// ------------------------------------------------------------
void FilterView::render() {
    if (!open_) return;

    ImGui::SetNextWindowSize(ImVec2(340, 600), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(60, 60), ImGuiCond_FirstUseEver);

    ImGuiWindowFlags flags = ImGuiWindowFlags_None;
    if (!ImGui::Begin("Filters##filterwin", &open_, flags)) {
        ImGui::End();
        return;
    }

    if (!analysis_) {
        ImGui::TextDisabled("Load a cluster to populate filters.");
        ImGui::End();
        return;
    }

    render_conn_section();
    render_driver_section();

    ImGui::End();
}
