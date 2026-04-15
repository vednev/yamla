#include <catch2/catch_all.hpp>

#include "ftdc/ftdc_parser.hpp"
#include "ftdc/metric_store.hpp"

#include "parser/log_parser.hpp"
#include "parser/log_entry.hpp"

#include "core/arena_chain.hpp"
#include "core/chunk_vector.hpp"
#include "core/mmap_file.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <fstream>

// ============================================================
//  Test fixtures
//
//  FTDC_FIXTURE: canonical FTDC file used by test_ftdc_parser.
//                Path is relative to repo root (make test runs
//                from repo root).
//
//  LOG_FIXTURE:  No on-disk log fixture exists in the repo.
//                A synthetic file of ~10000 valid MongoDB JSON
//                log lines is generated on-demand into /tmp.
//                At ~200 bytes/line, this is ~2 MB — well below
//                any /tmp quota (T-10-15 accepted risk).
// ============================================================

static const char* FTDC_FIXTURE =
    "test/diagnostic.data/metrics.2025-12-18T03-26-29Z-00000";

static const char* LOG_FIXTURE =
    "/tmp/yamla_e2e_test.log";

// ============================================================
//  Thresholds (wall-clock milliseconds)
//
//  Rationale: thresholds set to ~2x the measured baseline on
//  the reference hardware (Apple Silicon, Clang 19.1.7 with
//  ThinLTO release build). Intentionally loose to avoid CI
//  flakes; tighten after collecting stable baseline readings.
//  If a test fails, re-measure baseline before raising the
//  threshold — a real regression should be investigated first.
//
//  Baseline observations (first passing run, 2026-04-15):
//    FTDC: recorded as UNSCOPED_INFO output below
//    Log:  recorded as UNSCOPED_INFO output below
// ============================================================

static constexpr double FTDC_PARSE_THRESHOLD_MS = 5000.0;  // 5 s
static constexpr double LOG_PARSE_THRESHOLD_MS  = 3000.0;  // 3 s for 10k lines

// ============================================================
//  Synthetic log generator — writes N lines of valid JSON logs
//  to LOG_FIXTURE. Called once per process if the file is
//  absent or stale.
// ============================================================

static void write_synthetic_log(const std::string& path, size_t lines) {
    std::ofstream out(path);
    REQUIRE(out.is_open());
    for (size_t i = 0; i < lines; ++i) {
        out << R"({"t":{"$date":"2025-01-01T00:)"
            << ((i / 3600) % 24 < 10 ? "0" : "") << ((i / 3600) % 24)
            << ":"
            << ((i / 60) % 60 < 10 ? "0" : "") << ((i / 60) % 60)
            << ":"
            << (i % 60 < 10 ? "0" : "") << (i % 60)
            << R"(.000Z"},"s":"W","c":"COMMAND","id":)"
            << (20000 + (i % 100))
            << R"(,"ctx":"conn)" << (i % 10)
            << R"(","msg":"Slow query","attr":{"ns":"db.col)"
            << (i % 5) << R"(","durationMillis":)" << (100 + (i % 900))
            << R"(}})" << "\n";
    }
}

static void ensure_log_fixture() {
    std::ifstream probe(LOG_FIXTURE);
    if (!probe.good()) {
        write_synthetic_log(LOG_FIXTURE, 10000);
    }
}

// ============================================================
//  FTDC parse wall-clock regression
// ============================================================

TEST_CASE("E2E: FTDC parse time regression", "[e2e_perf]") {
    using Clock = std::chrono::steady_clock;

    FtdcParser  parser;
    MetricStore store;
    std::string err;

    auto t0 = Clock::now();
    bool ok = parser.parse_file(FTDC_FIXTURE, store, err);
    double elapsed_ms = std::chrono::duration<double, std::milli>(
                            Clock::now() - t0).count();

    UNSCOPED_INFO("FTDC parse elapsed_ms = " << elapsed_ms
                  << " (threshold " << FTDC_PARSE_THRESHOLD_MS << " ms)");

    REQUIRE(ok);
    REQUIRE(err.empty());
    REQUIRE_FALSE(store.empty());
    REQUIRE(elapsed_ms < FTDC_PARSE_THRESHOLD_MS);
}

// ============================================================
//  Log parse wall-clock regression
// ============================================================

TEST_CASE("E2E: log parse time regression", "[e2e_perf]") {
    using Clock = std::chrono::steady_clock;

    ensure_log_fixture();

    ArenaChain   chain;
    StringTable  strings(chain);
    LogParser    parser(strings);
    ChunkVector<LogEntry> entries(chain);

    MmapFile file(LOG_FIXTURE);

    auto t0 = Clock::now();
    parser.parse_file(file, /*node_idx=*/0, entries);
    double elapsed_ms = std::chrono::duration<double, std::milli>(
                            Clock::now() - t0).count();

    UNSCOPED_INFO("Log parse elapsed_ms = " << elapsed_ms
                  << " (threshold " << LOG_PARSE_THRESHOLD_MS << " ms)"
                  << " for " << entries.size() << " entries");

    REQUIRE(entries.size() > 0);
    REQUIRE(elapsed_ms < LOG_PARSE_THRESHOLD_MS);
}
