---
phase: 06-empty-state-welcome-screen
plan: 01
subsystem: ui/prefs
tags: [welcome-screen, recent-files, empty-state, prefs]
dependency_graph:
  requires: []
  provides: [recent-files-prefs, welcome-screen-ui]
  affects: [app.cpp-render-dockspace, prefs-json-schema]
tech_stack:
  added: []
  patterns: [imgui-centered-layout, json-array-serialization]
key_files:
  created: []
  modified:
    - src/core/prefs.hpp
    - src/core/prefs.cpp
    - src/ui/app.hpp
    - src/ui/app.cpp
    - test/test_prefs.cpp
decisions:
  - "Recent files tracked in handle_drop() only (single entry point), not at LoadState::Ready transition"
  - "Welcome screen shown behind loading popup overlay during active loads"
  - "has_log checks for LoadState::Ready; has_ftdc checks for != FtdcLoadState::Idle"
metrics:
  duration: 4m 34s
  completed: "2026-04-13T19:31:02Z"
  tasks: 2/2
  files_changed: 5
  tests_added: 4
  total_tests: 182
  total_assertions: 116282
---

# Phase 6 Plan 01: Empty State Welcome Screen Summary

**One-liner:** Centered welcome screen with YAMLA branding, drag instructions, and clickable recent files list replacing 3-column layout for empty sessions

## Task Results

| Task | Name | Commit | Status |
|------|------|--------|--------|
| 1 | Add recent_files to Prefs + JSON serialization | c599097 | Done |
| 2 | Welcome screen rendering + recent file tracking | b8fa0e2 | Done |

## What Was Built

### Task 1: Prefs recent_files (TDD)
- Added `std::vector<std::string> recent_files` field to `Prefs` struct in `prefs.hpp`
- Added `#include <vector>` to prefs.hpp
- Serialization in `save()`: builds JSON array string with `json_escape()` for each path, appended as `"recent_files":[...]` to the JSON output
- Parser in `load()`: hand-rolled JSON array parser that finds `"recent_files"` key, iterates `[` to `]`, handles escape sequences (`\"`, `\\`, `\n`, `\r`, `\t`)
- 4 new Catch2 tests: default empty, 3-file round-trip, special characters (spaces, backslashes, quotes), empty array round-trip

### Task 2: Welcome Screen + Recent File Tracking
- **render_welcome_screen(float h)**: New private method rendering centered content:
  - "YAMLA" at 1.5x font scale, horizontally centered
  - "Yet Another MongoDB Log Analyzer" tagline in dim text
  - Spacer
  - "Drag files onto this window to get started" instructions
  - "Supported: MongoDB log files (.log, .json) and FTDC diagnostic.data directories" in dim text
  - "Recent Files" section with clickable Selectable entries (filename only, full path as tooltip)
  - Clicking a recent file calls `handle_drop({full_path})`
- **render_dockspace() integration**: Before the 3-column layout, checks `has_log` (cluster Ready) and `has_ftdc` (not Idle). If neither has data, renders welcome screen instead of the entire 3-column layout. Old "Drop MongoDB log files here to begin." placeholder removed.
- **Recent file tracking**: In `handle_drop()`, before any load logic, updates `prefs_.recent_files` with dedup (erase-remove), LIFO insert at front, trim to max 10, then saves via `PrefsManager::save()`.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Recent file tracking location**
- **Found during:** Task 2 implementation
- **Issue:** Plan suggested tracking at both `handle_drop()` and `render_frame()` LoadState::Ready transition, but noted `Cluster::file_paths_` is private with no public accessor
- **Fix:** Tracked only in `handle_drop()` as the plan's NOTE recommended — this is the single entry point for all file drops and recent file clicks
- **Files modified:** src/ui/app.cpp

**2. [Rule 1 - Bug] breakdown_view.render() safety during Loading state**
- **Found during:** Task 2 code review
- **Issue:** After removing the `has_data` guard from the 3-column layout branch, `breakdown_view.render()` would be called unconditionally. With `has_log` checking for Ready, during Loading the welcome screen shows (safe), and the else-if branch only executes when data is Ready (breakdown_view has data).
- **Fix:** No code change needed — the logic flow is correct as designed. Welcome screen shows during Loading behind the loading popup overlay.

## Verification

```
$ make all → Built yamla (exit 0)
$ make test → All tests passed (116282 assertions in 182 test cases)
```

- `prefs.hpp` contains `std::vector<std::string> recent_files` ✅
- `prefs.hpp` contains `#include <vector>` ✅
- `prefs.cpp` contains `"recent_files":%s` in save() ✅
- `prefs.cpp` contains `strstr(buf, "\"recent_files\"")` in load() ✅
- `app.cpp` contains `"YAMLA"` and `"Recent Files"` ✅
- `app.cpp` contains `render_welcome_screen` ✅
- `app.cpp` does NOT contain "Drop MongoDB log files here to begin." ✅
- `app.cpp` contains `prefs_.recent_files` tracking with dedup/LIFO/max-10 ✅

## Self-Check: PASSED

- [x] src/core/prefs.hpp exists and contains recent_files
- [x] src/core/prefs.cpp exists and contains recent_files serialization
- [x] src/ui/app.hpp exists and contains render_welcome_screen
- [x] src/ui/app.cpp exists and contains welcome screen + recent file tracking
- [x] test/test_prefs.cpp exists and contains 4 new recent_files tests
- [x] Commit c599097 exists (Task 1)
- [x] Commit b8fa0e2 exists (Task 2)
