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

### Phase 3: FTDC Support

**Goal:** Parse and visualize MongoDB FTDC binary metric files (`diagnostic.data/metrics.*`). Provide time-series charts with preset dashboards, searchable metric tree, LTTB downsampling, synchronized crosshairs, anomaly thresholds, log event annotation markers, and bidirectional time-range linking with the log view. Single-node only.

**Context:** `.planning/phases/03-ftdc-support/03-CONTEXT.md`

**Plans:** 4 plans

Plans:
- [ ] 03-01-PLAN.md — Extract FTDC core + UI source files from wip-ftdc branch
- [ ] 03-02-PLAN.md — Write Catch2 tests for FtdcParser and FtdcAnalyzer (TDD)
- [ ] 03-03-PLAN.md — App integration: tab bar, drop detection, cross-view linking
- [ ] 03-04-PLAN.md — Build system (zlib), CI update, full build + test verification

---

*Roadmap created: 2026-04-12*
*Updated: 2026-04-12 — Phases 1 & 2 marked complete, Phase 3 planned (4 plans, 3 waves)*
