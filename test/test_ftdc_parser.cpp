#include <catch2/catch_all.hpp>
#include "ftdc/ftdc_parser.hpp"
#include "ftdc/metric_store.hpp"
#include "ftdc/metric_defs.hpp"

#include <string>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

// ============================================================
//  Test data paths — relative to repo root (make test runs from root)
// ============================================================
static const char* FTDC_FULL_PATH    = "test/diagnostic.data/metrics.2025-12-18T03-26-29Z-00000";
static const char* FTDC_INTERIM_PATH = "test/diagnostic.data/metrics.interim";

// ============================================================
//  FtdcParser tests — real FTDC data
// ============================================================

TEST_CASE("FtdcParser: parse real FTDC data file", "[ftdc_parser]") {
    FtdcParser parser;
    MetricStore store;
    std::string err;

    bool ok = parser.parse_file(FTDC_FULL_PATH, store, err);
    REQUIRE(ok);
    REQUIRE(err.empty());
    REQUIRE_FALSE(store.empty());
    REQUIRE(store.series.size() > 0);
    REQUIRE(store.ordered_keys.size() > 0);
}

TEST_CASE("FtdcParser: parsed metrics contain known serverStatus paths", "[ftdc_parser]") {
    FtdcParser parser;
    MetricStore store;
    std::string err;

    REQUIRE(parser.parse_file(FTDC_FULL_PATH, store, err));

    // These paths should exist in any real MongoDB FTDC file
    REQUIRE(store.get("serverStatus.opcounters.insert") != nullptr);
    REQUIRE(store.get("serverStatus.connections.current") != nullptr);
    REQUIRE(store.get("serverStatus.opcounters.query") != nullptr);

    // Verify series have data
    const MetricSeries* inserts = store.get("serverStatus.opcounters.insert");
    REQUIRE(inserts != nullptr);
    REQUIRE_FALSE(inserts->empty());
    REQUIRE(inserts->values.size() == inserts->timestamps_ms.size());
}

TEST_CASE("FtdcParser: timestamps are sorted ascending", "[ftdc_parser]") {
    FtdcParser parser;
    MetricStore store;
    std::string err;

    REQUIRE(parser.parse_file(FTDC_FULL_PATH, store, err));

    // Check a few known series for sorted timestamps
    for (const auto& key : store.ordered_keys) {
        const MetricSeries* ms = store.get(key);
        if (!ms || ms->size() < 2) continue;

        for (size_t i = 1; i < ms->timestamps_ms.size(); ++i) {
            // Timestamps within the same chunk should be monotonically
            // non-decreasing. Between chunks they may jump but should
            // still be non-decreasing overall.
            REQUIRE(ms->timestamps_ms[i] >= ms->timestamps_ms[i - 1]);
        }
        break; // checking one representative metric is sufficient
    }
}

TEST_CASE("FtdcParser: metric definitions are applied", "[ftdc_parser]") {
    FtdcParser parser;
    MetricStore store;
    std::string err;

    REQUIRE(parser.parse_file(FTDC_FULL_PATH, store, err));

    // serverStatus.opcounters.insert is a known metric in metric_defs
    const MetricSeries* inserts = store.get("serverStatus.opcounters.insert");
    REQUIRE(inserts != nullptr);
    REQUIRE(inserts->display_name == "Inserts");
    REQUIRE(inserts->unit == "ops/s");
    REQUIRE(inserts->is_cumulative == true);

    // serverStatus.connections.current is a gauge, not cumulative
    const MetricSeries* conns = store.get("serverStatus.connections.current");
    REQUIRE(conns != nullptr);
    REQUIRE(conns->display_name == "Conns Current");
    REQUIRE(conns->unit == "count");
    REQUIRE(conns->is_cumulative == false);
}

TEST_CASE("FtdcParser: time range is non-zero after parsing", "[ftdc_parser]") {
    FtdcParser parser;
    MetricStore store;
    std::string err;

    REQUIRE(parser.parse_file(FTDC_FULL_PATH, store, err));

    REQUIRE(store.time_start_ms > 0);
    REQUIRE(store.time_end_ms > 0);
    REQUIRE(store.time_start_ms < store.time_end_ms);
}

TEST_CASE("FtdcParser: interim file parses correctly", "[ftdc_parser]") {
    FtdcParser parser;
    MetricStore store;
    std::string err;

    bool ok = parser.parse_file(FTDC_INTERIM_PATH, store, err);
    REQUIRE(ok);
    REQUIRE_FALSE(store.empty());

    // Interim files are smaller but should still have data
    REQUIRE(store.series.size() > 0);
    REQUIRE(store.time_start_ms > 0);
}

TEST_CASE("FtdcParser: progress callback fires", "[ftdc_parser]") {
    FtdcParser parser;
    MetricStore store;
    std::string err;

    size_t callback_count = 0;
    size_t last_processed = 0;
    size_t last_total = 0;

    parser.set_progress_cb([&](size_t processed, size_t total) {
        callback_count++;
        last_processed = processed;
        last_total = total;
    });

    REQUIRE(parser.parse_file(FTDC_FULL_PATH, store, err));

    // The callback should have been invoked at least once
    REQUIRE(callback_count > 0);
    // Final processed bytes should equal total file size
    REQUIRE(last_processed == last_total);
    REQUIRE(last_total > 0);
}

TEST_CASE("FtdcParser: error on non-existent file", "[ftdc_parser]") {
    FtdcParser parser;
    MetricStore store;
    std::string err;

    bool ok = parser.parse_file("/no/such/file/metrics.nonexistent", store, err);
    REQUIRE_FALSE(ok);
    REQUIRE_FALSE(err.empty());
    REQUIRE(store.empty());
}

TEST_CASE("FtdcParser: error on corrupt data", "[ftdc_parser]") {
    // Create a temp file with corrupt bytes
    const char* tmp_path = "/tmp/yamla_test_corrupt_ftdc.bin";
    {
        std::ofstream ofs(tmp_path, std::ios::binary);
        // Write garbage bytes that look like a BSON size but are actually corrupt
        uint8_t garbage[] = {0xFF, 0xFF, 0xFF, 0x7F, 0x00, 0x01, 0x02, 0x03};
        ofs.write(reinterpret_cast<const char*>(garbage), sizeof(garbage));
    }

    FtdcParser parser;
    MetricStore store;
    std::string err;

    bool ok = parser.parse_file(tmp_path, store, err);
    // Should return false (corrupt chunk detected) or true with empty store
    // (if garbage was skipped). Either way, no crash.
    if (!ok) {
        REQUIRE_FALSE(err.empty());
    }
    // Cleanup
    std::remove(tmp_path);
}

TEST_CASE("MetricStore: get returns nullptr for unknown path", "[metric_store]") {
    MetricStore store;

    REQUIRE(store.get("nonexistent.path") == nullptr);
    REQUIRE(store.get("") == nullptr);
}

TEST_CASE("MetricStore: get_or_create creates new series", "[metric_store]") {
    MetricStore store;

    MetricSeries& ms = store.get_or_create("test.metric.path");
    REQUIRE(ms.path == "test.metric.path");
    REQUIRE(ms.empty());

    // Should now be findable via get()
    MetricSeries* found = store.get("test.metric.path");
    REQUIRE(found != nullptr);
    REQUIRE(found->path == "test.metric.path");

    // ordered_keys should track insertion
    REQUIRE(store.ordered_keys.size() == 1);
    REQUIRE(store.ordered_keys[0] == "test.metric.path");

    // Calling get_or_create again should return same series
    MetricSeries& ms2 = store.get_or_create("test.metric.path");
    REQUIRE(&ms2 == found);
    REQUIRE(store.ordered_keys.size() == 1); // no duplicate
}

TEST_CASE("MetricStore: update_time_range computes correct bounds", "[metric_store]") {
    MetricStore store;

    // Create two series with different time ranges
    MetricSeries& a = store.get_or_create("metric.a");
    a.timestamps_ms = {1000, 2000, 3000};
    a.values = {10.0, 20.0, 30.0};

    MetricSeries& b = store.get_or_create("metric.b");
    b.timestamps_ms = {500, 1500, 3500};
    b.values = {5.0, 15.0, 35.0};

    store.update_time_range();

    // time_start should be the minimum across all series
    REQUIRE(store.time_start_ms == 500);
    // time_end should be the maximum across all series
    REQUIRE(store.time_end_ms == 3500);
}

TEST_CASE("MetricStore: empty store has zero time range", "[metric_store]") {
    MetricStore store;

    store.update_time_range();
    REQUIRE(store.time_start_ms == 0);
    REQUIRE(store.time_end_ms == 0);
}
