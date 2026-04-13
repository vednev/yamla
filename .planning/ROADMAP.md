# YAMLA — Roadmap

**Project:** YAMLA — Yet Another MongoDB Log Analyzer
**Created:** 2026-04-12

---

## Milestone 1: Quality Foundation

### Phase 1: Automated Test Suite — COMPLETE

**Goal:** Build a comprehensive, robust test suite covering all critical non-UI components — parser, arena allocators, chunk vector, string table, analyzer, query shape normalizer, preferences manager, and JSON escape utilities.

**Requirements:** [TEST-01, TEST-02, TEST-03, TEST-04, TEST-05, TEST-06, TEST-07, TEST-08]

**Result:** 149 tests, 101,450 assertions. All passing. Commit `2268e27`.

Plans:
- [x] 01-01-PLAN.md — Test infrastructure + arena/core data structure tests
- [x] 01-02-PLAN.md — Parser and timestamp tests
- [x] 01-03-PLAN.md — Analyzer, query shape, and string table tests
- [x] 01-04-PLAN.md — Prefs round-trip, JSON escape, and format utility tests

### Phase 2: SSE Streaming + Chat UI Fix — COMPLETE

**Goal:** Replace synchronous LLM requests with SSE streaming for token-by-token UI updates, fix faux-bold text shift bug, and fix green text bleeding in markdown.

**Result:** SSE streaming via Anthropic protocol, agentic tool-use loop, auto-scroll, 300s read timeout, faux-bold removed. Commits `90c7fdb`, `8b491d6`.

## Milestone 2: FTDC Analytics

### Phase 3: FTDC Support — COMPLETE

**Goal:** Parse and visualize MongoDB FTDC binary metric files (`diagnostic.data/metrics.*`). Provide time-series charts with preset dashboards, searchable metric tree, LTTB downsampling, synchronized crosshairs, anomaly thresholds, log event annotation markers, and bidirectional time-range linking with the log view. Single-node only.

**Context:** `.planning/phases/03-ftdc-support/03-CONTEXT.md`

**Result:** 178 tests, 116,272 assertions. All FTDC subsystem files extracted, integrated, and tested.

Plans:
- [x] 03-01-PLAN.md — Extract FTDC core + UI source files from wip-ftdc branch
- [x] 03-02-PLAN.md — Write Catch2 tests for FtdcParser and FtdcAnalyzer (TDD)
- [x] 03-03-PLAN.md — App integration: tab bar, drop detection, cross-view linking
- [x] 03-04-PLAN.md — Build system (zlib), CI update, full build + test verification

### Phase 3b: FTDC View UX Overhaul — COMPLETE

**Goal:** Redesign the FTDC view UX: dashboard-first navigation with 15 curated category cards (toggle on/off), anomaly status badges, collapsible chart groups in the right panel, and a search overlay replacing the verbose raw metric tree. Based on `knowledge/ftdc_knowledge.md` domain expertise and external tool research.

**Context:** `.planning/phases/03-ftdc-support/03b-CONTEXT.md`

**Result:** 15 dashboard categories, 134 metric definitions, 8 new anomaly thresholds, dashboard-first navigation with toggle cards, anomaly badges, search overlay, collapsible chart groups. 178 tests still passing.

Plans:
- [x] 03-05-PLAN.md — Expand metric_defs.hpp: 15 dashboards, 66 new metrics, 8 new thresholds, disk I/O pattern helper
- [x] 03-06-PLAN.md — Rewrite MetricTreeView: dashboard cards, anomaly badges, search overlay
- [x] 03-07-PLAN.md — ChartPanelView category grouping + FtdcView wiring + full test verification

## Backlog

### Phase 999.1: Compressed FTDC Overview Graph with Timeline Navigation (BACKLOG)

**Goal:** Compress all FTDC data into a single overview graph that is always visible at the top of the FTDC view. Add regular timeline interval markers to this graph so the user can quickly click/drag to zoom all charts to a specific timeframe. The current minimap only appears when zoomed in and only shows one metric — this replaces it with a permanent, multi-metric summary view with interactive time navigation.

**Key Ideas:**
- Always-visible hint/overview map at the top (not just when zoomed)
- Compressed single view showing aggregate activity across all loaded metrics
- Regular time interval labels in the overview for orientation
- Click/drag on the overview to set the zoom window for all charts below
- Consider showing anomaly regions (red bands) in the overview where thresholds are exceeded

**Requirements:** TBD
**Plans:** 0 plans

Plans:
- [ ] TBD (promote with /gsd-review-backlog when ready)

### Phase 999.2: Multi-Session Tabs (BACKLOG)

**Goal:** Add application-level tabs where each tab is an independent session. Each session has its own log viewer and FTDC viewer with independent state (filters, scroll position, dashboard selection). Tab titles show the loaded filenames: `log: mongod.log` / `ftdc: metrics.2025-12-18`. This replaces the current single-session model where loading a new file replaces the previous one.

**Key Ideas:**
- Each tab is a fully independent session (own LogView, FtdcView, AnalysisResult, FilterState, etc.)
- Tab title bar shows `log: <filename>` and/or `ftdc: <filename>` based on what's loaded
- Drag-and-drop a file creates a new tab (or adds to current tab if compatible)
- Close tab button with unsaved-state warning if needed
- LLM chat could be per-session or shared across sessions (decision needed)
- Session state isolation: filters, scroll, zoom, dashboard selection all per-tab

**Requirements:** TBD
**Plans:** 0 plans

Plans:
- [ ] TBD (promote with /gsd-review-backlog when ready)

### Phase 999.3: FTDC Chart Layout Modes — List vs Columns (BACKLOG)

**Goal:** Allow users to switch between list view (current single-column stacked charts) and a multi-column grid view for FTDC metrics. A toggle button switches between list and column mode. In column mode, a dropdown lets the user pick the number of columns (2, 3, 4). Charts resize to fit the selected column count. This enables side-by-side comparison of related metrics and better use of wide screens.

**Key Ideas:**
- Toggle button (list icon / grid icon) in the chart panel header area
- List mode: current behavior — full-width charts stacked vertically
- Column mode: charts arranged in N-column grid (user picks 2, 3, or 4 via dropdown)
- Charts resize width proportionally; height stays fixed at 140px
- Column layout respects dashboard group boundaries (each group fills its own rows)
- Persist layout preference in prefs
- Consider: drag-and-drop reordering of charts within a group

**Requirements:** TBD
**Plans:** 0 plans

Plans:
- [ ] TBD (promote with /gsd-review-backlog when ready)

---

*Roadmap created: 2026-04-12*
*Updated: 2026-04-12 — Added backlog item 999.3 (chart layout modes)*
