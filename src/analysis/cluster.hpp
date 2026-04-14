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
//  NodeColor — fixed categorical RGBA assigned per cluster node
// ------------------------------------------------------------
struct NodeColor {
    float r, g, b, a;
};

// Fixed 12-colour palette — node 0 is always sky blue, node 1
// always orange, etc.  Stable regardless of total node count.
inline NodeColor node_color(uint16_t node_idx) {
    static constexpr NodeColor palette[] = {
        {0.302f, 0.788f, 0.965f, 1.0f},  // #4dc9f6 sky blue
        {0.965f, 0.439f, 0.098f, 1.0f},  // #f67019 orange
        {0.659f, 0.878f, 0.373f, 1.0f},  // #a8e05f lime green
        {0.910f, 0.365f, 0.459f, 1.0f},  // #e85d75 rose red
        {0.769f, 0.565f, 0.820f, 1.0f},  // #c490d1 lavender
        {0.965f, 0.788f, 0.267f, 1.0f},  // #f6c944 gold
        {0.341f, 0.769f, 0.725f, 1.0f},  // #57c4b9 teal
        {1.000f, 0.624f, 0.953f, 1.0f},  // #ff9ff3 pink
        {0.471f, 0.820f, 0.714f, 1.0f},  // #78d1b6 mint
        {0.769f, 0.639f, 0.353f, 1.0f},  // #c4a35a amber
        {0.486f, 0.549f, 0.961f, 1.0f},  // #7c8cf5 periwinkle
        {0.886f, 0.886f, 0.612f, 1.0f},  // #e2e29c pastel yellow
    };
    constexpr uint16_t N = sizeof(palette) / sizeof(palette[0]);
    return palette[node_idx % N];
}

// ------------------------------------------------------------
//  NodeInfo
// ------------------------------------------------------------
struct NodeInfo {
    std::string  path;
    std::string  hostname;
    uint16_t     port = 0;          // from "Process Details" attr.host
    NodeColor    color;
    uint16_t     idx = 0;
    bool         is_primary = false;
    std::vector<std::string> merged_paths; // files merged into this node
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

    // All file paths in parse order (indexed by file_idx on LogEntry)
    const std::vector<std::string>& file_paths() const { return file_paths_; }

    // Files that produced zero parsed entries (not valid log format)
    const std::vector<std::string>& failed_files() const { return failed_files_; }

    // For stacked (deduped) entries: get the raw file position for a
    // specific node. Returns true if found; fills out_offset/out_len
    // and out_file_idx.
    // If node_idx matches the entry's own node_idx, simply returns
    // the entry's raw_offset/raw_len/file_idx. Otherwise checks the
    // dedup alts.
    bool get_node_raw(size_t entry_idx, uint16_t node_idx,
                      uint64_t& out_offset, uint32_t& out_len,
                      uint16_t& out_file_idx) const;

    // Identity extracted from a log file's early lines.
    struct FileIdentity {
        std::string hostname;
        uint16_t    port = 0;
    };

    // Infer hostname from first lines; fall back to filename stem.
    static std::string infer_hostname(const MmapFile& file,
                                      const std::string& path);

    // Infer hostname AND port from "Process Details" or "host" field.
    static FileIdentity infer_identity(const MmapFile& file,
                                       const std::string& path);

private:
    void sort_entries_by_time();
    void dedup_entries();

    // After all files are parsed, merge files that came from the
    // same mongod instance (same hostname:port) into one logical node.
    // Re-stamps node_idx/node_mask on every LogEntry.
    void merge_nodes();

    // Two arena chains: one for strings (small, many), one for entries (large)
    ArenaChain string_chain_;
    ArenaChain entry_chain_;
    ArenaChain scratch_chain_; // used only during sort merge

    std::unique_ptr<StringTable>           strings_;
    std::unique_ptr<ChunkVector<LogEntry>> entries_;

    std::vector<std::string>  file_paths_;
    std::vector<NodeInfo>     nodes_;
    std::vector<std::string>  failed_files_;
    AnalysisResult            analysis_;

    // Per-node raw positions for deduped/stacked entries.
    // Key: final entry index in entries_.
    // Value: vector of {node_idx, raw_offset, raw_len} for merged duplicates.
    // The surviving entry's own raw_offset/raw_len is NOT stored here —
    // it's still in the LogEntry itself.
    struct DedupAlt {
        uint16_t node_idx;
        uint16_t file_idx;
        uint64_t raw_offset;
        uint32_t raw_len;
    };
    std::unordered_map<size_t, std::vector<DedupAlt>> dedup_alts_;

    float                     sample_ratio_ = 1.0f;

    std::atomic<LoadState>    state_    { LoadState::Idle };
    std::atomic<float>        progress_ { 0.0f };
    std::string               error_msg_;
};
