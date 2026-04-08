#pragma once

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <functional>
#include <cstdint>

#include "../core/arena.hpp"
#include "../core/arena_vector.hpp"
#include "../core/mmap_file.hpp"
#include "../parser/log_entry.hpp"
#include "../parser/log_parser.hpp"
#include "analyzer.hpp"

// ------------------------------------------------------------
//  NodeColor — pastel RGBA assigned per cluster node
// ------------------------------------------------------------
struct NodeColor {
    float r, g, b, a;
};

// Hue values evenly distributed around the HSL wheel.
// S=0.55, L=0.75 gives a distinct-but-pastel palette.
inline NodeColor pastel_color(uint16_t node_idx, uint16_t total_nodes) {
    float h = (total_nodes > 0)
              ? (static_cast<float>(node_idx) / static_cast<float>(total_nodes))
              : 0.0f;
    // HSL → RGB conversion (S=0.55, L=0.75)
    constexpr float s = 0.55f, l = 0.75f;
    auto hue2rgb = [](float p, float q, float t) -> float {
        if (t < 0) t += 1;
        if (t > 1) t -= 1;
        if (t < 1.0f/6) return p + (q - p) * 6 * t;
        if (t < 1.0f/2) return q;
        if (t < 2.0f/3) return p + (q - p) * (2.0f/3 - t) * 6;
        return p;
    };
    float q = (l < 0.5f) ? (l * (1 + s)) : (l + s - l * s);
    float p = 2 * l - q;
    return {
        hue2rgb(p, q, h + 1.0f/3),
        hue2rgb(p, q, h),
        hue2rgb(p, q, h - 1.0f/3),
        1.0f
    };
}

// ------------------------------------------------------------
//  NodeInfo — metadata for one cluster node
// ------------------------------------------------------------
struct NodeInfo {
    std::string  path;         // file path on disk
    std::string  hostname;     // inferred from log "host" field
    NodeColor    color;
    uint16_t     idx = 0;
    bool         is_primary = false; // true if "PRIMARY" role detected
};

// ------------------------------------------------------------
//  LoadState — progress tracking for async load
// ------------------------------------------------------------
enum class LoadState { Idle, Loading, Ready, Error };

// ------------------------------------------------------------
//  Cluster
//
//  Owns the arena, all LogEntry data, the StringTable, and
//  the post-parse AnalysisResult for a set of log files that
//  belong to one MongoDB replica-set cluster.
//
//  Loading is initiated via load_async(); check state() to
//  poll progress from the UI thread.
// ------------------------------------------------------------
class Cluster {
public:
    // Arena size: 1.5× the sum of all file sizes, capped at
    // available physical memory (caller is responsible for
    // reasonable limits).
    explicit Cluster(size_t arena_bytes);
    ~Cluster() = default;

    // Non-copyable
    Cluster(const Cluster&)            = delete;
    Cluster& operator=(const Cluster&) = delete;

    // Add a file path to load. Call before load().
    void add_file(const std::string& path);

    // Synchronous load + parse + analyze all added files.
    // Intended to be called from a background std::thread;
    // use state() / progress() to monitor from the UI thread.
    void load();

    // ---- State -------------------------------------------------
    LoadState          state()    const { return state_.load(); }
    float              progress() const { return progress_.load(); }
    const std::string& error()    const { return error_msg_; }

    // ---- Data (valid only after state == Ready) ----------------
    const ArenaVector<LogEntry>& entries()    const { return *entries_; }
    ArenaVector<LogEntry>&       entries()          { return *entries_; }
    const StringTable&           strings()    const { return *strings_; }
    const AnalysisResult&        analysis()   const { return analysis_; }
    const std::vector<NodeInfo>& nodes()      const { return nodes_; }

    // Infer hostname from the first few log lines; falls back to filename stem
    static std::string infer_hostname(const MmapFile& file,
                                      const std::string& path);

private:
    void sort_entries_by_time();
    void dedup_entries();

    ArenaAllocator             arena_;
    std::unique_ptr<StringTable>           strings_;
    std::unique_ptr<ArenaVector<LogEntry>> entries_;

    std::vector<std::string>  file_paths_;
    std::vector<NodeInfo>     nodes_;
    AnalysisResult            analysis_;

    std::atomic<LoadState>    state_    { LoadState::Idle };
    std::atomic<float>        progress_ { 0.0f };
    std::string               error_msg_;
};
