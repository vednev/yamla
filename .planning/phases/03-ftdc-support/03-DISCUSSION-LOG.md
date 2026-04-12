# Phase 3: FTDC Support - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-04-12
**Phase:** 03-ftdc-support
**Areas discussed:** WIP Integration Strategy, Tab/Navigation Design, Cross-Linking Behavior, Multi-Node FTDC

---

## WIP Integration Strategy

| Option | Description | Selected |
|--------|-------------|----------|
| Extract and apply | Extract 8 new FTDC source files from wip-ftdc as-is, write app.cpp/Makefile integration fresh against main | ✓ |
| Rewrite from scratch | Use wip-ftdc as reference but rewrite everything | |
| You decide | Agent's discretion on cleanest approach | |

**User's choice:** Extract and apply
**Notes:** The wip-ftdc branch diverged heavily from main (removed LLM, tests, knowledge). Direct merge/cherry-pick would be destructive. Extracting files preserves all main-branch work.

### Filter View Sub-question

| Option | Description | Selected |
|--------|-------------|----------|
| Keep main's approach | Filters stay in breakdown_view, no separate filter_view | |
| Revive filter_view | Bring back separate filter_view.hpp/.cpp from wip-ftdc | ✓ |
| You decide | Agent's discretion | |

**User's choice:** Revive filter_view
**Notes:** Cleaner separation of concerns.

### LLM + FTDC Sub-question

| Option | Description | Selected |
|--------|-------------|----------|
| Chat available everywhere | LLM chat accessible from both Log and FTDC views | ✓ |
| Chat only in Log view | Hide chat when FTDC view is active | |
| You decide | Agent's discretion | |

**User's choice:** Chat available everywhere
**Notes:** Could eventually add FTDC-aware LLM tools.

### FTDC Tests Sub-question

| Option | Description | Selected |
|--------|-------------|----------|
| Yes, add tests | Write Catch2 tests for FtdcParser, FtdcAnalyzer, MetricStore | ✓ |
| Defer tests | Focus on feature integration first | |
| You decide | Agent's discretion | |

**User's choice:** Yes, add tests
**Notes:** Consistent with Phase 1 test infrastructure. Use test FTDC data files from wip-ftdc.

---

## Tab/Navigation Design

| Option | Description | Selected |
|--------|-------------|----------|
| Tab bar at top | Simple tab bar: 'Logs' \| 'FTDC'. FTDC tab appears only when diagnostic.data loaded. | ✓ |
| Sidebar toggle | Sidebar icon/toggle to switch content area | |
| Split-screen | Show both views simultaneously | |

**User's choice:** Tab bar at top
**Notes:** Matches wip-ftdc approach. Clean, discoverable.

### Tab Visibility Sub-question

| Option | Description | Selected |
|--------|-------------|----------|
| Only when FTDC loaded | FTDC tab appears after drop. No tab bar before that. | ✓ |
| Always visible | Both tabs always shown, even if empty | |
| Auto-detect on drop | Detect file type, route automatically | |

**User's choice:** Only when FTDC loaded

### FTDC Loading Sub-question

| Option | Description | Selected |
|--------|-------------|----------|
| Drag-and-drop directory | Same as log files. App detects diagnostic.data vs log file. | ✓ |
| Menu/button to browse | Add a 'Load FTDC' button/menu | |
| Both | Support drag-and-drop AND button/menu | |

**User's choice:** Drag-and-drop directory

---

## Cross-Linking Behavior

| Option | Description | Selected |
|--------|-------------|----------|
| Annotation markers only | Draw markers on FTDC charts at log event timestamps | |
| Click-through linking | Click FTDC time point to filter logs, click marker to highlight data point | |
| Shared time range | Both views share synchronized time range. Zoom/pan updates both. | ✓ |

**User's choice:** Shared time range
**Notes:** Deepest linkage option.

### Sync Direction Sub-question

| Option | Description | Selected |
|--------|-------------|----------|
| Bidirectional | FTDC zoom/pan updates log filter AND log filter updates FTDC range | ✓ |
| FTDC to Logs only | FTDC drives log filter, not reverse | |
| Independent with manual sync | Separate ranges, manual sync button | |

**User's choice:** Bidirectional

### Annotation Markers Sub-question

| Option | Description | Selected |
|--------|-------------|----------|
| Yes, keep markers | Draw severity-colored vertical lines on FTDC charts | ✓ |
| No markers | Shared time range is enough | |
| Markers with severity filter | Only ERROR/FATAL markers | |

**User's choice:** Yes, keep markers
**Notes:** User reported that the wip-ftdc marker implementation was broken. Must verify and fix, not blindly copy.

---

## Multi-Node FTDC

| Option | Description | Selected |
|--------|-------------|----------|
| Single node for now | Phase 3 supports one diagnostic.data dir. Multi-node is future phase. | ✓ |
| Multi-node overlay | Multiple dirs, overlay same metrics from different nodes | |
| Multi-node side-by-side | Multiple dirs, separate chart panels per node | |

**User's choice:** Single node for now

---

## Agent's Discretion

- LTTB downsampling point count
- Chart height, minimap height, spacing constants
- Anomaly threshold band styling
- Minimap visual design
- Drag-to-zoom interaction details
- Stats row formatting
- Metric tree expansion behavior
- File type detection heuristic

## Deferred Ideas

- Multi-node FTDC support (overlay/comparison) — future phase
- FTDC-aware LLM tools — future enhancement
- FTDC export to CSV/JSON — future phase
