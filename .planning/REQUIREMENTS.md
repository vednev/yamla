# YAMLA — Requirements

## Test Suite Requirements

- **TEST-01:** Integrate Catch2 v3 test framework via Conan and add `make test` build target
- **TEST-02:** Test ArenaAllocator — allocation, alignment, reset, capacity exhaustion detection
- **TEST-03:** Test ArenaChain — multi-slab growth, intern_string stability, reset behavior
- **TEST-04:** Test ChunkVector — push_back, random access, sort correctness, iteration, clear
- **TEST-05:** Test StringTable — intern, dedup, lookup, UNKNOWN sentinel, FNV-1a hash correctness
- **TEST-06:** Test LogParser — timestamp parsing, known JSON log line → LogEntry field correctness, malformed input handling
- **TEST-07:** Test Analyzer — single-pass aggregation correctness from known entries, slow query counting, severity/component/namespace counting
- **TEST-08:** Test QueryShapeNormalizer — extended JSON types, nested objects, sorted keys, array normalization
- **TEST-09:** Test PrefsManager — save/load round-trip, values containing special characters (quotes, backslashes), buffer overflow for 512-byte limit
- **TEST-10:** Test json_esc/json_escape — all control characters, quotes, backslashes, unicode escaping
- **TEST-11:** Test format utilities — fmt_count comma formatting, fmt_compact SI-suffix formatting

## Performance Requirements (Phase 10)

- **PERF-01:** RAII timing infrastructure — ScopedTimer and TimingStats to measure parse time, filter rebuild time, frame render time, and memory usage per session (D-02)
- **PERF-02:** Cache LTTB downsampled data per metric with time-range invalidation; re-downsample only when X-axis bounds change beyond epsilon (D-04)
- **PERF-03:** Cache window statistics (min/avg/max/p99) per metric with window-change invalidation and reusable scratch buffer for sorted_vals (D-05)
- **PERF-04:** Optimize annotation markers: pre-filter to Error/Warning entries sorted by timestamp (built once on load), binary search per chart, share annotation X positions across all charts per frame (D-07)
- **PERF-05:** FTDC parser optimization: chunk-level parallelization for files larger than available memory; single-thread path tuning with persistent doc_buf and larger read buffers (D-08)
- **PERF-06:** Make deduplication optional with prefs toggle (off by default); skip O(N^2) dedup pass entirely when disabled (D-10)
- **PERF-07:** Incremental filtering via per-dimension packed uint64 bitmasks; only rescan changed dimension; AND all bitmasks for combined result (D-11)
- **PERF-08:** Trigram index built on load for O(log N) text search on queries >= 3 chars; fall back to linear scan for shorter queries (D-12)
- **PERF-09:** Developer-only debug panel showing detailed memory breakdown (arena slabs, string table, chunk vectors, FTDC metric store) and timing stats (D-13)
- **PERF-10:** Enable ThinLTO (-flto=thin) for release builds in CXXFLAGS and LDFLAGS (D-15)
- **PERF-11:** Add `make debug` target with -O0 -g -fsanitize=address -fno-omit-frame-pointer, separate from release (D-16)
- **PERF-12:** Adaptive frame rate: 60 FPS during interaction, throttle to 10-15 FPS when idle via SDL_WaitEventTimeout (D-17)
- **PERF-13:** End-to-end load-time regression test: load known FTDC + log files, assert wall-clock time under threshold (D-18)

---

*Requirements created: 2026-04-12*
*Updated: 2026-04-15 — Phase 10 performance requirements added*
