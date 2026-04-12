---
phase: 03-ftdc-support
plan: 03
subsystem: ui-integration
tags: [ftdc, tab-bar, cross-linking, drag-and-drop, filter]
dependency_graph:
  requires: [03-01]
  provides: [ftdc-app-integration, time-window-filter, tab-navigation]
  affects: [app.hpp, app.cpp, log_view.hpp, log_view.cpp]
tech_stack:
  added: []
  patterns: [imgui-tab-bar, conditional-tab-rendering, cross-view-filter-linking]
key_files:
  created: []
  modified:
    - src/ui/log_view.hpp
    - src/ui/log_view.cpp
    - src/ui/app.hpp
    - src/ui/app.cpp
decisions:
  - "Tab bar only renders when FTDC data is loaded (D-06), preserving clean log-only UX"
  - "FTDC drop detection uses path heuristic (diagnostic.data substring or metrics prefix) per D-07"
  - "Chat view renders outside tab-conditional blocks ensuring availability in both tabs (D-03)"
  - "log_entry_ptrs_ rebuilt on every log Ready transition for fresh annotation marker data"
metrics:
  duration_seconds: 215
  completed: "2026-04-12T17:14:37Z"
  tasks_completed: 2
  tasks_total: 2
  files_modified: 4
---

# Phase 03 Plan 03: App Integration - Tab Bar, Drop Detection, Cross-View Wiring Summary

**One-liner:** FTDC tab bar with conditional rendering, FTDC-aware drag-and-drop routing, time-window FilterState extension for bidirectional FTDC/Log cross-linking, and annotation marker data wiring.

## Tasks Completed

### Task 1: Extend FilterState and LogView for time-window cross-linking
**Commit:** `0dc9b76`

- Added `time_window_active`, `time_window_start_ms`, `time_window_end_ms` fields to `FilterState` struct
- Updated `FilterState::active()` to include `time_window_active` in its return expression
- Added time-window check in `LogView::entry_matches()` that filters entries outside the FTDC-specified time range
- Preserved `SelectCallback` signature as `std::function<void(size_t, uint16_t)>` with node support

**Files modified:** `src/ui/log_view.hpp`, `src/ui/log_view.cpp`

### Task 2: Integrate FTDC into App - tab bar, drop detection, cross-view wiring
**Commit:** `826e0bf`

- Added `#include "ftdc_view.hpp"` and `FtdcView ftdc_view_` member to `App` class
- Added `active_tab_` (int), `force_tab_switch_` (bool), `log_entry_ptrs_` (vector) members
- Wired `ftdc_view_.set_filter(&filter_)` in constructor
- Added `is_ftdc_path()` static helper detecting `diagnostic.data` dirs and `metrics.*` files
- Modified `handle_drop()` to route FTDC paths to `ftdc_view_.start_load()` with tab switch
- Added `ftdc_view_.poll_state()` in `render_frame()` for every-frame state polling
- Built `log_entry_ptrs_` from cluster entries on load Ready transition; wired to `ftdc_view_.set_log_data()`
- Added ImGui tab bar (`Logs`/`FTDC`) in `render_dockspace()`, shown only when FTDC loaded (D-06)
- Wrapped existing three-column log layout in `active_tab_ == 0` condition
- Added FTDC two-column render in `active_tab_ == 1` condition
- Added `ftdc_view_.render_loading_popup()` after existing cluster loading popup
- Verified `chat_view_.render()` remains outside tab-conditional blocks (D-03)

**Files modified:** `src/ui/app.hpp`, `src/ui/app.cpp`

## Preservation Verification

All existing functionality confirmed preserved:
- `append_load()` method: 3 references in app.cpp
- `setup_llm()` method: 4 references in app.cpp
- `load_knowledge()` method: 3 references in app.cpp
- `llm_client_` usage: 9 references in app.cpp
- `chat_view_` usage: 7 references in app.cpp
- `DetailView::set_entry` with offset/len: preserved in on_select lambda
- `LogView::SelectCallback` with `uint16_t` node support: preserved
- All prefs, font, menu code: untouched

## Decision References

| Decision | Implementation |
|----------|---------------|
| D-03 (LLM in both tabs) | `chat_view_.render()` called unconditionally in `render_frame()` |
| D-05 (Tab bar) | `ImGui::BeginTabBar("##main_tabs")` with Logs/FTDC tab items |
| D-06 (FTDC tab conditional) | `show_tab_bar = (ftdc_view_.load_state() != FtdcLoadState::Idle)` |
| D-07 (Drag-and-drop detection) | `is_ftdc_path()` checks for `diagnostic.data` or `metrics` prefix |
| D-08 (Bidirectional time range) | `FilterState::time_window_*` fields + `entry_matches()` check |
| D-09 (Annotation markers) | `log_entry_ptrs_` built and passed to `ftdc_view_.set_log_data()` |

## Deviations from Plan

None - plan executed exactly as written.

## Known Stubs

None - all wiring is complete. The actual FTDC chart rendering and annotation marker drawing are implemented in `ftdc_view.cpp` and `chart_panel_view.cpp` (extracted in Plan 01).

## Self-Check: PASSED

All 4 modified files verified present. Both task commits (0dc9b76, 826e0bf) verified in git log.
