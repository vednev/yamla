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
//  render_bar_chart
//  `field` is a pointer-to-member of FilterState for the uint32
//  that stores the active filter value for this category.
//  For severity we use a special 1-based encoding.
// ------------------------------------------------------------
void BreakdownView::render_bar_chart(const char* label, const CountMap& data,
                                      uint32_t FilterState::*field,
                                      bool is_severity)
{
    if (data.empty()) return;
    size_t N = std::min(data.size(), size_t(12)); // cap bars

    // Build C arrays for ImPlot
    static double  values[12];
    static const char* names[12];
    for (size_t i = 0; i < N; ++i) {
        values[i] = static_cast<double>(data[i].count);
        names[i]  = data[i].label.c_str();
    }

    ImGui::PushID(label);
    if (ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen)) {
        float chart_h = std::min(180.0f, 24.0f * static_cast<float>(N) + 30.0f);

        if (ImPlot::BeginPlot("##bc", ImVec2(-1, chart_h),
                               ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText))
        {
            ImPlot::SetupAxes(nullptr, nullptr,
                              ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_NoLabel,
                              ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_NoLabel);
            ImPlot::SetupAxisTicks(ImAxis_Y1, 0, static_cast<double>(N) - 1,
                                   static_cast<int>(N), names);

            // Highlight the selected bar
            uint32_t current = filter_ ? (filter_->*field) : 0;

            for (size_t i = 0; i < N; ++i) {
                // Resolve the filter value this bar represents using the same
                // method as the click handler — enum value + 1, not list position.
                uint32_t bar_val = 0;
                if (is_severity)
                    bar_val = static_cast<uint32_t>(
                        severity_from_string(data[i].label.c_str())) + 1;

                bool active = is_severity && current != 0 && (current == bar_val);

                ImPlot::PushStyleColor(ImPlotCol_Fill,
                    active ? ImVec4(1.0f, 0.65f, 0.1f, 1.0f)   // amber — selected
                           : ImVec4(0.3f, 0.6f,  0.9f, 0.85f)); // default blue

                double y = static_cast<double>(i);
                // values[i] is the bar length (count); y is its position on Y axis
                ImPlot::PlotBars("##", &values[i], &y, 1,
                                 0.6, ImPlotBarsFlags_Horizontal);
                ImPlot::PopStyleColor();
            }

            // Click detection
            if (ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(0)) {
                ImPlotPoint mp = ImPlot::GetPlotMousePos();
                int clicked_i = static_cast<int>(std::round(mp.y));
                if (clicked_i >= 0 && clicked_i < static_cast<int>(N) && filter_) {
                    if (is_severity) {
                        // Resolve the actual Severity enum value from the label,
                        // not the sorted position — they differ when sorted by count.
                        Severity sev = severity_from_string(data[clicked_i].label.c_str());
                        // Store as (enum_value + 1) so 0 remains "no filter"
                        uint32_t v = static_cast<uint32_t>(sev) + 1;
                        filter_->*field = (filter_->*field == v) ? 0 : v;
                    }
                    if (on_filter_changed_) on_filter_changed_();
                }
            }

            ImPlot::EndPlot();
        }
    }
    ImGui::PopID();
}

// ------------------------------------------------------------
//  render_table — for categories with string index filters
// ------------------------------------------------------------
void BreakdownView::render_table(const char* label, const CountMap& data,
                                  uint32_t FilterState::*field)
{
    if (data.empty()) return;
    ImGui::PushID(label);
    if (ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGuiTableFlags tf = ImGuiTableFlags_RowBg |
                             ImGuiTableFlags_BordersOuter |
                             ImGuiTableFlags_ScrollY |
                             ImGuiTableFlags_SizingStretchProp;
        float max_h = std::min(160.0f, 20.0f * static_cast<float>(data.size()) + 24.0f);
        if (ImGui::BeginTable("##t", 2, tf, ImVec2(-1, max_h))) {
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableHeadersRow();

            uint32_t current = filter_ ? (filter_->*field) : 0;

            // We need label → string_idx mapping; since CountMap uses labels,
            // we use a simple per-row selectable approach indexed by position.
            // For filtering we match by label lookup through StringTable.
            for (size_t i = 0; i < data.size() && i < 50; ++i) {
                ImGui::TableNextRow();
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableSetColumnIndex(0);

                uint32_t row_idx = strings_
                    ? const_cast<StringTable*>(strings_)->intern(data[i].label)
                    : 0;
                bool selected = (current == row_idx && row_idx != 0);

                // Invert on selection: white background set via TableBgColor
                if (selected) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                        IM_COL32(255, 255, 255, 255));
                }

                // Invisible spanning selectable for click detection
                bool clicked = ImGui::Selectable("##sel", selected,
                                                 ImGuiSelectableFlags_SpanAllColumns);
                bool hot = selected || ImGui::IsItemHovered();

                // Text colour: black on hot rows, white otherwise
                ImVec4 txt = hot ? ImVec4(0,0,0,1) : ImVec4(1,1,1,1);

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
                {
                    char cbuf[27];
                    ImGui::TextUnformatted(fmt_count_buf(data[i].count, cbuf, sizeof(cbuf)));
                }
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

    // Summary header
    {
        char c1[27], c2[27];
        ImGui::Text("Total entries: %s",
            fmt_count_buf(analysis_->total_entries, c1, sizeof(c1)));
        ImGui::Text("Slow queries:  %s",
            fmt_count_buf(analysis_->slow_queries, c2, sizeof(c2)));
    }
    ImGui::Separator();

    render_bar_chart("Severity", analysis_->by_severity,
                     &FilterState::severity_filter, /*is_severity=*/true);
    render_bar_chart("Operation Type", analysis_->by_op_type,
                     &FilterState::op_type_idx);
    render_table("Component",   analysis_->by_component, &FilterState::component_idx);
    render_table("Driver",      analysis_->by_driver,    &FilterState::driver_idx);
    render_table("Namespace",   analysis_->by_namespace, &FilterState::ns_idx);
    render_table("Query Shape", analysis_->by_shape,     &FilterState::shape_idx);
}
