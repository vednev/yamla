---
phase: 03-ftdc-support
plan: 05
subsystem: ftdc
tags: [metric-defs, dashboards, thresholds, disk-io]
dependency_graph:
  requires: []
  provides: [15-dashboard-categories, 134-metric-defs, disk-io-helpers, anomaly-thresholds]
  affects: [metric_tree_view, chart_panel_view, ftdc_view]
tech_stack:
  added: []
  patterns: [thread_local-cache, string_view-suffix-matching, inline-constexpr]
key_files:
  created: []
  modified:
    - src/ftdc/metric_defs.hpp
decisions:
  - "Combined both tasks into single commit — all changes are in one file (metric_defs.hpp)"
  - "Disk I/O dashboard uses empty metric_paths with DISK_IO_DASHBOARD_NAME constant for dynamic detection"
  - "find_metric_def uses thread_local MetricDef to return pointer to disk metric fallback safely"
metrics:
  duration: 274s
  completed: "2026-04-12T21:11:37Z"
  tasks_completed: 2
  tasks_total: 2
  files_modified: 1
---

# Phase 3 Plan 05: Expand Metric Definitions & Dashboard Categories Summary

**One-liner:** 134 MetricDef entries with 15 curated dashboards, 8 new anomaly thresholds, and dynamic disk I/O pattern matching for any device name

## What Was Done

### Task 1: Add new MetricDef entries and update thresholds
- Added 66 new MetricDef entries to the static table (68 -> 134 total)
- New sections: tcmalloc (7), WT checkpoint (6), WT lock (1), WT data-handle (3), WT session (1), WT cursor (8), WT transaction (8), WT thread-yield (1), storageEngine (1), repl metrics (6)
- Expanded existing sections: connections (+1 active), CPU (+1 nice_ms), WT cache (+14 eviction/history), WT tickets (+2 totalTickets), WT log (+6 journal metrics), repl.buffer (+1 maxSizeBytes)
- Added 8 new anomaly thresholds from knowledge file:
  - Checkpoint duration > 60,000 ms (60s)
  - Schema lock wait > 1,000,000 us/s
  - Eviction aggressive mode > 100
  - App thread eviction time > 0 (any non-zero is anomalous)
  - Active data handles > 10,000
  - Open sessions > 19,000
  - WT update conflicts > 1,000/s
  - Oldest pinned txn rolled back > 100/s
- All 68 existing entries preserved exactly as-is

### Task 2: Define 15 dashboard categories and add disk I/O pattern helper
- Replaced 7 preset dashboards with 15 curated categories per D-19:
  1. Overview (15 metrics)
  2. CPU & System (8 metrics)
  3. Memory & tcmalloc (13 metrics)
  4. WiredTiger Cache (9 metrics)
  5. Eviction (13 metrics)
  6. Tickets (10 metrics)
  7. Operations (18 metrics)
  8. Checkpoints (9 metrics)
  9. Replication (12 metrics)
  10. Network (7 metrics)
  11. Disk I/O (dynamic — empty paths, populated by MetricTreeView)
  12. Journal (8 metrics)
  13. History Store (5 metrics)
  14. Cursors & Handles (12 metrics)
  15. Transactions (7 metrics)
- Added `is_disk_metric()` — pattern matches `systemMetrics.disks.*.{suffix}` for any device name
- Added `disk_metric_def()` — returns correct MetricDef for any dynamic disk metric path
- Updated `find_metric_def()` with thread_local fallback for disk metrics
- Added `DISK_IO_DASHBOARD_NAME` inline constexpr constant

## Verification Results

| Check | Result |
|-------|--------|
| `make all` | PASS (zero compilation errors) |
| `make test` | PASS (178 test cases, 116,272 assertions) |
| Dashboard count | 15 |
| MetricDef entries | 134 (was 68) |
| New thresholds | 8 |
| Existing entries preserved | Yes (all 68 unchanged) |

## Deviations from Plan

None — plan executed exactly as written.

## Commits

| Hash | Message |
|------|---------|
| ea612b9 | feat(03-05): expand metric_defs.hpp with 15 dashboards, 66 new metrics, disk I/O helpers |

## Self-Check: PASSED

- [x] `src/ftdc/metric_defs.hpp` exists and modified
- [x] Commit ea612b9 exists in git log
- [x] 15 dashboards defined
- [x] 134 MetricDef entries (66 new)
- [x] 8 new thresholds set
- [x] `is_disk_metric`, `disk_metric_def`, `DISK_IO_DASHBOARD_NAME` present
- [x] `find_metric_def` has disk fallback
- [x] All 178 tests pass
