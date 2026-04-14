---
phase: 07-multi-select-file-picker
plan: 02
subsystem: ui
tags: [nfd, file-dialog, tag-chips, welcome-screen, menu-bar]
dependency_graph:
  requires: ["07-01"]
  provides: ["file-dialog-ui", "tag-chips", "pending-picks-state"]
  affects: ["src/ui/app.hpp", "src/ui/app.cpp"]
tech_stack:
  added: []
  patterns: ["NFD::Guard RAII for init/quit", "NFD::UniquePathSet RAII for path sets", "horizontal flow chip layout"]
key_files:
  created: []
  modified:
    - src/ui/app.hpp
    - src/ui/app.cpp
decisions:
  - "Used NFD::Guard RAII pattern for NFD init/quit scoped to each dialog call"
  - "Used NFD C++ wrapper (UniquePathSet, PathSet::Count, PathSet::GetPath) for automatic memory management"
  - "Tag chips use horizontal flow layout with line wrapping at 70% or 600px max width"
  - "Load button guarded by double-check (!pending_picks_.empty()) to handle the case where last chip was just removed"
metrics:
  duration: 250s
  completed: "2026-04-14T02:41:52Z"
  tasks_completed: 2
  tasks_total: 2
  files_modified: 2
---

# Phase 7 Plan 02: File Dialog UI + Tag Chips Summary

NFD-extended file dialog wired to App class with multi-select, tag chip deselect UI, Load button routing through handle_drop(), File > Open menu item, and Ctrl+O shortcut.

## Task Results

| Task | Name | Commit | Status | Files |
|------|------|--------|--------|-------|
| 1 | Add file dialog method and app.hpp members | 8437cf3 | Done | src/ui/app.hpp, src/ui/app.cpp |
| 2 | Tag chips UI + Load button on welcome screen | de3d63b | Done | src/ui/app.cpp |

## Changes Made

### Task 1: File Dialog Method + State
- Added `pending_picks_` (vector<string>) and `last_dialog_dir_` (string) members to App class
- Added `open_file_dialog()` method declaration to App class
- Added `#include <nfd.hpp>` and `#include <nfd_sdl2.h>` to app.cpp
- Implemented `open_file_dialog()` using NFD::Guard RAII, NFD::OpenDialogMultiple with UniquePathSet, null filter (D-69), parent window handle via NFD_GetNativeWindowFromSDLWindow (D-68)
- Path deduplication in pending_picks_ prevents adding the same file twice
- Last directory remembered in last_dialog_dir_ for next dialog open (D-77)
- NFD_CANCEL and NFD_ERROR handled gracefully (no crash, no state change)
- Replaced disabled "Open Cluster (drag & drop files)" menu item with active "Open..." + "Ctrl+O" display hint
- Added Ctrl+O / Cmd+O keyboard shortcut in render_frame() via ImGui::IsKeyPressed

### Task 2: Tag Chips + Open/Load Buttons
- Added "Open Files..." button centered on welcome screen below supported types (D-70)
- Button styled with slightly lighter background, hover highlight
- When pending_picks_ is non-empty, renders tag chips as rounded pills (FrameRounding 12.0f, D-75)
- Each chip shows extracted filename with " x" suffix; clicking removes that path
- Chips have dark background (0.15 gray) with full-path tooltip on hover
- Horizontal flow layout: chips wrap to next line when exceeding region width (70% / 600px max)
- "Load" button (green-tinted) appears below chips, calls handle_drop(pending_picks_) then clears
- Updated content_h calculation for vertical centering to include button, chip, and load heights

## Verification Results

| Check | Result |
|-------|--------|
| `make clean && make all` | Exits 0 |
| `make test` | 182 tests, 116,282 assertions |
| `pending_picks_` in app.hpp | 1 occurrence |
| `open_file_dialog` in app.cpp | 5 occurrences |
| `NFD::OpenDialogMultiple` in app.cpp | 1 occurrence |
| `Open...` in app.cpp | 1 occurrence |
| `handle_drop(pending_picks_)` in app.cpp | 1 occurrence |

## Deviations from Plan

### Minor Adjustments

**1. [Rule 1 - Bug prevention] Double-check pending_picks_ before Load button**
- **Found during:** Task 2
- **Issue:** If user clicks the last chip's "x" button, pending_picks_ becomes empty in the same frame, but the Load button render code would still try to render
- **Fix:** Added `if (!pending_picks_.empty())` guard around the Load button section (inside the outer chips block)
- **Impact:** Prevents rendering a Load button with no files to load

**2. [Adjustment] NFD_GetNativeWindowFromSDLWindow API signature**
- **Found during:** Task 1
- **Issue:** The plan's pseudo-code showed `NFD_GetNativeWindowFromSDLWindow(window_)` as returning a value, but the actual API takes an output parameter: `NFD_GetNativeWindowFromSDLWindow(SDL_Window*, nfdwindowhandle_t*)`
- **Fix:** Used the correct two-parameter API with a local `nfdwindowhandle_t parent_handle{}` variable
- **Impact:** Correct API usage, dialog properly parented to SDL window

**3. [Adjustment] Used C++ wrapper PathSet API instead of C API**
- **Found during:** Task 1
- **Issue:** Plan showed raw C API calls (`NFD_PathSet_GetCount`, `NFD_PathSet_GetPath`). Used `NFD::PathSet::Count()` and `NFD::PathSet::GetPath()` from nfd.hpp instead for consistent RAII
- **Fix:** Used NFD::PathSet::Count(out_paths, count) and NFD::PathSet::GetPath(out_paths, i, path) with UniquePathSetPathN
- **Impact:** Automatic memory management for individual paths via RAII unique_ptr wrappers

## Self-Check: PASSED

- src/ui/app.hpp: FOUND
- src/ui/app.cpp: FOUND
- 07-02-SUMMARY.md: FOUND
- Commit 8437cf3 (Task 1): FOUND
- Commit de3d63b (Task 2): FOUND
- No untracked files
