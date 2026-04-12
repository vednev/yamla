#include "chart_panel_view.hpp"

#include <imgui.h>
#include <implot.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cmath>

#include "../ftdc/metric_store.hpp"
#include "../ftdc/metric_defs.hpp"
#include "../ftdc/ftdc_analyzer.hpp"
#include "../parser/log_entry.hpp"
#include "log_view.hpp"   // FilterState

// ---- Helpers ----
static constexpr ImVec4 COL_CROSSHAIR      = ImVec4(1.0f, 1.0f, 1.0f, 0.50f);
static constexpr ImVec4 COL_ANNOTATION_ERR = ImVec4(1.0f, 0.3f, 0.3f, 0.80f);
static constexpr ImVec4 COL_ANNOTATION_WARN= ImVec4(1.0f, 0.8f, 0.0f, 0.80f);
static constexpr ImVec4 COL_STATS          = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);

// Convert epoch ms to ImPlot double (ImPlot's date axis uses seconds)
static double ms_to_plot(int64_t ms) {
    return static_cast<double>(ms) / 1000.0;
}
static int64_t plot_to_ms(double x) {
    return static_cast<int64_t>(x * 1000.0);
}

// ============================================================
//  fmt_metric_value
// ============================================================
void ChartPanelView::fmt_metric_value(char* buf, size_t bufsz,
                                       double value, const std::string& unit)
{
    if (unit == "bytes" || unit == "bytes/s") {
        const char* suffix = unit == "bytes/s" ? "/s" : "";
        if (value >= 1024.0 * 1024.0 * 1024.0)
            std::snprintf(buf, bufsz, "%.2f GB%s", value / (1024.0*1024.0*1024.0), suffix);
        else if (value >= 1024.0 * 1024.0)
            std::snprintf(buf, bufsz, "%.2f MB%s", value / (1024.0*1024.0), suffix);
        else if (value >= 1024.0)
            std::snprintf(buf, bufsz, "%.1f KB%s", value / 1024.0, suffix);
        else
            std::snprintf(buf, bufsz, "%.0f B%s", value, suffix);
    } else if (value >= 1000000.0) {
        std::snprintf(buf, bufsz, "%.2fM %s", value / 1000000.0, unit.c_str());
    } else if (value >= 1000.0) {
        std::snprintf(buf, bufsz, "%.1fK %s", value / 1000.0, unit.c_str());
    } else {
        std::snprintf(buf, bufsz, "%.1f %s", value, unit.c_str());
    }
}

// ============================================================
//  set_*
// ============================================================
void ChartPanelView::set_store(const MetricStore* store) {
    store_            = store;
    axis_initialized_ = false;
    chart_states_.clear();
    crosshair_x_      = std::numeric_limits<double>::quiet_NaN();
    hover_time_ms_    = -1;
    if (store_) {
        x_min_ = ms_to_plot(store_->time_start_ms);
        x_max_ = ms_to_plot(store_->time_end_ms);
        x_view_min_ = x_min_;
        x_view_max_ = x_max_;
    }
}

void ChartPanelView::set_selected_metrics(const std::unordered_set<std::string>* sel) {
    selected_ = sel;
}

void ChartPanelView::set_log_data(const std::vector<const LogEntry*>* log_entries,
                                   const StringTable*                  log_strings) {
    log_entries_ = log_entries;
    log_strings_ = log_strings;
}

void ChartPanelView::set_filter(FilterState* filter) {
    filter_ = filter;
}

void ChartPanelView::set_dashboard_groups(const std::vector<DashboardInfo>* groups) {
    dashboard_groups_ = groups;
}

void ChartPanelView::set_custom_metrics(const std::unordered_set<std::string>* custom) {
    custom_metrics_ = custom;
}

// ============================================================
//  render_minimap
// ============================================================
void ChartPanelView::render_minimap(float width, float height) {
    if (!store_ || store_->empty()) return;

    // Find a representative metric for the minimap (first selected)
    const MetricSeries* ms = nullptr;
    if (selected_) {
        for (const auto& path : *selected_) {
            ms = store_->get(path);
            if (ms && !ms->empty()) break;
        }
    }
    if (!ms) return;

    ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(4, 2));

    if (ImPlot::BeginPlot("##minimap", ImVec2(width, height),
        ImPlotFlags_NoLegend | ImPlotFlags_NoMenus |
        ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText |
        ImPlotFlags_NoFrame))
    {
        // Setup axes
        ImPlot::SetupAxis(ImAxis_X1, nullptr,
            ImPlotAxisFlags_NoDecorations | ImPlotAxisFlags_NoHighlight);
        ImPlot::SetupAxis(ImAxis_Y1, nullptr,
            ImPlotAxisFlags_NoDecorations | ImPlotAxisFlags_NoHighlight |
            ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisLimits(ImAxis_X1, x_min_, x_max_, ImGuiCond_Always);

        // Draw thin lines for all selected metrics
        if (selected_) {
            for (const auto& path : *selected_) {
                const MetricSeries* s = store_->get(path);
                if (!s || s->empty()) continue;

                const auto& values = (s->is_cumulative && !s->rate.empty())
                                     ? s->rate : s->values;
                size_t n = std::min(s->timestamps_ms.size(), values.size());
                if (n == 0) continue;

                std::vector<double> xs(n), ys(n);
                for (size_t i = 0; i < n; ++i) {
                    xs[i] = ms_to_plot(s->timestamps_ms[i]);
                    ys[i] = values[i];
                }

                ImPlot::SetNextLineStyle(IMPLOT_AUTO_COL, 0.5f);
                ImPlot::PlotLine(path.c_str(), xs.data(), ys.data(),
                                 static_cast<int>(n));
            }
        }

        // Draw the current view window as a highlighted region
        if (x_view_min_ > x_min_ || x_view_max_ < x_max_) {
            // Dim regions outside the view
            double dim_x_l[] = {x_min_, x_view_min_};
            double dim_y_lo[] = {-1e18, -1e18};
            double dim_y_hi[] = { 1e18,  1e18};
            if (x_view_min_ > x_min_) {
                ImPlot::SetNextFillStyle(ImVec4(0.0f, 0.0f, 0.0f, 0.5f));
                ImPlot::PlotShaded("##dim_l", dim_x_l, dim_y_lo, dim_y_hi, 2);
            }
            double dim_x_r[] = {x_view_max_, x_max_};
            if (x_view_max_ < x_max_) {
                ImPlot::SetNextFillStyle(ImVec4(0.0f, 0.0f, 0.0f, 0.5f));
                ImPlot::PlotShaded("##dim_r", dim_x_r, dim_y_lo, dim_y_hi, 2);
            }
        }

        ImPlot::EndPlot();
    }
    ImPlot::PopStyleVar();
}

// ============================================================
//  render_stats_row
// ============================================================
void ChartPanelView::render_stats_row(const MetricSeries& series,
                                       bool use_rate,
                                       int64_t t0_ms,
                                       int64_t t1_ms)
{
    const auto& values = use_rate ? series.rate : series.values;
    if (values.empty()) return;
    if (use_rate && series.timestamps_ms.size() < 2) return;

    const auto& times  = use_rate
        ? std::vector<int64_t>(series.timestamps_ms.begin() + 1,
                               series.timestamps_ms.end())
        : series.timestamps_ms;

    if (times.empty()) return;

    WindowStats ws = FtdcAnalyzer::compute_window_stats(times, values, t0_ms, t1_ms);
    if (!ws.valid) return;

    const std::string& unit = series.unit;
    char min_s[32], avg_s[32], max_s[32], p99_s[32];
    fmt_metric_value(min_s, sizeof(min_s), ws.min, unit);
    fmt_metric_value(avg_s, sizeof(avg_s), ws.avg, unit);
    fmt_metric_value(max_s, sizeof(max_s), ws.max, unit);
    fmt_metric_value(p99_s, sizeof(p99_s), ws.p99, unit);

    ImGui::PushStyleColor(ImGuiCol_Text, COL_STATS);
    ImGui::Text("  min: %s  avg: %s  max: %s  p99: %s", min_s, avg_s, max_s, p99_s);
    ImGui::PopStyleColor();
}

// ============================================================
//  render_annotation_markers
// ============================================================
void ChartPanelView::render_annotation_markers(double x_min, double x_max) {
    if (!log_entries_ || !log_strings_) return;

    // Batch annotation X values by severity to avoid ImPlot ID conflicts (#38).
    // Only show Error/Warning level events to avoid noise (#18).
    std::vector<double> err_xs, warn_xs;
    for (const auto* e : *log_entries_) {
        if (e->severity > Severity::Warning) continue; // skip Info/Debug
        double ex = ms_to_plot(e->timestamp_ms);
        if (ex < x_min || ex > x_max) continue;
        if (e->severity <= Severity::Error)
            err_xs.push_back(ex);
        else
            warn_xs.push_back(ex);
    }

    if (!err_xs.empty()) {
        ImPlot::SetNextLineStyle(COL_ANNOTATION_ERR, 1.0f);
        ImPlot::PlotInfLines("##ann_err", err_xs.data(),
                             static_cast<int>(err_xs.size()));
    }
    if (!warn_xs.empty()) {
        ImPlot::SetNextLineStyle(COL_ANNOTATION_WARN, 1.0f);
        ImPlot::PlotInfLines("##ann_warn", warn_xs.data(),
                             static_cast<int>(warn_xs.size()));
    }
}

// ============================================================
//  render_chart
// ============================================================
void ChartPanelView::render_chart(const MetricSeries& series,
                                   ChartState& state,
                                   float width,
                                   float chart_h)
{
    bool use_rate = series.is_cumulative && !series.rate.empty()
                    && state.show_rate && series.timestamps_ms.size() >= 2;
    const auto& values = use_rate ? series.rate : series.values;
    if (values.empty()) return;

    // The rate series has one fewer point; align timestamps
    std::vector<int64_t> ts_for_values;
    if (use_rate) {
        ts_for_values.assign(series.timestamps_ms.begin() + 1,
                              series.timestamps_ms.end());
    }
    const auto& ts = use_rate ? ts_for_values : series.timestamps_ms;

    size_t n = std::min(ts.size(), values.size());

    // LTTB downsample for rendering
    std::vector<size_t> indices = FtdcAnalyzer::lttb_downsample(values, MAX_PLOT_PTS);
    std::vector<double> plot_x, plot_y;
    plot_x.reserve(indices.size());
    plot_y.reserve(indices.size());
    for (size_t idx : indices) {
        if (idx < n) {
            plot_x.push_back(ms_to_plot(ts[idx]));
            plot_y.push_back(values[idx]);
        }
    }

    // Chart title with controls
    std::string title = series.display_name;
    if (use_rate) title += " (rate)";

    // Draw rate/raw toggle + log scale toggle before the chart
    ImGui::PushID(series.path.c_str());
    if (series.is_cumulative) {
        if (ImGui::SmallButton(state.show_rate ? "rate" : "raw")) {
            state.show_rate = !state.show_rate;
        }
        ImGui::SameLine();
    }
    if (ImGui::SmallButton(state.log_scale ? "log" : "lin")) {
        state.log_scale = !state.log_scale;
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(title.c_str());

    // Compute threshold for this series
    double thresh = metric_threshold(series.path);
    bool   has_threshold = !std::isnan(thresh);

    // ImPlot flags — NoChild prevents ImPlot from capturing scroll (we handle
    // it manually: Ctrl+scroll = zoom, plain scroll = scroll chart list).
    // NoBoxSelect because we implement our own drag-to-zoom.
    // Axis limits are forced every frame via ImGuiCond_Always, so ImPlot's
    // built-in scroll zoom is effectively overridden.
    ImPlotFlags plot_flags = ImPlotFlags_NoLegend | ImPlotFlags_NoMenus
                           | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoChild;
    ImPlotAxisFlags y_flags = ImPlotAxisFlags_AutoFit;

    if (ImPlot::BeginPlot(series.path.c_str(), ImVec2(width, chart_h), plot_flags))
    {
        // X axis — linked across all charts
        ImPlot::SetupAxis(ImAxis_X1, nullptr,
            ImPlotAxisFlags_NoDecorations | ImPlotAxisFlags_NoHighlight);
        ImPlot::SetupAxisLimits(ImAxis_X1, x_view_min_, x_view_max_, ImGuiCond_Always);

        // Y axis — auto-fit, optional log scale
        ImPlot::SetupAxis(ImAxis_Y1, series.unit.c_str(), y_flags);
        if (state.log_scale)
            ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);

        // Threshold highlight band
        if (has_threshold && !plot_x.empty()) {
            // Find max Y in current view for the band top
            double band_top = *std::max_element(plot_y.begin(), plot_y.end()) * 1.5;
            if (band_top < thresh * 2.0) band_top = thresh * 2.0;

            // Draw shaded rect above threshold
            std::vector<double> band_x = {plot_x.front(), plot_x.back()};
            std::vector<double> band_lo = {thresh, thresh};
            std::vector<double> band_hi = {band_top, band_top};

            ImPlot::SetNextFillStyle(ImVec4(1.0f, 0.2f, 0.2f, 0.15f));
            ImPlot::PlotShaded("##thresh",
                               band_x.data(), band_lo.data(), band_hi.data(),
                               static_cast<int>(band_x.size()));

            // Threshold line
            ImPlot::SetNextLineStyle(ImVec4(1.0f, 0.4f, 0.4f, 0.8f), 1.0f);
            ImPlot::PlotInfLines("##thresh_line", &thresh, 1,
                                 ImPlotInfLinesFlags_Horizontal);
        }

        // Main data line
        if (!plot_x.empty()) {
            ImPlot::SetNextLineStyle(IMPLOT_AUTO_COL, 1.5f);
            ImPlot::PlotLine(series.display_name.c_str(),
                             plot_x.data(), plot_y.data(),
                             static_cast<int>(plot_x.size()));
        }

        // Log event annotation markers
        ImPlotRect limits = ImPlot::GetPlotLimits();
        render_annotation_markers(limits.X.Min, limits.X.Max);

        // Crosshair — shared vertical line across all charts
        if (!std::isnan(crosshair_x_)) {
            ImPlot::SetNextLineStyle(COL_CROSSHAIR, 1.0f);
            ImPlot::PlotInfLines("##crosshair", &crosshair_x_, 1);
        }

        // ---- Mouse interaction (manual, since NoInputs is set) ----
        bool plot_hovered = ImPlot::IsPlotHovered();
        if (plot_hovered) {
            crosshair_x_  = ImPlot::GetPlotMousePos().x;
            hover_time_ms_ = plot_to_ms(crosshair_x_);

            // Tooltip: show value at hover time
            if (!dragging_) {
                ImGui::BeginTooltip();
                size_t hover_idx = FtdcAnalyzer::find_sample_at(ts,
                    plot_to_ms(crosshair_x_));
                if (hover_idx < values.size()) {
                    char val_buf[64];
                    fmt_metric_value(val_buf, sizeof(val_buf),
                                     values[hover_idx], series.unit);
                    ImGui::Text("%s: %s", series.display_name.c_str(), val_buf);
                }
                ImGui::EndTooltip();
            }

            // Start drag on left mouse down
            if (ImGui::IsMouseClicked(0)) {
                dragging_        = true;
                drag_start_x_   = crosshair_x_;
                drag_start_px_x_= ImGui::GetMousePos().x;
            }
        }

        // Draw drag highlight band (across all charts via shared state)
        if (dragging_) {
            // Get current mouse X in this plot's coordinate space
            double cur_x = ImPlot::GetPlotMousePos().x;
            double lo = std::min(drag_start_x_, cur_x);
            double hi = std::max(drag_start_x_, cur_x);

            // Draw semi-transparent blue band
            ImPlotRect lims = ImPlot::GetPlotLimits();
            double band_xs[] = {lo, hi};
            double band_lo[] = {lims.Y.Min, lims.Y.Min};
            double band_hi[] = {lims.Y.Max, lims.Y.Max};
            ImPlot::SetNextFillStyle(ImVec4(0.3f, 0.5f, 1.0f, 0.25f));
            ImPlot::PlotShaded("##drag_sel", band_xs, band_lo, band_hi, 2);

            // Vertical lines at drag edges
            ImPlot::SetNextLineStyle(ImVec4(0.4f, 0.6f, 1.0f, 0.8f), 1.0f);
            ImPlot::PlotInfLines("##drag_lo", &lo, 1);
            ImPlot::PlotInfLines("##drag_hi", &hi, 1);

            // Capture end position while still inside BeginPlot/EndPlot
            // where GetPlotMousePos() is valid
            if (ImGui::IsMouseReleased(0) && !drag_committed_) {
                drag_committed_ = true;
                drag_end_x_     = cur_x;
                drag_end_px_x_  = ImGui::GetMousePos().x;
            }
        }

        ImPlot::EndPlot();
    }

    // Stats row below chart (computed over the current view window)
    int64_t t0 = plot_to_ms(x_view_min_);
    int64_t t1 = plot_to_ms(x_view_max_);
    render_stats_row(series, use_rate, t0, t1);

    ImGui::PopID();
    ImGui::Spacing();
}

// ============================================================
//  render_inner
// ============================================================
void ChartPanelView::render_inner() {
    if (!store_ || store_->empty()) {
        ImGui::TextDisabled("No FTDC data loaded.");
        return;
    }

    if (!selected_ || selected_->empty()) {
        ImGui::TextDisabled("Select metrics from the left panel.");
        return;
    }

    // Initialize axis limits on first render or after new data
    if (!axis_initialized_) {
        x_min_ = ms_to_plot(store_->time_start_ms);
        x_max_ = ms_to_plot(store_->time_end_ms);
        x_view_min_ = x_min_;
        x_view_max_ = x_max_;
        axis_initialized_ = true;
    }

    float avail_w = ImGui::GetContentRegionAvail().x;

    // ---- Ctrl+Scroll zoom handling ----
    // When Ctrl is held, scroll wheel zooms the X axis centered on the mouse.
    // Without Ctrl, scroll wheel scrolls the chart list (default ImGui behavior).
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
        ImGuiIO& io = ImGui::GetIO();
        bool ctrl = io.KeyCtrl;
        float wheel = io.MouseWheel;

        if (ctrl && wheel != 0.0f) {
            // Consume the scroll event so it doesn't scroll the parent
            io.MouseWheel = 0.0f;

            double range = x_view_max_ - x_view_min_;
            if (range <= 0.0) range = 1.0; // guard against division by zero
            double zoom_factor = (wheel > 0.0f) ? 0.85 : 1.0 / 0.85;

            double center = (x_view_min_ + x_view_max_) * 0.5;

            double new_range = range * zoom_factor;
            double full_range = x_max_ - x_min_;
            if (full_range <= 0.0) full_range = 1.0;
            if (new_range < 10.0) new_range = 10.0;
            if (new_range > full_range) new_range = full_range;

            double frac = (center - x_view_min_) / range;
            x_view_min_ = center - new_range * frac;
            x_view_max_ = center + new_range * (1.0 - frac);

            // Clamp to data bounds
            if (x_view_min_ < x_min_) { x_view_max_ += (x_min_ - x_view_min_); x_view_min_ = x_min_; }
            if (x_view_max_ > x_max_) { x_view_min_ -= (x_view_max_ - x_max_); x_view_max_ = x_max_; }
            if (x_view_min_ < x_min_) x_view_min_ = x_min_;
        }
    }

    // Minimap + reset button only shown when zoomed in
    bool zoomed = (x_view_min_ > x_min_ + 0.1 || x_view_max_ < x_max_ - 0.1);
    if (zoomed) {
        ImGui::TextDisabled("Overview");
        render_minimap(avail_w - 8.0f, MINIMAP_HEIGHT);
        ImGui::Spacing();
    }

    if (zoomed) {
        if (ImGui::SmallButton("Reset zoom")) {
            x_view_min_ = x_min_;
            x_view_max_ = x_max_;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Ctrl+Scroll to zoom");
        ImGui::Spacing();
    }

    // "Clear time window" button if active
    if (filter_ && filter_->time_window_active) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("Clear time filter")) {
            filter_->time_window_active = false;
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled("(±30s window active in Logs tab)");
        ImGui::Spacing();
    }

    // ---- Grouped chart rendering ----
    bool any_plot_hovered = false;

    // If we have dashboard groups, render charts by group
    if (dashboard_groups_ && !dashboard_groups_->empty()) {
        for (const auto& [group_name, group_paths] : *dashboard_groups_) {
            if (group_paths.empty()) continue;

            // Count how many metrics in this group actually have data
            int data_count = 0;
            for (const auto& path : group_paths) {
                const MetricSeries* ms = store_->get(path);
                if (ms && !ms->empty()) ++data_count;
            }
            if (data_count == 0) continue;

            ImGui::PushID(group_name.c_str());

            // Collapsed state for this group (default: expanded)
            auto [it, inserted] = group_collapsed_.try_emplace(group_name, false);
            bool& collapsed = it->second;

            // Category header — styled as a collapsible section
            ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.12f, 0.12f, 0.16f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered,  ImVec4(0.18f, 0.18f, 0.24f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive,   ImVec4(0.15f, 0.15f, 0.20f, 1.0f));

            // CollapsingHeader returns true when OPEN
            bool header_open = ImGui::CollapsingHeader(group_name.c_str(),
                collapsed ? ImGuiTreeNodeFlags_None : ImGuiTreeNodeFlags_DefaultOpen);
            collapsed = !header_open;

            ImGui::PopStyleColor(3);

            if (header_open) {
                // Render each chart in this group
                for (const auto& path : group_paths) {
                    const MetricSeries* ms = store_->get(path);
                    if (!ms || ms->empty()) continue;

                    auto cit = chart_states_.find(path);
                    if (cit == chart_states_.end()) {
                        ChartState cs;
                        cs.show_rate = ms->is_cumulative;
                        cit = chart_states_.emplace(path, cs).first;
                    }

                    render_chart(*ms, cit->second, avail_w - 8.0f, CHART_HEIGHT);
                    if (!std::isnan(crosshair_x_)) any_plot_hovered = true;
                }
            }

            ImGui::PopID();
            ImGui::Spacing();
        }
    }

    // Render "Custom" group for individually-selected metrics (per D-18)
    if (custom_metrics_ && !custom_metrics_->empty()) {
        int custom_data_count = 0;
        for (const auto& path : *custom_metrics_) {
            const MetricSeries* ms = store_->get(path);
            if (ms && !ms->empty()) ++custom_data_count;
        }

        if (custom_data_count > 0) {
            ImGui::PushID("##custom_group");

            auto [it, inserted] = group_collapsed_.try_emplace("Custom", false);
            bool& collapsed = it->second;

            ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.16f, 0.10f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered,  ImVec4(0.22f, 0.14f, 0.28f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive,   ImVec4(0.18f, 0.12f, 0.24f, 1.0f));

            bool header_open = ImGui::CollapsingHeader("Custom",
                collapsed ? ImGuiTreeNodeFlags_None : ImGuiTreeNodeFlags_DefaultOpen);
            collapsed = !header_open;

            ImGui::PopStyleColor(3);

            if (header_open) {
                for (const auto& path : *custom_metrics_) {
                    const MetricSeries* ms = store_->get(path);
                    if (!ms || ms->empty()) continue;

                    auto cit = chart_states_.find(path);
                    if (cit == chart_states_.end()) {
                        ChartState cs;
                        cs.show_rate = ms->is_cumulative;
                        cit = chart_states_.emplace(path, cs).first;
                    }

                    render_chart(*ms, cit->second, avail_w - 8.0f, CHART_HEIGHT);
                    if (!std::isnan(crosshair_x_)) any_plot_hovered = true;
                }
            }

            ImGui::PopID();
            ImGui::Spacing();
        }
    }

    // Fallback: if no groups are set, render flat (backward compat)
    if ((!dashboard_groups_ || dashboard_groups_->empty()) &&
        (!custom_metrics_ || custom_metrics_->empty())) {
        for (const auto& path : *selected_) {
            const MetricSeries* ms = store_->get(path);
            if (!ms || ms->empty()) continue;

            auto it = chart_states_.find(path);
            if (it == chart_states_.end()) {
                ChartState cs;
                cs.show_rate = ms->is_cumulative;
                it = chart_states_.emplace(path, cs).first;
            }

            render_chart(*ms, it->second, avail_w - 8.0f, CHART_HEIGHT);
            if (!std::isnan(crosshair_x_)) any_plot_hovered = true;
        }
    }

    // Apply drag-to-zoom after all charts have rendered
    if (drag_committed_) {
        drag_committed_ = false;
        dragging_       = false;
        float px_moved  = std::abs(drag_end_px_x_ - drag_start_px_x_);

        if (px_moved > 5.0f) {
            double lo = std::min(drag_start_x_, drag_end_x_);
            double hi = std::max(drag_start_x_, drag_end_x_);
            if (hi - lo > 1.0) {
                x_view_min_ = std::max(lo, x_min_);
                x_view_max_ = std::min(hi, x_max_);
            }
        } else {
            // Quick click: set ±30s time window filter for cross-view linking
            if (filter_) {
                int64_t click_ms = plot_to_ms(drag_end_x_);
                filter_->time_window_active   = true;
                filter_->time_window_start_ms = click_ms - 30000LL;
                filter_->time_window_end_ms   = click_ms + 30000LL;
                if (on_time_click_) on_time_click_(click_ms);
            }
        }
    }

    // Cancel drag if mouse released outside all plots
    if (dragging_ && !ImGui::IsMouseDown(0)) {
        dragging_ = false;
    }

    // Reset crosshair only when no plot is hovered
    if (!any_plot_hovered) {
        crosshair_x_  = std::numeric_limits<double>::quiet_NaN();
        hover_time_ms_= -1;
    }
}
