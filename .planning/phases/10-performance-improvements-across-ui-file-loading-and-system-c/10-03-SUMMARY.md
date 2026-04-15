---
phase: 10-performance-improvements
plan: "03"
subsystem: ui
tags: [bitmask, trigram, filter, log-view, performance, cpp]

requires:
  - phase: 10-01
    provides: log_entry_ptrs pre-filter at load, visible range via lower_bound

provides:
  - DimensionMask struct with packed uint64_t bitmask per filter dimension
  - and_masks() utility to AND active dimension masks into combined result
  - Per-dimension incremental bitmask filtering: only changed dimension rescanned on filter change
  - Trigram index built once at set_entries() for O(log N) text search on queries >= 3 chars
  - Linear scan fallback for text search queries < 3 chars
  - 150ms debounce retained for text search; non-text filters apply immediately

affects: [log-view-filter, breakdown-view, filter-view]

tech-stack:
  added: []
  patterns:
    - "DimensionMask: packed uint64 bitmask per filter dimension, bit i = entry i passes"
    - "Trigram index: sorted vector<pair<uint32_t key, uint32_t entry_idx>> for O(log N) text search"
    - "Incremental rebuild: prev_filter_ snapshot detects which dimension changed"
    - "apply_combined_masks: and_masks() + __builtin_ctzll bit extraction into filtered_indices_"

key-files:
  created:
    - src/core/bitmask_filter.hpp
  modified:
    - src/ui/log_view.hpp
    - src/ui/log_view.cpp

key-decisions:
  - "D-11: 11 per-dimension DimensionMask members; all_pass=true skips AND for inactive dimensions"
  - "D-12: Trigram index as sorted vector<pair<uint32,uint32>>; built once at set_entries(); queries < 3 chars use linear scan"
  - "Debounce path in render_inner() calls search_trigram() + apply_combined_masks() directly, bypassing full rebuild_filter_index()"
  - "driver dimension merges driver_idx (single) and driver_idx_include (set) into one mask"

patterns-established:
  - "Bitmask filter pattern: DimensionMask per dimension, and_masks() for combined result, __builtin_ctzll for bit extraction"
  - "Trigram search pattern: build index at load time, binary search for each trigram, exact-match verify candidates"

requirements-completed: [PERF-07, PERF-08]

duration: 4min
completed: 2026-04-15
---

# Phase 10 Plan 03: Incremental Bitmask Filter + Trigram Text Search Summary

**Per-dimension packed-uint64 bitmask filtering with trigram text search index reduces filter-change cost from O(N) full scan to O(N/64) per changed dimension plus a fast AND pass, with O(log N) text search for queries >= 3 chars.**

## Performance

- **Duration:** ~4 min
- **Started:** 2026-04-15T16:15:15Z
- **Completed:** 2026-04-15T16:19:01Z
- **Tasks:** 2
- **Files modified:** 3 (1 created, 2 modified)

## Accomplishments

- Created `src/core/bitmask_filter.hpp` with `DimensionMask` struct (packed uint64 bits, `all_pass` flag, `resize`/`set`/`test`/`clear_all` methods) and `and_masks()` utility function
- Added 11 per-dimension `DimensionMask` members to `LogView` plus `trigram_index_`, `trigram_index_built_`, and `prev_filter_` snapshot
- Implemented incremental `rebuild_filter_index()` that detects changed dimensions via `prev_filter_` comparison and only rescans the affected mask(s)
- Implemented `build_trigram_index()` building a sorted `vector<pair<uint32_t, uint32_t>>` at `set_entries()` time
- Implemented `search_trigram()` with trigram intersection + exact-match verify for queries >= 3 chars, linear scan fallback for < 3 chars
- Implemented `apply_combined_masks()` using packed uint64 AND via `and_masks()` and `__builtin_ctzll` bit extraction to populate `filtered_indices_`
- Preserved 150ms debounce for text search; debounce path calls `search_trigram()` + `apply_combined_masks()` directly (skips full rebuild_filter_index)

## Task Commits

Each task was committed atomically:

1. **Task 1: Create bitmask_filter.hpp and add dimension mask members to LogView** - `700dadf` (feat)
2. **Task 2: Implement incremental bitmask filtering and trigram text search in log_view.cpp** - `46869bf` (feat)

## Files Created/Modified

- `src/core/bitmask_filter.hpp` - New: DimensionMask struct with packed uint64 bitmask + and_masks() AND utility
- `src/ui/log_view.hpp` - Added bitmask_filter.hpp include, 11 DimensionMask members, trigram_index_, prev_filter_, and method declarations
- `src/ui/log_view.cpp` - Full rewrite of rebuild_filter_index() as incremental; added build_trigram_index(), search_trigram(), 11 rebuild_*_mask() functions, apply_combined_masks(), updated set_entries() and render_inner() debounce path

## Decisions Made

- `D-11`: 11 per-dimension DimensionMask members (severity, component, op_type, ns, shape, slow_query, conn_id, driver, node, time_window, text). `all_pass = true` causes `and_masks()` to skip that dimension entirely — inactive dimensions have zero cost.
- `D-12`: Trigram index as sorted `vector<pair<uint32_t key, uint32_t entry_idx>>` — binary search per trigram, posting list intersection with `candidate_mask`, then exact-match verify. Queries < 3 chars fall back to linear scan.
- Driver dimension merges `driver_idx` (single-value) and `driver_idx_include` (set-based) into one `mask_driver_` — both signal the same logical dimension with OR semantics.
- Debounce path in `render_inner()` calls `search_trigram()` + `apply_combined_masks()` directly rather than `rebuild_filter_index()` so only the text mask is recomputed on each debounce fire.

## Deviations from Plan

None — plan executed exactly as written.

## Issues Encountered

- 9 pre-existing test failures in `test_cluster.cpp` / `test_cluster_append.cpp` confirmed via `git stash` round-trip — identical failure count before and after this plan's changes. These are unrelated to log_view and out of scope.

## Threat Surface Scan

No new network endpoints, auth paths, file access patterns, or schema changes introduced. Threat items T-10-05 (trigram memory O(N*avg_msg_len)) and T-10-06 (bitmask size mismatch — mitigated: all masks resized atomically in set_entries()) are addressed as specified.

## Known Stubs

None — all mask data flows from real LogEntry/FilterState values. No placeholder text or hardcoded empty values.

## Next Phase Readiness

- Incremental bitmask filter and trigram index are live; all filter paths in LogView use the new code path
- Existing tests pass (182 total; 9 pre-existing cluster failures unrelated to this plan)
- Ready for Phase 10-04 or subsequent performance plans

---
*Phase: 10-performance-improvements*
*Completed: 2026-04-15*

## Self-Check: PASSED

- FOUND: src/core/bitmask_filter.hpp
- FOUND: src/ui/log_view.hpp
- FOUND: src/ui/log_view.cpp
- FOUND: 10-03-SUMMARY.md
- FOUND commit: 700dadf (Task 1)
- FOUND commit: 46869bf (Task 2)
