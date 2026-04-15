---
phase: 10-performance-improvements
plan: 06
subsystem: ui/event-loop
tags: [adaptive-frame-rate, idle-throttle, SDL_WaitEventTimeout, cpu-reduction, event-loop]
dependency_graph:
  requires: [10-05 (app.hpp/app.cpp structure, Session/FtdcView)]
  provides: [idle-cpu-throttle, adaptive-frame-rate-15-60fps]
  affects: [src/ui/app.hpp, src/ui/app.cpp]
tech_stack:
  added: []
  patterns: [SDL_WaitEventTimeout idle throttle, shared handle_sdl_event() helper, any_loading bypass for progress bar responsiveness]
key_files:
  created: []
  modified: [src/ui/app.hpp, src/ui/app.cpp]
decisions:
  - "D-17: SDL_WaitEventTimeout replaces SDL_PollEvent spin; timeout_ms=0 during interaction, 66ms when idle >500ms"
  - "handle_sdl_event() extracted to avoid duplicating the switch body across wait and drain paths"
  - "any_loading bypasses throttle while any session cluster or FTDC cluster is in Loading state (T-10-12 mitigation)"
  - "union SDL_Event forward-declared in app.hpp to enable handle_sdl_event() declaration without pulling in SDL headers"
metrics:
  duration: ~15 minutes
  completed: 2026-04-15
  tasks_completed: 1
  files_modified: 2
---

# Phase 10 Plan 06: Adaptive Frame Rate via SDL_WaitEventTimeout Summary

**One-liner:** App::run() event loop replaced with SDL_WaitEventTimeout idle throttle — blocks up to 66ms when idle (>500ms since last input), giving ~15 FPS at rest and 60 FPS during interaction, with any_loading bypass keeping progress bars responsive.

## Tasks Completed

| # | Name | Commit | Files |
|---|------|--------|-------|
| 1 | Replace SDL_PollEvent spin with SDL_WaitEventTimeout + idle detection | 8d610a7 | src/ui/app.hpp, src/ui/app.cpp |

## What Was Built

### Idle Detection and Throttle (D-17)

Added three private members to `App` in `app.hpp`:
- `last_interaction_tick_` (`uint32_t`) — SDL_GetTicks() at last user input
- `IDLE_THRESHOLD_MS` (500) — ms of no input before entering idle mode
- `IDLE_TIMEOUT_MS` (66) — timeout passed to SDL_WaitEventTimeout in idle mode (~15 FPS)

### handle_sdl_event() Shared Helper

Extracted the entire SDL event switch body (SDL_QUIT, SDL_KEYDOWN with F12/Alt+F4/Ctrl+A/ESC, SDL_DROPFILE/SDL_DROPCOMPLETE) into a new `App::handle_sdl_event(const SDL_Event&)` private method. Called from both the SDL_WaitEventTimeout branch and the drain loop, avoiding duplication.

### Restructured Event Loop

The `App::run()` loop now:
1. Checks `any_loading` across all sessions (cluster and ftdc_view) — bypasses throttle while loading
2. Reads ImGuiIO for mouse deltas, wheel, keyboard capture, and mouse down state
3. Updates `last_interaction_tick_` when interacting; computes `idle` flag after 500ms of no input
4. Calls `SDL_WaitEventTimeout(&event, timeout_ms)` — blocks up to 66ms when idle, 0ms when active
5. Processes the first event via `handle_sdl_event()`, updates `last_interaction_tick_` on input events
6. Drains remaining queued events via `SDL_PollEvent` drain loop (same handler + tick update)
7. Proceeds to `render_frame()` and present — renders up to once per wake-up regardless

### Threat Model Mitigations

- **T-10-12 (progress bar stalling):** `any_loading` check forces `interacting = true` → `timeout_ms = 0` while any session is loading. Progress bars remain responsive.
- **T-10-13 (SDL event processing unchanged):** `handle_sdl_event()` is a pure refactor — no new trust boundary, all existing handlers preserved verbatim.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Adaptation] Used `s->ftdc_view.load_state()` instead of `s->ftdc_cluster->state()`**
- **Found during:** Task 1
- **Issue:** The plan's pseudocode referenced `s->ftdc_cluster->state()` but Session has no `ftdc_cluster` member. FTDC loading state is accessed via `s->ftdc_view.load_state()` which returns `FtdcLoadState`.
- **Fix:** Used `s->ftdc_view.load_state() == FtdcLoadState::Loading` in the any_loading check.
- **Files modified:** src/ui/app.cpp
- **Commit:** 8d610a7

**2. [Rule 3 - Forward Declaration] Added `union SDL_Event` forward declaration to app.hpp**
- **Found during:** Task 1
- **Issue:** `handle_sdl_event(const SDL_Event& event)` declaration in `app.hpp` required `SDL_Event` to be known, but the header only forward-declared `SDL_Window` and `SDL_Renderer`.
- **Fix:** Added `union SDL_Event;` forward declaration alongside the other SDL forward declarations in app.hpp.
- **Files modified:** src/ui/app.hpp
- **Commit:** 8d610a7

## Known Stubs

None.

## Threat Flags

None — no new network endpoints, auth paths, file access patterns, or schema changes introduced.

## Self-Check: PASSED

- src/ui/app.hpp — modified, contains last_interaction_tick_, IDLE_THRESHOLD_MS, IDLE_TIMEOUT_MS, handle_sdl_event declaration
- src/ui/app.cpp — modified, contains SDL_WaitEventTimeout, handle_sdl_event, any_loading, LoadState::Loading
- Commit 8d610a7 verified in git log
- Build: succeeded (2 pre-existing imgui warnings, no errors)
- Tests: 175/184 passing — 9 pre-existing failures in test_cluster.cpp/test_cluster_append.cpp confirmed pre-existing before this plan
