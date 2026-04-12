---
phase: 03-ftdc-support
plan: 07
subsystem: ui
tags: [collapsible-groups, category-headers, chart-grouping, wiring, imgui]
dependency_graph:
  requires: [03-05, 03-06]
  provides: [collapsible-chart-groups, custom-metric-group, dashboard-chart-wiring]
  affects: [chart_panel_view, ftdc_view]
tech_stack:
  added: []
  patterns: [collapsible-header-grouping, pointer-based-data-wiring]
key_files:
  created: []
  modified:
    - src/ui/chart_panel_view.hpp
    - src/ui/chart_panel_view.cpp
    - src/ui/ftdc_view.hpp
    - src/ui/ftdc_view.cpp
decisions:
  - "Used DashboardInfo (pair<string, vector<string>>) from MetricTreeView directly — no intermediate type"
  - "Flat fallback rendering preserved for backward compatibility when groups not set"
  - "Left panel widened to 300px to accommodate dashboard card layout"
metrics:
  duration: 178s
  completed: "2026-04-12T21:21:36Z"
  tasks_completed: 2
  tasks_total: 2
  files_modified: 4
  tests_passed: 178
  assertions_passed: 116272
---

# Phase 3 Plan 07: Collapsible Category Grouping + FtdcView Wiring Summary

Collapsible category grouping in ChartPanelView with FtdcView wiring — charts organized under CollapsingHeaders matching active dashboards, Custom group for search-selected metrics

## Tasks Completed

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 | Add category grouping to ChartPanelView | b3dfafd | chart_panel_view.hpp, chart_panel_view.cpp |
| 2 | Wire FtdcView + build verification | dc89478 | ftdc_view.hpp, ftdc_view.cpp |

## What Changed

### ChartPanelView (chart_panel_view.hpp/cpp)
- Added `DashboardInfo` type alias, `set_dashboard_groups()` and `set_custom_metrics()` setters
- Added `dashboard_groups_`, `custom_metrics_` pointers and `group_collapsed_` persistent state map
- Replaced flat metric iteration with grouped rendering under `ImGui::CollapsingHeader`
- Each dashboard group renders with styled header (dark background), defaults to open
- Custom metrics from search overlay render in a separate "Custom" group with distinct purple-tinted styling
- Flat fallback preserved when no groups are set (backward compatibility)
- All existing chart features untouched: LTTB downsampling, crosshairs, annotations, drag-to-zoom, minimap, stats rows, threshold bands

### FtdcView (ftdc_view.hpp/cpp)
- `on_selection_changed()` now passes `tree_view_.active_dashboards()` and `tree_view_.custom_metrics()` to chart panel
- Initial wiring added in `poll_state()` Loading→Ready transition block
- Left panel default width increased from 280px to 300px

## Verification Results

- `make clean && make -j4` — zero compiler warnings, build succeeds
- `make test` — all 178 tests pass (116,272 assertions)
- `grep CollapsingHeader src/ui/chart_panel_view.cpp` — 2 matches (dashboard groups + Custom group)
- `grep dashboard_groups_ src/ui/chart_panel_view.hpp` — present in private members
- `grep set_dashboard_groups src/ui/ftdc_view.cpp` — 2 matches (on_selection_changed + poll_state)
- Only UI files modified: chart_panel_view.hpp/cpp, ftdc_view.hpp/cpp (per D-23)

## Deviations from Plan

None — plan executed exactly as written.

## Known Stubs

None — all functionality is fully wired.

## Phase 3b Completion

This plan completes Phase 3b (FTDC View UX Overhaul). The full Phase 3b feature set:

| Plan | Feature | Status |
|------|---------|--------|
| 03-05 | 15 curated dashboards, 66 new metrics, disk I/O helpers | Complete |
| 03-06 | Dashboard-first navigation panel, anomaly badges, search overlay | Complete |
| 03-07 | Collapsible category grouping, FtdcView wiring | Complete |

All decisions D-12 through D-24 are implemented:
- D-12/D-13: Dashboard-first navigation with toggle behavior
- D-14/D-22: Anomaly badges on dashboard cards
- D-15: Search overlay for individual metrics
- D-16: Charts remain individual at 140px with all existing features
- D-17: Charts organized under collapsible category headers
- D-18: Custom metrics in "Custom" group at bottom
- D-19/D-20/D-21: 15 curated dashboards with Overview auto-selected
- D-23: Only UI files modified
- D-24: All 178 tests pass

## Self-Check: PASSED

All files exist, all commits verified, build succeeds, 178/178 tests pass.
