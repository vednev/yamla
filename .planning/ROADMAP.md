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

### Phase 4: FTDC Chart Layout Modes — COMPLETE

**Goal:** Allow users to switch between list view (current single-column stacked charts) and a multi-column grid view for FTDC metrics. A toggle button switches between list and column mode. In column mode, a dropdown lets the user pick the number of columns (2, 3, 4). Charts resize to fit the selected column count.

**Result:** List/Grid toggle toolbar, 2/3/4-column grid via ImGui::BeginTable, auto-detect from window width, prefs persistence, code reviewed + 6 findings fixed.

Plans:
- [x] 04-01-PLAN.md — Layout toggle toolbar + column grid rendering + prefs persistence

### Phase 5: Multi-Session Tabs — COMPLETE

**Goal:** Add application-level tabs where each tab is an independent session with its own log viewer, FTDC viewer, and LLM chat. Smart file drop routing, per-session state isolation, combined tab titles.

**Result:** Session struct extraction (~20 members), outer tab bar with close confirmation, smart 4-rule drop routing, per-session LLM chat with tool bindings. 182 tests passing.

Plans:
- [x] 05-01-PLAN.md — Session struct extraction + tab bar UI + session-scoped rendering
- [x] 05-02-PLAN.md — Smart file drop routing + per-session LLM + build/test verification

### Phase 6: Empty State Welcome Screen — COMPLETE

**Goal:** Centered welcome screen when no data is loaded, with app name, drag instructions, supported file types, and clickable recent files list persisted in prefs.

**Result:** Full-area welcome screen, recent_files in prefs (max 10, deduped), 4 new tests. 182 tests passing.

Plans:
- [x] 06-01-PLAN.md — Welcome screen rendering + recent files tracking + prefs serialization

### Phase 7: Multi-Select File Picker with Deselect Tags — COMPLETE

**Goal:** Native file dialog via NFD-extended with tag chip preview before loading. Open Files button on welcome screen + File > Open menu + Ctrl+O shortcut.

**Result:** NFD-extended vendored (Cocoa on macOS, GTK3 on Linux), tag chips on welcome screen with deselect "x", Load button, SDL2 window parenting. 182 tests passing.

Plans:
- [x] 07-01-PLAN.md — Vendor NFD-extended + build system integration (Makefile + CI)
- [x] 07-02-PLAN.md — Dialog integration + tag chips UI + File > Open menu item

### Phase 8: Refine FTDC Metrics Graphs and Display

**Goal:** Polish and refine how FTDC metric charts are rendered and how metric data is presented to the user. Ongoing display quality improvements — chart aesthetics, computed/derived metrics, tooltip improvements, stats row layout, threshold band refinement, and visual consistency.

**Depends on:** Phase 4

Plans:
- [ ] TBD

## Backlog

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

### Phase 999.7: Session Save/Restore with Recent Sessions (BACKLOG)

**Goal:** Allow saving session state (loaded files, filter selections, scroll positions, dashboard toggles, chat history) and restoring them later. The welcome screen shows "Recent Sessions" instead of (or alongside) "Recent Files" — each session entry remembers what files were loaded and the full UI state, so the user can pick up exactly where they left off.

**Key Ideas:**
- Serialize session state to a JSON or binary file (e.g., `~/.config/yamla/sessions/`)
- Each saved session captures: file paths, filter state, scroll position, active dashboards, chart layout mode, chat conversation history
- Welcome screen shows "Recent Sessions" with descriptive titles (e.g., "mongod.log + diagnostic.data — 2 hours ago")
- Clicking a recent session restores the full state (loads files + applies saved filters/positions)
- Auto-save session state on close (or periodically)
- Consider: explicit "Save Session As..." for named sessions

**Requirements:** TBD
**Plans:** 0 plans

Plans:
- [ ] TBD (promote with /gsd-review-backlog when ready)

---

*Roadmap created: 2026-04-12*
*Updated: 2026-04-13 — Phase 7 complete, added backlog 999.7 (session save/restore)*
