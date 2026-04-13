# Phase 4: FTDC Chart Layout Modes — Context

**Gathered:** 2026-04-12
**Status:** Ready for planning
**Depends on:** Phase 3b (FTDC View UX Overhaul) — complete

<domain>
## Phase Boundary

Add list vs multi-column grid layout toggle to the FTDC chart panel. Users can switch between single-column stacked charts (current) and 2/3/4-column grid for side-by-side metric comparison. Persist preference. Auto-detect default from window width.

**NOT in scope:** Drag-and-drop chart reordering, chart resizing handles, per-group layout control, chart overlay/stacking (multiple metrics on one chart). These remain in backlog item 999.7.

</domain>

<decisions>
## Implementation Decisions

### Toggle UI
- **D-25:** Place a small toolbar row at the top of the chart panel (above minimap/charts). Contains: [List | Grid] toggle button + column count dropdown (2, 3, 4). Always visible when FTDC data is loaded.
- **D-26:** The toggle is a simple two-state button. Grid mode shows the column dropdown; list mode hides it.

### Column Layout Behavior
- **D-27:** Group-scoped columns — each dashboard group fills its own rows independently. If "CPU" has 5 charts in 3-column mode, it renders as 2 rows (3+2). Groups don't share row space. The CollapsingHeader spans full width above its charts.
- **D-28:** Chart width = `(available_width - (num_cols - 1) * spacing) / num_cols`. Chart height stays at fixed `CHART_HEIGHT` (140px).
- **D-29:** Stats row (min/avg/max/p99) remains visible below each chart in column mode. No change to stats behavior.
- **D-30:** Minimap always spans full panel width regardless of column mode. It's a global time navigation tool.

### Crosshair and Interaction
- **D-31:** Crosshair synchronization works across ALL charts in column mode. The `crosshair_x_` shared state is already global — it just needs to render correctly when charts are arranged in columns. Hovering any chart updates the vertical crosshair line in all visible charts.
- **D-32:** Drag-to-zoom continues to work in column mode. The X-axis is shared across all charts (same time range).

### Default and Persistence
- **D-33:** Default layout: auto-detect from window width. If `available_width > 1600px`, default to 2-column grid. Otherwise default to list mode.
- **D-34:** Persist the layout mode and column count in `Prefs` struct. Add `int chart_layout_columns = 0` where 0 = auto-detect (list for narrow, 2-col for wide), 1 = list, 2/3/4 = that many columns.

### Scope Control
- **D-35:** Only modify `chart_panel_view.hpp/cpp` (layout logic + toggle UI), `prefs.hpp` (new field), and `prefs.cpp` (serialization). No changes to `metric_tree_view`, `ftdc_view`, `app`, or any other files.
- **D-36:** All 178 existing tests must continue to pass. No new tests required (UI-only change).

### Agent's Discretion
- Toggle button visual style (icons vs text labels)
- Spacing between columns
- Whether column count dropdown is a combo box or small buttons (2/3/4)
- Animation/transition between layout modes (none is fine)
- How to handle very narrow windows where even 2 columns would be too cramped (fall back to list)

</decisions>

<canonical_refs>
## Canonical References

### Current Implementation (to be modified)
- `src/ui/chart_panel_view.hpp` — ChartPanelView class, CHART_HEIGHT constant, render_chart method signature
- `src/ui/chart_panel_view.cpp` — `render_inner()` where grouped chart rendering happens (lines 594-714), `render_chart()` which takes a `width` parameter already
- `src/core/prefs.hpp` — Prefs struct (add new field)
- `src/core/prefs.cpp` — JSON serialization (add new field)

### Prior Phase Context
- `.planning/phases/03-ftdc-support/03b-CONTEXT.md` — Phase 3b decisions (D-12 through D-24)

</canonical_refs>

<code_context>
## Existing Code Insights

### Chart Rendering (key integration point)
- `render_chart()` already takes a `float width` parameter — column layout just passes a narrower width
- `render_inner()` currently iterates `dashboard_groups_` and renders each chart at `avail_w - 8.0f` width
- Charts are rendered via `ImGui::PushID` / `ImPlot::BeginPlot` with `ImVec2(width, chart_h)`
- The grouped rendering loop (lines 598-648) iterates group paths and calls `render_chart()` — this is where column layout inserts `ImGui::SameLine()` between charts

### ImGui Column Layout Pattern
- Use `ImGui::SameLine()` after every chart except the last in a row
- Or use `ImGui::BeginTable("##grid", num_cols)` / `ImGui::TableNextColumn()` for automatic grid layout
- `ImGui::GetContentRegionAvail().x` gives available width for computing column width

### Prefs System
- `Prefs` is a plain struct in `prefs.hpp`
- `PrefsManager::load()` reads JSON, `PrefsManager::save()` writes JSON
- Adding a field: add to struct + add JSON read/write in prefs.cpp
- Prefs are accessible in App via `prefs_` member, passed to views

### Integration Point
- `ChartPanelView::render_inner()` is called from `FtdcView::render_inner()`
- The Prefs are not currently passed to ChartPanelView — need to add a setter or pass the column count

</code_context>

<deferred>
## Deferred Ideas

- **Drag-and-drop chart reordering** — let users rearrange charts within groups (999.7)
- **Per-group layout control** — different column counts per group
- **Resizable chart height** — drag handle to change CHART_HEIGHT
- **Chart overlay** — multiple metrics on one chart (999.7)

</deferred>

---

*Phase: 04-ftdc-chart-layout-modes*
*Context gathered: 2026-04-12*
