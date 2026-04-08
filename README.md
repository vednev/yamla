# YAMLA

Yet Another MongoDB Log Analyzer. Fast, terminal-aesthetic desktop GUI for
inspecting and filtering MongoDB 4.4+ structured JSON logs.

> **Supported log type:** `mongod` server logs only.
> YAMLA does not currently support MongoDB Agent logs, Ops Manager logs,
> `mongos` router logs, or FTDC diagnostic data files.

## Screenshots

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

## MongoDB log format

Requires MongoDB 4.4+ structured JSON log format (one JSON object per line).
Legacy text logs are not supported.
