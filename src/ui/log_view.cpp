#include "log_view.hpp"

#include <imgui.h>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <algorithm>

#include "../parser/log_entry.hpp"
#include "../analysis/cluster.hpp"
#include "colors.hpp"

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

void LogView::set_entries(const LogEntry* entries, size_t count,
                           const StringTable* strings,
                           const std::vector<NodeInfo>* nodes)
{
    entries_ = entries;
    count_   = count;
    strings_ = strings;
    nodes_   = nodes;
    rebuild_filter_index();
}

void LogView::set_filter(FilterState* filter) {
    filter_ = filter;
}

void LogView::set_on_select(SelectCallback cb) {
    on_select_ = std::move(cb);
}

// ------------------------------------------------------------
//  entry_matches
// ------------------------------------------------------------
bool LogView::entry_matches(const LogEntry& e) const {
    if (!filter_ || !filter_->active()) return true;

    if (filter_->severity_filter &&
        static_cast<uint32_t>(e.severity) != filter_->severity_filter - 1)
        return false;

    if (filter_->component_idx && e.component_idx != filter_->component_idx) return false;
    if (filter_->op_type_idx   && e.op_type_idx   != filter_->op_type_idx)   return false;
    if (filter_->driver_idx    && e.driver_idx     != filter_->driver_idx)    return false;
    if (filter_->ns_idx        && e.ns_idx         != filter_->ns_idx)        return false;
    if (filter_->shape_idx     && e.shape_idx      != filter_->shape_idx)     return false;

    // Set-based inclusion filters — non-empty means "show only these values"
    if (!filter_->conn_id_include.empty() &&
        !filter_->conn_id_include.count(e.conn_id)) return false;
    if (!filter_->driver_idx_include.empty() &&
        !filter_->driver_idx_include.count(e.driver_idx)) return false;

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

    return true;
}

// ------------------------------------------------------------
//  rebuild_filter_index
// ------------------------------------------------------------
void LogView::rebuild_filter_index() {
    search_lower_.clear();
    filtered_indices_.clear();
    if (!entries_) return;
    filtered_indices_.reserve(count_);
    for (size_t i = 0; i < count_; ++i) {
        if (entry_matches(entries_[i]))
            filtered_indices_.push_back(i);
    }
    selected_row_ = -1;
}

// ------------------------------------------------------------
//  render_inner — contents only, no Begin/End
// ------------------------------------------------------------
void LogView::render_inner() {
    // ---- Search bar ----------------------------------------
    if (filter_) {
        static char search_buf[256] = {};
        bool changed = ImGui::InputText("Search", search_buf, sizeof(search_buf));
        ImGui::SameLine();
        if (ImGui::Button("Clear")) {
            filter_->clear();
            std::memset(search_buf, 0, sizeof(search_buf));
            rebuild_filter_index();
        }
        if (changed) {
            filter_->text_search = search_buf;
            rebuild_filter_index();
        }
        ImGui::Text("%zu / %zu entries",
                    filtered_indices_.size(), count_);
        ImGui::Separator();
    }

    // ---- Table header ----------------------------------------
    ImGuiTableFlags table_flags =
        ImGuiTableFlags_RowBg        | ImGuiTableFlags_BordersOuter |
        ImGuiTableFlags_BordersV     | ImGuiTableFlags_ScrollY      |
        ImGuiTableFlags_Resizable    | ImGuiTableFlags_SizingStretchProp;

    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (!ImGui::BeginTable("log_table", 6, table_flags, ImVec2(0, avail.y - 4)))
        return;

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Timestamp",  ImGuiTableColumnFlags_WidthStretch, 1.8f);
    ImGui::TableSetupColumn("Sev",        ImGuiTableColumnFlags_WidthFixed,   38.0f);
    ImGui::TableSetupColumn("Component",  ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Namespace",  ImGuiTableColumnFlags_WidthStretch, 1.2f);
    ImGui::TableSetupColumn("Message",    ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn("Nodes",      ImGuiTableColumnFlags_WidthFixed,   70.0f);
    ImGui::TableHeadersRow();

    // ---- Virtual scroll via ImGuiListClipper ---------------
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(filtered_indices_.size()));

    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            size_t idx = filtered_indices_[static_cast<size_t>(row)];
            const LogEntry& e = entries_[idx];

            ImGui::TableNextRow();
            ImGui::PushID(row); // unique ID scope per row — prevents duplicate-timestamp ID collisions

            bool is_selected = (selected_row_ == row);

            // Selected rows get a solid white background so the full
            // row is visually inverted (text will be pushed to black below).
            if (is_selected) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                    IM_COL32(255, 255, 255, 255));
            }

            // Timestamp + selectable
            ImGui::TableSetColumnIndex(0);
            {
                int64_t ts = e.timestamp_ms;
                time_t sec = static_cast<time_t>(ts / 1000);
                int    ms  = static_cast<int>(ts % 1000);
                struct tm t{};
#if defined(_WIN32)
                gmtime_s(&t, &sec);
#else
                gmtime_r(&sec, &t);
#endif
                char ts_buf[64];
                std::snprintf(ts_buf, sizeof(ts_buf),
                              "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                              t.tm_year+1900, t.tm_mon+1, t.tm_mday,
                              t.tm_hour, t.tm_min, t.tm_sec, ms);

                // Draw the invisible spanning selectable first so we can
                // query hover state for the text colour decision below.
                bool clicked = ImGui::Selectable("##row", is_selected,
                                                 ImGuiSelectableFlags_SpanAllColumns,
                                                 ImVec2(0, 0));
                bool row_hot = is_selected || ImGui::IsItemHovered();

                ImGui::SameLine();
                // On a hot (hovered/selected) row use black text so it's
                // readable against the white fill; otherwise use severity colour.
                ImVec4 ts_col = row_hot
                    ? ImVec4(0, 0, 0, 1)
                    : ImGui::ColorConvertU32ToFloat4(severity_color_u32(e.severity));
                ImGui::PushStyleColor(ImGuiCol_Text, ts_col);
                ImGui::TextUnformatted(ts_buf);
                ImGui::PopStyleColor();

                if (clicked) {
                    selected_row_ = row;
                    if (on_select_) on_select_(idx);
                }

                // Store row_hot so other columns can use it
                // (ImGui has no built-in per-row hover, so we re-check via
                //  the item rect of the selectable we already submitted)
                if (row_hot) {
                    // Push black text for all remaining columns in this row
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 1));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
                }
            }

            // Severity
            ImGui::TableSetColumnIndex(1);
            {
                // On hot rows keep black; on normal rows use severity colour
                // (we already have a pushed colour from the block above —
                //  pop it, push the right one for severity, then restore)
                ImGui::PopStyleColor(); // pop the row-wide colour
                bool row_hot2 = is_selected; // re-derive from selected flag
                ImVec4 sev_col = row_hot2
                    ? ImVec4(0, 0, 0, 1)
                    : ImGui::ColorConvertU32ToFloat4(severity_color_u32(e.severity));
                ImGui::PushStyleColor(ImGuiCol_Text, sev_col);
                ImGui::TextUnformatted(severity_string(e.severity));
                ImGui::PopStyleColor();
                // Re-push a neutral white for the remaining plain columns
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
            }

            // Component
            ImGui::TableSetColumnIndex(2);
            if (strings_ && e.component_idx) {
                auto sv = strings_->get(e.component_idx);
                ImGui::TextUnformatted(sv.data(), sv.data() + sv.size());
            }

            // Namespace
            ImGui::TableSetColumnIndex(3);
            if (strings_ && e.ns_idx) {
                auto sv = strings_->get(e.ns_idx);
                ImGui::TextUnformatted(sv.data(), sv.data() + sv.size());
            }

            // Message
            ImGui::TableSetColumnIndex(4);
            if (strings_ && e.msg_idx) {
                auto sv = strings_->get(e.msg_idx);
                ImGui::TextUnformatted(sv.data(),
                                       sv.data() + std::min(sv.size(), size_t(200)));
            }

            // Node badges
            ImGui::TableSetColumnIndex(5);
            if (nodes_) {
                for (size_t ni = 0; ni < nodes_->size() && ni < 16; ++ni) {
                    if (!(e.node_mask & (1u << ni))) continue;
                    const NodeColor& c = (*nodes_)[ni].color;
                    ImGui::ColorButton("##nb",
                        ImVec4(c.r, c.g, c.b, c.a),
                        ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
                        ImVec2(12, 12));
                    ImGui::SameLine(0, 2);
                }
            }

            ImGui::PopStyleColor(); // pop the row-wide text colour (white or black)
            ImGui::PopID(); // matches PushID(row) above
        }
    }
    clipper.End();
    ImGui::EndTable();
}

// ------------------------------------------------------------
//  render — standalone window wrapper
// ------------------------------------------------------------
void LogView::render() {
    ImGui::Begin("Log View");
    render_inner();
    ImGui::End();
}
