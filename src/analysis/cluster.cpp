#include "cluster.hpp"
#include "../parser/log_parser.hpp"

#include <simdjson.h>
#include <algorithm>
#include <stdexcept>
#include <cstring>

// ------------------------------------------------------------
//  Constructor
// ------------------------------------------------------------
Cluster::Cluster(size_t arena_bytes)
    : arena_(arena_bytes)
{
    strings_ = std::make_unique<StringTable>(arena_);
    entries_ = std::make_unique<ArenaVector<LogEntry>>(arena_, 1024 * 1024);
}

// ------------------------------------------------------------
//  add_file
// ------------------------------------------------------------
void Cluster::add_file(const std::string& path) {
    file_paths_.push_back(path);
}

// ------------------------------------------------------------
//  infer_hostname — scan first ~8KB of the file for "host" field.
//  Uses padded_string to satisfy SIMDJSON_PADDING requirement.
//  Falls back to the filename stem if no host field is found.
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
                // Use padded_string so simdjson can read SIMDJSON_PADDING
                // bytes past the end without undefined behaviour
                simdjson::padded_string ps(p, line_len);
                simdjson::dom::element doc;
                if (parser.parse(ps).get(doc) == simdjson::SUCCESS) {
                    std::string_view host;
                    if (doc["host"].get_string().get(host) == simdjson::SUCCESS
                        && !host.empty())
                    {
                        return std::string(host);
                    }
                }
            }
            if (!nl) break;
            p = nl + 1;
        }
    }

    // No host field found — derive a readable name from the file path.
    // Take the last path component and strip the extension.
    std::string stem = path;
    // Strip directory
    size_t slash = stem.rfind('/');
    if (slash != std::string::npos) stem = stem.substr(slash + 1);
    // Strip extension (last '.' onwards)
    size_t dot = stem.rfind('.');
    if (dot != std::string::npos) stem = stem.substr(0, dot);
    return stem.empty() ? path : stem;
}

// ------------------------------------------------------------
//  sort_entries_by_time
// ------------------------------------------------------------
void Cluster::sort_entries_by_time() {
    // std::sort on the raw data pointer — ArenaVector supports
    // random access through its data() pointer.
    LogEntry* begin = entries_->data();
    LogEntry* end_  = begin + entries_->size();
    std::sort(begin, end_, [](const LogEntry& a, const LogEntry& b) {
        return a.timestamp_ms < b.timestamp_ms;
    });
}

// ------------------------------------------------------------
//  dedup_entries — collapse identical messages within ±1 ms
//  across different nodes into a single entry with node_mask.
// ------------------------------------------------------------
void Cluster::dedup_entries() {
    if (entries_->size() < 2) return;

    // Entries are timestamp-sorted. Walk with two pointers.
    // Two entries are "duplicate" if same (msg_idx, severity,
    // component_idx, ns_idx) and |ts_a - ts_b| <= 1.
    //
    // Because entries are sorted by timestamp we only need to
    // look within a small sliding window.

    // We'll build the dedup'd list in a temporary std::vector
    // then copy back (arena doesn't support remove-if).
    std::vector<size_t> keep; // indices to keep
    keep.reserve(entries_->size());

    std::vector<bool> merged(entries_->size(), false);

    for (size_t i = 0; i < entries_->size(); ++i) {
        if (merged[i]) continue;
        LogEntry& ei = (*entries_)[i];
        keep.push_back(i);

        for (size_t j = i + 1; j < entries_->size(); ++j) {
            const LogEntry& ej = (*entries_)[j];
            // Window check — timestamps are sorted
            if (ej.timestamp_ms - ei.timestamp_ms > 1) break;

            if (!merged[j] &&
                ej.msg_idx        == ei.msg_idx &&
                ej.severity       == ei.severity &&
                ej.component_idx  == ei.component_idx &&
                ej.ns_idx         == ei.ns_idx &&
                ej.node_idx       != ei.node_idx)
            {
                ei.node_mask |= ej.node_mask;
                merged[j] = true;
            }
        }
    }

    // Rebuild entries_ in place using a fresh ArenaVector.
    // The old backing memory stays in the arena (dead weight until reset).
    auto* new_vec = new ArenaVector<LogEntry>(arena_, keep.size());
    for (size_t idx : keep)
        new_vec->push_back((*entries_)[idx]);

    entries_.reset(new_vec);
}

// ------------------------------------------------------------
//  load — blocking, call from background thread
// ------------------------------------------------------------
void Cluster::load() {
    state_.store(LoadState::Loading);
    progress_.store(0.0f);

    try {
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
        cfg.num_threads = 0; // auto
        LogParser parser(*strings_, cfg);

        float file_weight = 1.0f / static_cast<float>(n);

        for (uint16_t i = 0; i < n; ++i) {
            MmapFile file(file_paths_[i]);

            // Infer hostname from log content; fall back to filename stem
            nodes_[i].hostname = infer_hostname(file, file_paths_[i]);

            float base = static_cast<float>(i) * file_weight;
            parser.parse_file(file, i, *entries_,
                [&, base, file_weight](size_t done, size_t total) {
                    float frac = (total > 0)
                                 ? static_cast<float>(done) / static_cast<float>(total)
                                 : 1.0f;
                    progress_.store(base + frac * file_weight);
                });
        }

        // Sort all entries by timestamp
        sort_entries_by_time();

        // Deduplicate cross-node identical messages
        dedup_entries();

        // Analysis pass
        analysis_ = Analyzer::analyze(*entries_, *strings_);

        progress_.store(1.0f);
        state_.store(LoadState::Ready);

    } catch (const std::exception& ex) {
        error_msg_ = ex.what();
        state_.store(LoadState::Error);
    }
}
