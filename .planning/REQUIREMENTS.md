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

---

*Requirements created: 2026-04-12*
