# YAMLA

Yet Another MongoDB Log Analyzer. Fast, and intended for
inspecting and filtering MongoDB 5.4+ structured JSON logs with a built-in chat + AI agent to analyze them. It is heavily vibe coded. Proceed with caution!

> **Supported log type:** `mongod` server logs only.
> YAMLA does not currently support MongoDB Agent logs, Ops Manager logs,
> `mongos` router logs, or FTDC diagnostic data files (FTDC is WIP on a separate
> branch)

## Releases

Pre-built binaries are attached to each [GitHub Release](../../releases):

| Platform | File |
|---|---|
| macOS (Apple Silicon) | `yamla-vX.Y.Z-macos-arm64.tar.gz` |
| Linux x86\_64 | `yamla-vX.Y.Z-linux-amd64.tar.gz` |
| Linux arm64 | `yamla-vX.Y.Z-linux-arm64.tar.gz` |

**Runtime requirements:** macOS 12+; Linux requires `libsdl2-2.0-0`.

## Screenshots
#### The UI is being updated constantly at this point, so the screenshots may not accurately reflect the current state.

**Start screen** — drop one or more `mongod` log files onto the window to begin.
![Start screen](screenshots/start_screen.png)

**Post-load overview** — breakdown charts and tables on the left, virtual-scroll log list in the centre, entry detail panel on the right. Connection ID and driver filters sit below the breakdowns.
![Post-load screen](screenshots/post_load_screen.png)

**Slow query filter** — clicking the "Slow queries" counter narrows the log list to only entries where `durationMillis > 100`, highlighted in amber.
![Slow query filter](screenshots/slow_query_filter.png)

**Single filter active** — selecting a severity, component, or any other category highlights the active row in pastel blue and filters the log list in real time. A per-section Clear button removes the filter.
![Single filter](screenshots/single_filter.png)

**Multiple filters and text search** — filters compose: here a component selection and a text search are applied simultaneously, with the entry count showing the narrowed result set.
![Multi-filter and search](screenshots/multi_filter_and_search.png)

## Dependencies

| Tool | Notes |
|------|-------|
| clang++ | C++17 |
| Homebrew SDL2 | `brew install sdl2` |
| Homebrew OpenSSL | `brew install openssl@3` (for HTTPS in AI assistant) |
| pkg-config | `brew install pkg-config` |
| Conan 2 | `pip3 install conan` |
| Python 3 | For Conan |

## Build

**One-time setup** — install Conan deps (imgui, implot, simdjson):

```sh
conan profile detect --force
make deps
```

**Compile:**

```sh
make
```

Binary is written to `./yamla`.

## Run

```sh
make run
# or
./yamla
```

The app opens at 1920×1080. Drag one or more MongoDB log files onto the window
to load them. Multiple files are treated as a single replica-set cluster and
merged by timestamp.

## Fonts

Four fonts ship in `vendor/fonts/` (all OFL-licensed):
Inter, IBM Plex Sans, Fira Code, JetBrains Mono.

To re-download them:

```sh
make fonts
```

Font and size are changed via **Edit → Preferences…** and persist to
`~/.config/yamla/prefs.json`.

## Features

- Virtual-scroll log list — handles millions of entries without lag
- Click any row to inspect the full entry as a collapsible JSON tree
- Breakdown bar charts and tables: severity, op type, component, driver,
  namespace, query shape
- Connection ID and driver type filter panel with checkboxes
- Text search, category filters, cross-filter wiring
- Resizable detail panel (drag the splitter); word-wrap toggle
- Multi-node cluster support with per-node colour badges
- Stacked entry support — click individual node badges to view each node's
  version of a deduplicated log entry
- Timestamp column sorting (click the header to toggle ascending/descending)
- Built-in AI assistant for log analysis (see below)

## AI Assistant

Press `Ctrl+A` to open the AI chat window. The assistant connects to an
Anthropic-compatible API (via internal Azure Foundry) and can query the loaded log
data using six built-in tools:

- `get_analysis_summary` — severity counts, top components, namespaces, etc.
- `search_logs` — filter entries by severity, component, namespace, time range, text
- `get_entry_detail` — fetch the full raw JSON of a specific entry
- `get_slow_queries` — slow query shapes, duration percentiles, affected namespaces
- `get_connections` — per-driver counts and top connection IDs
- `get_error_details` — top error/warning messages with sample entries

The assistant runs an agentic loop: it can call multiple tools per question,
inspect the results, and call more tools before responding. Responses are
rendered with markdown formatting (bold, code blocks, tables, lists).

Configure the API key, endpoint, and model in **Edit > Preferences**. The
prefs file (`~/.config/yamla/prefs.json`) is created with `0600` permissions
since it contains the key.

A `knowledge/knowledge.md` file is loaded at startup and included in the
system prompt to give the assistant context about MongoDB log structure.

Assistant responses can be exported as text files to a directory configured
in preferences (the "Export Dir" field). Press the Export button next to any
assistant message to save it.

Feel free to add support for more LLMs.

To cut a new release:

```sh
git tag v0.1.0
git push origin v0.1.0
```

GitHub Actions builds all three binaries natively and publishes the release automatically.

## MongoDB log format

Requires MongoDB 4.4+ structured JSON log format (one JSON object per line).
Legacy text logs are not supported.
