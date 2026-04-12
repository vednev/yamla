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

### Phase 3b: FTDC View UX Overhaul

**Goal:** Redesign the FTDC view UX: dashboard-first navigation with 12-15 curated category cards (toggle on/off), anomaly status badges, collapsible chart groups in the right panel, and a search overlay replacing the verbose raw metric tree. Based on `knowledge/ftdc_knowledge.md` domain expertise and external tool research.

**Context:** `.planning/phases/03-ftdc-support/03b-CONTEXT.md`

**Requirements:** [D-12, D-13, D-14, D-15, D-16, D-17, D-18, D-19, D-20, D-21, D-22, D-23, D-24]

**Plans:** 3 plans

Plans:
- [ ] 03-05-PLAN.md — Expand metric_defs.hpp: 15 dashboards, ~50 new metrics, 8 new thresholds, disk I/O pattern helper
- [ ] 03-06-PLAN.md — Rewrite MetricTreeView: dashboard cards, anomaly badges, search overlay
- [ ] 03-07-PLAN.md — ChartPanelView category grouping + FtdcView wiring + full test verification

---

*Roadmap created: 2026-04-12*
*Updated: 2026-04-12 — Phase 3b planned (3 plans in 2 waves)*
