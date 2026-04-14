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
                alts[keep_idx].push_back({ej.node_idx, ej.raw_offset, ej.raw_len});
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
                nodes_[i].hostname = infer_hostname(file, file_paths_[i]);

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
        dedup_entries();

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
                nodes_[ni].hostname = infer_hostname(file, new_paths[i]);

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
        dedup_entries();

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
                            uint64_t& out_offset, uint32_t& out_len) const
{
    if (entry_idx >= entries_->size()) return false;
    const LogEntry& e = (*entries_)[entry_idx];

    // If the entry's own node matches, use its raw position directly
    if (e.node_idx == node_idx) {
        out_offset = e.raw_offset;
        out_len    = e.raw_len;
        return true;
    }

    // Check the dedup alts map
    auto it = dedup_alts_.find(entry_idx);
    if (it == dedup_alts_.end()) return false;

    for (const auto& alt : it->second) {
        if (alt.node_idx == node_idx) {
            out_offset = alt.raw_offset;
            out_len    = alt.raw_len;
            return true;
        }
    }
    return false;
}
