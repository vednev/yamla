---
phase: 03-ftdc-support
plan: 06
subsystem: ui
tags: [dashboard, navigation, anomaly-badges, search-overlay, imgui]
dependency_graph:
  requires: [03-05]
  provides: [dashboard-navigation, anomaly-badges, search-overlay, active-dashboards-api]
  affects: [metric_tree_view, ftdc_view]
tech_stack:
  added: []
  patterns: [dashboard-cards, toggle-state, anomaly-threshold-check, dynamic-disk-resolution]
key_files:
  created: []
  modified:
    - src/ui/metric_tree_view.hpp
    - src/ui/metric_tree_view.cpp
decisions:
  - "D-12: Dashboard-first navigation replaces flat metric tree"
  - "D-13: Toggle behavior — multiple dashboards active simultaneously"
  - "D-14/D-22: Anomaly badges using metric_threshold() and metric_is_cumulative()"
  - "D-15: Search overlay with filtered dropdown replaces raw metric tree"
  - "D-21: Overview auto-selected on first load"
  - "2-column card layout with custom ImGui button drawing and DrawList overlays"
  - "Custom metrics tracked separately for ungrouped chart display"
  - "Disk I/O dashboard dynamically resolved from store ordered_keys"
metrics:
  duration: "2m 40s"
  completed: "2026-04-12"
  tasks_completed: 2
  tasks_total: 2
  files_modified: 2
---

# Phase 3 Plan 06: Dashboard-First Navigation Panel Summary

Rewrite MetricTreeView as a dashboard-first navigation panel with 15 toggle cards, threshold-based anomaly badges, and a search overlay for individual metric selection.

## One-Liner

Dashboard-first left panel with 15 toggle cards, red/green anomaly badges checking metric thresholds, and search overlay for custom metric selection.

## Completed Tasks

| # | Task | Commit | Key Changes |
|---|------|--------|-------------|
| 1 | Rewrite MetricTreeView header | `7ce2587` | New class design with dashboard_active_, active_dashboards_, custom_metrics_, check_anomaly(), DashboardInfo type alias |
| 2 | Implement dashboard cards, anomaly badges, search overlay | `aced651` | Full render implementation: 2-column card grid, toggle behavior, anomaly check logic, search with filtered dropdown, custom metric tags |

## Implementation Details

### Dashboard Cards (D-12, D-13)
- 15 preset dashboards rendered as clickable buttons in a 2-column grid
- Each card has active (blue) / inactive (dark) styling with rounded corners and borders
- Toggle on click — multiple dashboards can be active simultaneously
- Dashboard name rendered via ImGui DrawList for precise positioning

### Anomaly Badges (D-14, D-22)
- Red dot: any metric in the dashboard exceeds its threshold
- Green dot: dashboard is active with no anomalies
- For cumulative metrics: checks last rate value against threshold
- For gauge metrics: checks last raw value against threshold
- Special handling for threshold=0 (e.g., app thread eviction time: any non-zero rate = anomaly)
- Disk I/O dashboard resolves paths dynamically from store before checking

### Search Overlay (D-15)
- ImGui::InputText at bottom of panel
- Filters store->ordered_keys by case-insensitive substring match on path and display name
- Shows up to 15 matching results with checkboxes
- Metrics already in active dashboards shown as disabled (greyed out)
- Custom-selected metrics tracked in custom_metrics_ set
- Custom metrics shown as removable tag buttons above search

### Auto-Select Overview (D-21)
- set_store() initializes dashboard_active_[0] = true
- Overview metrics immediately populate selected_metrics_

### New APIs for ChartPanelView
- `active_dashboards()`: returns (name, resolved_paths) pairs for category grouping
- `custom_metrics()`: returns set of individually-selected search metrics
- Both used by Plan 03-07 for collapsible chart category headers

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Warning] Removed unused search_focused_ member**
- **Found during:** Task 2 build verification
- **Issue:** `search_focused_` declared in header but never used — triggered -Wunused-private-field
- **Fix:** Removed the member to comply with zero-warning policy
- **Files modified:** src/ui/metric_tree_view.hpp
- **Commit:** aced651

**2. [Rule 1 - Bug] Fixed iterator invalidation in custom metric removal**
- **Found during:** Task 2 implementation
- **Issue:** Plan's code erased from custom_metrics_ while iterating it (break after erase)
- **Fix:** Copy keys to vector before iterating, safe to erase from set during loop
- **Files modified:** src/ui/metric_tree_view.cpp
- **Commit:** aced651

## Verification Results

- **`make all`:** ✅ Compiles with zero warnings
- **`make test`:** ✅ 178 tests pass (116,272 assertions)
- **grep `dashboard_active_`:** ✅ Found in both .hpp and .cpp
- **grep `check_anomaly`:** ✅ Found in both .hpp and .cpp
- **grep `render_search_overlay`:** ✅ Found in both .hpp and .cpp
- **grep `render_dashboard_cards`:** ✅ Found in both .hpp and .cpp

## Known Stubs

None — all functionality is fully wired. Dashboard cards read from preset_dashboards(), anomaly badges query MetricStore live values, search overlay filters from store->ordered_keys.

## Self-Check: PASSED

- [x] src/ui/metric_tree_view.hpp exists and contains `class MetricTreeView`
- [x] src/ui/metric_tree_view.cpp exists and contains `render_dashboard_cards`
- [x] Commit 7ce2587 exists in git log
- [x] Commit aced651 exists in git log
- [x] `make all` exits 0 with zero warnings
- [x] `make test` exits 0 — 178 tests, 116,272 assertions
