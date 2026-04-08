#include "breakdown_view.hpp"

#include <imgui.h>
#include <implot.h>
#include <cstring>
#include <algorithm>
#include <cstdio>

#include "../parser/log_entry.hpp"
#include "../core/format.hpp"

// ------------------------------------------------------------

void BreakdownView::set_analysis(const AnalysisResult* analysis,
                                  const StringTable* strings)
{
    analysis_ = analysis;
    strings_  = strings;
}

void BreakdownView::set_filter(FilterState* filter) {
    filter_ = filter;
}

void BreakdownView::set_on_filter_changed(FilterChangedCb cb) {
    on_filter_changed_ = std::move(cb);
}

// ------------------------------------------------------------
//  inline_reset — renders a right-aligned "×" SmallButton on
//  the same line as the caller (use after a CollapsingHeader).
//  Only renders when `active` is true. Returns true if clicked.
// ------------------------------------------------------------
static bool inline_reset(const char* id, bool active, const char* tooltip = nullptr) {
    if (!active) return false;
    ImGui::SameLine();
    float x = ImGui::GetContentRegionMax().x
              - ImGui::CalcTextSize("x").x
              - ImGui::GetStyle().FramePadding.x * 2.0f - 2.0f;
    ImGui::SetCursorPosX(x);

    // Red tint for the reset button so it reads as "destructive / clear"
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.0f,  0.0f,  0.0f,  1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.06f, 0.06f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.28f, 0.08f, 0.08f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
    bool clicked = ImGui::SmallButton(id);
    ImGui::PopStyleColor(4);

    if (tooltip && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip);
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

    // Per-section reset — visible when this field has an active filter
    bool section_active = filter_ && (filter_->*field) != 0;
    char reset_id[64];
    std::snprintf(reset_id, sizeof(reset_id), "x##rst_%s", label);
    char tooltip[128];
    std::snprintf(tooltip, sizeof(tooltip), "Clear %s filter", label);
    if (inline_reset(reset_id, section_active, tooltip)) {
        filter_->*field = 0;
        if (on_filter_changed_) on_filter_changed_();
    }

    if (open) {
        float chart_h = std::min(180.0f, 24.0f * static_cast<float>(N) + 30.0f);

        ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(8, 4));

        if (ImPlot::BeginPlot("##bc", ImVec2(-1, chart_h),
                               ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText))
        {
            ImPlot::SetupAxes(nullptr, nullptr,
                              ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_NoLabel |
                              ImPlotAxisFlags_NoTickMarks,
                              ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_NoLabel |
                              ImPlotAxisFlags_NoTickMarks);

            double max_val = 0;
            for (size_t i = 0; i < N; ++i)
                if (values[i] > max_val) max_val = values[i];
            if (max_val > 0)
                ImPlot::SetupAxisLimits(ImAxis_X1, 0, max_val * 1.15, ImGuiCond_Always);

            ImPlot::SetupAxisTicks(ImAxis_Y1, 0, static_cast<double>(N) - 1,
                                   static_cast<int>(N), names);

            uint32_t current = filter_ ? (filter_->*field) : 0;

            for (size_t i = 0; i < N; ++i) {
                uint32_t bar_val = 0;
                if (is_severity)
                    bar_val = static_cast<uint32_t>(
                        severity_from_string(data[i].label.c_str())) + 1;

                bool active = is_severity && current != 0 && (current == bar_val);

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
                    if (is_severity) {
                        Severity sev = severity_from_string(data[clicked_i].label.c_str());
                        uint32_t v   = static_cast<uint32_t>(sev) + 1;
                        filter_->*field = (filter_->*field == v) ? 0 : v;
                    }
                    if (on_filter_changed_) on_filter_changed_();
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

    // Per-section reset
    bool section_active = filter_ && (filter_->*field) != 0;
    char reset_id[64];
    std::snprintf(reset_id, sizeof(reset_id), "x##rst_%s", label);
    char tooltip[128];
    std::snprintf(tooltip, sizeof(tooltip), "Clear %s filter", label);
    if (inline_reset(reset_id, section_active, tooltip)) {
        filter_->*field = 0;
        if (on_filter_changed_) on_filter_changed_();
    }

    if (open) {
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

    // Per-section reset — only when the set is non-empty
    bool section_active = filter_ && !(filter_->*set_field).empty();
    char reset_id[64];
    std::snprintf(reset_id, sizeof(reset_id), "x##rst_%s", label);
    char tooltip[128];
    std::snprintf(tooltip, sizeof(tooltip), "Clear %s filter", label);
    if (inline_reset(reset_id, section_active, tooltip)) {
        (filter_->*set_field).clear();
        if (on_filter_changed_) on_filter_changed_();
    }

    if (open) {
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

    render_bar_chart("Severity", analysis_->by_severity,
                     &FilterState::severity_filter, /*is_severity=*/true);
    render_bar_chart("Operation Type", analysis_->by_op_type,
                     &FilterState::op_type_idx);
    render_table_multi("Component", analysis_->by_component,
                       &FilterState::component_idx_include);
    render_table("Driver",      analysis_->by_driver,    &FilterState::driver_idx);
    render_table("Namespace",   analysis_->by_namespace, &FilterState::ns_idx);
    render_table("Query Shape", analysis_->by_shape,     &FilterState::shape_idx);
}
