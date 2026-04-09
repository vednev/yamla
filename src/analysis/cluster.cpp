#include "cluster.hpp"
#include "../parser/log_parser.hpp"

#include <simdjson.h>
#include <algorithm>
#include <stdexcept>
#include <cstring>

// ------------------------------------------------------------
//  add_file
// ------------------------------------------------------------
void Cluster::add_file(const std::string& path) {
    file_paths_.push_back(path);
}

// ------------------------------------------------------------
//  infer_hostname
// ------------------------------------------------------------
std::string Cluster::infer_hostname(const MmapFile& file,
                                     const std::string& path) {
    if (file.size() > 0) {
        size_t scan_len = std::min<size_t>(file.size(), 8192);
        const char* p   = file.data();
        const char* end = p + scan_len;

        simdjson::dom::parser parser;

        while (p < end) {
            const char* nl = static_cast<const char*>(
                std::memchr(p, '\n', static_cast<size_t>(end - p)));
            size_t line_len = nl ? static_cast<size_t>(nl - p)
                                 : static_cast<size_t>(end - p);

            if (line_len > 0 && p[0] == '{') {
                simdjson::padded_string ps(p, line_len);
                simdjson::dom::element doc;
                if (parser.parse(ps).get(doc) == simdjson::SUCCESS) {
                    std::string_view host;
                    if (doc["host"].get_string().get(host) == simdjson::SUCCESS
                        && !host.empty()
                        && host != "mongod"
                        && host != "mongos"
                        && host != "mongo")
                    {
                        return std::string(host);
                    }
                }
            }
            if (!nl) break;
            p = nl + 1;
        }
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

    for (size_t i = 0; i < n; ++i) {
        if (merged[i]) continue;
        LogEntry& ei = (*entries_)[i];
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
                merged[j] = true;
            }
        }
    }

    // Rebuild into a new ChunkVector in the entry_chain_
    auto* new_vec = new ChunkVector<LogEntry>(entry_chain_);
    for (size_t idx : keep)
        new_vec->push_back((*entries_)[idx]);

    entries_.reset(new_vec);
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
            nodes_[i].color = pastel_color(i, n);
        }

        // Parse each file
        LogParser::Config cfg;
        cfg.num_threads  = 0; // auto
        cfg.sample_ratio = sample_ratio_;
        LogParser parser(*strings_, cfg);

        float file_weight = 1.0f / static_cast<float>(n);

        for (uint16_t i = 0; i < n; ++i) {
            // Open, parse, then drop the mmap (detail view re-opens on demand)
            {
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
            }
        }

        sort_entries_by_time();
        dedup_entries();

        analysis_ = Analyzer::analyze(*entries_, *strings_);

        progress_.store(1.0f);
        state_.store(LoadState::Ready);

    } catch (const std::exception& ex) {
        error_msg_ = ex.what();
        state_.store(LoadState::Error);
    }
}
