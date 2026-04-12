---
phase: 03-ftdc-support
plan: 04
subsystem: build-system
tags: [makefile, zlib, ci, integration, build]
dependency_graph:
  requires: [03-01, 03-02, 03-03]
  provides: [ftdc-build-integration, zlib-linking, ci-ftdc-support]
  affects: [Makefile, .github/workflows/release.yml]
tech_stack:
  added: [zlib]
  patterns: [pkg-config-with-fallback]
key_files:
  modified:
    - Makefile
    - .github/workflows/release.yml
decisions:
  - "zlib linked via pkg-config with -lz fallback for systems without pkg-config zlib metadata"
  - "SRCS auto-detection (find src/ -name *.cpp) already includes src/ftdc/*.cpp — no Makefile source list changes needed"
  - "TEST_DEP_SRCS exclusion filters (main.cpp, ui/, llm_client.cpp) don't affect src/ftdc/ — FTDC sources automatically included in test build"
metrics:
  duration: 3m15s
  completed: "2026-04-12T17:20:04Z"
  tasks: 2
  files: 2
---

# Phase 3 Plan 4: Build Integration (zlib + CI) Summary

**One-liner:** zlib pkg-config linking with -lz fallback added to Makefile and test build; CI updated with zlib1g-dev for Linux; full build compiles 22 source files + 3 FTDC sources; all 178 tests pass (116,272 assertions).

## Tasks Completed

### Task 1: Add zlib to Makefile and update test build for FTDC sources
- **Commit:** `00f9356`
- **Changes:**
  - Added `ZLIB_CFLAGS` and `ZLIB_LIBS` via pkg-config after SSL lines
  - Added `-lz` fallback when pkg-config returns empty (for systems without zlib .pc file)
  - Added `$(ZLIB_CFLAGS)` to `PKG_CFLAGS` and `$(ZLIB_LIBS)` to `PKG_LIBS`
  - Added `$(ZLIB_LIBS)` to `TEST_LDFLAGS`
  - Verified SRCS auto-detection picks up `src/ftdc/*.cpp` (no change needed)
  - Verified TEST_DEP_SRCS includes FTDC sources (not excluded by ui/main/llm_client filters)
  - Verified all vendor paths preserved (-Ivendor/httplib, VENDOR_C_SRCS for md4c)

### Task 2: Update CI workflow + verify full build and tests
- **Commit:** `ddbe322`
- **Changes:**
  - Added `zlib1g-dev` to `apt-get install` in `build-linux-amd64` job (line 67)
  - Added `zlib1g-dev` to `apt-get install` in `build-linux-arm64` job (line 115)
  - macOS uses system zlib via `-lz` (no Homebrew package needed)
  - Verified `make clean && make all` exits 0
  - Verified `make test` exits 0, all tests pass

## Build Results

### `make all` — PASS
- Compiled 22 C++ source files + 2 ImGui backend files + 1 vendor C file (md4c)
- FTDC sources compiled: `ftdc_parser.cpp`, `ftdc_analyzer.cpp`, `ftdc_cluster.cpp`
- zlib linked as `-lz` (system zlib on macOS)
- Binary `yamla` produced successfully
- Only warnings: benign macOS version mismatch linker warnings (pre-existing)

### `make test` — PASS
- **178 test cases, 116,272 assertions — all passed**
- Existing Phase 1 tests: 149+ test cases still passing
- New FTDC tests included: `test_ftdc_parser.cpp`, `test_ftdc_analyzer.cpp`
- FTDC source dependencies correctly linked with zlib in test build

## Deviations from Plan

None — plan executed exactly as written. No compilation errors encountered during integration. All FTDC sources from Plans 01-03 compiled cleanly with the existing codebase on the first attempt.

## Files Modified

| File | Change |
|------|--------|
| `Makefile` | Added ZLIB_CFLAGS/ZLIB_LIBS with pkg-config + fallback; added to PKG_CFLAGS, PKG_LIBS, TEST_LDFLAGS |
| `.github/workflows/release.yml` | Added `zlib1g-dev` to both Linux CI build jobs |

## Verification Checklist

- [x] `make all` exits 0 (binary compiles with FTDC sources + zlib)
- [x] `make test` exits 0 (all 178 tests pass, 116,272 assertions)
- [x] Makefile contains `ZLIB_CFLAGS` and `ZLIB_LIBS` definitions
- [x] Makefile `PKG_CFLAGS` includes `$(ZLIB_CFLAGS)`
- [x] Makefile `PKG_LIBS` includes `$(ZLIB_LIBS)`
- [x] Makefile `TEST_LDFLAGS` includes `$(ZLIB_LIBS)`
- [x] Makefile still contains `-Ivendor/httplib` in CXXFLAGS
- [x] Makefile still contains `VENDOR_C_SRCS` for md4c
- [x] CI `release.yml` Linux jobs include `zlib1g-dev`
- [x] Existing Phase 1 tests still pass (no regressions)
- [x] New FTDC tests pass

## Self-Check: PASSED

- [x] `03-04-SUMMARY.md` exists at `.planning/phases/03-ftdc-support/03-04-SUMMARY.md`
- [x] Commit `00f9356` found (Task 1: Makefile zlib changes)
- [x] Commit `ddbe322` found (Task 2: CI workflow changes)
