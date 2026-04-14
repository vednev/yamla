# Phase 7: Multi-Select File Picker with Deselect Tags — Context

**Gathered:** 2026-04-13
**Status:** Ready for planning
**Depends on:** Phase 6 (Welcome Screen — provides the initial button entry point)

<domain>
## Phase Boundary

Add a native file dialog using NFD-extended (nativefiledialog-extended) that lets users pick multiple files. Selected files appear as tag/chip boxes on the welcome screen with "x" deselect buttons. A "Load" button confirms and loads the files using the existing smart drop routing. The dialog is accessible from both the welcome screen ("Open Files..." button) and the menu bar (File > Open).

**NOT in scope:** Save dialog, export dialog, drag-and-drop improvements, file type associations, recent files improvements (already in Phase 6). Tag chips only appear on the welcome screen — once data is loaded, the welcome screen disappears and tags are no longer visible.

</domain>

<decisions>
## Implementation Decisions

### Library Choice
- **D-65:** Use NFD-extended (nativefiledialog-extended) for native file dialogs. Vendor into `vendor/nfd/`. Compile platform-specific backend (nfd_cocoa.m on macOS, nfd_gtk.cpp on Linux).
- **D-66:** Link against AppKit + UniformTypeIdentifiers frameworks on macOS. Link against GTK3 via pkg-config on Linux. Add to Makefile following the existing md4c vendor pattern.

### Dialog Behavior
- **D-67:** Open the dialog in multi-file select mode (`NFD_OpenDialogMultiple`). Users can select log files AND navigate to diagnostic.data directories. The returned paths are analyzed: files starting with "metrics" or paths containing "diagnostic.data" are treated as FTDC, everything else as log files.
- **D-68:** Use the `nfd_sdl2.h` helper to parent the dialog to the SDL2 window (keeps it modal and on top).
- **D-69:** File filter: show "All Supported (*.log, *.json, *)" or no filter (let users pick anything). FTDC directories don't have extensions, so the filter should not be restrictive.

### Button Placement
- **D-70:** "Open Files..." button on the welcome screen, below the instructions text and above the recent files section. Styled as a visible button (not just text).
- **D-71:** File > Open menu item in the menu bar that opens the same dialog. Works regardless of whether the welcome screen is visible (always available).

### Tag Chips (Pre-Load Review)
- **D-72:** After the dialog returns selected paths, they appear as tag/chip boxes on the welcome screen (below the "Open Files..." button). Each chip shows the filename with an "x" button to remove it.
- **D-73:** A "Load" button appears below the tag chips. Clicking it sends all remaining (non-removed) paths through the existing `handle_drop()` smart routing logic.
- **D-74:** Tag chips are only visible on the welcome screen. Once files are loaded, the welcome screen disappears and chips are gone. There is no persistent chip bar.
- **D-75:** Tag chip visual style: small rounded pill with filename text + "x" close button. Dark background with subtle border, matching the theme.

### Integration
- **D-76:** The dialog call blocks the main thread (NFD-extended is synchronous). This is acceptable — native file dialogs are expected to block. ImGui rendering pauses while the dialog is open.
- **D-77:** The dialog should remember the last-used directory (use `NFD_OpenDialogMultiple`'s `defaultPath` parameter, store in a member variable or derive from recent_files).

### Scope Control
- **D-78:** Files modified: `app.hpp` (pending_picks_ member for tag chips), `app.cpp` (welcome screen changes, menu item, dialog call), `Makefile` (NFD-extended compilation), `.github/workflows/release.yml` (CI: add GTK3 dev on Linux). New vendor files in `vendor/nfd/`.
- **D-79:** All 182 existing tests must continue to pass. No new tests needed (native dialog can't be unit tested).

### Agent's Discretion
- Tag chip layout (horizontal flow vs vertical list)
- Exact button size and padding for "Open Files..." and "Load"
- Whether to show file sizes or types in the tag chips
- How to handle the edge case where the dialog is cancelled (no paths returned)
- Whether the "Load" button is disabled when no chips remain

</decisions>

<canonical_refs>
## Canonical References

### Library to Vendor
- https://github.com/btzy/nativefiledialog-extended — NFD-extended repo
- Files needed: `src/include/nfd.h`, `src/include/nfd.hpp`, `src/include/nfd_sdl2.h`, `src/nfd_cocoa.m` (macOS), `src/nfd_gtk.cpp` (Linux)

### Current Implementation (to be modified)
- `src/ui/app.hpp` — App class (add pending_picks_ vector, dialog method)
- `src/ui/app.cpp` — `render_welcome_screen()` (add Open button + tag chips), `render_dockspace()` (add File > Open menu item)
- `Makefile` — Add NFD-extended vendor compilation rules
- `.github/workflows/release.yml` — Add libgtk-3-dev to Linux CI

### Prior Phase Context
- `.planning/phases/06-empty-state-welcome-screen/06-CONTEXT.md` — Welcome screen decisions (D-54 through D-64)

</canonical_refs>

<code_context>
## Existing Code Insights

### Welcome Screen (from Phase 6)
- `App::render_welcome_screen()` renders the centered welcome content
- Called from `render_dockspace()` when session has no data
- Already has: title, tagline, instructions, recent files list
- The "Open Files..." button and tag chips go between instructions and recent files

### Menu Bar
- `app.cpp` has a menu bar in `render_dockspace()` with File, Edit, View menus
- File menu currently has a disabled "Open Cluster" item — this becomes the active "Open..." item

### handle_drop Integration
- `App::handle_drop(const std::vector<std::string>& paths)` already handles smart routing (D-44)
- Tag chips just need to build a path vector and call handle_drop when "Load" is clicked

### Vendor Pattern (md4c example)
- `vendor/md4c/md4c.c` + `vendor/md4c/md4c.h` compiled via `VENDOR_C_SRCS` in Makefile
- NFD-extended follows the same pattern but with platform-conditional compilation (.m on macOS, .cpp on Linux)

</code_context>

<deferred>
## Deferred Ideas

- **Drag-and-drop file preview** — show tag chips during drag hover (before drop)
- **File type icons** — show log/FTDC icons in tag chips
- **Directory browser** — custom in-app directory browser instead of native dialog
- **Save/export dialog** — use NFD-extended for export features too

</deferred>

---

*Phase: 07-multi-select-file-picker*
*Context gathered: 2026-04-13*
