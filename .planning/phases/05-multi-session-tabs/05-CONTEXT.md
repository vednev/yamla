# Phase 5: Multi-Session Tabs — Context

**Gathered:** 2026-04-12
**Status:** Ready for planning
**Depends on:** Phase 4 (Chart Layout Modes) — complete

<domain>
## Phase Boundary

Extract session-specific state from the monolithic `App` class into a `Session` struct. Add an outer tab bar where each tab is an independent session with its own log data, FTDC data, views, filters, chat, and LLM client. Smart file drop routing: empty tabs receive data, FTDC drops onto log-only sessions merge, otherwise new tab. Per-session LLM chat with isolated conversation history and tool bindings.

**NOT in scope:** Per-session layout/splitter positions (global), drag-to-reorder tabs, tab persistence across restarts, tab pinning, multi-window support. The inner Logs/FTDC sub-tab bar within each session is unchanged.

</domain>

<decisions>
## Implementation Decisions

### Session Extraction
- **D-37:** Extract ~20 session-specific members from `App` into a `Session` struct. Session owns: `Cluster` (unique_ptr), `load_thread`, `FilterState`, `LogView`, `DetailView`, `BreakdownView`, `FtdcView`, `LlmClient`, `ChatView`, `active_tab_` (inner Logs/FTDC), `force_tab_switch_`, `log_entry_ptrs_`, `last_cluster_state_`, `sample_mode_`, `sample_ratio_`, `sample_notice_dismissed_`, `total_file_bytes_`, `load_duration_s_`, `load_start_`, `knowledge_text_`.
- **D-38:** `App` holds a `std::vector<std::unique_ptr<Session>>` and an `int active_session_idx_`. Shared members stay in `App`: `window_`, `renderer_`, `font_mgr_`, `prefs_`, `prefs_view_`, `running_`, `pending_drops_`.
- **D-39:** Layout state (`left_w_`, `right_w_`) remains global in `App`, shared across all sessions (per user preference).

### Tab Bar UI
- **D-40:** An outer session tab bar renders at the top of the window using `ImGui::BeginTabBar("##sessions")`. Each tab shows the session's file info as its title. The inner Logs/FTDC tab bar within each session is unchanged.
- **D-41:** Tab title format: if session has both log and FTDC, show `"mongod.log + diagnostic.data"`. If only log: `"mongod.log"`. If only FTDC: `"diagnostic.data"`. Show just filenames, not full paths.
- **D-42:** Each tab has a close button (`ImGuiTabItemFlags_None` with the `p_open` parameter). Always confirm before closing: show a dialog "Close session? Chat history and loaded data will be lost."
- **D-43:** New session creation: a small "+" button at the end of the tab bar (using `ImGui::TabItemButton("+", ...)`) creates an empty session.

### File Drop Routing (Smart)
- **D-44:** Smart drop routing logic:
  1. If active session has no data (empty), add files to it.
  2. If dropping FTDC onto a session that has logs but no FTDC, add FTDC to that session (creates combined session with cross-linking).
  3. If dropping log files onto a session that already has logs, create a new tab.
  4. If dropping any files with no sessions open, create a new tab.
- **D-45:** No limit on number of tabs. Each session consumes memory proportional to its loaded data.

### Per-Session LLM Chat
- **D-46:** Each `Session` owns its own `LlmClient` and `ChatView`. Conversation history, tool bindings, and system prompt are per-session. The `LlmClient`'s tool layer binds to that session's `Cluster`.
- **D-47:** When a session's cluster loads, call `session.llm_client_.tools().set_cluster(session.cluster_.get())` and `session.llm_client_.clear()` (reset conversation for new data).
- **D-48:** `knowledge_text_` is loaded once by `App` at startup and passed to each `Session`'s `LlmClient` during construction. It doesn't need to be per-session.

### Rendering
- **D-49:** `render_frame()` renders only the active session. Inactive sessions don't render (no wasted GPU cycles). State transitions (LoadState changes) are still polled for all sessions each frame (to detect background load completion).
- **D-50:** `render_loading_popup()` checks all sessions, not just the active one, so loading popups appear regardless of which tab is focused.
- **D-51:** ImGui widget IDs must be scoped per session using `ImGui::PushID(session_index)` to avoid ID conflicts between tabs.

### Scope Control
- **D-52:** Primary files modified: `app.hpp` (Session struct extraction, vector of sessions), `app.cpp` (tab bar rendering, drop routing, session lifecycle). Secondary: minor wiring changes in `ftdc_view.cpp` and `log_view.cpp` if needed for re-parenting.
- **D-53:** All 178 existing tests must continue to pass. No new tests required (UI-only architectural change).

### Agent's Discretion
- Session struct name (`Session` vs `AppSession` vs `SessionState`)
- Tab bar styling (colors, padding)
- Close confirmation dialog styling
- "+" button visual treatment
- How to handle the edge case where all tabs are closed (show empty state or auto-create new tab)
- Whether to show a session count indicator

</decisions>

<canonical_refs>
## Canonical References

### Current Implementation (to be refactored)
- `src/ui/app.hpp` — App class with ~20 session-specific members to extract
- `src/ui/app.cpp` — Monolithic render loop, handle_drop, state transitions
- `src/ui/log_view.hpp` — LogView class (cleanly instantiable, no singletons)
- `src/ui/ftdc_view.hpp` — FtdcView class (owns FtdcCluster, cleanly instantiable)
- `src/ui/detail_view.hpp` — DetailView class
- `src/ui/breakdown_view.hpp` — BreakdownView class
- `src/ui/chat_view.hpp` — ChatView class
- `src/llm/llm_client.hpp` — LlmClient class (tool bindings to Cluster)

### Prior Phase Context
- `.planning/phases/04-ftdc-chart-layout-modes/04-CONTEXT.md` — Phase 4 decisions

</canonical_refs>

<code_context>
## Existing Code Insights

### App Class Structure (from codebase scout)
- **Session-specific (~20 members):** cluster_, load_thread_, filter_, log_view_, detail_view_, breakdown_view_, ftdc_view_, llm_client_, chat_view_, active_tab_, force_tab_switch_, log_entry_ptrs_, last_cluster_state_, sample_mode_, sample_ratio_, sample_notice_dismissed_, total_file_bytes_, load_duration_s_, load_start_, knowledge_text_
- **Shared/global (~8 members):** window_, renderer_, font_mgr_, prefs_, prefs_view_, running_, pending_drops_, left_w_, right_w_

### View Independence
- All views (LogView, FtdcView, DetailView, BreakdownView, ChatView) use pointer-based wiring with no singletons or global mutable state
- Multiple instances work correctly with ImGui PushID scoping
- FtdcView owns its own FtdcCluster and load_thread (already self-contained)
- ChatView + LlmClient are a pair that bind to one Cluster instance

### handle_drop Current Flow
- SDL_DROPFILE events accumulate in pending_drops_
- SDL_DROPCOMPLETE triggers handle_drop(pending_drops_)
- is_ftdc_path() auto-detects FTDC files
- Log files: either start_load (new cluster) or append_load (add to existing)
- FTDC: ftdc_view_.start_load(path), switch to FTDC tab

### ImGui ID Scoping
- Current widget IDs are hardcoded strings ("##logview", "##detail", etc.)
- Multiple sessions need PushID(session_index) wrapping to avoid conflicts
- ImPlot charts use metric path as ID — safe across sessions since each session has its own store

</code_context>

<deferred>
## Deferred Ideas

- **Per-session splitter positions** — each tab remembers its own layout
- **Tab drag-to-reorder** — rearrange session tabs
- **Tab persistence** — save/restore sessions across app restarts
- **Tab pinning** — prevent accidental close
- **Multi-window** — tear off tabs into separate OS windows
- **Session cloning** — duplicate a session with its data and filters

</deferred>

---

*Phase: 05-multi-session-tabs*
*Context gathered: 2026-04-12*
