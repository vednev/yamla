#include "cluster.hpp"
#include "../parser/log_parser.hpp"

#include <simdjson.h>
#include <algorithm>
#include <stdexcept>
#include <cstdio>
#include <cstring>

// ------------------------------------------------------------
//  add_file
// ------------------------------------------------------------
void Cluster::add_file(const std::string& path) {
    if (file_paths_.size() >= 32) {
        std::fprintf(stderr, "Warning: max 32 nodes supported, ignoring %s\n", path.c_str());
        return;
    }
    file_paths_.push_back(path);
}

// ------------------------------------------------------------
//  infer_hostname
// ------------------------------------------------------------
std::string Cluster::infer_hostname(const MmapFile& file,
                                     const std::string& path) {
    if (file.size() > 0) {
        // Scan up to 64KB — "Process Details" appears in the first few
        // startup lines but there can be preamble. 64KB is plenty.
        size_t scan_len = std::min<size_t>(file.size(), 65536);
        const char* p   = file.data();
        const char* end = p + scan_len;

        simdjson::dom::parser parser;

        // Best candidate so far (from top-level "host" field)
        std::string fallback_host;

        while (p < end) {
            const char* nl = static_cast<const char*>(
                std::memchr(p, '\n', static_cast<size_t>(end - p)));
            size_t line_len = nl ? static_cast<size_t>(nl - p)
                                 : static_cast<size_t>(end - p);

            if (line_len > 0 && p[0] == '{') {
                simdjson::padded_string ps(p, line_len);
                simdjson::dom::element doc;
                if (parser.parse(ps).get(doc) == simdjson::SUCCESS) {
                    // Priority 1: look for "Process Details" msg — attr.host
                    // has the real FQDN (e.g. "myhost.example.com:27017")
                    std::string_view msg;
                    if (doc["msg"].get_string().get(msg) == simdjson::SUCCESS &&
                        msg == "Process Details")
                    {
                        simdjson::dom::element attr;
                        std::string_view host;
                        if (doc["attr"].get(attr) == simdjson::SUCCESS &&
                            attr["host"].get_string().get(host) == simdjson::SUCCESS &&
                            !host.empty())
                        {
                            // Strip the port suffix if present (e.g. ":27017")
                            std::string h(host);
                            size_t colon = h.rfind(':');
                            if (colon != std::string::npos) {
                                // Only strip if what follows the colon is all digits (a port)
                                bool all_digits = true;
                                for (size_t ci = colon + 1; ci < h.size(); ++ci) {
                                    if (h[ci] < '0' || h[ci] > '9') {
                                        all_digits = false;
                                        break;
                                    }
                                }
                                if (all_digits && colon + 1 < h.size())
                                    h = h.substr(0, colon);
                            }
                            return h;
                        }
                    }

                    // Priority 2: top-level "host" field (skip mongod/mongos)
                    if (fallback_host.empty()) {
                        std::string_view host;
                        if (doc["host"].get_string().get(host) == simdjson::SUCCESS
                            && !host.empty()
                            && host != "mongod"
                            && host != "mongos"
                            && host != "mongo")
                        {
                            fallback_host = std::string(host);
                        }
                    }
                }
            }
            if (!nl) break;
            p = nl + 1;
        }

        if (!fallback_host.empty())
            return fallback_host;
    }

    // Derive from filename stem
    std::string stem = path;
    size_t slash = stem.rfind('/');
    if (slash != std::string::npos) stem = stem.substr(slash + 1);
    size_t dot = stem.rfind('.');
    if (dot != std::string::npos) stem = stem.substr(0, dot);
    return stem.empty() ? path : stem;
}

// ------------------------------------------------------------
//  infer_identity — hostname + port from log preamble
// ------------------------------------------------------------
Cluster::FileIdentity Cluster::infer_identity(const MmapFile& file,
                                               const std::string& path) {
    FileIdentity id;

    if (file.size() > 0) {
        size_t scan_len = std::min<size_t>(file.size(), 65536);
        const char* p   = file.data();
        const char* end = p + scan_len;

        simdjson::dom::parser parser;
        std::string fallback_host;

        while (p < end) {
            const char* nl = static_cast<const char*>(
                std::memchr(p, '\n', static_cast<size_t>(end - p)));
            size_t line_len = nl ? static_cast<size_t>(nl - p)
                                 : static_cast<size_t>(end - p);

            if (line_len > 0 && p[0] == '{') {
                simdjson::padded_string ps(p, line_len);
                simdjson::dom::element doc;
                if (parser.parse(ps).get(doc) == simdjson::SUCCESS) {
                    // Priority 1: "Process Details" — attr.host has FQDN:port
                    std::string_view msg;
                    if (doc["msg"].get_string().get(msg) == simdjson::SUCCESS &&
                        msg == "Process Details")
                    {
                        simdjson::dom::element attr;
                        std::string_view host;
                        if (doc["attr"].get(attr) == simdjson::SUCCESS &&
                            attr["host"].get_string().get(host) == simdjson::SUCCESS &&
                            !host.empty())
                        {
                            std::string h(host);
                            size_t colon = h.rfind(':');
                            if (colon != std::string::npos) {
                                bool all_digits = true;
                                for (size_t ci = colon + 1; ci < h.size(); ++ci) {
                                    if (h[ci] < '0' || h[ci] > '9') {
                                        all_digits = false;
                                        break;
                                    }
                                }
                                if (all_digits && colon + 1 < h.size()) {
                                    id.port = static_cast<uint16_t>(
                                        std::stoul(h.substr(colon + 1)));
                                    h = h.substr(0, colon);
                                }
                            }
                            // Also try attr.port if present
                            if (id.port == 0) {
                                int64_t p_val = 0;
                                if (attr["port"].get_int64().get(p_val) == simdjson::SUCCESS
                                    && p_val > 0 && p_val <= 65535)
                                    id.port = static_cast<uint16_t>(p_val);
                            }
                            id.hostname = h;
                            return id;
                        }
                    }

                    // Priority 2: top-level "host" field
                    if (fallback_host.empty()) {
                        std::string_view host;
                        if (doc["host"].get_string().get(host) == simdjson::SUCCESS
                            && !host.empty()
                            && host != "mongod"
                            && host != "mongos"
                            && host != "mongo")
                        {
                            fallback_host = std::string(host);
                        }
                    }
                }
            }
            if (!nl) break;
            p = nl + 1;
        }

        if (!fallback_host.empty()) {
            id.hostname = fallback_host;
            return id;
        }
    }

    // Derive from filename stem
    std::string stem = path;
    size_t slash = stem.rfind('/');
    if (slash != std::string::npos) stem = stem.substr(slash + 1);
    size_t dot = stem.rfind('.');
    if (dot != std::string::npos) stem = stem.substr(0, dot);
    id.hostname = stem.empty() ? path : stem;
    return id;
}

// ------------------------------------------------------------
//  strip_rotation_suffix
//
//  "mongod.log.2024-01-15T00-00-00" → "mongod.log"
//  "mongod.log"                      → "mongod.log"
// ------------------------------------------------------------
static std::string strip_rotation_suffix(const std::string& filename) {
    // MongoDB rotation suffixes are dates like ".2024-01-15T00-00-00"
    // Pattern: .YYYY-MM-DDThh-mm-ss (20 chars after the dot)
    size_t dot = filename.rfind('.');
    if (dot == std::string::npos || dot + 1 >= filename.size())
        return filename;

    // Check if the suffix after the last dot looks like a rotation date
    std::string_view suffix(filename.data() + dot + 1, filename.size() - dot - 1);
    if (suffix.size() >= 19 && suffix[4] == '-' && suffix[7] == '-' &&
        suffix[10] == 'T')
        return filename.substr(0, dot);

    return filename;
}

// Extract (directory, base_filename) for grouping files by name
static std::pair<std::string, std::string> file_group_key(const std::string& path) {
    std::string dir, file;
    size_t slash = path.rfind('/');
#if defined(_WIN32)
    size_t bslash = path.rfind('\\');
    if (bslash != std::string::npos &&
        (slash == std::string::npos || bslash > slash))
        slash = bslash;
#endif
    if (slash != std::string::npos) {
        dir  = path.substr(0, slash);
        file = path.substr(slash + 1);
    } else {
        file = path;
    }
    return {dir, strip_rotation_suffix(file)};
}

// ------------------------------------------------------------
//  merge_nodes — group files by (hostname, port) identity
// ------------------------------------------------------------
void Cluster::merge_nodes() {
    if (nodes_.size() <= 1) return;

    // Phase 1: group file indices by identity key
    struct Group {
        std::string             key;
        std::string             hostname;
        uint16_t                port = 0;
        std::vector<uint16_t>   file_indices;  // original node indices
    };

    std::vector<Group> groups;
    std::unordered_map<std::string, size_t> key_to_group;

    bool all_have_hostname = true;

    for (uint16_t i = 0; i < static_cast<uint16_t>(nodes_.size()); ++i) {
        const auto& n = nodes_[i];

        // Check if the hostname was derived from Process Details or host field
        // (not from filename stem). Heuristic: if port != 0 or hostname
        // contains a dot, it's a real hostname.
        bool has_real_hostname = (n.port != 0) ||
                                (n.hostname.find('.') != std::string::npos);

        std::string key;
        if (has_real_hostname) {
            // Group by (hostname, port)
            key = n.hostname + ":" + std::to_string(n.port);
        } else {
            all_have_hostname = false;
            // Fallback: group by (directory, base_filename)
            auto [dir, base] = file_group_key(n.path);
            key = dir + "/" + base;
        }

        auto it = key_to_group.find(key);
        if (it != key_to_group.end()) {
            groups[it->second].file_indices.push_back(i);
        } else {
            Group g;
            g.key      = key;
            g.hostname = n.hostname;
            g.port     = n.port;
            g.file_indices.push_back(i);
            key_to_group[key] = groups.size();
            groups.push_back(std::move(g));
        }
    }

    // If no merging would happen, bail out early
    if (groups.size() == nodes_.size()) return;

    // Phase 2: build old→new index mapping
    std::vector<uint16_t> old_to_new(nodes_.size(), 0);
    for (uint16_t gi = 0; gi < static_cast<uint16_t>(groups.size()); ++gi) {
        for (uint16_t fi : groups[gi].file_indices)
            old_to_new[fi] = gi;
    }

    // Phase 3: re-stamp every LogEntry
    for (size_t i = 0; i < entries_->size(); ++i) {
        LogEntry& e = (*entries_)[i];
        uint16_t old_idx = e.node_idx;
        uint16_t new_idx = old_to_new[old_idx];
        e.node_idx = new_idx;

        // Rebuild node_mask from scratch — collect all old node bits,
        // map each to its new index, set new bits
        uint32_t old_mask = e.node_mask;
        uint32_t new_mask = 0;
        for (uint16_t b = 0; b < 32 && b < nodes_.size(); ++b) {
            if (old_mask & (1u << b))
                new_mask |= (1u << old_to_new[b]);
        }
        e.node_mask = new_mask;
    }

    // Phase 4: re-stamp DedupAlt entries
    for (auto& [idx, alts] : dedup_alts_) {
        for (auto& alt : alts)
            alt.node_idx = old_to_new[alt.node_idx];
    }

    // Phase 5: rebuild nodes_ vector — one entry per logical node
    std::vector<NodeInfo> merged_nodes;
    merged_nodes.reserve(groups.size());

    for (uint16_t gi = 0; gi < static_cast<uint16_t>(groups.size()); ++gi) {
        const auto& g = groups[gi];
        NodeInfo ni;
        ni.idx      = gi;
        ni.hostname = g.hostname;
        ni.port     = g.port;
        ni.color    = node_color(gi);

        // Use first file's path as the canonical path; track all merged paths
        ni.path = nodes_[g.file_indices[0]].path;
        for (uint16_t fi : g.file_indices)
            ni.merged_paths.push_back(nodes_[fi].path);

        merged_nodes.push_back(std::move(ni));
    }

    nodes_ = std::move(merged_nodes);

    // Phase 6: rebuild file_paths_ to match new nodes
    // (Each node may now represent multiple files)
    // Keep file_paths_ as a flat list of all files for reference
    // (no change needed — it's already the full list)

    (void)all_have_hostname; // may be used for ambiguity dialog in the future
}

// ------------------------------------------------------------
//  sort_entries_by_time — chunk-aware merge sort
// ------------------------------------------------------------
void Cluster::sort_entries_by_time() {
    entries_->sort(scratch_chain_,
                   [](const LogEntry& a, const LogEntry& b) {
                       return a.timestamp_ms < b.timestamp_ms;
                   });
}

// ------------------------------------------------------------
//  dedup_entries
// ------------------------------------------------------------
void Cluster::dedup_entries() {
    size_t n = entries_->size();
    if (n < 2) return;

    std::vector<bool> merged(n, false);
    std::vector<size_t> keep;
    keep.reserve(n);

    // Temporary map: keep-index -> vector of merged entries' raw positions
    // We use the position in the keep vector as the key (final entry index).
    std::unordered_map<size_t, std::vector<DedupAlt>> alts;

    for (size_t i = 0; i < n; ++i) {
        if (merged[i]) continue;
        LogEntry& ei = (*entries_)[i];
        size_t keep_idx = keep.size();
        keep.push_back(i);

        for (size_t j = i + 1; j < n; ++j) {
            const LogEntry& ej = (*entries_)[j];
            if (ej.timestamp_ms - ei.timestamp_ms > 1) break;

            if (!merged[j] &&
                ej.msg_idx       == ei.msg_idx &&
                ej.severity      == ei.severity &&
                ej.component_idx == ei.component_idx &&
                ej.ns_idx        == ei.ns_idx &&
                ej.node_idx      != ei.node_idx)
            {
                ei.node_mask |= ej.node_mask;
                // Save the merged entry's raw position so we can
                // show any node's version of this stacked entry.
                alts[keep_idx].push_back({ej.node_idx, ej.file_idx, ej.raw_offset, ej.raw_len});
                merged[j] = true;
            }
        }
    }

    // Rebuild into a new ChunkVector in the entry_chain_
    auto* new_vec = new ChunkVector<LogEntry>(entry_chain_);
    for (size_t idx : keep)
        new_vec->push_back((*entries_)[idx]);

    entries_.reset(new_vec);
    dedup_alts_ = std::move(alts);
}

// ------------------------------------------------------------
//  load
// ------------------------------------------------------------
void Cluster::load() {
    state_.store(LoadState::Loading);
    progress_.store(0.0f);

    try {
        // Init storage
        strings_ = std::make_unique<StringTable>(string_chain_);
        entries_ = std::make_unique<ChunkVector<LogEntry>>(entry_chain_);

        // Build NodeInfo list
        uint16_t n = static_cast<uint16_t>(file_paths_.size());
        nodes_.resize(n);
        for (uint16_t i = 0; i < n; ++i) {
            nodes_[i].idx   = i;
            nodes_[i].path  = file_paths_[i];
            nodes_[i].color = node_color(i);
        }

        // Parse each file
        LogParser::Config cfg;
        cfg.num_threads  = 0; // auto
        cfg.sample_ratio = sample_ratio_;
        LogParser parser(*strings_, cfg);

        float file_weight = 1.0f / static_cast<float>(n);
        failed_files_.clear();

        for (uint16_t i = 0; i < n; ++i) {
            // Open, parse, then drop the mmap (detail view re-opens on demand)
            {
                size_t entries_before = entries_->size();
                MmapFile file(file_paths_[i]);
                auto ident = infer_identity(file, file_paths_[i]);
                nodes_[i].hostname = ident.hostname;
                nodes_[i].port     = ident.port;

                float base = static_cast<float>(i) * file_weight;
                parser.parse_file(file, i, *entries_,
                    [&, base, file_weight](size_t done, size_t total) {
                        float frac = (total > 0)
                                     ? static_cast<float>(done) / static_cast<float>(total)
                                     : 1.0f;
                        progress_.store(base + frac * file_weight);
                    });
                // MmapFile destructs here — closes the mmap.
                // The detail view will re-open the file on demand.

                // Track files that produced zero entries (not valid log format)
                if (entries_->size() == entries_before && file.size() > 0) {
                    // Extract basename for display
                    const std::string& p = file_paths_[i];
                    size_t slash = p.rfind('/');
#if defined(_WIN32)
                    size_t bslash = p.rfind('\\');
                    if (bslash != std::string::npos &&
                        (slash == std::string::npos || bslash > slash))
                        slash = bslash;
#endif
                    std::string basename = (slash != std::string::npos)
                                           ? p.substr(slash + 1) : p;
                    failed_files_.push_back(std::move(basename));
                }
            }
        }

        sort_entries_by_time();
        scratch_chain_.reset();  // free merge-sort scratch memory
        if (dedup_enabled_) dedup_entries();
        merge_nodes();

        analysis_ = Analyzer::analyze(*entries_, *strings_);

        progress_.store(1.0f);
        state_.store(LoadState::Ready);

    } catch (const std::exception& ex) {
        error_msg_ = ex.what();
        state_.store(LoadState::Error);
    }
}

// ------------------------------------------------------------
//  append_files — add new files to an existing cluster
// ------------------------------------------------------------
void Cluster::append_files(const std::vector<std::string>& new_paths) {
    if (new_paths.empty()) return;

    state_.store(LoadState::Loading);
    progress_.store(0.0f);

    try {
        // Determine new node indices starting after existing nodes
        uint16_t old_node_count = static_cast<uint16_t>(nodes_.size());
        uint16_t new_count      = static_cast<uint16_t>(new_paths.size());
        uint16_t total_nodes    = old_node_count + new_count;

        // Add NodeInfo entries for the new files
        nodes_.resize(total_nodes);
        for (uint16_t i = 0; i < new_count; ++i) {
            uint16_t ni = old_node_count + i;
            nodes_[ni].idx   = ni;
            nodes_[ni].path  = new_paths[i];
            nodes_[ni].color = node_color(ni);
        }

        // No need to reassign existing node colours — they are
        // stable per-index with the fixed categorical palette.

        // Also track new paths in file_paths_
        for (const auto& p : new_paths)
            file_paths_.push_back(p);

        // Before re-sorting, we need to un-dedup: the current entries_
        // is already deduped. We can't undo that, but we don't need to —
        // new entries will merge in during dedup. We just need to rebuild
        // entries_ from scratch to include the new data.
        //
        // However, rebuilding from scratch means re-parsing ALL files.
        // That's expensive. Instead, we:
        //   1. Clear the dedup_alts_ (will be rebuilt)
        //   2. Parse only the new files, appending to entries_
        //   3. Re-sort the entire entries_ vector
        //   4. Re-dedup (will re-stack across old and new nodes)
        //   5. Re-analyze

        dedup_alts_.clear();

        // Parse the new files
        LogParser::Config cfg;
        cfg.num_threads  = 0;
        cfg.sample_ratio = sample_ratio_;
        LogParser parser(*strings_, cfg);

        float file_weight = 1.0f / static_cast<float>(new_count);

        for (uint16_t i = 0; i < new_count; ++i) {
            uint16_t ni = old_node_count + i;
            {
                size_t entries_before = entries_->size();
                MmapFile file(new_paths[i]);
                auto ident = infer_identity(file, new_paths[i]);
                nodes_[ni].hostname = ident.hostname;
                nodes_[ni].port     = ident.port;

                float base = static_cast<float>(i) * file_weight;
                parser.parse_file(file, ni, *entries_,
                    [&, base, file_weight](size_t done, size_t total) {
                        float frac = (total > 0)
                                     ? static_cast<float>(done) / static_cast<float>(total)
                                     : 1.0f;
                        progress_.store(base + frac * file_weight);
                    });

                // Track files that produced zero entries
                if (entries_->size() == entries_before && file.size() > 0) {
                    const std::string& p = new_paths[i];
                    size_t slash = p.rfind('/');
#if defined(_WIN32)
                    size_t bslash = p.rfind('\\');
                    if (bslash != std::string::npos &&
                        (slash == std::string::npos || bslash > slash))
                        slash = bslash;
#endif
                    std::string basename = (slash != std::string::npos)
                                           ? p.substr(slash + 1) : p;
                    failed_files_.push_back(std::move(basename));
                }
            }
        }

        sort_entries_by_time();
        scratch_chain_.reset();  // free merge-sort scratch memory
        if (dedup_enabled_) dedup_entries();
        merge_nodes();

        analysis_ = Analyzer::analyze(*entries_, *strings_);

        progress_.store(1.0f);
        state_.store(LoadState::Ready);

    } catch (const std::exception& ex) {
        error_msg_ = ex.what();
        state_.store(LoadState::Error);
    }
}

// ------------------------------------------------------------
//  get_node_raw — look up raw file position for a specific node
// ------------------------------------------------------------
bool Cluster::get_node_raw(size_t entry_idx, uint16_t node_idx,
                            uint64_t& out_offset, uint32_t& out_len,
                            uint16_t& out_file_idx) const
{
    if (entry_idx >= entries_->size()) return false;
    const LogEntry& e = (*entries_)[entry_idx];

    // If the entry's own node matches, use its raw position directly
    if (e.node_idx == node_idx) {
        out_offset   = e.raw_offset;
        out_len      = e.raw_len;
        out_file_idx = e.file_idx;
        return true;
    }

    // Check the dedup alts map
    auto it = dedup_alts_.find(entry_idx);
    if (it == dedup_alts_.end()) return false;

    for (const auto& alt : it->second) {
        if (alt.node_idx == node_idx) {
            out_offset   = alt.raw_offset;
            out_len      = alt.raw_len;
            out_file_idx = alt.file_idx;
            return true;
        }
    }
    return false;
}
