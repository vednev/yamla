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

### Phase 999.4: Multi-Select File Picker with Deselect Tags (BACKLOG)

**Goal:** Add a multi-select file picker that lets users pick multiple files across different directories using a native or custom file dialog. Selected files appear as tag/chip boxes on screen, each with a small "x" button to deselect. This replaces the current drag-and-drop-only model with an explicit file picker alternative that gives users fine-grained control over which files to load.

**Key Ideas:**
- File picker button (or menu item) that opens a multi-select file dialog
- Support picking files across different directories (log files + FTDC directories)
- Selected files shown as tag/chip boxes (pill-shaped with filename + "x" close button)
- Clicking "x" on a tag deselects that file (removes it from the pending load list)
- Tags could appear in a horizontal flow area at the top of the window or in a sidebar
- "Load" button to confirm and start loading all selected files
- Consider: integrate with the multi-session tabs (999.2) — each selection set creates a session
- Consider: remember recent file paths for quick re-selection

**Requirements:** TBD
**Plans:** 0 plans

Plans:
- [ ] TBD (promote with /gsd-review-backlog when ready)

### Phase 999.5: Empty State Welcome Screen (BACKLOG)

**Goal:** When no files are loaded, display a welcoming blank screen with clear instructions on how to interact with the app. Currently the app opens to an empty view with no guidance. The welcome screen should explain that users can drag in MongoDB log files and/or FTDC diagnostic.data directories for viewing, and provide visual cues (drop zone, icons, or example commands).

**Key Ideas:**
- Centered welcome message with app name/logo
- Clear instructions: "Drag log files (.log) or FTDC directories (diagnostic.data) here to get started"
- Visual drop zone indicator (dashed border or large icon)
- Possibly list supported file types with icons
- "Open File" button linking to the file picker (ties into 999.4)
- Show recent files for quick re-open (if prefs support it)
- Disappears automatically when files are loaded

**Requirements:** TBD
**Plans:** 0 plans

Plans:
- [ ] TBD (promote with /gsd-review-backlog when ready)

### Phase 999.6: Dynamic Token Sizing for LLM Requests (BACKLOG)

**Goal:** Remove the max tokens preference menu item and replace it with automatic, dynamic token sizing. The system should estimate how much data the user is sending to the AI agent (log context, FTDC summaries, conversation history) and adjust the max_tokens parameter accordingly to maximize response quality without requiring manual tuning.

**Key Ideas:**
- Remove the max_tokens slider/input from the preferences UI
- Estimate input token count from: system prompt + conversation history + attached log/FTDC context
- Set max_tokens = model_limit - estimated_input_tokens - safety_margin
- Different models have different context windows — look up per-model limits
- Consider: show a subtle indicator of "context budget used" so users understand when they're sending too much data
- Consider: automatic context truncation strategies (summarize older messages, trim log context)

**Requirements:** TBD
**Plans:** 0 plans

Plans:
- [ ] TBD (promote with /gsd-review-backlog when ready)

---

*Roadmap created: 2026-04-12*
*Updated: 2026-04-12 — Added backlog item 999.6 (dynamic token sizing)*
