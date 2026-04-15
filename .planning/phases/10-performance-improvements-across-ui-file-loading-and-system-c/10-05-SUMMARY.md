---
phase: 10-performance-improvements
plan: 05
subsystem: ui/debug
tags: [debug-panel, timing, memory, imgui, f12, dedup, prefs]
dependency_graph:
  requires: [10-01 (timing.hpp, dedup_enabled prefs)]
  provides: [debug-panel-overlay, session-timing-instrumentation, dedup-prefs-toggle]
  affects: [src/ui/debug_panel.hpp, src/ui/debug_panel.cpp, src/ui/app.hpp, src/ui/app.cpp, src/ui/prefs_view.cpp, src/ui/ftdc_view.hpp, src/analysis/cluster.hpp]
tech_stack:
  added: []
  patterns: [NullableTimer RAII for nullable frame timing, forward-decl-only debug headers, per-frame set_sources() T-10-11 null-safety pattern]
key_files:
  created: [src/ui/debug_panel.hpp, src/ui/debug_panel.cpp]
  modified: [src/ui/app.hpp, src/ui/app.cpp, src/ui/prefs_view.cpp, src/ui/ftdc_view.hpp, src/analysis/cluster.hpp]
decisions:
  - "D-13: DebugPanel uses forward declarations only in header; no ImGui include in .hpp"
  - "NullableTimer local struct in render_frame() measures frame duration without crashing on empty sessions"
  - "FtdcView::metric_store() accessor added to expose MetricStore* to debug panel without breaking encapsulation"
  - "set_sources() called every frame when panel visible — satisfies T-10-11 dangling pointer mitigation"
metrics:
  duration: ~20 minutes
  completed: 2026-04-15
  tasks_completed: 2
  files_modified: 7
---

# Phase 10 Plan 05: DebugPanel Overlay, Timing Wiring, Dedup Prefs Summary

**One-liner:** Dev-only F12 DebugPanel overlay displaying arena slabs/bytes, StringTable size, FTDC series counts, and parse/filter/frame timings wired via ScopedTimer at all hot-path call sites.

## Tasks Completed

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 | Add TimingStats to Session, arena getters to Cluster, ScopedTimer at parse/filter/frame | 0f3edfd | src/ui/app.hpp, src/ui/app.cpp, src/analysis/cluster.hpp |
| 2 | Create DebugPanel (hpp/cpp), F12 toggle, dedup_enabled Prefs checkbox | a04eb63 | src/ui/debug_panel.hpp (new), src/ui/debug_panel.cpp (new), src/ui/app.hpp, src/ui/app.cpp, src/ui/prefs_view.cpp, src/ui/ftdc_view.hpp |

## What Was Built

### Task 1: Timing Infrastructure Wiring

**Session::timing (TimingStats):**
- Added `#include "../core/timing.hpp"` to `app.hpp`
- Added `TimingStats timing;` field to the `Session` struct (after `log_entry_ptrs`)
- TimingStats carries `parse_ms`, `filter_ms`, `frame_ms`, `memory_bytes`

**Cluster arena getters (D-13):**
- Added `const ArenaChain& string_chain() const` and `const ArenaChain& entry_chain() const` as public accessors on Cluster — no encapsulation broken, returns const refs to private members

**ScopedTimer call sites:**
- `start_load()`: background thread lambda wraps `cluster->load()` with `ScopedTimer _pt(s.timing.parse_ms)`
- `append_load()`: background thread lambda wraps `cluster->append_files()` with `ScopedTimer _pt(s.timing.parse_ms)`
- `wire_session()` breakdown callback: wraps `rebuild_filter_index()` with `ScopedTimer _t(s.timing.filter_ms)`
- `on_filter_changed()`: wraps `rebuild_filter_index()` with `ScopedTimer _t(s.timing.filter_ms)`

**NullableTimer in render_frame():**
- Local RAII struct at the top of `render_frame()` captures `timing.frame_ms` pointer from active session
- Handles empty sessions gracefully (pointer is null, destructor checks before writing)
- Measures full render_frame duration from first line to last

**memory_bytes snapshot:**
- Updated at `LoadState::Ready` transition: sums `string_chain().approximate_used()` + `entry_chain().approximate_used()`

### Task 2: DebugPanel + F12 + Dedup Prefs

**src/ui/debug_panel.hpp:**
- `#pragma once`, forward declarations only (ArenaChain, StringTable, MetricStore, TimingStats — no heavy includes)
- `set_sources(5 const pointers)` — non-owning; called every frame per T-10-11
- `render_inner()` — content only, caller provides Begin/End
- `bool visible = false; void toggle()` — public API for F12 handler

**src/ui/debug_panel.cpp:**
- Memory section: String arena slabs + MB used/cap, Entry arena slabs + MB used/cap, StringTable interned count, FTDC series count + total samples
- Timing section: Parse ms, Filter ms, Frame ms with FPS calculation, Memory snapshot MB
- All pointers null-checked before dereferencing

**src/ui/ftdc_view.hpp:**
- Added `metric_store()` accessor returning `const MetricStore*` (nullptr if not Ready or no store)
- Enables debug panel to read FTDC store without exposing `cluster_` directly

**src/ui/app.hpp:**
- Added `#include "debug_panel.hpp"` 
- Added `DebugPanel debug_panel_;` as private App member (D-13)

**src/ui/app.cpp:**
- SDL_KEYDOWN: F12 check added before Alt+F4, calls `debug_panel_.toggle(); break;`
- render_frame: After `prefs_view_.render()`, if `debug_panel_.visible`, calls `set_sources()` from active session each frame (T-10-11 null safety), then `ImGui::Begin("Debug (F12)", ...) / render_inner() / End()`

**src/ui/prefs_view.cpp:**
- `ImGui::Checkbox("Enable dedup (O(N^2), reload required)", &staging_.dedup_enabled)` added after prefer_checkboxes
- Tooltip: "Deduplicates identical log entries across files. Off by default for performance. Takes effect on next file load."

## Verification Results

- `make` succeeds; `debug_panel.o` auto-included via Makefile `find src -name '*.cpp'`
- `make test`: 175/184 pass — same 9 pre-existing cluster test failures (environment issue, not introduced by this plan; documented in 10-01-SUMMARY.md)
- `grep -c 'ScopedTimer' src/ui/app.cpp` returns 4
- `grep -n 'NullableTimer' src/ui/app.cpp` returns match in render_frame
- `grep -n 'SDLK_F12' src/ui/app.cpp` returns match
- `grep -n 'debug_panel_.toggle' src/ui/app.cpp` returns match
- `grep -n 'debug_panel_.render_inner' src/ui/app.cpp` returns match
- `grep -n 'dedup_enabled' src/ui/prefs_view.cpp` returns match
- `grep -n 'reload required' src/ui/prefs_view.cpp` returns match
- `grep -n 'TimingStats timing' src/ui/app.hpp` returns match in Session struct
- `grep -n 'string_chain() const' src/analysis/cluster.hpp` returns match
- `grep -n 'entry_chain() const' src/analysis/cluster.hpp` returns match

## Deviations from Plan

### Auto-adjusted: FtdcCluster access pattern

**Found during:** Task 2

**Issue:** Plan referenced `act->ftdc_cluster->store()` but there is no `ftdc_cluster` member on Session — the FtdcCluster is private inside `FtdcView`. The `Session` struct has `ftdc_view` (FtdcView), not a raw `FtdcCluster`.

**Fix:** Added `metric_store()` accessor to `FtdcView` that returns `const MetricStore*` (nullptr if not Ready). The debug panel call site uses `act->ftdc_view.metric_store()` instead. This is a cleaner encapsulation boundary than the plan assumed.

**Files modified:** `src/ui/ftdc_view.hpp`

**Rule:** Rule 1 (auto-fix — code would not compile as written in plan)

## Known Stubs

None — all instrumentation wires to real data sources.

## Threat Surface Scan

No new network endpoints, auth paths, file access patterns, or schema changes introduced. DebugPanel displays internal memory/timing stats — developer-only, hidden by default per T-10-10 (accepted in threat model).

## Self-Check: PASSED

- [x] `src/ui/debug_panel.hpp` exists with `class DebugPanel`, forward declarations, no imgui.h
- [x] `src/ui/debug_panel.cpp` exists with `render_inner`, slab_count, approximate_used, Memory/Timing sections
- [x] Commit `0f3edfd` exists (Task 1)
- [x] Commit `a04eb63` exists (Task 2)
- [x] `make` succeeds
- [x] 9 pre-existing test failures only, no new failures
