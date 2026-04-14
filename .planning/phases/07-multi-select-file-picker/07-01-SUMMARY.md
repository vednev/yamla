---
phase: 07-multi-select-file-picker
plan: 01
subsystem: build-system
tags: [nfd-extended, vendor, makefile, ci, platform-conditional]
dependency_graph:
  requires: []
  provides: [nfd-vendor-files, nfd-build-integration]
  affects: [Makefile, release.yml]
tech_stack:
  added: [nfd-extended]
  patterns: [platform-conditional-vendor-compilation, objc-mrc]
key_files:
  created:
    - vendor/nfd/nfd.h
    - vendor/nfd/nfd.hpp
    - vendor/nfd/nfd_sdl2.h
    - vendor/nfd/nfd_cocoa.m
    - vendor/nfd/nfd_gtk.cpp
    - vendor/nfd/nfd_linux_shared.hpp
  modified:
    - Makefile
    - .github/workflows/release.yml
decisions:
  - "Use -fno-objc-arc instead of -fobjc-arc for nfd_cocoa.m (NFD uses manual retain/release)"
  - "Include nfd_linux_shared.hpp as extra vendor file (required by nfd_gtk.cpp)"
  - "Place GTK3 pkg-config inside Linux platform block rather than separate section"
metrics:
  duration: "3m 51s"
  completed: "2026-04-14"
  tasks_completed: 2
  tasks_total: 2
---

# Phase 7 Plan 01: Vendor NFD-extended & Build Integration Summary

NFD-extended vendored from GitHub with platform-conditional Makefile integration (Cocoa/MRC on macOS, GTK3 on Linux) and CI updated for GTK3 dev headers.

## Tasks Completed

| Task | Name | Commit | Key Files |
|------|------|--------|-----------|
| 1 | Vendor NFD-extended source files | da41a1b | vendor/nfd/{nfd.h, nfd.hpp, nfd_sdl2.h, nfd_cocoa.m, nfd_gtk.cpp, nfd_linux_shared.hpp} |
| 2 | Makefile + CI integration | 84fd9af | Makefile, .github/workflows/release.yml |

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Changed -fobjc-arc to -fno-objc-arc for nfd_cocoa.m**
- **Found during:** Task 2 (build verification)
- **Issue:** Plan specified `-fobjc-arc` for the Objective-C compilation rule, but NFD-extended's `nfd_cocoa.m` uses manual retain/release (MRC) memory management (`[filterStr release]`, `[urls retain]`, etc.), which is incompatible with ARC.
- **Fix:** Changed compilation flag from `-fobjc-arc` to `-fno-objc-arc` in the Makefile rule for `nfd_cocoa.o`.
- **Files modified:** Makefile
- **Commit:** 84fd9af

**2. [Rule 2 - Missing critical functionality] Added nfd_linux_shared.hpp to vendor files**
- **Found during:** Task 1 (file verification)
- **Issue:** `nfd_gtk.cpp` has `#include "nfd_linux_shared.hpp"` which was not listed in the plan's 5 required files. Without it, Linux builds would fail.
- **Fix:** Downloaded and vendored `nfd_linux_shared.hpp` alongside the other 5 files.
- **Files modified:** vendor/nfd/nfd_linux_shared.hpp (new)
- **Commit:** da41a1b

## Build Results

- `make clean && make all`: **PASS** (exit 0, "Built yamla")
- `make test`: **PASS** (182 test cases, 116,282 assertions)
- NFD Cocoa compiled with: `cc -O3 -march=native -Ivendor/nfd $(SDL2_CFLAGS) -fno-objc-arc`
- Linked with: `-framework AppKit -framework UniformTypeIdentifiers`

## Verification Results

| Check | Result |
|-------|--------|
| vendor/nfd/nfd.h contains NFD_OpenDialogMultiple | PASS |
| vendor/nfd/nfd.hpp contains OpenDialogMultiple | PASS |
| vendor/nfd/nfd_sdl2.h contains NFD_GetNativeWindowFromSDLWindow | PASS |
| vendor/nfd/nfd_cocoa.m contains NSOpenPanel | PASS |
| vendor/nfd/nfd_gtk.cpp contains gtk_file_chooser | PASS |
| Makefile contains VENDOR_NFD_SRCS/VENDOR_NFD_OBJS | PASS |
| Makefile contains -Ivendor/nfd | PASS |
| Makefile contains nfd_cocoa.o rule with -fno-objc-arc | PASS |
| Makefile contains nfd_gtk.o rule | PASS |
| Makefile ALL_OBJS includes VENDOR_NFD_OBJS | PASS |
| Makefile Darwin PLATFORM_LIBS has -framework AppKit | PASS |
| release.yml has libgtk-3-dev (2 occurrences) | PASS |
| make all exits 0 | PASS |
| make test exits 0 (182 tests) | PASS |

## Decisions Made

1. **MRC over ARC**: NFD-extended uses manual Objective-C memory management (retain/release/autorelease). Compiling with `-fobjc-arc` causes 20+ errors. Used `-fno-objc-arc` to match upstream's compilation model.
2. **Extra vendor file**: Added `nfd_linux_shared.hpp` (2,644 bytes) as it's a transitive dependency of `nfd_gtk.cpp`.
3. **GTK3 flags placement**: Placed GTK3 pkg-config resolution inside the Linux platform detection block (alongside PLATFORM_CFLAGS/PLATFORM_LIBS) rather than as a separate top-level section, keeping platform-specific concerns co-located.

## Self-Check: PASSED

- All 9 key files verified present on disk
- Both commit hashes (da41a1b, 84fd9af) verified in git log
