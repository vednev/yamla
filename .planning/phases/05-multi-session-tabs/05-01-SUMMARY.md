---
phase: 05-multi-session-tabs
plan: 01
subsystem: ui
tags: [imgui, multi-session, tabs, session-extraction, refactor]

requires:
  - phase: 04-ftdc-chart-layout-modes
    provides: "Single-session App with FTDC view integrated"
provides:
  - "Session struct owning all per-session state (~20 members)"
  - "App with vector<unique_ptr<Session>> and active_session_idx_"
  - "Outer session tab bar with dynamic titles"
  - "Close confirmation dialog"
  - "'+' button for creating new sessions"
  - "Session-scoped rendering with PushID"
  - "All-session polling for load-state transitions and FTDC"
affects: [05-02-smart-drop-routing]

tech-stack:
  added: []
  patterns:
    - "Session struct extraction from monolithic App class"
    - "ImGui session-scoped widget IDs via PushID(session_index)"
    - "All-session polling loop for background load detection"

key-files:
  created: []
  modified:
    - src/ui/app.hpp
    - src/ui/app.cpp

key-decisions:
  - "D-37: Session struct owns ~20 session-specific members (cluster, views, filters, LLM, FTDC)"
  - "D-38: App holds vector<unique_ptr<Session>> + active_session_idx_; shared members stay in App"
  - "D-39: Layout state (left_w_, right_w_) remains global in App"
  - "D-40: Outer session tab bar at top of window"
  - "D-41: Dynamic tab titles from loaded filenames"
  - "D-42: Close button with confirmation dialog"
  - "D-43: '+' button creates new empty session"
  - "D-49: Only active session renders; all sessions polled for state changes"
  - "D-50: Loading popups check all sessions"
  - "D-51: Widget IDs scoped per session via PushID"

patterns-established:
  - "Session lifecycle: create_session() -> wire_session() -> close_session()"
  - "active_session() accessor with bounds checking (T-05-01 mitigation)"
  - "compute_tab_title() for dynamic tab titles based on loaded data"

requirements-completed: []

duration: 9min
completed: 2026-04-13
---

# Phase 5 Plan 01: Session Extraction and Tab Bar Summary

**Extracted Session struct from monolithic App class and implemented outer session tab bar with close confirmation, '+' button, and PushID-scoped rendering**

## Tasks Completed

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 | Extract Session struct from App class | ea53496 | src/ui/app.hpp, src/ui/app.cpp |
| 2 | Session tab bar UI, close confirmation, and session-scoped rendering | 05e53ed | src/ui/app.hpp, src/ui/app.cpp |

## Implementation Details

### Task 1: Session Extraction
- Defined `Session` struct in app.hpp with all ~20 session-specific members: cluster, load_thread, filter, log_view, detail_view, breakdown_view, llm_client, chat_view, ftdc_view, active_tab, force_tab_switch, log_entry_ptrs, last_cluster_state, sample_mode, sample_ratio, sample_notice_dismissed, total_file_bytes, load_duration_s, load_start, title, open
- Session destructor joins load_thread, cancels LLM client, resets cluster
- App holds `vector<unique_ptr<Session>>` and `int active_session_idx_`
- Implemented: `active_session()` (bounds-checked), `create_session()`, `wire_session()`, `close_session()`, `compute_tab_title()`
- Refactored ALL methods: start_load, append_load, handle_drop, render_frame, render_dockspace, render_menu_bar, render_loading_popup, on_filter_changed, setup_llm — all access session members through active_session()
- Constructor creates one empty session via create_session()
- render_frame polls ALL sessions for load-state transitions (D-49)
- setup_llm configures ALL sessions' LLM clients

### Task 2: Tab Bar UI
- Outer `BeginTabBar("##sessions")` with `AutoSelectNewTabs` and `FittingPolicyResizeDown`
- Each tab shows dynamic title from `compute_tab_title()`: filename, "filename + FTDC", "FTDC", or "New Session"
- Close button via `p_open` parameter triggers confirmation dialog
- Confirmation dialog: "Close session? Chat history and loaded data will be lost." with Close/Cancel buttons
- "+" button at end of tab bar creates new empty session
- All session content wrapped in `PushID(active_session_idx_)` / `PopID()` for unique widget IDs
- FTDC loading popups check ALL sessions with per-session PushID scoping
- Keyboard shortcuts (Ctrl+A, Escape) target active session's chat view

## Verification

- `make all` exits 0 with zero compiler warnings
- `make test`: 178 test cases, 116,272 assertions — all passed
- All acceptance criteria verified via grep

## Deviations from Plan

None — plan executed exactly as written.

## Self-Check: PASSED

- src/ui/app.hpp: FOUND
- src/ui/app.cpp: FOUND
- 05-01-SUMMARY.md: FOUND
- Commit ea53496 (Task 1): FOUND
- Commit 05e53ed (Task 2): FOUND
- make all: 0 errors, 0 warnings
- make test: 178 tests, 116,272 assertions — all passed
