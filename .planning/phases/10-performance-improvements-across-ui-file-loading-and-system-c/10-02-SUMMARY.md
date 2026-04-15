---
phase: 10-performance-improvements
plan: 02
subsystem: ui/chart-rendering
tags: [lttb-cache, stats-cache, annotation-optimization, binary-search, performance]
dependency_graph:
  requires: [10-01]
  provides: [lttb-cache, stats-cache, annotation-prefilter, frame-shared-annotations]
  affects: [src/ui/chart_panel_view.hpp, src/ui/chart_panel_view.cpp, src/ftdc/ftdc_analyzer.hpp, src/ftdc/ftdc_analyzer.cpp, src/ui/app.cpp]
tech_stack:
  added: []
  patterns: [per-metric LTTB cache with epsilon staleness, per-metric stats cache with window invalidation, reusable scratch buffer for p99 sort, frame-level shared annotation vectors, std::lower_bound binary search on presorted pointer vector]
key_files:
  created: []
  modified:
    - src/ui/chart_panel_view.hpp
    - src/ui/chart_panel_view.cpp
    - src/ftdc/ftdc_analyzer.hpp
    - src/ftdc/ftdc_analyzer.cpp
    - src/ui/app.cpp
decisions:
  - "D-04: LTTB cache invalidated only when x_view_min_/x_view_max_ change beyond LTTB_CACHE_EPSILON (0.001s = 1ms)"
  - "D-05: Stats cache invalidated when t0_ms or t1_ms changes; scratch buffer reused per ChartState to eliminate per-call heap allocation"
  - "D-07: log_entry_ptrs pre-filtered to severity <= Warning at load time in app.cpp; visible range located via std::lower_bound, not linear scan"
metrics:
  duration: ~20 minutes
  completed: 2026-04-15
  tasks_completed: 2
  files_modified: 5
---

# Phase 10 Plan 02: Chart Rendering Cache Optimizations Summary

**One-liner:** Per-metric LTTB and stats caches with epsilon/window invalidation, scratch buffer for p99 sort, and pre-filtered annotation pointers with per-frame binary-search visible range location.

## Tasks Completed

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 | Add cache structs to ChartState and scratch buffer overload to FtdcAnalyzer | bf3bb82 | src/ui/chart_panel_view.hpp, src/ftdc/ftdc_analyzer.hpp, src/ftdc/ftdc_analyzer.cpp |
| 2 | Implement LTTB caching, stats caching, and binary-search annotation optimization | a153907 | src/ui/chart_panel_view.cpp, src/ui/app.cpp |

## What Was Built

### Task 1: Cache Struct Definitions and Scratch Buffer Overload

**src/ui/chart_panel_view.hpp:**
- Added `LttbCache` struct: `plot_x`, `plot_y` vectors + `cached_x_min`, `cached_x_max`, `valid` fields (D-04)
- Added `StatsCache` struct: `WindowStats ws`, `cached_t0_ms`, `cached_t1_ms`, `valid` fields (D-05)
- Extended `ChartState` with `LttbCache lttb`, `StatsCache stats`, `std::vector<double> sorted_vals_scratch`
- Added `frame_err_xs_` and `frame_warn_xs_` members to `ChartPanelView` private section (D-07)
- Updated `render_stats_row` declaration to accept `ChartState& state`
- Updated `render_annotation_markers` to no-arg signature (reads from frame vectors)
- Added `#include "../ftdc/ftdc_analyzer.hpp"` for `WindowStats` type used in `StatsCache`

**src/ftdc/ftdc_analyzer.hpp + ftdc_analyzer.cpp:**
- Added 5-arg `compute_window_stats` overload accepting `std::vector<double>& sorted_vals_scratch`
- Made original 4-arg overload delegate to 5-arg with a local scratch vector
- New overload uses `scratch.clear()` + `scratch.reserve()` instead of declaring a new local vector — eliminates heap allocation on cache hits when scratch already has capacity

### Task 2: Render Path Caching and Annotation Optimization

**src/ui/chart_panel_view.cpp — render_chart():**
- LTTB cache check: `lttb_stale` evaluated before every `lttb_downsample()` call
  - Stale if `!state.lttb.valid` OR either view bound changed beyond `LTTB_CACHE_EPSILON` (0.001s)
  - Hit: uses `state.lttb.plot_x`/`state.lttb.plot_y` directly for `ImPlot::PlotLine` and `PlotShaded`
  - Miss: recomputes, stores result in cache, marks valid

**src/ui/chart_panel_view.cpp — render_stats_row():**
- Updated signature to accept `ChartState& state`
- Stats cache check: `stats_stale` evaluated before every `compute_window_stats()` call
  - Stale if `!state.stats.valid` OR `t0_ms`/`t1_ms` changed
  - Hit: reads `state.stats.ws` directly
  - Miss: calls 5-arg `compute_window_stats` with `state.sorted_vals_scratch`, stores result

**src/ui/chart_panel_view.cpp — render_inner():**
- Annotation pre-computation added at top of method (D-07):
  - Clears `frame_err_xs_` and `frame_warn_xs_` each frame
  - Converts `x_view_min_`/`x_view_max_` to milliseconds via `plot_to_ms()`
  - Binary searches `log_entries_` using `std::lower_bound` with comparator `[](const LogEntry* e, int64_t t) { return e->timestamp_ms < t; }`
  - Iterates forward from first match; stops immediately when `timestamp_ms > t_max_ms`
  - Partitions into `frame_err_xs_` (severity <= Error) and `frame_warn_xs_` (Warning)

**src/ui/chart_panel_view.cpp — render_annotation_markers():**
- Rewritten to simply plot from `frame_err_xs_` and `frame_warn_xs_` (no log iteration)
- Signature changed to no parameters

**src/ui/app.cpp:**
- `log_entry_ptrs` build updated to filter `entries[i].severity <= Severity::Warning` (captures Fatal=0, Error=1, Warning=2)
- All filtered entries are naturally sorted by timestamp (entries sorted by `sort_entries_by_time()` in Cluster)
- This sorted + filtered invariant is required for the `std::lower_bound` binary search in `render_inner()`

## Verification Results

- `make` succeeds with no new warnings (only pre-existing ImGui header warnings)
- `grep 'std::lower_bound' src/ui/chart_panel_view.cpp` returns match at line 826 inside `render_inner()`
- No `if (ex < x_view_min_) continue` linear scan pattern anywhere in `render_inner()`
- `make test`: 173/182 pass; 9 failures are pre-existing (cluster load state issues documented in 10-01-SUMMARY)

## Deviations from Plan

None — plan executed exactly as written.

## Pre-existing Test Failures (Out of Scope)

Same 9 failures as documented in 10-01-SUMMARY. Verified pre-existing before this plan's changes.

## Known Stubs

None — all changes wire real behavior. Caches start invalid and are populated on first frame.

## Self-Check: PASSED

- [x] `src/ui/chart_panel_view.hpp` contains `struct LttbCache` with correct fields
- [x] `src/ui/chart_panel_view.hpp` contains `struct StatsCache` with correct fields
- [x] `ChartState` contains `lttb`, `stats`, `sorted_vals_scratch`
- [x] `frame_err_xs_` and `frame_warn_xs_` members present in `ChartPanelView`
- [x] `src/ftdc/ftdc_analyzer.hpp` contains `sorted_vals_scratch` overload
- [x] `src/ftdc/ftdc_analyzer.cpp` has two `compute_window_stats` implementations
- [x] `render_stats_row` declaration includes `ChartState& state` parameter
- [x] Commit `bf3bb82` exists (Task 1)
- [x] Commit `a153907` exists (Task 2)
- [x] `make` succeeds
