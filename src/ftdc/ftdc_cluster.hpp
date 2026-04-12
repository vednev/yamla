#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <memory>
#include <cassert>

#include "metric_store.hpp"

// ------------------------------------------------------------
//  FtdcCluster
//
//  Loads all FTDC metric files from a diagnostic.data directory.
//  Files are loaded in filename-sorted order (metrics.* files),
//  then merged into a single MetricStore.
//
//  Call load() from a background thread; poll state() from the
//  main thread.
// ------------------------------------------------------------
class FtdcCluster {
public:
    FtdcCluster() = default;
    ~FtdcCluster() = default;

    FtdcCluster(const FtdcCluster&)            = delete;
    FtdcCluster& operator=(const FtdcCluster&) = delete;

    // Set the path to a diagnostic.data directory (or a single metrics.* file).
    void set_path(const std::string& path) { path_ = path; }

    // Synchronous load — call from a background thread.
    void load();

    // ---- State polling (main thread) ----
    FtdcLoadState state()    const { return state_.load(); }
    float         progress() const { return progress_.load(); }
    const std::string& error()  const { return error_msg_; }

    // ---- Results (valid only when state() == Ready) ----
    bool has_store() const { return store_ != nullptr; }
    const MetricStore& store()  const { assert(store_); return *store_; }
    MetricStore&       store()        { assert(store_); return *store_; }

private:
    std::string path_;

    std::unique_ptr<MetricStore> store_;

    std::atomic<FtdcLoadState> state_    { FtdcLoadState::Idle };
    std::atomic<float>         progress_ { 0.0f };
    std::string                error_msg_;
};
