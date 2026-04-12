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

    if (filter_) {
        filter_->conn_id_include.clear();
        filter_->driver_idx_include.clear();
        filter_->node_idx_include.clear();
    }
}

void FilterView::set_nodes(const std::vector<NodeInfo>* nodes) {
    nodes_ = nodes;
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
        for (; needle[j] && haystack[i + j]; ++j)
            if (std::tolower((unsigned char)haystack[i + j]) !=
                std::tolower((unsigned char)needle[j]))
                break;
        if (!needle[j]) return true;
    }
    return false;
}

// Red "Clear" SmallButton — used inside collapsible bodies.
// Returns true if clicked.
static bool clear_button(const char* id) {
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.0f,  0.0f,  0.0f,  1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.06f, 0.06f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.28f, 0.08f, 0.08f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
    bool clicked = ImGui::SmallButton(id);
    ImGui::PopStyleColor(4);
    return clicked;
}

// ------------------------------------------------------------
//  Driver helpers
// ------------------------------------------------------------

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
//  render_node_section
// ------------------------------------------------------------
void FilterView::render_node_section() {
    if (!nodes_ || nodes_->size() < 2 || !filter_) return;

    bool open = ImGui::CollapsingHeader("Nodes", ImGuiTreeNodeFlags_DefaultOpen);

    if (open) {
        // Clear button inside body — visible when any node is selected
        if (!filter_->node_idx_include.empty()) {
            float btn_w = ImGui::CalcTextSize("Clear").x
                          + ImGui::GetStyle().FramePadding.x * 2.0f;
            ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - btn_w - 2.0f);
            if (clear_button("Clear##nd_rst")) {
                filter_->node_idx_include.clear();
                if (on_filter_changed_) on_filter_changed_();
            }
        }

        float avail_h = ImGui::GetContentRegionAvail().y;
        float list_h  = std::min(avail_h - 4.0f,
                                  22.0f * static_cast<float>(nodes_->size()) + 8.0f);
        list_h = std::max(list_h, 40.0f);
        ImGui::BeginChild("##node_list", ImVec2(-1, list_h), true);

        for (const auto& node : *nodes_) {
            bool checked = filter_->node_idx_include.count(
                               static_cast<uint16_t>(node.idx)) > 0;
            ImVec4 col(node.color.r, node.color.g, node.color.b, 1.0f);
            ImGui::ColorButton("##nc", col,
                ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
                ImVec2(12, 12));
            ImGui::SameLine(0, 6);
            const std::string& host = node.hostname.empty() ? node.path : node.hostname;
            char chk_id[512];
            std::snprintf(chk_id, sizeof(chk_id), "%s##nd%u", host.c_str(), node.idx);
            if (ImGui::Checkbox(chk_id, &checked)) {
                if (checked)
                    filter_->node_idx_include.insert(static_cast<uint16_t>(node.idx));
                else
                    filter_->node_idx_include.erase(static_cast<uint16_t>(node.idx));
                if (on_filter_changed_) on_filter_changed_();
            }
        }
        ImGui::EndChild();
    }
}

// ------------------------------------------------------------
//  render_conn_section — integrated into main filter flow
// ------------------------------------------------------------
void FilterView::render_conn_section() {
    if (!analysis_ || !filter_ || analysis_->by_conn_id.empty()) return;

    char hdr[64];
    std::snprintf(hdr, sizeof(hdr), "Connections (%zu)",
                  analysis_->by_conn_id.size());
    bool open = ImGui::CollapsingHeader(hdr, ImGuiTreeNodeFlags_DefaultOpen);

    if (open) {
        // Clear button — only when filter is active
        if (!filter_->conn_id_include.empty()) {
            float btn_w = ImGui::CalcTextSize("Clear").x
                          + ImGui::GetStyle().FramePadding.x * 2.0f;
            ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - btn_w - 2.0f);
            if (clear_button("Clear##conn_rst")) {
                filter_->conn_id_include.clear();
                if (on_filter_changed_) on_filter_changed_();
            }
        }

        // Search box
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##conn_search", conn_search_, sizeof(conn_search_));

        float avail_h = ImGui::GetContentRegionAvail().y;
        float list_h  = std::min(avail_h - 4.0f,
                                  20.0f * static_cast<float>(analysis_->by_conn_id.size()) + 8.0f);
        list_h = std::max(list_h, 40.0f);
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
}

// ------------------------------------------------------------
//  render_driver_section
// ------------------------------------------------------------
void FilterView::render_driver_section() {
    if (!analysis_ || !filter_ || !strings_ || analysis_->by_driver.empty()) return;

    char hdr[64];
    std::snprintf(hdr, sizeof(hdr), "Drivers (%zu)", analysis_->by_driver.size());
    bool open = ImGui::CollapsingHeader(hdr, ImGuiTreeNodeFlags_DefaultOpen);

    if (open) {
        // Toolbar: Clear (when active) + All
        {
            bool drv_active = !filter_->driver_idx_include.empty();
            float all_w = ImGui::CalcTextSize("All").x
                          + ImGui::GetStyle().FramePadding.x * 2.0f;
            float clr_w = ImGui::CalcTextSize("Clear").x
                          + ImGui::GetStyle().FramePadding.x * 2.0f;
            float right  = ImGui::GetContentRegionMax().x;

            if (drv_active) {
                // Clear and All, right-aligned
                ImGui::SetCursorPosX(right - clr_w - all_w - ImGui::GetStyle().ItemSpacing.x - 2.0f);
                if (clear_button("Clear##drv_rst")) clear_all_driver();
                ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x);
            } else {
                ImGui::SetCursorPosX(right - all_w - 2.0f);
            }
            if (ImGui::SmallButton("All##d")) select_all_driver();
        }

        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##drv_search", driver_search_, sizeof(driver_search_));

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
}

// ------------------------------------------------------------
//  render_inner
// ------------------------------------------------------------
void FilterView::render_inner() {
    if (!analysis_) {
        ImGui::TextDisabled("Load a cluster to see filters.");
        return;
    }
    render_node_section();
    render_conn_section();
    render_driver_section();
}
