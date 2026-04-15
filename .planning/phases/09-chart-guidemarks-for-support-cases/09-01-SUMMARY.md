---
phase: 09-chart-guidemarks-for-support-cases
plan: "01"
subsystem: ui/chart
tags: [guidemark, ftdc, chart, toolbar, implot]
dependency_graph:
  requires: [Phase 8 - FTDC Metrics Display Refinement]
  provides: [guidemark-mode, mark-toolbar-buttons, guidemark-rendering]
  affects: [src/ui/chart_panel_view.hpp, src/ui/chart_panel_view.cpp]
tech_stack:
  added: []
  patterns:
    - ImPlot::PlotInfLines for shared vertical lines across all charts
    - ImPlot::Annotation at Y.Max with clamp=true for top-of-chart labels
    - Modal toolbar toggle with PushStyleColor active-highlight idiom
    - drag_committed_ dual-mode dispatch (mark vs normal)
key_files:
  created: []
  modified:
    - src/ui/chart_panel_view.hpp
    - src/ui/chart_panel_view.cpp
    - test/test_ftdc_parser.cpp
decisions:
  - "D-91: Mark mode quick-clicks place guidemarks only; no time filter or other side effects"
  - "D-92: Drag-to-zoom disabled in mark mode; drag band suppressed"
  - "D-94: COL_GUIDEMARK = amber/orange (1.0, 0.65, 0.0, 0.9) — distinct from crosshair white and chart blue"
  - "D-95: Number labels via Annotation at lims.Y.Max with clamp=true — top of chart data area, not TagX at bottom"
  - "D-97/D-98/D-99: Mark+Clear buttons right-aligned on same toolbar row; Mark highlighted when active; Clear only visible when marks exist or mark mode active"
  - "D-101: Clear resets next_mark_number_ to 1"
  - "Review HIGH resolved: drag-cancel guard in Mark toggle handler resets dragging_/drag_committed_ if mid-drag"
metrics:
  duration_minutes: 25
  completed_date: "2026-04-15"
  tasks_completed: 1
  tasks_total: 2
  files_changed: 3
---

# Phase 09 Plan 01: Guidemark Mode for FTDC Charts Summary

Guidemark mode added to FTDC chart panel: right-aligned Mark toggle and Clear toolbar buttons, numbered amber vertical lines across all charts via ImPlot::PlotInfLines, top-of-chart number labels via ImPlot::Annotation at Y.Max, mark placement on quick-click in mark mode with drag suppression, and modal tooltip hint during mark mode with clean restoration on exit.

## Tasks Completed

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 | Implement guidemark state, rendering, toolbar, and click dispatch | 081b765 | chart_panel_view.hpp, chart_panel_view.cpp, test_ftdc_parser.cpp |

## Tasks Pending (Checkpoint)

| Task | Name | Type | Status |
|------|------|------|--------|
| 2 | Visual verification of guidemark functionality | checkpoint:human-verify | Awaiting user verification |

## Modification Points Applied

All 7 modification points from the plan were applied:

1. **chart_panel_view.hpp**: Added `GuideMark` struct with `double x` and `int number` members, plus `mark_mode_`, `marks_`, `next_mark_number_` member state after `layout_columns_`.

2. **chart_panel_view.cpp**: Added `COL_GUIDEMARK = ImVec4(1.0f, 0.65f, 0.0f, 0.90f)` amber/orange color constant adjacent to `COL_CROSSHAIR`.

3. **chart_panel_view.cpp**: Added guidemark rendering loop in `render_chart()` after the crosshair block, inside `BeginPlot/EndPlot`. Uses unique `##gm_N` label IDs per mark, `ImPlot::PlotInfLines` for the vertical line, and `ImPlot::Annotation` at `lims.Y.Max` with `clamp=true` for the top-of-chart number label.

4. **chart_panel_view.cpp**: Modified tooltip block — when `mark_mode_` is true, shows `"Click to place mark #N"` hint; when false, runs the original timestamp+value tooltip code. Clean restoration on mode exit.

5. **chart_panel_view.cpp**: Replaced monolithic `if (dragging_)` drag band with `if (dragging_ && !mark_mode_)` for the visual band, plus separate `if (dragging_ && mark_mode_)` for release capture. Preserves `dragging_ = true` on click unconditionally so `drag_committed_` still fires.

6. **chart_panel_view.cpp**: Added right-aligned Mark/Clear toolbar block after `ImGui::PopStyleVar(2)` closing the layout toolbar. Calculates `jump_x` from right edge, guards against narrow window overflow, uses `PushStyleColor(ImGuiCol_Button)` highlight for active Mark state, drag-cancel guard on mode toggle.

7. **chart_panel_view.cpp**: Replaced `if (drag_committed_)` block with mode-aware dispatch: `if (mark_mode_)` branch places guidemark on quick-click (px_moved <= 5), `else` branch runs original zoom/time-filter logic unchanged.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed stale test assertion from Phase 8 abbreviation (D-87/D-88)**
- **Found during:** Task 1 build verification (`make test`)
- **Issue:** `test/test_ftdc_parser.cpp:93` asserted `conns->display_name == "Connections Current"` but Phase 8 commit `0e7e119` abbreviated the display name to `"Conns Current"` in `metric_defs.hpp`. The test binary in the main repo passed because it was compiled against pre-abbreviation sources; fresh compilation in the worktree revealed the mismatch.
- **Fix:** Updated assertion to `"Conns Current"` to match the current source.
- **Files modified:** `test/test_ftdc_parser.cpp`
- **Commit:** 081b765 (bundled with Task 1 changes)

## Known Stubs

None — guidemark state is fully wired. `marks_` vector drives both rendering (PlotInfLines + Annotation loop in render_chart) and placement (push_back in drag_committed_ dispatch). `mark_mode_` and `next_mark_number_` are live state updated by toolbar buttons and dispatch.

## Threat Flags

None — no new network endpoints, auth paths, file access patterns, or schema changes introduced. This is a pure UI state addition within ChartPanelView.

## Self-Check

### Files created/modified:
- [x] FOUND: src/ui/chart_panel_view.hpp (struct GuideMark, mark_mode_, marks_, next_mark_number_ added)
- [x] FOUND: src/ui/chart_panel_view.cpp (COL_GUIDEMARK, guidemark render loop, tooltip update, drag band update, Mark/Clear buttons, dispatch update)
- [x] FOUND: test/test_ftdc_parser.cpp (stale assertion fixed)
- [x] FOUND: .planning/phases/09-chart-guidemarks-for-support-cases/09-01-SUMMARY.md

### Commits:
- [x] 081b765: feat(09-01): add guidemark mode to FTDC chart panel

### Build:
- [x] `make` compiled with zero errors (2 pre-existing warnings from third-party imgui.h memset, unrelated to our changes)
- [x] `make test` passed all 182 test cases (116,284 assertions)

## Self-Check: PASSED
