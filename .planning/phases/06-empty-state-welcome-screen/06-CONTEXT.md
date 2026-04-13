# Phase 6: Empty State Welcome Screen — Context

**Gathered:** 2026-04-13
**Status:** Ready for planning
**Depends on:** None (standalone, but benefits from Phase 5 session tabs)

<domain>
## Phase Boundary

When a session tab has no data loaded, replace the entire 3-column layout with a centered welcome screen. Show the app name, tagline, drag instructions, supported file types, and a list of recently opened files. Recent files are tracked in prefs.json. The welcome screen disappears automatically when data is loaded. Minimal dark visual style matching the existing theme.

**NOT in scope:** Open File button/dialog (Phase 7), animated transitions, onboarding tutorial, splash screen on startup. The welcome screen is purely informational + recent files for quick re-open.

</domain>

<decisions>
## Implementation Decisions

### Welcome Screen Layout
- **D-54:** When a session has no data (`!has_log && !has_ftdc`), skip the entire 3-column layout (left panel, splitters, center, right panel). Instead, render a single full-area centered welcome screen.
- **D-55:** The welcome screen replaces the existing `ImGui::TextDisabled("Drop MongoDB log files here to begin.")` at `app.cpp:916` and the empty FTDC view.

### Welcome Screen Content
- **D-56:** Content (top to bottom, centered):
  1. App name "YAMLA" in larger text (use `ImGui::SetWindowFontScale(1.5f)` or similar)
  2. Tagline: "Yet Another MongoDB Log Analyzer" in dim text
  3. Spacer
  4. Instructions: "Drag files onto this window to get started" in normal text
  5. Supported types: "Supported: MongoDB log files (.log, .json) and FTDC diagnostic.data directories" in dim text
  6. Spacer
  7. Recent files section (if any exist): "Recent Files" header + clickable list of recent paths
- **D-57:** Clicking a recent file entry loads it into the current session (same as drag-and-drop).

### Recent Files
- **D-58:** Add `std::vector<std::string> recent_files` to the `Prefs` struct. Stored as a JSON array in prefs.json under `"recent_files"` key.
- **D-59:** Maximum 10 recent entries. When a new file/directory is loaded, add it to the front. Remove duplicates. Trim to 10.
- **D-60:** Recent files are updated in `App` after a successful load (when LoadState transitions to Ready, or when FtdcLoadState transitions to Ready).
- **D-61:** Display only the filename (not full path) in the recent files list, but store the full path. Show full path as a tooltip on hover.

### Visual Style
- **D-62:** Minimal dark style matching the existing theme. White text for app name, dim/gray text for instructions and types, normal text for recent file entries. No borders, no icons, no drop zone indicator. Centered vertically and horizontally in the available area.

### Scope Control
- **D-63:** Primary files modified: `app.cpp` (welcome screen rendering in render_dockspace), `prefs.hpp` (recent_files field), `prefs.cpp` (JSON serialization). No new source files.
- **D-64:** All 178 existing tests must continue to pass.

### Agent's Discretion
- Exact font scale factor for the app name
- Spacing between content sections
- Recent file entry hover highlight color
- Whether to show a "(no recent files)" message when the list is empty
- How to handle recent files that no longer exist on disk (skip silently or show dimmed)

</decisions>

<canonical_refs>
## Canonical References

### Current Implementation (to be modified)
- `src/ui/app.cpp` — `render_dockspace()` lines 906-916 where the empty-state text currently lives
- `src/core/prefs.hpp` — Prefs struct (add recent_files)
- `src/core/prefs.cpp` — JSON serialization (add recent_files)

### Prior Phase Context
- `.planning/phases/05-multi-session-tabs/05-CONTEXT.md` — Session struct, tab bar, smart drop routing

</canonical_refs>

<code_context>
## Existing Code Insights

### Current Empty State
- `app.cpp:906-916`: When `!has_data`, left panel shows `ImGui::TextDisabled("Drop MongoDB log files here to begin.")`
- Center and right panels are empty but still render (empty ImGui children)
- The FTDC view shows `ImGui::TextDisabled("No FTDC data loaded.")` when empty

### Centering Pattern in ImGui
- Use `ImGui::GetContentRegionAvail()` for available space
- Use `ImGui::SetCursorPos()` to center text blocks
- Use `ImGui::CalcTextSize()` to measure text for centering
- Or use `ImGui::SetCursorPosX((avail.x - text_width) * 0.5f)` per line

### Recent Files — Prefs Pattern
- `Prefs` struct is in `prefs.hpp`, JSON in `prefs.cpp`
- Adding a field: add to struct + add JSON read/write
- simdjson is used for JSON reading; manual std::string for writing

</code_context>

<deferred>
## Deferred Ideas

- **Open File button** — Phase 7 (Multi-Select File Picker)
- **Animated drop zone indicator** — visual feedback during drag hover
- **Onboarding tutorial** — step-by-step guide for new users
- **Splash screen** — show during app startup before window is ready

</deferred>

---

*Phase: 06-empty-state-welcome-screen*
*Context gathered: 2026-04-13*
