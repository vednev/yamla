#include "chart_panel_view.hpp"

#include <imgui.h>
#include <implot.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <ctime>

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
//  unit_display_config — per-unit Y-axis display strategy.
//  Follows Grafana/PMM conventions for MongoDB metrics.
// ============================================================
UnitDisplayConfig unit_display_config(const std::string& unit) {
    // defaults: anchor at zero, 10% padding, no log, no hard max
    constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

    // Byte sizes (absolute)
    if (unit == "bytes")   return { true, 0.10, false, NaN };
    if (unit == "bytes/s") return { true, 0.10, false, NaN };
    if (unit == "KB")      return { true, 0.10, false, NaN };
    if (unit == "MB")      return { true, 0.10, false, NaN };

    // Durations — slightly more padding for threshold bands
    if (unit == "ms")      return { true, 0.15, true,  NaN };
    if (unit == "s")       return { false, 0.05, false, NaN }; // uptime floats

    // Latency — long-tailed, log-eligible
    if (unit == "us")      return { true, 0.10, true,  NaN };

    // Time-rates (CPU time, sync duration per second)
    if (unit == "ms/s")    return { true, 0.10, true,  NaN };
    if (unit == "us/s")    return { true, 0.10, true,  NaN };

    // Percentage
    if (unit == "%")       return { true, 0.05, false, 100.0 };

    // Gauge counts (connections, tickets, queue depths, dhandles, sessions)
    if (unit == "count")   return { true, 0.10, false, NaN };

    // All throughput rates: ops/s, docs/s, txns/s, pages/s, conns/s,
    // req/s, count/s, conflicts/s, records/s, sectors/s
    // — anchor at zero, eligible for log if wide range
    return { true, 0.10, true, NaN };
}

// ============================================================
//  format_value_with_unit — shared formatting core.
//  Writes a human-readable representation of `value` with the
//  given unit into `buf`.
//
//  `decimals`: 1 for axis ticks (compact), 2 for tooltips (precise).
//  `show_unit`: false for Y-axis ticks (unit is in chart title),
//               true for tooltips / stats rows.
// ============================================================
static int format_value_with_unit(double value, char* buf, int size,
                                  const char* unit, int decimals,
                                  bool show_unit)
{
    size_t sz = static_cast<size_t>(size);
    if (value < 0.0) {
        char tmp[64];
        format_value_with_unit(-value, tmp, sizeof(tmp), unit, decimals, show_unit);
        return std::snprintf(buf, sz, "-%s", tmp);
    }
    if (value == 0.0)
        return std::snprintf(buf, sz, "0");

    std::string u(unit ? unit : "");

    // Helper: append unit label only when show_unit is true.
    // For axis ticks the unit is shown once in the chart title.
    auto sfx = [&](const char* label) -> const char* {
        return show_unit ? label : "";
    };

    // ---- IEC byte sizes ----
    if (u == "bytes" || u == "bytes/s") {
        const char* rs = (u == "bytes/s" && show_unit) ? "/s" : "";
        if (value >= 1024.0 * 1024.0 * 1024.0)
            return std::snprintf(buf, sz, "%.*f%s%s", decimals, value / (1024.0*1024.0*1024.0), sfx(" GB"), rs);
        if (value >= 1024.0 * 1024.0)
            return std::snprintf(buf, sz, "%.*f%s%s", decimals, value / (1024.0*1024.0), sfx(" MB"), rs);
        if (value >= 1024.0)
            return std::snprintf(buf, sz, "%.*f%s%s", decimals, value / 1024.0, sfx(" KB"), rs);
        return std::snprintf(buf, sz, "%.0f%s%s", value, sfx(" B"), rs);
    }

    // ---- KB → MB → GB ----
    if (u == "KB") {
        if (value >= 1048576.0)
            return std::snprintf(buf, sz, "%.*f%s", decimals, value / 1048576.0, sfx(" GB"));
        if (value >= 1024.0)
            return std::snprintf(buf, sz, "%.*f%s", decimals, value / 1024.0, sfx(" MB"));
        return std::snprintf(buf, sz, "%.0f%s", value, sfx(" KB"));
    }
    // ---- MB → GB ----
    if (u == "MB") {
        if (value >= 1024.0)
            return std::snprintf(buf, sz, "%.*f%s", decimals, value / 1024.0, sfx(" GB"));
        return std::snprintf(buf, sz, "%.*f%s", decimals, value, sfx(" MB"));
    }

    // ---- Milliseconds (duration): ms → s → min ----
    if (u == "ms") {
        if (value >= 60000.0)
            return std::snprintf(buf, sz, "%.*f%s", decimals, value / 60000.0, sfx(" min"));
        if (value >= 1000.0)
            return std::snprintf(buf, sz, "%.*f%s", decimals, value / 1000.0, sfx(" s"));
        return std::snprintf(buf, sz, "%.*f%s", decimals, value, sfx(" ms"));
    }

    // ---- Microseconds (latency): us → ms → s ----
    if (u == "us") {
        if (value >= 1000000.0)
            return std::snprintf(buf, sz, "%.*f%s", decimals, value / 1000000.0, sfx(" s"));
        if (value >= 1000.0)
            return std::snprintf(buf, sz, "%.*f%s", decimals, value / 1000.0, sfx(" ms"));
        return std::snprintf(buf, sz, "%.0f%s", value, sfx(" us"));
    }

    // ---- Seconds (uptime): s → min → hr → d ----
    if (u == "s") {
        if (value >= 86400.0)
            return std::snprintf(buf, sz, "%.*f%s", decimals, value / 86400.0, sfx(" d"));
        if (value >= 3600.0)
            return std::snprintf(buf, sz, "%.*f%s", decimals, value / 3600.0, sfx(" hr"));
        if (value >= 60.0)
            return std::snprintf(buf, sz, "%.*f%s", decimals, value / 60.0, sfx(" min"));
        return std::snprintf(buf, sz, "%.*f%s", decimals, value, sfx(" s"));
    }

    // ---- Time-rates: ms/s and us/s ----
    // These mean "milliseconds (or microseconds) of X per second of wall
    // time."  Use SI prefixes on the raw number; unit label is in the
    // chart title for axis ticks.
    if (u == "ms/s" || u == "us/s") {
        const char* ulbl = (u == "ms/s") ? " ms/s" : " us/s";
        if (value >= 1000000.0)
            return std::snprintf(buf, sz, "%.*fM%s", decimals, value / 1000000.0, sfx(ulbl));
        if (value >= 1000.0)
            return std::snprintf(buf, sz, "%.*fK%s", decimals, value / 1000.0, sfx(ulbl));
        if (u == "us/s")
            return std::snprintf(buf, sz, "%.0f%s", value, sfx(ulbl));
        return std::snprintf(buf, sz, "%.*f%s", decimals, value, sfx(ulbl));
    }

    // ---- Percentage ----
    if (u == "%")
        return std::snprintf(buf, sz, "%.*f%s", decimals, value, sfx("%"));

    // ---- Generic SI suffixes for everything else (ops/s, count, etc.) ----
    // For tooltips, append the unit after the number.
    const char* unit_sfx = (show_unit && !u.empty()) ? u.c_str() : "";
    char unit_buf[32] = {};
    if (show_unit && !u.empty())
        std::snprintf(unit_buf, sizeof(unit_buf), " %s", unit_sfx);

    if (value >= 1000000.0)
        return std::snprintf(buf, sz, "%.*fM%s", decimals, value / 1000000.0, unit_buf);
    if (value >= 1000.0)
        return std::snprintf(buf, sz, "%.*fK%s", decimals, value / 1000.0, unit_buf);
    if (value < 1.0)
        return std::snprintf(buf, sz, "%.2f%s", value, unit_buf);
    // Integer-like values: avoid ".0" for clean tick labels
    if (value == std::floor(value) && value < 10000.0)
        return std::snprintf(buf, sz, "%.0f%s", value, unit_buf);
    return std::snprintf(buf, sz, "%.*f%s", decimals, value, unit_buf);
}

// ============================================================
//  ImPlot Y-axis formatter callback.
//  user_data points to a C string (the unit).
//  show_unit=false — the unit is shown once in the chart title.
// ============================================================
static int y_axis_formatter(double value, char* buf, int size, void* user_data) {
    return format_value_with_unit(value, buf, size,
                                  static_cast<const char*>(user_data),
                                  1, false);
}

// ============================================================
//  fmt_metric_value — public formatting for tooltips and stats.
//  show_unit=true — include the unit for context.
// ============================================================
void ChartPanelView::fmt_metric_value(char* buf, size_t bufsz,
                                       double value, const std::string& unit)
{
    format_value_with_unit(value, static_cast<char*>(buf),
                           static_cast<int>(bufsz), unit.c_str(),
                           2, true);
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
        // Setup axes — X has time labels for orientation, Y is hidden
        ImPlot::SetupAxis(ImAxis_X1, nullptr,
            ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoHighlight);
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
        ImPlot::SetupAxis(ImAxis_Y1, nullptr,
            ImPlotAxisFlags_NoDecorations | ImPlotAxisFlags_NoHighlight |
            ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisLimits(ImAxis_X1, x_min_, x_max_, ImGuiCond_Always);

        // Draw thin lines for all selected metrics.
        // Reuse a single pair of vectors to avoid per-metric allocations.
        if (selected_) {
            std::vector<double> xs, ys;
            for (const auto& path : *selected_) {
                const MetricSeries* s = store_->get(path);
                if (!s || s->empty()) continue;

                const auto& values = (s->is_cumulative && !s->rate.empty())
                                     ? s->rate : s->values;
                size_t n = std::min(s->timestamps_ms.size(), values.size());
                if (n == 0) continue;

                xs.resize(n); ys.resize(n);
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
    // Cumulative metrics always show rate — no toggle needed
    bool use_rate = series.is_cumulative && !series.rate.empty()
                    && series.timestamps_ms.size() >= 2;
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

    // ---- Compute Y-axis min/max from FULL data (not downsampled) ----
    // Using the original values array ensures we don't miss true extremes
    // that LTTB may have dropped.
    double y_data_min =  std::numeric_limits<double>::max();
    double y_data_max = -std::numeric_limits<double>::max();
    for (size_t i = 0; i < n; ++i) {
        double v = values[i];
        if (v < y_data_min) y_data_min = v;
        if (v > y_data_max) y_data_max = v;
    }
    if (y_data_min > y_data_max) {
        y_data_min = 0.0;
        y_data_max = 1.0;
    }

    // Look up unit-aware display config
    UnitDisplayConfig ucfg = unit_display_config(series.unit);

    // Auto-detect log scale (once per metric, on first data scan)
    if (!state.initialized) {
        state.show_rate = use_rate;
        state.log_scale = false;
        if (ucfg.auto_log_eligible) {
            double pos_min = std::numeric_limits<double>::max();
            double pos_max = 0.0;
            for (size_t i = 0; i < n; ++i) {
                double v = values[i];
                if (v > 0.0) {
                    if (v < pos_min) pos_min = v;
                    if (v > pos_max) pos_max = v;
                }
            }
            state.log_scale = (pos_min > 0.0 && pos_max > 0.0
                               && pos_max / pos_min > 1000.0);
        }
        state.initialized = true;
    }

    // ---- Compute padded Y limits using unit-aware strategy ----
    double y_range = y_data_max - y_data_min;
    double y_pad   = (y_range > 0.0) ? y_range * ucfg.y_pad_fraction : 1.0;
    double y_lo, y_hi;

    // Constant data: show a meaningful range around the value
    if (y_range == 0.0) {
        double val = y_data_min;
        if (val == 0.0) {
            y_lo = 0.0;
            y_hi = 1.0;
        } else {
            double spread = std::abs(val) * 0.10;
            y_lo = val - spread;
            y_hi = val + spread;
        }
    } else if (ucfg.anchor_at_zero && y_data_min >= 0.0) {
        // Anchor at zero: Y starts at 0, pad above max
        y_lo = 0.0;
        y_hi = y_data_max + y_pad;
    } else {
        // Floating axis: pad both sides
        y_lo = y_data_min - y_pad;
        y_hi = y_data_max + y_pad;
    }

    // Apply hard Y max (e.g., 100 for percentages)
    if (!std::isnan(ucfg.hard_y_max) && y_hi > ucfg.hard_y_max)
        y_hi = ucfg.hard_y_max;

    // For non-negative units, never show negative Y values
    if (ucfg.anchor_at_zero && y_lo < 0.0)
        y_lo = 0.0;

    // For log scale, ensure strictly positive limits
    if (state.log_scale) {
        if (y_lo <= 0.0) y_lo = (y_data_min > 0.0) ? y_data_min * 0.5 : 0.1;
        if (y_hi <= y_lo) y_hi = y_lo * 10.0;
    }

    // Chart title: metric name + unit (unit shown here, not on Y-axis ticks).
    // Cumulative metrics already have rate units (ops/s, bytes/s) so no
    // need to append /s again.
    std::string title = series.display_name;
    if (!series.unit.empty())
        title += "  (" + series.unit + ")";

    ImGui::PushID(series.path.c_str());
    ImGui::BeginGroup(); // Group title+plot+stats so SameLine() treats as one unit
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
                           | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoChild
                           | ImPlotFlags_NoMouseText;

    if (ImPlot::BeginPlot(series.path.c_str(), ImVec2(width, chart_h), plot_flags))
    {
        // X axis — linked across all charts, time-formatted tick labels.
        // In narrow column mode, reduce tick density to prevent label
        // overflow beyond the plot frame.
        ImPlotAxisFlags x_flags = ImPlotAxisFlags_NoLabel
                                | ImPlotAxisFlags_NoHighlight;
        if (width < 350.0f) {
            // Very narrow charts: hide tick labels entirely to prevent
            // bleeding — time info is still on the first/wider charts
            x_flags |= ImPlotAxisFlags_NoTickLabels;
        }
        ImPlot::SetupAxis(ImAxis_X1, nullptr, x_flags);
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
        ImPlot::SetupAxisLimits(ImAxis_X1, x_view_min_, x_view_max_, ImGuiCond_Always);

        // Y axis — unit-aware padded limits, custom tick formatter.
        // ImGuiCond_Always to keep limits stable while scrolling
        // through multiple charts. SetupAxisLimitsConstraints prevents
        // nonsensical zoom/pan ranges for non-negative metrics.
        ImPlot::SetupAxis(ImAxis_Y1, nullptr, ImPlotAxisFlags_NoLabel);
        ImPlot::SetupAxisLimits(ImAxis_Y1, y_lo, y_hi, ImGuiCond_Always);
        if (ucfg.anchor_at_zero)
            ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0.0, INFINITY);
        ImPlot::SetupAxisFormat(ImAxis_Y1, y_axis_formatter,
                                const_cast<char*>(series.unit.c_str()));
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

        // Main data line — thicker for visibility (D-80)
        if (!plot_x.empty()) {
            ImPlot::SetNextLineStyle(IMPLOT_AUTO_COL, 2.0f);
            ImPlot::PlotLine(series.display_name.c_str(),
                             plot_x.data(), plot_y.data(),
                             static_cast<int>(plot_x.size()));

            // Area fill below the line at ~15% opacity (D-81, Grafana-style)
            ImVec4 line_color = ImPlot::GetLastItemColor();
            line_color.w = 0.15f;
            ImPlot::SetNextFillStyle(line_color);
            double shade_ref = y_lo;  // shade down to Y-axis bottom
            ImPlot::PlotShaded(series.display_name.c_str(),
                               plot_x.data(), plot_y.data(),
                               static_cast<int>(plot_x.size()),
                               shade_ref);
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

            // Tooltip: show timestamp + value at hover time
            if (!dragging_) {
                ImGui::BeginTooltip();
                // Format timestamp from epoch ms
                {
                    int64_t t_ms = plot_to_ms(crosshair_x_);
                    time_t  t_sec = static_cast<time_t>(t_ms / 1000);
                    int     t_frac_ms = static_cast<int>(t_ms % 1000);
                    struct tm tm_buf;
#if defined(_WIN32)
                    localtime_s(&tm_buf, &t_sec);
#else
                    localtime_r(&t_sec, &tm_buf);
#endif
                    char time_str[32];
                    std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_buf);
                    ImGui::TextDisabled("%s.%03d", time_str, t_frac_ms);
                }
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

    ImGui::EndGroup(); // End group started before title
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

    // Overview minimap — always visible for time orientation.
    // Shows aggregate metric activity + time axis labels + zoom window highlight.
    bool zoomed = (x_view_min_ > x_min_ + 0.1 || x_view_max_ < x_max_ - 0.1);
    render_minimap(avail_w - 8.0f, MINIMAP_HEIGHT);
    ImGui::Spacing();

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

    // ---- Layout mode toolbar (per D-25, D-26) ----
    // Resolve effective column count (per D-33: auto-detect from width)
    int effective_cols = layout_columns_;
    if (effective_cols == 0) {
        effective_cols = (avail_w > 1600.0f) ? 2 : 1;
    }
    // Fallback for very narrow windows: force list if columns < 100px each
    if (effective_cols >= 2) {
        float min_col_w = (avail_w - (effective_cols - 1) * 8.0f) / effective_cols;
        if (min_col_w < 100.0f) effective_cols = 1;
    }

    {
        bool is_grid = (effective_cols >= 2);

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 0));

        // List button — highlighted when active
        if (!is_grid) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.25f, 0.35f, 1.0f));
        if (ImGui::SmallButton("List")) {
            layout_columns_ = 1;
            effective_cols = 1;
        }
        if (!is_grid) ImGui::PopStyleColor();

        ImGui::SameLine();

        // Grid button — highlighted when active
        if (is_grid) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.25f, 0.35f, 1.0f));
        if (ImGui::SmallButton("Grid")) {
            layout_columns_ = (layout_columns_ >= 2) ? layout_columns_ : 2;
            effective_cols = (layout_columns_ >= 2) ? layout_columns_ : 2;
        }
        if (is_grid) ImGui::PopStyleColor();

        // Auto button — returns to auto-detect mode (per WR-02)
        if (layout_columns_ != 0) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Auto")) {
                layout_columns_ = 0;
                effective_cols = (avail_w > 1600.0f) ? 2 : 1;
            }
        }

        // Column count selector — only visible in grid mode (per D-26)
        is_grid = (effective_cols >= 2);
        if (is_grid) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(50.0f);
            const char* col_labels[] = { "2", "3", "4" };
            int col_idx = effective_cols - 2;  // 0=2cols, 1=3cols, 2=4cols
            if (col_idx < 0) col_idx = 0;
            if (col_idx > 2) col_idx = 2;
            if (ImGui::Combo("##col_count", &col_idx, col_labels, 3)) {
                layout_columns_ = col_idx + 2;
                effective_cols = layout_columns_;
            }
        }

        ImGui::PopStyleVar(2);
    }
    ImGui::Spacing();

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
                // Column width per D-28
                float spacing = 8.0f;
                float chart_width = (effective_cols >= 2)
                    ? (avail_w - (effective_cols - 1) * spacing) / effective_cols
                    : avail_w - spacing;

                // Use ImGui::BeginTable for multi-column grid layout.
                // SameLine() doesn't work with ImPlot charts because
                // BeginPlot/EndPlot internally advances the cursor.
                bool use_table = (effective_cols >= 2);
                if (use_table) {
                    use_table = ImGui::BeginTable("##chart_grid", effective_cols,
                        ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_SizingFixedFit);
                    if (use_table) {
                        for (int c = 0; c < effective_cols; ++c) {
                            ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, chart_width);
                        }
                    }
                }

                int col_idx = 0;
                for (const auto& path : group_paths) {
                    const MetricSeries* ms = store_->get(path);
                    if (!ms || ms->empty()) continue;

                    auto cit = chart_states_.find(path);
                    if (cit == chart_states_.end()) {
                        ChartState cs;
                        cs.show_rate = ms->is_cumulative;
                        cit = chart_states_.emplace(path, cs).first;
                    }

                    if (use_table) {
                        if (col_idx == 0) ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(col_idx);
                    }

                    render_chart(*ms, cit->second, chart_width, CHART_HEIGHT);
                    if (!std::isnan(crosshair_x_)) any_plot_hovered = true;

                    col_idx++;
                    if (col_idx >= effective_cols) col_idx = 0;
                }

                if (use_table) {
                    ImGui::EndTable();
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
                float spacing = 8.0f;
                float chart_width = (effective_cols >= 2)
                    ? (avail_w - (effective_cols - 1) * spacing) / effective_cols
                    : avail_w - spacing;

                bool use_table = (effective_cols >= 2);
                if (use_table) {
                    use_table = ImGui::BeginTable("##custom_chart_grid", effective_cols,
                        ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_SizingFixedFit);
                    if (use_table) {
                        for (int c = 0; c < effective_cols; ++c) {
                            ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, chart_width);
                        }
                    }
                }

                int col_idx = 0;
                for (const auto& path : *custom_metrics_) {
                    const MetricSeries* ms = store_->get(path);
                    if (!ms || ms->empty()) continue;

                    auto cit = chart_states_.find(path);
                    if (cit == chart_states_.end()) {
                        ChartState cs;
                        cs.show_rate = ms->is_cumulative;
                        cit = chart_states_.emplace(path, cs).first;
                    }

                    if (use_table) {
                        if (col_idx == 0) ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(col_idx);
                    }

                    render_chart(*ms, cit->second, chart_width, CHART_HEIGHT);
                    if (!std::isnan(crosshair_x_)) any_plot_hovered = true;

                    col_idx++;
                    if (col_idx >= effective_cols) col_idx = 0;
                }

                if (use_table) {
                    ImGui::EndTable();
                }
            }

            ImGui::PopID();
            ImGui::Spacing();
        }
    }

    // Fallback: if no groups are set, render flat (backward compat)
    if ((!dashboard_groups_ || dashboard_groups_->empty()) &&
        (!custom_metrics_ || custom_metrics_->empty())) {
        float spacing = 8.0f;
        float chart_width = (effective_cols >= 2)
            ? (avail_w - (effective_cols - 1) * spacing) / effective_cols
            : avail_w - spacing;

        bool use_table = (effective_cols >= 2);
        if (use_table) {
            use_table = ImGui::BeginTable("##flat_chart_grid", effective_cols,
                ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_SizingFixedFit);
            if (use_table) {
                for (int c = 0; c < effective_cols; ++c) {
                    ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, chart_width);
                }
            }
        }

        int col_idx = 0;
        for (const auto& path : *selected_) {
            const MetricSeries* ms = store_->get(path);
            if (!ms || ms->empty()) continue;

            auto it = chart_states_.find(path);
            if (it == chart_states_.end()) {
                ChartState cs;
                cs.show_rate = ms->is_cumulative;
                it = chart_states_.emplace(path, cs).first;
            }

            if (use_table) {
                if (col_idx == 0) ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(col_idx);
            }

            render_chart(*ms, it->second, chart_width, CHART_HEIGHT);
            if (!std::isnan(crosshair_x_)) any_plot_hovered = true;

            col_idx++;
            if (col_idx >= effective_cols) col_idx = 0;
        }

        if (use_table) {
            ImGui::EndTable();
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
