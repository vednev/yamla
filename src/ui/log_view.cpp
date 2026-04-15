#include "log_view.hpp"

#include <imgui.h>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <algorithm>
#include <string_view>

#include "../parser/log_entry.hpp"
#include "../analysis/cluster.hpp"
#include "colors.hpp"

// Soft pastel blue used for selected rows (black text over this is readable)
static constexpr ImU32 SEL_ROW_BG = IM_COL32(160, 200, 255, 255);

// ------------------------------------------------------------
//  Helpers
// ------------------------------------------------------------

static std::string to_lower(const std::string& s) {
    std::string out(s.size(), ' ');
    for (size_t i = 0; i < s.size(); ++i)
        out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
    return out;
}

// ------------------------------------------------------------
//  LogView::set_*
// ------------------------------------------------------------

void LogView::set_entries(const ChunkVector<LogEntry>* entries,
                           const StringTable* strings,
                           const std::vector<NodeInfo>* nodes)
{
    entries_ = entries;
    strings_ = strings;
    nodes_   = nodes;

    // D-11: Initialize all dimension bitmasks to the new entry count
    size_t n = entries_ ? entries_->size() : 0;
    mask_severity_.resize(n);
    mask_component_.resize(n);
    mask_op_type_.resize(n);
    mask_ns_.resize(n);
    mask_shape_.resize(n);
    mask_slow_query_.resize(n);
    mask_conn_id_.resize(n);
    mask_driver_.resize(n);
    mask_node_.resize(n);
    mask_time_window_.resize(n);
    mask_text_.resize(n);
    prev_filter_ = FilterState{}; // reset snapshot

    // D-12: Build trigram index (after parse complete)
    build_trigram_index();

    rebuild_filter_index();
}

void LogView::set_filter(FilterState* filter) {
    filter_ = filter;
}

void LogView::set_on_select(SelectCallback cb) {
    on_select_ = std::move(cb);
}

// ------------------------------------------------------------
//  entry_matches — retained for correctness fallback
// ------------------------------------------------------------
bool LogView::entry_matches(const LogEntry& e) const {
    if (!filter_ || !filter_->active()) return true;

    if (filter_->severity_filter &&
        static_cast<uint32_t>(e.severity) != filter_->severity_filter - 1)
        return false;

    if (!filter_->component_idx_include.empty() &&
        !filter_->component_idx_include.count(e.component_idx)) return false;
    if (filter_->op_type_idx && e.op_type_idx != filter_->op_type_idx) return false;
    if (filter_->driver_idx    && e.driver_idx     != filter_->driver_idx)    return false;
    if (filter_->ns_idx        && e.ns_idx         != filter_->ns_idx)        return false;
    if (filter_->shape_idx     && e.shape_idx      != filter_->shape_idx)     return false;

    // Slow query filter: match only entries MongoDB explicitly tagged as slow
    // (msg starts with "Slow"). Mirrors the count logic in Analyzer.
    if (filter_->slow_query_only) {
        bool is_slow = false;
        if (strings_ && e.msg_idx != 0) {
            std::string_view msg = strings_->get(e.msg_idx);
            is_slow = (msg.size() >= 4 &&
                       (msg[0]=='S'||msg[0]=='s') &&
                       msg[1]=='l' && msg[2]=='o' && msg[3]=='w');
        }
        if (!is_slow) return false;
    }

    // Set-based inclusion filters — non-empty means "show only these values"
    if (!filter_->conn_id_include.empty() &&
        !filter_->conn_id_include.count(e.conn_id)) return false;
    if (!filter_->driver_idx_include.empty() &&
        !filter_->driver_idx_include.count(e.driver_idx)) return false;
    if (!filter_->node_idx_include.empty() &&
        !filter_->node_idx_include.count(e.node_idx)) return false;

    if (!filter_->text_search.empty() && strings_) {
        std::string_view msg = strings_->get(e.msg_idx);
        if (search_lower_.empty())
            search_lower_ = to_lower(filter_->text_search);
        bool found = false;
        if (msg.size() >= search_lower_.size()) {
            for (size_t i = 0; i <= msg.size() - search_lower_.size(); ++i) {
                bool match = true;
                for (size_t j = 0; j < search_lower_.size() && match; ++j)
                    match = (std::tolower(static_cast<unsigned char>(msg[i+j]))
                             == search_lower_[j]);
                if (match) { found = true; break; }
            }
        }
        if (!found) return false;
    }

    // Time-window filter (set by FTDC cross-link)
    if (filter_->time_window_active) {
        if (e.timestamp_ms < filter_->time_window_start_ms ||
            e.timestamp_ms > filter_->time_window_end_ms)
            return false;
    }

    return true;
}

// ------------------------------------------------------------
//  D-12: build_trigram_index
// ------------------------------------------------------------
void LogView::build_trigram_index() {
    trigram_index_.clear();
    trigram_index_built_ = false;
    if (!entries_ || !strings_) return;
    size_t n = entries_->size();
    trigram_index_.reserve(n * 4); // estimate: avg 4 trigrams per message
    for (size_t i = 0; i < n; ++i) {
        const LogEntry& e = (*entries_)[i];
        if (e.msg_idx == 0) continue; // UNKNOWN sentinel
        std::string_view msg = strings_->get(e.msg_idx);
        for (size_t j = 0; j + 2 < msg.size(); ++j) {
            uint8_t c0 = static_cast<uint8_t>(std::tolower(static_cast<unsigned char>(msg[j])));
            uint8_t c1 = static_cast<uint8_t>(std::tolower(static_cast<unsigned char>(msg[j+1])));
            uint8_t c2 = static_cast<uint8_t>(std::tolower(static_cast<unsigned char>(msg[j+2])));
            uint32_t key = (uint32_t(c0) << 16) | (uint32_t(c1) << 8) | c2;
            trigram_index_.emplace_back(key, static_cast<uint32_t>(i));
        }
    }
    std::sort(trigram_index_.begin(), trigram_index_.end());
    trigram_index_built_ = true;
}

// ------------------------------------------------------------
//  D-12: search_trigram
// ------------------------------------------------------------
void LogView::search_trigram(const std::string& query, DimensionMask& mask) {
    size_t n = entries_ ? entries_->size() : 0;
    if (n == 0) return;

    // For queries < 3 chars, fall back to linear scan
    if (query.size() < 3 || !trigram_index_built_) {
        std::string q_lower;
        q_lower.reserve(query.size());
        for (char c : query) q_lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        mask.all_pass = false;
        for (size_t w = 0; w < mask.bits.size(); ++w) mask.bits[w] = 0;
        for (size_t i = 0; i < n; ++i) {
            const LogEntry& e = (*entries_)[i];
            if (e.msg_idx == 0) continue;
            std::string_view msg = strings_->get(e.msg_idx);
            bool found = false;
            for (size_t p = 0; p + q_lower.size() <= msg.size(); ++p) {
                bool match = true;
                for (size_t k = 0; k < q_lower.size(); ++k) {
                    if (std::tolower(static_cast<unsigned char>(msg[p+k])) != static_cast<unsigned char>(q_lower[k])) {
                        match = false; break;
                    }
                }
                if (match) { found = true; break; }
            }
            if (found) mask.set(i, true);
        }
        return;
    }

    // Extract trigrams from query
    std::vector<uint32_t> query_trigrams;
    for (size_t j = 0; j + 2 < query.size(); ++j) {
        uint8_t c0 = static_cast<uint8_t>(std::tolower(static_cast<unsigned char>(query[j])));
        uint8_t c1 = static_cast<uint8_t>(std::tolower(static_cast<unsigned char>(query[j+1])));
        uint8_t c2 = static_cast<uint8_t>(std::tolower(static_cast<unsigned char>(query[j+2])));
        query_trigrams.push_back((uint32_t(c0) << 16) | (uint32_t(c1) << 8) | c2);
    }

    // Intersect posting lists for each trigram
    std::vector<uint64_t> candidate_mask((n + 63) / 64, ~uint64_t(0));
    for (uint32_t trig : query_trigrams) {
        // Binary search for range [trig, trig] in sorted trigram_index_
        auto lo = std::lower_bound(trigram_index_.begin(), trigram_index_.end(),
                                   std::make_pair(trig, uint32_t(0)));
        auto hi = std::upper_bound(lo, trigram_index_.end(),
                                   std::make_pair(trig, UINT32_MAX));
        std::vector<uint64_t> trig_mask((n + 63) / 64, 0);
        for (auto it = lo; it != hi; ++it) {
            size_t idx = it->second;
            trig_mask[idx / 64] |= (uint64_t(1) << (idx % 64));
        }
        for (size_t w = 0; w < candidate_mask.size(); ++w)
            candidate_mask[w] &= trig_mask[w];
    }

    // Exact-match verify candidates
    std::string q_lower;
    q_lower.reserve(query.size());
    for (char c : query) q_lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    mask.all_pass = false;
    for (size_t w = 0; w < mask.bits.size(); ++w) mask.bits[w] = 0;
    for (size_t w = 0; w < candidate_mask.size(); ++w) {
        uint64_t word = candidate_mask[w];
        while (word) {
            int bit = __builtin_ctzll(word);
            size_t idx = w * 64 + bit;
            if (idx < n) {
                const LogEntry& e = (*entries_)[idx];
                if (e.msg_idx != 0) {
                    std::string_view msg = strings_->get(e.msg_idx);
                    for (size_t p = 0; p + q_lower.size() <= msg.size(); ++p) {
                        bool match = true;
                        for (size_t k = 0; k < q_lower.size(); ++k) {
                            if (std::tolower(static_cast<unsigned char>(msg[p+k])) != static_cast<unsigned char>(q_lower[k])) {
                                match = false; break;
                            }
                        }
                        if (match) { mask.set(idx, true); break; }
                    }
                }
            }
            word &= word - 1; // clear lowest set bit
        }
    }
}

// ------------------------------------------------------------
//  D-11: Per-dimension mask rebuild functions
// ------------------------------------------------------------

void LogView::rebuild_severity_mask() {
    size_t n = entries_ ? entries_->size() : 0;
    if (n == 0) return;
    if (filter_->severity_filter == 0) {
        mask_severity_.clear_all();
        return;
    }
    mask_severity_.all_pass = false;
    for (size_t w = 0; w < mask_severity_.bits.size(); ++w) mask_severity_.bits[w] = 0;
    for (size_t i = 0; i < n; ++i) {
        bool pass = (static_cast<uint32_t>((*entries_)[i].severity) + 1 == filter_->severity_filter);
        mask_severity_.set(i, pass);
    }
}

void LogView::rebuild_component_mask() {
    size_t n = entries_ ? entries_->size() : 0;
    if (n == 0) return;
    if (filter_->component_idx_include.empty()) {
        mask_component_.clear_all();
        return;
    }
    mask_component_.all_pass = false;
    for (size_t w = 0; w < mask_component_.bits.size(); ++w) mask_component_.bits[w] = 0;
    for (size_t i = 0; i < n; ++i) {
        bool pass = filter_->component_idx_include.count((*entries_)[i].component_idx) > 0;
        mask_component_.set(i, pass);
    }
}

void LogView::rebuild_op_type_mask() {
    size_t n = entries_ ? entries_->size() : 0;
    if (n == 0) return;
    if (filter_->op_type_idx == 0) {
        mask_op_type_.clear_all();
        return;
    }
    mask_op_type_.all_pass = false;
    for (size_t w = 0; w < mask_op_type_.bits.size(); ++w) mask_op_type_.bits[w] = 0;
    for (size_t i = 0; i < n; ++i) {
        bool pass = ((*entries_)[i].op_type_idx == filter_->op_type_idx);
        mask_op_type_.set(i, pass);
    }
}

void LogView::rebuild_ns_mask() {
    size_t n = entries_ ? entries_->size() : 0;
    if (n == 0) return;
    if (filter_->ns_idx == 0) {
        mask_ns_.clear_all();
        return;
    }
    mask_ns_.all_pass = false;
    for (size_t w = 0; w < mask_ns_.bits.size(); ++w) mask_ns_.bits[w] = 0;
    for (size_t i = 0; i < n; ++i) {
        bool pass = ((*entries_)[i].ns_idx == filter_->ns_idx);
        mask_ns_.set(i, pass);
    }
}

void LogView::rebuild_shape_mask() {
    size_t n = entries_ ? entries_->size() : 0;
    if (n == 0) return;
    if (filter_->shape_idx == 0) {
        mask_shape_.clear_all();
        return;
    }
    mask_shape_.all_pass = false;
    for (size_t w = 0; w < mask_shape_.bits.size(); ++w) mask_shape_.bits[w] = 0;
    for (size_t i = 0; i < n; ++i) {
        bool pass = ((*entries_)[i].shape_idx == filter_->shape_idx);
        mask_shape_.set(i, pass);
    }
}

void LogView::rebuild_slow_query_mask() {
    size_t n = entries_ ? entries_->size() : 0;
    if (n == 0) return;
    if (!filter_->slow_query_only) {
        mask_slow_query_.clear_all();
        return;
    }
    mask_slow_query_.all_pass = false;
    for (size_t w = 0; w < mask_slow_query_.bits.size(); ++w) mask_slow_query_.bits[w] = 0;
    for (size_t i = 0; i < n; ++i) {
        const LogEntry& e = (*entries_)[i];
        bool is_slow = false;
        if (strings_ && e.msg_idx != 0) {
            std::string_view msg = strings_->get(e.msg_idx);
            is_slow = (msg.size() >= 4 &&
                       (msg[0]=='S'||msg[0]=='s') &&
                       msg[1]=='l' && msg[2]=='o' && msg[3]=='w');
        }
        mask_slow_query_.set(i, is_slow);
    }
}

void LogView::rebuild_conn_id_mask() {
    size_t n = entries_ ? entries_->size() : 0;
    if (n == 0) return;
    if (filter_->conn_id_include.empty()) {
        mask_conn_id_.clear_all();
        return;
    }
    mask_conn_id_.all_pass = false;
    for (size_t w = 0; w < mask_conn_id_.bits.size(); ++w) mask_conn_id_.bits[w] = 0;
    for (size_t i = 0; i < n; ++i) {
        bool pass = filter_->conn_id_include.count((*entries_)[i].conn_id) > 0;
        mask_conn_id_.set(i, pass);
    }
}

void LogView::rebuild_driver_mask() {
    size_t n = entries_ ? entries_->size() : 0;
    if (n == 0) return;
    // driver_idx_include (set-based) takes priority; driver_idx (single) is secondary
    bool has_set    = !filter_->driver_idx_include.empty();
    bool has_single = (filter_->driver_idx != 0);
    if (!has_set && !has_single) {
        mask_driver_.clear_all();
        return;
    }
    mask_driver_.all_pass = false;
    for (size_t w = 0; w < mask_driver_.bits.size(); ++w) mask_driver_.bits[w] = 0;
    for (size_t i = 0; i < n; ++i) {
        uint32_t di = (*entries_)[i].driver_idx;
        bool pass = false;
        if (has_set)    pass = filter_->driver_idx_include.count(di) > 0;
        if (!pass && has_single) pass = (di == filter_->driver_idx);
        mask_driver_.set(i, pass);
    }
}

void LogView::rebuild_node_mask() {
    size_t n = entries_ ? entries_->size() : 0;
    if (n == 0) return;
    if (filter_->node_idx_include.empty()) {
        mask_node_.clear_all();
        return;
    }
    mask_node_.all_pass = false;
    for (size_t w = 0; w < mask_node_.bits.size(); ++w) mask_node_.bits[w] = 0;
    for (size_t i = 0; i < n; ++i) {
        bool pass = filter_->node_idx_include.count((*entries_)[i].node_idx) > 0;
        mask_node_.set(i, pass);
    }
}

void LogView::rebuild_time_window_mask() {
    size_t n = entries_ ? entries_->size() : 0;
    if (n == 0) return;
    if (!filter_->time_window_active) {
        mask_time_window_.clear_all();
        return;
    }
    mask_time_window_.all_pass = false;
    for (size_t w = 0; w < mask_time_window_.bits.size(); ++w) mask_time_window_.bits[w] = 0;
    for (size_t i = 0; i < n; ++i) {
        int64_t ts = (*entries_)[i].timestamp_ms;
        bool pass = (ts >= filter_->time_window_start_ms && ts <= filter_->time_window_end_ms);
        mask_time_window_.set(i, pass);
    }
}

void LogView::rebuild_text_mask() {
    if (!filter_ || filter_->text_search.empty()) {
        mask_text_.clear_all();
        return;
    }
    search_trigram(filter_->text_search, mask_text_);
}

void LogView::rebuild_all_dimension_masks() {
    if (!filter_) return;
    rebuild_severity_mask();
    rebuild_component_mask();
    rebuild_op_type_mask();
    rebuild_ns_mask();
    rebuild_shape_mask();
    rebuild_slow_query_mask();
    rebuild_conn_id_mask();
    rebuild_driver_mask();
    rebuild_node_mask();
    rebuild_time_window_mask();
    rebuild_text_mask();
}

// ------------------------------------------------------------
//  D-11: apply_combined_masks
// ------------------------------------------------------------
void LogView::apply_combined_masks() {
    size_t n = entries_ ? entries_->size() : 0;
    if (n == 0) { filtered_indices_.clear(); return; }
    size_t words = (n + 63) / 64;
    std::vector<const DimensionMask*> active;
    for (auto* m : {&mask_severity_, &mask_component_, &mask_op_type_,
                    &mask_ns_, &mask_shape_, &mask_slow_query_,
                    &mask_conn_id_, &mask_driver_, &mask_node_,
                    &mask_time_window_, &mask_text_})
        if (!m->all_pass) active.push_back(m);
    auto combined = and_masks(active, words);
    filtered_indices_.clear();
    filtered_indices_.reserve(n / 2);
    for (size_t w = 0; w < words; ++w) {
        uint64_t word = combined[w];
        while (word) {
            int bit = __builtin_ctzll(word);
            size_t idx = w * 64 + bit;
            if (idx < n) filtered_indices_.push_back(idx);
            word &= word - 1;
        }
    }
    if (!sort_ascending_)
        std::reverse(filtered_indices_.begin(), filtered_indices_.end());
    selected_row_ = -1;
}

// ------------------------------------------------------------
//  rebuild_filter_index — incremental per-dimension bitmask path
// ------------------------------------------------------------
void LogView::rebuild_filter_index() {
    if (!entries_ || !filter_) {
        filtered_indices_.clear();
        return;
    }

    // On first call after set_entries, masks are already sized (set_entries does it).
    // If masks are empty (e.g. filter set before entries), size them now.
    bool first_call = mask_severity_.bits.empty();
    if (first_call) {
        size_t n = entries_->size();
        mask_severity_.resize(n); mask_component_.resize(n);
        mask_op_type_.resize(n);  mask_ns_.resize(n);
        mask_shape_.resize(n);    mask_slow_query_.resize(n);
        mask_conn_id_.resize(n);  mask_driver_.resize(n);
        mask_node_.resize(n);     mask_time_window_.resize(n);
        mask_text_.resize(n);
    }

    // Detect changed dimensions and rebuild only those masks
    if (first_call || filter_->severity_filter != prev_filter_.severity_filter)
        rebuild_severity_mask();
    if (first_call || filter_->component_idx_include != prev_filter_.component_idx_include)
        rebuild_component_mask();
    if (first_call || filter_->op_type_idx != prev_filter_.op_type_idx)
        rebuild_op_type_mask();
    if (first_call || filter_->ns_idx != prev_filter_.ns_idx)
        rebuild_ns_mask();
    if (first_call || filter_->shape_idx != prev_filter_.shape_idx)
        rebuild_shape_mask();
    if (first_call || filter_->slow_query_only != prev_filter_.slow_query_only)
        rebuild_slow_query_mask();
    if (first_call || filter_->conn_id_include != prev_filter_.conn_id_include)
        rebuild_conn_id_mask();
    if (first_call || filter_->driver_idx_include != prev_filter_.driver_idx_include
                   || filter_->driver_idx != prev_filter_.driver_idx)
        rebuild_driver_mask();
    if (first_call || filter_->node_idx_include != prev_filter_.node_idx_include)
        rebuild_node_mask();
    if (first_call || filter_->time_window_active != prev_filter_.time_window_active
                   || filter_->time_window_start_ms != prev_filter_.time_window_start_ms
                   || filter_->time_window_end_ms != prev_filter_.time_window_end_ms)
        rebuild_time_window_mask();

    // Text search: handled via debounce in render_inner; also rebuild here when
    // called directly (e.g. Clear button or non-debounced callers).
    if (first_call || filter_->text_search != prev_filter_.text_search) {
        if (filter_->text_search.empty()) {
            mask_text_.clear_all();
        } else {
            search_trigram(filter_->text_search, mask_text_);
        }
    }

    prev_filter_ = *filter_;
    apply_combined_masks();
}

// ------------------------------------------------------------
//  render_inner — contents only, no Begin/End
// ------------------------------------------------------------
void LogView::render_inner() {
    // ---- Search bar ----------------------------------------
    if (filter_) {
        static char search_buf[256] = {};

        // Black background button with red text for Clear
        const char* clear_label = "Clear";
        float btn_w = ImGui::CalcTextSize(clear_label).x
                      + ImGui::GetStyle().FramePadding.x * 2.0f + 8.0f;
        float input_w = ImGui::GetContentRegionAvail().x - btn_w
                        - ImGui::GetStyle().ItemSpacing.x;

        ImGui::SetNextItemWidth(input_w);
        bool changed = ImGui::InputText("##search", search_buf, sizeof(search_buf));

        ImGui::SameLine();

        // Clear: black fill, red text, subtle hover
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.06f, 0.06f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.28f, 0.08f, 0.08f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
        if (ImGui::Button(clear_label)) {
            filter_->clear();
            std::memset(search_buf, 0, sizeof(search_buf));
            search_dirty_ = false;
            rebuild_filter_index();
        }
        ImGui::PopStyleColor(4);

        if (changed) {
            // Mark dirty and record timestamp — actual rebuild is debounced below
            filter_->text_search = search_buf;
            search_dirty_     = true;
            search_dirty_time_ = ImGui::GetTime();
        }

        // Debounce: rebuild text mask after DEBOUNCE_MS of inactivity.
        // Only updates the text dimension mask + re-applies combined masks.
        if (search_dirty_) {
            double elapsed_ms = (ImGui::GetTime() - search_dirty_time_) * 1000.0;
            if (elapsed_ms >= DEBOUNCE_MS) {
                // Only rebuild text dimension — non-text masks are up to date
                if (filter_->text_search.empty()) {
                    mask_text_.clear_all();
                } else {
                    search_trigram(filter_->text_search, mask_text_);
                }
                prev_filter_.text_search = filter_->text_search;
                apply_combined_masks();
                search_dirty_ = false;
            }
        }

        ImGui::Spacing();
        ImGui::TextDisabled("%zu / %zu entries",
                    filtered_indices_.size(), (entries_ ? entries_->size() : 0));
        ImGui::Separator();
    }

    // ---- Table -----------------------------------------------
    ImGuiTableFlags table_flags =
        ImGuiTableFlags_RowBg        | ImGuiTableFlags_BordersOuter |
        ImGuiTableFlags_BordersV     | ImGuiTableFlags_ScrollY      |
        ImGuiTableFlags_Resizable    | ImGuiTableFlags_SizingStretchProp |
        ImGuiTableFlags_Sortable;

    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (!ImGui::BeginTable("log_table", 6, table_flags, ImVec2(0, avail.y - 4)))
        return;

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Timestamp",
        ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort |
        ImGuiTableColumnFlags_PreferSortAscending, 1.8f);
    ImGui::TableSetupColumn("Sev",
        ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 38.0f);
    ImGui::TableSetupColumn("Component",
        ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort, 1.0f);
    ImGui::TableSetupColumn("Namespace",
        ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort, 1.2f);
    ImGui::TableSetupColumn("Message",
        ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort, 3.0f);
    ImGui::TableSetupColumn("Nodes",
        ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 70.0f);
    ImGui::TableHeadersRow();

    // Handle sort direction changes on the Timestamp column
    if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
        if (sort_specs->SpecsDirty && sort_specs->SpecsCount > 0) {
            bool new_ascending = (sort_specs->Specs[0].SortDirection ==
                                  ImGuiSortDirection_Ascending);
            if (new_ascending != sort_ascending_) {
                sort_ascending_ = new_ascending;
                // Reverse the filtered indices to flip the sort order
                std::reverse(filtered_indices_.begin(), filtered_indices_.end());
                selected_row_ = -1;  // clear selection — row indices changed
            }
            sort_specs->SpecsDirty = false;
        }
    }

    // ---- Virtual scroll via ImGuiListClipper ---------------
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(filtered_indices_.size()));

    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            size_t idx = filtered_indices_[static_cast<size_t>(row)];
            const LogEntry& e = (*entries_)[idx];

            ImGui::TableNextRow();
            ImGui::PushID(row);

            bool is_selected = (selected_row_ == row);

            // Selected row: soft pastel blue fill, all text in black.
            // Normal rows: dark alternating ImGui default, white text.
            if (is_selected) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, SEL_ROW_BG);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, SEL_ROW_BG);
            }

            // Text colours for this row
            const ImVec4 text_normal = ImVec4(1, 1, 1, 1);
            const ImVec4 text_sel    = ImVec4(0, 0, 0, 1);
            const ImVec4 row_text    = is_selected ? text_sel : text_normal;

            // ---- Col 0: Timestamp + spanning selectable ----
            bool row_clicked = false;
            ImGui::TableSetColumnIndex(0);
            {
                int64_t ts = e.timestamp_ms;
                time_t  sec = static_cast<time_t>(ts / 1000);
                int     ms  = static_cast<int>(ts % 1000);
                struct tm t{};
#if defined(_WIN32)
                gmtime_s(&t, &sec);
#else
                gmtime_r(&sec, &t);
#endif
                char ts_buf[32];
                std::snprintf(ts_buf, sizeof(ts_buf),
                              "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                              t.tm_year+1900, t.tm_mon+1, t.tm_mday,
                              t.tm_hour, t.tm_min, t.tm_sec, ms);

                // Push the pastel colour as the Header (selected-selectable fill)
                // so the Selectable's own background draw uses it instead of black.
                // Also push it for HeaderHovered so hovering a selected row stays pastel.
                static const ImVec4 pastel = ImGui::ColorConvertU32ToFloat4(SEL_ROW_BG);
                ImGui::PushStyleColor(ImGuiCol_Header,        pastel);
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, pastel);
                ImGui::PushStyleColor(ImGuiCol_HeaderActive,  pastel);
                row_clicked = ImGui::Selectable("##row", is_selected,
                                                 ImGuiSelectableFlags_SpanAllColumns |
                                                 ImGuiSelectableFlags_AllowOverlap,
                                                 ImVec2(0, 0));
                ImGui::PopStyleColor(3);
                ImGui::SameLine();

                // Timestamp in severity colour on normal rows, black on selected
                ImVec4 ts_col = is_selected
                    ? text_sel
                    : ImGui::ColorConvertU32ToFloat4(severity_color_u32(e.severity));
                ImGui::PushStyleColor(ImGuiCol_Text, ts_col);
                ImGui::TextUnformatted(ts_buf);
                ImGui::PopStyleColor();
            }

            // ---- Col 1: Severity ----
            ImGui::TableSetColumnIndex(1);
            {
                ImVec4 sev_col = is_selected
                    ? text_sel
                    : ImGui::ColorConvertU32ToFloat4(severity_color_u32(e.severity));
                ImGui::PushStyleColor(ImGuiCol_Text, sev_col);
                ImGui::TextUnformatted(severity_string(e.severity));
                ImGui::PopStyleColor();
            }

            // ---- Col 2: Component ----
            ImGui::TableSetColumnIndex(2);
            if (strings_ && e.component_idx) {
                auto sv = strings_->get(e.component_idx);
                ImGui::PushStyleColor(ImGuiCol_Text, row_text);
                ImGui::TextUnformatted(sv.data(), sv.data() + sv.size());
                ImGui::PopStyleColor();
            }

            // ---- Col 3: Namespace ----
            ImGui::TableSetColumnIndex(3);
            if (strings_ && e.ns_idx) {
                auto sv = strings_->get(e.ns_idx);
                ImGui::PushStyleColor(ImGuiCol_Text, row_text);
                ImGui::TextUnformatted(sv.data(), sv.data() + sv.size());
                ImGui::PopStyleColor();
            }

            // ---- Col 4: Message ----
            ImGui::TableSetColumnIndex(4);
            if (strings_ && e.msg_idx) {
                auto sv = strings_->get(e.msg_idx);
                ImGui::PushStyleColor(ImGuiCol_Text, row_text);
                ImGui::TextUnformatted(sv.data(),
                                       sv.data() + std::min(sv.size(), size_t(200)));
                ImGui::PopStyleColor();
            }

            // ---- Col 5: Node badges (clickable for stacked entries) ----
            ImGui::TableSetColumnIndex(5);
            bool node_badge_clicked = false;
            if (nodes_) {
                for (size_t ni = 0; ni < nodes_->size() && ni < 32; ++ni) {
                    if (!(e.node_mask & (1u << ni))) continue;
                    const NodeColor& c = (*nodes_)[ni].color;

                    // Highlight the currently selected node for this row
                    bool is_active_node = is_selected &&
                                          static_cast<uint16_t>(ni) == selected_node_;

                    // Use a small button so it's clickable
                    char btn_id[32];
                    std::snprintf(btn_id, sizeof(btn_id), "##n%zu", ni);

                    if (is_active_node) {
                        // Draw with a bright border to indicate selection
                        ImGui::PushStyleColor(ImGuiCol_Button,
                            ImVec4(c.r, c.g, c.b, c.a));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                            ImVec4(c.r, c.g, c.b, c.a));
                        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);
                        ImGui::PushStyleColor(ImGuiCol_Border,
                            ImVec4(1.0f, 1.0f, 1.0f, 0.9f));
                    } else {
                        ImGui::PushStyleColor(ImGuiCol_Button,
                            ImVec4(c.r, c.g, c.b, 0.6f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                            ImVec4(c.r, c.g, c.b, 0.9f));
                        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
                        ImGui::PushStyleColor(ImGuiCol_Border,
                            ImVec4(0, 0, 0, 0));
                    }
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                        ImVec4(c.r, c.g, c.b, 1.0f));
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));

                    if (ImGui::Button(btn_id, ImVec2(12, 12))) {
                        // Clicked a node badge — select this row + this node
                        selected_row_ = row;
                        selected_node_ = static_cast<uint16_t>(ni);
                        if (on_select_) on_select_(idx, selected_node_);
                        node_badge_clicked = true;
                    }

                    ImGui::PopStyleVar(3);   // FramePadding, FrameRounding, FrameBorderSize
                    ImGui::PopStyleColor(4); // Button, ButtonHovered, Border, ButtonActive

                    if (ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        ImGui::TextUnformatted((*nodes_)[ni].hostname.c_str());
                        ImGui::EndTooltip();
                    }

                    ImGui::SameLine(0, 2);
                }
            }

            // Process row click (deferred so node badge clicks take priority)
            if (row_clicked && !node_badge_clicked) {
                selected_row_ = row;
                selected_node_ = leftmost_node(e.node_mask);
                if (on_select_) on_select_(idx, selected_node_);
            }

            ImGui::PopID();
        }
    }
    clipper.End();
    ImGui::EndTable();
}

// ------------------------------------------------------------
//  render — standalone window wrapper
// ------------------------------------------------------------
uint16_t LogView::leftmost_node(uint32_t mask) {
    if (mask == 0) return 0;
    for (uint16_t i = 0; i < 32; ++i)
        if (mask & (1u << i)) return i;
    return 0;
}

void LogView::render() {
    ImGui::Begin("Log View");
    render_inner();
    ImGui::End();
}
