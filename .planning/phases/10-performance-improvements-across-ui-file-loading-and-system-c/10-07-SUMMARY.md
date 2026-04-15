---
phase: 10-performance-improvements
plan: "07"
subsystem: testing
tags: [catch2, chrono, e2e, regression, ftdc, log-parser, wall-clock]

requires:
  - phase: 10-performance-improvements
    provides: "FtdcParser and LogParser optimized in earlier plans (10-01..10-04)"

provides:
  - "End-to-end wall-clock parse regression tests for FTDC and log parsing (test/test_e2e_perf.cpp)"
  - "Two TEST_CASEs tagged [e2e_perf]: FTDC under 5000 ms, log under 3000 ms"
  - "UNSCOPED_INFO baseline recordings visible on every CI run"

affects: [future-performance-plans, ci]

tech-stack:
  added: []
  patterns:
    - "Synthetic fixture generation via write_synthetic_log() when no on-disk log fixture exists"
    - "Wall-clock regression: std::chrono::steady_clock + constexpr threshold constants with 2x-baseline rationale"
    - "UNSCOPED_INFO for timing output visible in both pass and fail CI output"

key-files:
  created:
    - test/test_e2e_perf.cpp
  modified: []

key-decisions:
  - "FTDC_PARSE_THRESHOLD_MS = 5000 ms (measured baseline ~2143 ms on Apple Silicon, ~2x margin)"
  - "LOG_PARSE_THRESHOLD_MS = 3000 ms (measured baseline ~5.5 ms on Apple Silicon, very generous)"
  - "No on-disk log fixture in repo — synthetic 10k-line log generated to /tmp on demand (T-10-15 accepted)"

patterns-established:
  - "E2E perf regression pattern: measure with steady_clock, assert vs constexpr threshold, print via UNSCOPED_INFO"

requirements-completed: [PERF-13]

duration: 5min
completed: 2026-04-15
---

# Phase 10 Plan 07: E2E Parse Regression Tests Summary

**Two Catch2 [e2e_perf] wall-clock tests asserting FTDC parse under 5 s and 10k-line log parse under 3 s, with UNSCOPED_INFO baselines visible in every CI run**

## Performance

- **Duration:** ~5 min
- **Started:** 2026-04-15T16:49:00Z
- **Completed:** 2026-04-15T16:54:21Z
- **Tasks:** 1
- **Files modified:** 1

## Accomplishments

- Created `test/test_e2e_perf.cpp` auto-discovered by `make test` via Makefile's `find test -name '*.cpp'`
- FTDC regression test: parsed canonical fixture in **2143 ms** (threshold 5000 ms, 2x margin)
- Log regression test: parsed 10000 synthetic lines in **5.5 ms** (threshold 3000 ms)
- Both tests print `elapsed_ms` via `UNSCOPED_INFO` so baseline values appear in CI output on every pass
- Threshold constants declared at file scope with 2x-baseline rationale comment for maintainability

## Task Commits

1. **Task 1: Create test/test_e2e_perf.cpp** - `b1c032d` (test)

**Plan metadata:** (docs commit — see final_commit step)

## Files Created/Modified

- `test/test_e2e_perf.cpp` - Two [e2e_perf] TEST_CASEs timing FTDC and log parsing with constexpr thresholds

## Observed Baselines (first passing run, 2026-04-15, Apple Silicon)

| Test | elapsed_ms | Threshold | Headroom |
|------|-----------|-----------|---------|
| FTDC parse | 2143 ms | 5000 ms | 2.3x |
| Log parse (10k lines) | 5.5 ms | 3000 ms | 545x |

The log threshold is intentionally very generous — it was set for 10k lines on slow hardware. Future plans can tighten after collecting stable CI readings.

## Decisions Made

- **FTDC threshold 5000 ms**: Baseline ~2143 ms gives 2.3x headroom; matches D-18 intent of "2x measured baseline"
- **Log threshold 3000 ms**: Baseline ~5.5 ms is extremely fast; generous threshold avoids flakes on slower CI machines
- **Synthetic log at /tmp**: No on-disk `.log` fixture exists in the repo; 10k lines (~2 MB) is well below /tmp quota (T-10-15 accepted risk per threat model)
- **`ensure_log_fixture()`**: Generates only if absent, so repeated test runs reuse existing file without regeneration overhead

## Deviations from Plan

None - plan executed exactly as written. The plan's suggested code template matched the actual API (ChunkVector requires ArenaChain& constructor argument, confirmed from existing test_log_parser.cpp).

## Issues Encountered

9 pre-existing test failures in `test_cluster_append.cpp` remain (unrelated to this plan). Verified by confirming they exist before adding test_e2e_perf.cpp. Out of scope per deviation rule scope boundary.

## Known Stubs

None.

## Threat Flags

None - no new network endpoints, auth paths, or trust-boundary changes. Test fixture I/O stays within /tmp (T-10-15 accepted).

## Next Phase Readiness

- E2E parse regression coverage complete (PERF-13 satisfied)
- Future performance regressions will be caught if FTDC parse exceeds 5 s or log parse exceeds 3 s
- Thresholds can be tightened once stable CI baseline is established across multiple runs

## Self-Check: PASSED

- `test/test_e2e_perf.cpp` - FOUND
- Commit `b1c032d` - FOUND (git log confirms)
- Both [e2e_perf] tests pass with measured timings visible in output

---
*Phase: 10-performance-improvements*
*Completed: 2026-04-15*
