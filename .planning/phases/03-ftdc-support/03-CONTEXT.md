# Phase 3: FTDC Support - Context

**Gathered:** 2026-04-12
**Status:** Ready for planning

<domain>
## Phase Boundary

Parse and visualize MongoDB FTDC (Full-Time Diagnostic Data Capture) binary metric files — the `diagnostic.data/metrics.*` files from mongod/mongos. Provide time-series charts with preset dashboards, a searchable metric tree, LTTB downsampling, synchronized crosshairs, anomaly thresholds, and bidirectional time-range linking with the log view. Single-node FTDC only (one `diagnostic.data` directory at a time). Multi-node FTDC is a separate future phase.

</domain>

<decisions>
## Implementation Decisions

### WIP Integration Strategy
- **D-01:** Extract the 8 new FTDC source files from the `wip-ftdc` branch (commit `eeb3ee7`) as-is, then write the `app.cpp`/`Makefile` integration fresh against `main`. Do NOT merge or cherry-pick the branch — it diverged too far (removed LLM, tests, knowledge).
- **D-02:** Revive `filter_view.hpp/.cpp` from `wip-ftdc` as a separate file. Move filter rendering out of `breakdown_view` into a standalone `FilterView` class for cleaner separation of concerns.
- **D-03:** LLM chat remains available in both Log and FTDC views. Do not remove or hide it when FTDC tab is active. Future phases may add FTDC-aware LLM tools.
- **D-04:** Write Catch2 tests for FtdcParser, FtdcAnalyzer, and MetricStore in this phase. Use the existing test FTDC data files from `wip-ftdc` (`test/diagnostic.data/`). Consistent with Phase 1 test infrastructure.

### Tab/Navigation Design
- **D-05:** Tab bar at top of the window: "Logs" | "FTDC". Simple, discoverable. Each tab shows its own layout (three-column for Logs, two-column for FTDC).
- **D-06:** FTDC tab only appears after the user loads diagnostic.data. Before that, no tab bar — just the log view (or empty drop prompt).
- **D-07:** FTDC data loaded via drag-and-drop: user drags a `diagnostic.data` directory (or single `metrics.*` file) onto the window. App detects the file type and routes to FTDC loading. No menu/button needed.

### Cross-Linking Behavior
- **D-08:** Bidirectional shared time range between Log View and FTDC View. Zooming/panning FTDC charts updates the log view's time filter. Setting a time filter in log view updates the FTDC chart visible range. Both views stay synchronized.
- **D-09:** Annotation markers on FTDC charts — draw severity-colored vertical lines at timestamps where log events occurred. Visually correlates metric spikes with log entries.
- **D-10:** The annotation marker code from `wip-ftdc` (`ChartPanelView::render_annotation_markers()`) was broken in practice. Do NOT blindly carry it forward. Verify the implementation for correctness, fix it, and test it.

### Multi-Node FTDC
- **D-11:** Single node only in Phase 3. Support one `diagnostic.data` directory at a time. Multi-node FTDC overlay/comparison is a future phase.

### Agent's Discretion
- LTTB downsampling point count (wip-ftdc used MAX_PLOT_PTS = 2000)
- Exact chart height, minimap height, spacing constants
- Anomaly threshold highlight band styling
- Minimap visual design
- Drag-to-zoom interaction specifics
- Stats row formatting (min/avg/max/p99 layout)
- Metric tree node expansion/collapse behavior
- Exact file type detection heuristic (log file vs diagnostic.data)

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### WIP FTDC Branch (reference implementation)
- `wip-ftdc` branch, commit `eeb3ee7` — Full FTDC implementation (parser, analyzer, UI). Extract source files from here. Do not merge.
- Key files to extract:
  - `src/ftdc/ftdc_parser.hpp` + `src/ftdc/ftdc_parser.cpp` — BSON parsing, zlib decompression, zigzag varint, delta reconstruction
  - `src/ftdc/ftdc_analyzer.hpp` + `src/ftdc/ftdc_analyzer.cpp` — Rate computation, LTTB downsampling, window stats
  - `src/ftdc/ftdc_cluster.hpp` + `src/ftdc/ftdc_cluster.cpp` — Background directory loading
  - `src/ftdc/metric_store.hpp` — MetricSeries + MetricStore data structures
  - `src/ftdc/metric_defs.hpp` — 60+ known metric definitions + 7 preset dashboards
  - `src/ui/ftdc_view.hpp` + `src/ui/ftdc_view.cpp` — Top-level FTDC tab layout
  - `src/ui/metric_tree_view.hpp` + `src/ui/metric_tree_view.cpp` — Left panel metric browser
  - `src/ui/chart_panel_view.hpp` + `src/ui/chart_panel_view.cpp` — ImPlot chart rendering
  - `src/ui/filter_view.hpp` + `src/ui/filter_view.cpp` — Standalone filter panel (revive)
- Test data: `test/diagnostic.data/metrics.2025-12-18T03-26-29Z-00000` + `test/diagnostic.data/metrics.interim`

### Existing codebase (main branch)
- `src/ui/app.hpp` + `src/ui/app.cpp` — Main loop, drop handling, tab integration point
- `src/ui/breakdown_view.hpp` + `src/ui/breakdown_view.cpp` — Currently owns filter rendering (to be extracted)
- `src/ui/log_view.hpp` + `src/ui/log_view.cpp` — FilterState struct, time range filter target
- `Makefile` — Build target for new FTDC source files
- `conanfile.py` — ImPlot already available (0.16)

### Codebase maps
- `.planning/codebase/ARCHITECTURE.md` — Layer structure, data flow, threading model
- `.planning/codebase/STACK.md` — Dependencies, build system, platform detection
- `.planning/codebase/CONVENTIONS.md` — Code style, patterns

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- **ImPlot 0.16** (`conanfile.py`) — Already available via Conan. Used for bar charts in breakdown_view. FTDC charts will use ImPlot line plots.
- **Background thread loading pattern** (`Cluster::load()`) — Same atomic state + progress polling pattern used for FtdcCluster.
- **FilterState** (`src/ui/log_view.hpp`) — Needs extension for time range fields to support bidirectional FTDC linking.
- **ArenaChain** — Not used by FTDC (MetricStore uses std::vector<double> for time series). Different allocation pattern is appropriate here.
- **Catch2 test infrastructure** (`test/test_main.cpp`, `Makefile` test target) — Ready for FTDC tests.
- **SDL drop handling** (`App::handle_drop()`) — Already handles file drops. Needs extension to detect directory drops for diagnostic.data.

### Established Patterns
- **Atomic state machine** for background loading (`LoadState::Idle → Loading → Ready | Error`)
- **Three threads**: main (SDL+ImGui), background load, background LLM. FTDC adds a 4th background thread during load.
- **pread on-demand** for log detail view — FTDC doesn't need this (all data in memory after parse)
- **Terminal aesthetic** — black bg, white text, gray borders, pastel accents

### Integration Points
- **App::handle_drop()** — Route diagnostic.data directories to FtdcView::start_load() instead of Cluster
- **App::render_frame()** — Add tab bar when FTDC is loaded; render appropriate view per tab
- **FilterState** — Add shared time range fields for bidirectional FTDC<->Log sync
- **Makefile** — Add `src/ftdc/*.cpp` and new UI source files to build targets

</code_context>

<specifics>
## Specific Ideas

- Annotation markers on FTDC charts were broken in the wip-ftdc implementation — must be verified and fixed, not blindly copied
- The wip-ftdc branch used `zlib` for FTDC chunk decompression — verify this links correctly on all platforms (macOS system zlib, Linux -lz)
- FTDC binary format uses BSON + zigzag varint encoding — the parser handles this but is complex; test thoroughly with real data

</specifics>

<deferred>
## Deferred Ideas

- **Multi-node FTDC** — Support multiple diagnostic.data directories with overlay/comparison charts. Own phase.
- **FTDC-aware LLM tools** — Let the AI assistant query FTDC metrics (e.g., "what was CPU usage at the time of the slow query?"). Future enhancement.
- **FTDC export** — Export selected metric time ranges to CSV/JSON. Future phase.

</deferred>

---

*Phase: 03-ftdc-support*
*Context gathered: 2026-04-12*
