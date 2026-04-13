---
phase: 05-multi-session-tabs
plan: 02
subsystem: ui
tags: [imgui, multi-session, smart-drop, per-session-llm, routing]

requires:
  - phase: 05-multi-session-tabs
    plan: 01
    provides: "Session struct, tab bar, session lifecycle"
provides:
  - "Smart 4-rule file drop routing (D-44)"
  - "Per-session LLM wiring with isolated conversation history (D-46/D-47/D-48)"
  - "No tab count limit (D-45)"
  - "Full build + test verification (D-53)"
affects: []

tech-stack:
  added: []
  patterns:
    - "Smart drop routing: empty→fill, FTDC merge, logs→new tab, implicit FTDC-only→add logs"
    - "Per-session LLM prefs and system prompt wired in wire_session()"

key-files:
  created: []
  modified:
    - src/ui/app.cpp

key-decisions:
  - "D-44: Smart drop routing with 4 rules — empty session fills, FTDC merges into log-only, logs onto logs creates new tab, no-data-session fills"
  - "D-45: No limit on tab count"
  - "D-46: Per-session LlmClient with isolated conversation via wire_session()"
  - "D-47: LLM tools bound to session's Cluster on load, conversation cleared (already in render_frame from Plan 01)"
  - "D-48: knowledge_text_ loaded once by App, passed to each session's LlmClient via wire_session()"

patterns-established:
  - "Smart drop routing pattern: check session state → decide route → create_session() if needed"

requirements-completed: []

duration: 3min
completed: 2026-04-13
---

# Phase 5 Plan 02: Smart Drop Routing and Per-Session LLM Summary

**4-rule smart file drop routing with per-session LLM wiring, verified by clean build and 178 passing tests**

## Tasks Completed

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 | Smart file drop routing and per-session LLM wiring | e82869b | src/ui/app.cpp |
| 2 | Full build and test verification | (verification only — no code changes) | — |

## Implementation Details

### Task 1: Smart File Drop Routing and Per-Session LLM

**Smart drop routing (D-44):** Rewrote `handle_drop()` with 4 rules:
1. **Empty session** — If active session has no data (no cluster and no FTDC), add files to it
2. **FTDC merge** — If dropping FTDC onto a session with logs but no FTDC, merge FTDC into the session (creates combined log+FTDC session)
3. **Logs onto logs** — If dropping log files onto a session that already has logs (Ready or Loading), create a new tab and load there
4. **Implicit** — If session has FTDC but no logs, add logs to the existing session

Additional routing: FTDC onto a session that already has FTDC creates a new tab.

**Per-session LLM wiring (D-46/D-48):** Added LLM configuration to `wire_session()`:
- `s.llm_client.set_prefs(&prefs_)` — shared prefs pointer
- `s.llm_client.set_system_prompt(knowledge_text_)` — knowledge base loaded once by App

**Already correct from Plan 01:**
- D-47: `render_frame()` load-completion loop binds `set_cluster()` and calls `clear()` per session
- `setup_llm()` iterates all sessions to re-configure LLM on prefs change

### Task 2: Full Build and Test Verification

- `make clean && make all`: Clean build, zero compiler errors, zero warnings
- `make test`: 178 test cases, 116,272 assertions — **all passed**
- Verified: `handle_drop()` contains all 4 smart routing rules
- Verified: `wire_session()` contains `set_prefs` and `set_system_prompt`
- Verified: `render_loading_popup()` checks all sessions (D-50)
- Verified: `render_frame()` polls all sessions for load transitions (D-49)
- Verified: `set_cluster` binding in per-session load completion (D-47)

## Deviations from Plan

None — plan executed exactly as written.

## Self-Check: PASSED

- src/ui/app.cpp: FOUND
- 05-02-SUMMARY.md: FOUND
- Commit e82869b (Task 1): FOUND
- make clean && make all: 0 errors, 0 warnings
- make test: 178 tests, 116,272 assertions — all passed
