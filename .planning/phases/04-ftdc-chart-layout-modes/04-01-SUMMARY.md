---
phase: 04-ftdc-chart-layout-modes
plan: 01
subsystem: ui
tags: [imgui, implot, ftdc, chart-layout, grid, multi-column]

requires:
  - phase: 03b-ftdc-view-ux-overhaul
    provides: "ChartPanelView with grouped chart rendering, dashboard navigation, crosshair sync"
provides:
  - "List/Grid toggle toolbar in chart panel"
  - "2/3/4 column grid layout for FTDC charts"
  - "chart_layout_columns pref field with JSON serialization"
  - "Auto-detect layout from window width (>1600px = 2-col)"
  - "set_layout_columns() API on ChartPanelView"
affects: [ftdc-view, prefs-wiring]

tech-stack:
  added: []
  patterns: ["ImGui::SameLine column grid within grouped chart rendering"]

key-files:
  created: []
  modified:
    - src/core/prefs.hpp
    - src/core/prefs.cpp
    - src/ui/chart_panel_view.hpp
    - src/ui/chart_panel_view.cpp

key-decisions:
  - "D-34: Prefs field int chart_layout_columns = 0 (0=auto, 1=list, 2/3/4=columns)"
  - "D-35: Only 4 files modified (prefs.hpp/cpp, chart_panel_view.hpp/cpp)"
  - "D-27: Group-scoped columns — each dashboard group fills own rows independently"
  - "D-28: Chart width = (avail_w - (cols-1)*spacing) / cols"
  - "D-33: Auto-detect from width: >1600px = 2-col, else list"
  - "D-30: Minimap always full width regardless of column mode"

patterns-established:
  - "SameLine-based column grid: track col_idx, call SameLine(0, spacing) for col_idx > 0, reset at effective_cols"
  - "Auto-detect with override: default 0 auto-detects every frame, user clicks set explicit mode"

requirements-completed: []

duration: 3min
completed: 2026-04-13
---

# Phase 4 Plan 01: FTDC Chart Layout Modes Summary

**List/Grid toggle toolbar with 2/3/4 column grid layout for FTDC charts, auto-detecting from window width**

## Performance

- **Duration:** 3 min
- **Started:** 2026-04-13T02:03:03Z
- **Completed:** 2026-04-13T02:06:24Z
- **Tasks:** 2/2
- **Files modified:** 4

## Accomplishments

### Task 1: Prefs field + JSON serialization
- Added `int chart_layout_columns = 0` to Prefs struct (0=auto, 1=list, 2/3/4=columns)
- Added `parse_int("\"chart_cols\"", ...)` in load() and `"chart_cols":%d` in save()
- Clamped out-of-range values to 0 (auto-detect) — mitigates T-04-01 tampering threat
- All 178 tests pass (116,272 assertions)

### Task 2: Toolbar UI + column layout in ChartPanelView
- Added `set_layout_columns(int cols)` public setter and `layout_columns_` private member
- Toolbar row with [List] / [Grid] toggle buttons + column count combo (2/3/4)
- Auto-detect: `avail_w > 1600px` → 2-col grid, otherwise list (when layout_columns_ == 0)
- Narrow-window fallback: if computed column width < 100px, force list mode (T-04-02)
- Group-scoped column layout in all 3 rendering paths (dashboard groups, custom group, fallback flat)
- Uses `ImGui::SameLine(0, spacing)` for column placement — charts arranged side-by-side
- Minimap stays full width (`avail_w - 8.0f`) per D-30
- Crosshair sync unchanged (shared `crosshair_x_` member) per D-31
- Drag-to-zoom unchanged per D-32
- Stats rows render below each chart in grid cells per D-29
- Clean build, all 178 tests pass

## Commits

| Task | Commit | Description |
|------|--------|-------------|
| 1 | `4b0f617` | feat(04-01): add chart_layout_columns to Prefs struct with JSON serialization |
| 2 | `3f7a411` | feat(04-01): implement List/Grid toggle toolbar and column layout in ChartPanelView |

## Deviations from Plan

None — plan executed exactly as written.

## Verification Results

| Check | Result |
|-------|--------|
| `make all` exits 0 | PASS |
| `make test` exits 0 (178 tests, 116,272 assertions) | PASS |
| chart_panel_view.hpp contains `layout_columns_` | PASS |
| chart_panel_view.cpp contains `SameLine` (column layout) | PASS |
| prefs.hpp contains `chart_layout_columns` | PASS |
| prefs.cpp contains `chart_cols` | PASS |
| Minimap at full width (`avail_w - 8.0f`) | PASS |

## Self-Check: PASSED

- All 4 source files exist
- SUMMARY.md exists
- Commit 4b0f617 found
- Commit 3f7a411 found
- `make all` exits 0
- `make test` exits 0 (178 tests, 116,272 assertions)
