#pragma once

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <functional>
#include <cstdint>
#include <unordered_map>

#include "../core/arena_chain.hpp"
#include "../core/chunk_vector.hpp"
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

inline NodeColor pastel_color(uint16_t node_idx, uint16_t total_nodes) {
    float h = (total_nodes > 0)
              ? (static_cast<float>(node_idx) / static_cast<float>(total_nodes))
              : 0.0f;
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
//  NodeInfo
// ------------------------------------------------------------
struct NodeInfo {
    std::string  path;
    std::string  hostname;
    NodeColor    color;
    uint16_t     idx = 0;
    bool         is_primary = false;
};

// ------------------------------------------------------------
//  LoadState
// ------------------------------------------------------------
enum class LoadState { Idle, Loading, Ready, Error };

// ------------------------------------------------------------
//  Cluster
//
//  Owns ArenaChain (string storage), ChunkVector<LogEntry>
//  (entry storage, can exceed 2 GB), and AnalysisResult.
//
//  The mmap files used during parsing are closed after parsing
//  completes — the detail view re-opens files on demand.
// ------------------------------------------------------------
class Cluster {
public:
    explicit Cluster() = default;
    ~Cluster() = default;

    Cluster(const Cluster&)            = delete;
    Cluster& operator=(const Cluster&) = delete;

    void add_file(const std::string& path);

    // sample_ratio: 1.0 = full load, <1.0 = load only this fraction of lines.
    void set_sample_ratio(float ratio) { sample_ratio_ = ratio; }
    float sample_ratio() const { return sample_ratio_; }

    // Synchronous load — call from background thread.
    void load();

    // Append new files to an already-loaded cluster.
    // Parses only the new files, merges entries with existing data,
    // re-sorts by timestamp, re-deduplicates, and re-analyzes.
    // Synchronous — call from background thread.
    void append_files(const std::vector<std::string>& new_paths);

    // ---- State -------------------------------------------------
    LoadState          state()    const { return state_.load(); }
    float              progress() const { return progress_.load(); }
    const std::string& error()    const { return error_msg_; }

    // ---- Data (valid after state == Ready) --------------------
    const ChunkVector<LogEntry>& entries()  const { return *entries_; }
    ChunkVector<LogEntry>&       entries()        { return *entries_; }
    const StringTable&           strings()  const { return *strings_; }
    const AnalysisResult&        analysis() const { return analysis_; }
    const std::vector<NodeInfo>& nodes()    const { return nodes_; }

    // For stacked (deduped) entries: get the raw file position for a
    // specific node. Returns true if found; fills out_offset/out_len.
    // If node_idx matches the entry's own node_idx, simply returns
    // the entry's raw_offset/raw_len. Otherwise checks the dedup alts.
    bool get_node_raw(size_t entry_idx, uint16_t node_idx,
                      uint64_t& out_offset, uint32_t& out_len) const;

    // Infer hostname from first lines; fall back to filename stem.
    static std::string infer_hostname(const MmapFile& file,
                                      const std::string& path);

private:
    void sort_entries_by_time();
    void dedup_entries();

    // Two arena chains: one for strings (small, many), one for entries (large)
    ArenaChain string_chain_;
    ArenaChain entry_chain_;
    ArenaChain scratch_chain_; // used only during sort merge

    std::unique_ptr<StringTable>           strings_;
    std::unique_ptr<ChunkVector<LogEntry>> entries_;

    std::vector<std::string>  file_paths_;
    std::vector<NodeInfo>     nodes_;
    AnalysisResult            analysis_;

    // Per-node raw positions for deduped/stacked entries.
    // Key: final entry index in entries_.
    // Value: vector of {node_idx, raw_offset, raw_len} for merged duplicates.
    // The surviving entry's own raw_offset/raw_len is NOT stored here —
    // it's still in the LogEntry itself.
    struct DedupAlt {
        uint16_t node_idx;
        uint64_t raw_offset;
        uint32_t raw_len;
    };
    std::unordered_map<size_t, std::vector<DedupAlt>> dedup_alts_;

    float                     sample_ratio_ = 1.0f;

    std::atomic<LoadState>    state_    { LoadState::Idle };
    std::atomic<float>        progress_ { 0.0f };
    std::string               error_msg_;
};
