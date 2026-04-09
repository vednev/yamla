#include "breakdown_view.hpp"

#include <imgui.h>
#include <implot.h>
#include <cstring>
#include <cctype>
#include <cmath>
#include <algorithm>
#include <cstdio>

#include "../parser/log_entry.hpp"
#include "../core/format.hpp"
#include "../core/prefs.hpp"

// ------------------------------------------------------------

void BreakdownView::set_analysis(const AnalysisResult* analysis,
                                  const StringTable* strings)
{
    analysis_ = analysis;
    strings_  = strings;
}

void BreakdownView::set_nodes(const std::vector<NodeInfo>* nodes) {
    nodes_ = nodes;
}

void BreakdownView::set_filter(FilterState* filter) {
    filter_ = filter;
}

void BreakdownView::set_on_filter_changed(FilterChangedCb cb) {
    on_filter_changed_ = std::move(cb);
}

void BreakdownView::set_prefs(const Prefs* prefs) {
    prefs_ = prefs;
}

// ------------------------------------------------------------
//  Shared helpers for checkbox-list sections
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

// Red "Clear" SmallButton — for inline use inside collapsible bodies.
static bool red_clear_button(const char* id) {
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.0f,  0.0f,  0.0f,  1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.06f, 0.06f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.28f, 0.08f, 0.08f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
    bool clicked = ImGui::SmallButton(id);
    ImGui::PopStyleColor(4);
    return clicked;
}

// ------------------------------------------------------------
//  section_clear_button
//
//  Right-aligned red "Clear" SmallButton rendered INSIDE the
//  open body of a CollapsingHeader (never on the header row).
//  Returns true if clicked.  Only renders when active == true.
// ------------------------------------------------------------
static bool section_clear_button(const char* id, bool active)
{
    if (!active) return false;
    float btn_w = ImGui::CalcTextSize("Clear").x
                  + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - btn_w - 2.0f);
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.0f,  0.0f,  0.0f,  1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.06f, 0.06f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.28f, 0.08f, 0.08f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
    bool clicked = ImGui::SmallButton(id);
    ImGui::PopStyleColor(4);
    return clicked;
}

// ------------------------------------------------------------
//  render_reset_button — global "Reset all" (kept for header compat)
// ------------------------------------------------------------
void BreakdownView::render_reset_button() {
    if (!filter_ || !filter_->active()) return;
    float btn_w = ImGui::CalcTextSize("Reset all").x
                  + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine(ImGui::GetContentRegionMax().x - btn_w);
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.0f,  0.0f,  0.0f,  1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.06f, 0.06f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.28f, 0.08f, 0.08f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
    if (ImGui::SmallButton("Reset all")) {
        filter_->clear();
        if (on_filter_changed_) on_filter_changed_();
    }
    ImGui::PopStyleColor(4);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Clear every active filter");
}

// ------------------------------------------------------------
//  render_bar_chart
// ------------------------------------------------------------
void BreakdownView::render_bar_chart(const char* label, const CountMap& data,
                                      uint32_t FilterState::*field,
                                      bool is_severity)
{
    if (data.empty()) return;
    size_t N = std::min(data.size(), size_t(12));

    static double      values[12];
    static const char* names[12];
    for (size_t i = 0; i < N; ++i) {
        values[i] = static_cast<double>(data[i].count);
        names[i]  = data[i].label.c_str();
    }

    ImGui::PushID(label);
    bool open = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);

    if (open) {
        // Clear button inside the body — first item so it has exclusive click area.
        // Placed here (not on the header row) because CollapsingHeader claims the
        // full row width and intercepts all SameLine widgets regardless of overlap flags.
        bool section_active = filter_ && (filter_->*field) != 0;
        char clear_id[64];
        std::snprintf(clear_id, sizeof(clear_id), "Clear##bc_%s", label);
        if (section_clear_button(clear_id, section_active)) {
            filter_->*field = 0;
            if (on_filter_changed_) on_filter_changed_();
        }

        // Each bar gets a fixed 22px height so all bars are comfortably
        // clickable regardless of how many there are. No upper cap.
        static constexpr float BAR_H = 22.0f;
        float chart_h = BAR_H * static_cast<float>(N) + 36.0f; // +36 for X-axis area

        ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(8, 4));

        if (ImPlot::BeginPlot("##bc", ImVec2(-1, chart_h),
                               ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText))
        {
            ImPlot::SetupAxes(nullptr, nullptr,
                              ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_NoLabel |
                              ImPlotAxisFlags_NoTickMarks,
                              ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_NoLabel |
                              ImPlotAxisFlags_NoTickMarks);

            // X-axis: compact K/M/B tick formatter.
            // Limit tick count to what fits in the available plot width so
            // numbers never overlap — fall back to "..." when too narrow.
            ImPlot::SetupAxisFormat(ImAxis_X1, [](double value, char* buf, int size, void*) -> int {
                if (value < 0) { buf[0] = '0'; buf[1] = '\0'; return 1; }
                char tmp[16];
                fmt_compact(static_cast<uint64_t>(value), tmp, sizeof(tmp));
                return std::snprintf(buf, static_cast<size_t>(size), "%s", tmp);
            }, nullptr);
            // Estimate available width and derive a safe tick count.
            // Each tick label needs ~38px; clamp to [1, 6].
            {
                float plot_w = ImGui::GetContentRegionAvail().x - 20.0f;
                int max_ticks = std::max(1, std::min(6, static_cast<int>(plot_w / 38.0f)));
                ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, 0, 1e18);
                // We don't call SetupAxisTicks for X (auto), but we set a
                // reduced tick density via ImPlot style.
                ImPlot::GetStyle().FitPadding = ImVec2(0.15f, 0.0f);
                (void)max_ticks; // ImPlot auto-spaces; compact formatter keeps labels short
            }

            double max_val = 0;
            for (size_t i = 0; i < N; ++i)
                if (values[i] > max_val) max_val = values[i];
            if (max_val > 0)
                ImPlot::SetupAxisLimits(ImAxis_X1, 0, max_val * 1.15, ImGuiCond_Always);

            ImPlot::SetupAxisTicks(ImAxis_Y1, 0, static_cast<double>(N) - 1,
                                   static_cast<int>(N), names);

            uint32_t current = filter_ ? (filter_->*field) : 0;

            for (size_t i = 0; i < N; ++i) {
                // Determine the filter value this bar represents.
                // For severity: use enum value + 1 (label-based, order-independent).
                // For other fields: use the StringTable index of the label.
                uint32_t bar_val = 0;
                if (is_severity) {
                    bar_val = static_cast<uint32_t>(
                        severity_from_string(data[i].label.c_str())) + 1;
                } else if (strings_) {
                    bar_val = const_cast<StringTable*>(strings_)->intern(
                        data[i].label);
                }

                bool active = (current != 0 && bar_val != 0 && current == bar_val);

                ImPlot::PushStyleColor(ImPlotCol_Fill,
                    active ? ImVec4(1.0f, 0.65f, 0.1f, 1.0f)
                           : ImVec4(0.3f, 0.6f,  0.9f, 0.85f));
                double y = static_cast<double>(i);
                ImPlot::PlotBars("##", &values[i], &y, 1, 0.6,
                                 ImPlotBarsFlags_Horizontal);
                ImPlot::PopStyleColor();
            }

            if (ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(0)) {
                ImPlotPoint mp = ImPlot::GetPlotMousePos();
                int clicked_i  = static_cast<int>(std::round(mp.y));
                if (clicked_i >= 0 && clicked_i < static_cast<int>(N) && filter_) {
                    uint32_t bar_val = 0;
                    if (is_severity) {
                        Severity sev = severity_from_string(data[clicked_i].label.c_str());
                        bar_val = static_cast<uint32_t>(sev) + 1;
                    } else if (strings_) {
                        bar_val = const_cast<StringTable*>(strings_)->intern(
                            data[clicked_i].label);
                    }
                    if (bar_val != 0) {
                        filter_->*field = (filter_->*field == bar_val) ? 0 : bar_val;
                        if (on_filter_changed_) on_filter_changed_();
                    }
                }
            }

            ImPlot::EndPlot();
        }
        ImPlot::PopStyleVar(); // PlotPadding
    }
    ImGui::PopID();
}

// ------------------------------------------------------------
//  render_table — single-select
// ------------------------------------------------------------
void BreakdownView::render_table(const char* label, const CountMap& data,
                                  uint32_t FilterState::*field)
{
    if (data.empty()) return;
    ImGui::PushID(label);
    bool open = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);

    if (open) {
        // Clear button inside the body — same fix as Component filter.
        bool section_active = filter_ && (filter_->*field) != 0;
        char clear_id[64];
        std::snprintf(clear_id, sizeof(clear_id), "Clear##tbl_%s", label);
        if (section_clear_button(clear_id, section_active)) {
            filter_->*field = 0;
            if (on_filter_changed_) on_filter_changed_();
        }

        ImGuiTableFlags tf = ImGuiTableFlags_RowBg        |
                             ImGuiTableFlags_BordersOuter  |
                             ImGuiTableFlags_ScrollY        |
                             ImGuiTableFlags_SizingStretchProp;
        float max_h = std::min(160.0f, 20.0f * static_cast<float>(data.size()) + 24.0f);
        if (ImGui::BeginTable("##t", 2, tf, ImVec2(-1, max_h))) {
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableHeadersRow();

            uint32_t current = filter_ ? (filter_->*field) : 0;
            static constexpr ImU32 SEL_BG = IM_COL32(160, 200, 255, 255);

            for (size_t i = 0; i < data.size() && i < 50; ++i) {
                ImGui::TableNextRow();
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableSetColumnIndex(0);

                uint32_t row_idx = strings_
                    ? const_cast<StringTable*>(strings_)->intern(data[i].label)
                    : 0;
                bool selected = (current == row_idx && row_idx != 0);

                if (selected) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, SEL_BG);
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, SEL_BG);
                }

                static const ImVec4 pastel_hdr = ImGui::ColorConvertU32ToFloat4(SEL_BG);
                ImGui::PushStyleColor(ImGuiCol_Header,        pastel_hdr);
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, pastel_hdr);
                ImGui::PushStyleColor(ImGuiCol_HeaderActive,  pastel_hdr);
                bool clicked = ImGui::Selectable("##sel", selected,
                                                 ImGuiSelectableFlags_SpanAllColumns);
                ImGui::PopStyleColor(3);

                ImVec4 txt = selected ? ImVec4(0,0,0,1) : ImVec4(1,1,1,1);
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, txt);
                ImGui::TextUnformatted(data[i].label.c_str());
                ImGui::PopStyleColor();

                if (clicked && filter_) {
                    filter_->*field = selected ? 0 : row_idx;
                    if (on_filter_changed_) on_filter_changed_();
                }

                ImGui::TableSetColumnIndex(1);
                ImGui::PushStyleColor(ImGuiCol_Text, txt);
                { char cbuf[27]; ImGui::TextUnformatted(
                    fmt_count_buf(data[i].count, cbuf, sizeof(cbuf))); }
                ImGui::PopStyleColor();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    ImGui::PopID();
}

// ------------------------------------------------------------
//  render_table_multi — multi-select with inclusion set
// ------------------------------------------------------------
void BreakdownView::render_table_multi(const char* label, const CountMap& data,
                                        std::unordered_set<uint32_t> FilterState::*set_field)
{
    if (data.empty()) return;
    ImGui::PushID(label);

    bool open = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);

    if (open) {
        // Clear button inside the body — exclusive click area, no header conflict.
        bool section_active = filter_ && !(filter_->*set_field).empty();
        if (section_clear_button("Clear##multi_rst", section_active) && filter_) {
            (filter_->*set_field).clear();
            if (on_filter_changed_) on_filter_changed_();
        }
        ImGuiTableFlags tf = ImGuiTableFlags_RowBg         |
                             ImGuiTableFlags_BordersOuter   |
                             ImGuiTableFlags_ScrollY         |
                             ImGuiTableFlags_SizingStretchProp;
        float max_h = std::min(160.0f, 20.0f * static_cast<float>(data.size()) + 24.0f);
        if (ImGui::BeginTable("##tm", 2, tf, ImVec2(-1, max_h))) {
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableHeadersRow();

            static constexpr ImU32 SEL_BG = IM_COL32(160, 200, 255, 255);
            static const ImVec4 pastel_hdr = {160/255.0f, 200/255.0f, 255/255.0f, 1.0f};

            auto& active_set = filter_ ? (filter_->*set_field)
                                       : *static_cast<std::unordered_set<uint32_t>*>(nullptr);

            for (size_t i = 0; i < data.size() && i < 50; ++i) {
                ImGui::TableNextRow();
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableSetColumnIndex(0);

                uint32_t row_idx = strings_
                    ? const_cast<StringTable*>(strings_)->intern(data[i].label)
                    : 0;
                bool selected = filter_ && row_idx != 0
                                && active_set.count(row_idx) > 0;

                if (selected) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, SEL_BG);
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, SEL_BG);
                }

                ImGui::PushStyleColor(ImGuiCol_Header,        pastel_hdr);
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, pastel_hdr);
                ImGui::PushStyleColor(ImGuiCol_HeaderActive,  pastel_hdr);
                bool clicked = ImGui::Selectable("##sel", selected,
                                                 ImGuiSelectableFlags_SpanAllColumns);
                ImGui::PopStyleColor(3);

                ImVec4 txt = selected ? ImVec4(0,0,0,1) : ImVec4(1,1,1,1);
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, txt);
                ImGui::TextUnformatted(data[i].label.c_str());
                ImGui::PopStyleColor();

                if (clicked && filter_ && row_idx != 0) {
                    if (selected) active_set.erase(row_idx);
                    else          active_set.insert(row_idx);
                    if (on_filter_changed_) on_filter_changed_();
                }

                ImGui::TableSetColumnIndex(1);
                ImGui::PushStyleColor(ImGuiCol_Text, txt);
                { char cbuf[27]; ImGui::TextUnformatted(
                    fmt_count_buf(data[i].count, cbuf, sizeof(cbuf))); }
                ImGui::PopStyleColor();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    ImGui::PopID();
}

// ------------------------------------------------------------
//  render_nodes_section
// ------------------------------------------------------------
void BreakdownView::render_nodes_section() {
    if (!nodes_ || nodes_->size() < 2 || !filter_) return;

    char hdr[32];
    std::snprintf(hdr, sizeof(hdr), "Nodes (%zu)", nodes_->size());
    bool open = ImGui::CollapsingHeader(hdr, ImGuiTreeNodeFlags_DefaultOpen);

    if (open) {
        if (!filter_->node_idx_include.empty()) {
            float btn_w = ImGui::CalcTextSize("Clear").x
                          + ImGui::GetStyle().FramePadding.x * 2.0f;
            ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - btn_w - 2.0f);
            if (red_clear_button("Clear##nd_rst")) {
                filter_->node_idx_include.clear();
                if (on_filter_changed_) on_filter_changed_();
            }
        }

        float list_h = std::min(120.0f,
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
//  render_connections_section
// ------------------------------------------------------------
void BreakdownView::render_connections_section() {
    if (!analysis_ || !filter_ || analysis_->by_conn_id.empty()) return;

    char hdr[64];
    std::snprintf(hdr, sizeof(hdr), "Connections (%zu)",
                  analysis_->by_conn_id.size());
    bool open = ImGui::CollapsingHeader(hdr, ImGuiTreeNodeFlags_DefaultOpen);

    if (open) {
        if (!filter_->conn_id_include.empty()) {
            float btn_w = ImGui::CalcTextSize("Clear").x
                          + ImGui::GetStyle().FramePadding.x * 2.0f;
            ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - btn_w - 2.0f);
            if (red_clear_button("Clear##conn_rst")) {
                filter_->conn_id_include.clear();
                if (on_filter_changed_) on_filter_changed_();
            }
        }

        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##conn_search", conn_search_, sizeof(conn_search_));

        float list_h = std::min(160.0f,
            20.0f * static_cast<float>(analysis_->by_conn_id.size()) + 8.0f);
        list_h = std::max(list_h, 40.0f);
        ImGui::BeginChild("##conn_list", ImVec2(-1, list_h), true);

        for (const auto& ce : analysis_->by_conn_id) {
            char label[64];
            std::snprintf(label, sizeof(label), "conn%u", ce.conn_id);
            if (!str_contains_ci(label, conn_search_)) continue;

            bool checked = filter_->conn_id_include.count(ce.conn_id) > 0;
            char cnt_buf[27], chk_id[80];
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
//  render_bar_as_checkboxes
//
//  Checkbox-list alternative to render_bar_chart, used when the
//  user has enabled "Prefer checkboxes over graphs" in Preferences.
//  Each item shows a checkbox + label + count.  Checking an item
//  sets the filter field to that item's StringTable index (or
//  severity enum+1 for severity).  Single-select: checking a new
//  item deselects the previous one.
// ------------------------------------------------------------
void BreakdownView::render_bar_as_checkboxes(const char* label,
                                              const CountMap& data,
                                              uint32_t FilterState::*field,
                                              bool is_severity)
{
    if (data.empty()) return;
    size_t N = std::min(data.size(), size_t(12));

    ImGui::PushID(label);
    bool open = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);

    if (open) {
        bool section_active = filter_ && (filter_->*field) != 0;
        char clear_id[64];
        std::snprintf(clear_id, sizeof(clear_id), "Clear##ckbc_%s", label);
        if (section_clear_button(clear_id, section_active)) {
            filter_->*field = 0;
            if (on_filter_changed_) on_filter_changed_();
        }

        uint32_t current = filter_ ? (filter_->*field) : 0;
        static constexpr ImU32 SEL_BG = IM_COL32(160, 200, 255, 255);

        float list_h = std::min(160.0f, 22.0f * static_cast<float>(N) + 8.0f);
        list_h = std::max(list_h, 40.0f);
        ImGui::BeginChild("##ckbc_list", ImVec2(-1, list_h), true);

        for (size_t i = 0; i < N; ++i) {
            // Resolve filter value for this item
            uint32_t item_val = 0;
            if (is_severity) {
                item_val = static_cast<uint32_t>(
                    severity_from_string(data[i].label.c_str())) + 1;
            } else if (strings_) {
                item_val = const_cast<StringTable*>(strings_)->intern(data[i].label);
            }

            bool checked = (current != 0 && item_val != 0 && current == item_val);

            // Highlight selected row
            if (checked) {
                ImVec2 pos = ImGui::GetCursorScreenPos();
                ImVec2 size(ImGui::GetContentRegionAvail().x,
                            ImGui::GetTextLineHeightWithSpacing());
                ImGui::GetWindowDrawList()->AddRectFilled(
                    pos, ImVec2(pos.x + size.x, pos.y + size.y),
                    SEL_BG);
            }

            char cnt_buf[27], chk_id[256];
            std::snprintf(chk_id, sizeof(chk_id), "%s  (%s)##ckbc%zu",
                          data[i].label.c_str(),
                          fmt_count_buf(data[i].count, cnt_buf, sizeof(cnt_buf)),
                          i);
            ImVec4 txt = checked ? ImVec4(0,0,0,1) : ImVec4(1,1,1,1);
            ImGui::PushStyleColor(ImGuiCol_Text, txt);
            if (ImGui::Checkbox(chk_id, &checked)) {
                if (filter_) {
                    // Single-select: toggle off if same, set if different
                    filter_->*field = (current == item_val) ? 0 : item_val;
                    if (on_filter_changed_) on_filter_changed_();
                }
            }
            ImGui::PopStyleColor();
        }

        ImGui::EndChild();
    }
    ImGui::PopID();
}

// ------------------------------------------------------------
//  render
// ------------------------------------------------------------
void BreakdownView::render() {
    if (!analysis_) {
        ImGui::TextDisabled("No data loaded.");
        return;
    }

    // ---- Summary header ----------------------------------------
    {
        char c1[27], c2[27];
        ImGui::Text("Total entries: %s",
            fmt_count_buf(analysis_->total_entries, c1, sizeof(c1)));

        // Global "Reset all" — right-aligned on same line, red, shown when any filter active
        render_reset_button();

        // Slow queries: clickable, toggles slow_query_only filter
        bool slow_active = filter_ && filter_->slow_query_only;
        if (slow_active)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.1f, 1.0f));

        char sq_label[64];
        std::snprintf(sq_label, sizeof(sq_label), "Slow queries:  %s",
            fmt_count_buf(analysis_->slow_queries, c2, sizeof(c2)));

        if (ImGui::Selectable(sq_label, slow_active, ImGuiSelectableFlags_None, ImVec2(0,0))) {
            if (filter_) {
                filter_->slow_query_only = !filter_->slow_query_only;
                if (on_filter_changed_) on_filter_changed_();
            }
        }
        if (slow_active) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Click to filter to slow queries only (durationMillis > 100)");
    }

    ImGui::Separator();

    bool ckbox = prefs_ && prefs_->prefer_checkboxes;
    if (ckbox) {
        render_bar_as_checkboxes("Severity", analysis_->by_severity,
                                 &FilterState::severity_filter, /*is_severity=*/true);
        render_bar_as_checkboxes("Operation Type", analysis_->by_op_type,
                                 &FilterState::op_type_idx);
    } else {
        render_bar_chart("Severity", analysis_->by_severity,
                         &FilterState::severity_filter, /*is_severity=*/true);
        render_bar_chart("Operation Type", analysis_->by_op_type,
                         &FilterState::op_type_idx);
    }
    render_table_multi("Component", analysis_->by_component,
                       &FilterState::component_idx_include);
    render_table("Driver",      analysis_->by_driver,    &FilterState::driver_idx);
    render_table("Namespace",   analysis_->by_namespace, &FilterState::ns_idx);
    render_table("Query Shape", analysis_->by_shape,     &FilterState::shape_idx);
    render_nodes_section();
    render_connections_section();
}
