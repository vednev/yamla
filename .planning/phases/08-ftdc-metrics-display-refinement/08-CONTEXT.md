# Phase 8: Refine FTDC Metrics Graphs and Display — Context

**Gathered:** 2026-04-14
**Status:** Ready for planning
**Depends on:** Phase 4 (Chart Layout Modes)

<domain>
## Phase Boundary

Comprehensive visual polish of FTDC metric charts. Improve chart line styling (thicker lines with area fill), tooltip formatting, threshold band visuals (gradient fill above threshold), stats row color-coding, and metric display name abbreviation. No new metrics or computed/derived metrics (deferred to backlog).

**NOT in scope:** Computed/derived metrics (backlog), new dashboard categories, parser changes, new metric definitions, chart interaction changes (zoom/pan/crosshair behavior unchanged).

</domain>

<decisions>
## Implementation Decisions

### Chart Line Styling
- **D-80:** Increase chart line width from 1px to 1.5-2px for better visibility on both list and grid modes.
- **D-81:** Add subtle semi-transparent area fill below the chart line (Grafana-style). Use the same line color at ~15-20% opacity for the fill. This makes charts much easier to read at a glance.

### Tooltip/Crosshair
- **D-82:** Tooltip shows: timestamp (formatted as `YYYY-MM-DD HH:MM:SS`) + value with unit (e.g., `1.5 GB`). Clean single-line format. Already partially working — refine the formatting and ensure consistency.

### Threshold Bands
- **D-83:** Replace the current threshold rendering with a gradient fill above the threshold value. Gradient goes from transparent at the threshold line to semi-transparent red at the top of the chart. This provides a dramatic but clear visual indicator of the danger zone.
- **D-84:** Keep a thin horizontal line at the threshold value as well (subtle, dashed or solid) for precise reference.

### Stats Row
- **D-85:** Stats row shows `Min | Avg | Max | p99` in a single horizontal line below each chart.
- **D-86:** Values are color-coded: green when within normal range (below threshold), yellow when approaching threshold (above 75% of threshold), red when above threshold. Metrics without a defined threshold default to white/normal text.

### Chart Titles / Display Names
- **D-87:** Abbreviate long metric display names to be more concise. E.g., "WT Cache Bytes Currently" instead of the full serverStatus path. Review all 134 metric definitions and shorten verbose names.
- **D-88:** Keep the unit in parentheses after the title: `"WT Cache Used  (bytes)"`.

### Scope Control
- **D-89:** Only modify `src/ui/chart_panel_view.cpp` (rendering changes), `src/ui/chart_panel_view.hpp` (if new constants needed), and `src/ftdc/metric_defs.hpp` (shortened display names).
- **D-90:** All 182 existing tests must continue to pass.

### Agent's Discretion
- Exact line width value (1.5 vs 2.0)
- Area fill opacity value (15% vs 20%)
- Gradient fill implementation (ImPlot shade or custom draw list)
- Exact color values for green/yellow/red stats coding
- Which display names to abbreviate and how
- Stats row font size (same as current or slightly smaller)

</decisions>

<canonical_refs>
## Canonical References

### Files to Modify
- `src/ui/chart_panel_view.cpp` — render_chart() for line styling, area fill, threshold gradient, stats row colors, tooltip
- `src/ui/chart_panel_view.hpp` — constants if needed
- `src/ftdc/metric_defs.hpp` — display_name strings for abbreviation

### Prior Context
- `.planning/phases/04-ftdc-chart-layout-modes/04-CONTEXT.md` — Chart layout decisions

</canonical_refs>

<deferred>
## Deferred Ideas

- **Computed/derived metrics** — cache fill %, dirty %, ticket utilization, replication lag (separate backlog item)
- **Color-coded chart lines by severity** — line color changes based on proximity to threshold
- **Trend indicators** — up/down arrows on chart titles
- **Sparkline previews in dashboard cards**

</deferred>

---

*Phase: 08-ftdc-metrics-display-refinement*
*Context gathered: 2026-04-14*
