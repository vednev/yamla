#include "log_view.hpp"

#include <imgui.h>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <algorithm>

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

    if (!filter_->component_idx_include.empty() &&
        !filter_->component_idx_include.count(e.component_idx)) return false;
    if (filter_->op_type_idx && e.op_type_idx != filter_->op_type_idx) return false;
    if (filter_->driver_idx    && e.driver_idx     != filter_->driver_idx)    return false;
    if (filter_->ns_idx        && e.ns_idx         != filter_->ns_idx)        return false;
    if (filter_->shape_idx     && e.shape_idx      != filter_->shape_idx)     return false;

    if (filter_->slow_query_only && e.duration_ms <= 100) return false;

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

        // Debounce: rebuild only after DEBOUNCE_MS of inactivity
        // This keeps the render loop free while the user is typing.
        if (search_dirty_) {
            double elapsed_ms = (ImGui::GetTime() - search_dirty_time_) * 1000.0;
            if (elapsed_ms >= DEBOUNCE_MS) {
                rebuild_filter_index();
                search_dirty_ = false;
            }
        }

        ImGui::Spacing();
        ImGui::TextDisabled("%zu / %zu entries",
                    filtered_indices_.size(), count_);
        ImGui::Separator();
    }

    // ---- Table -----------------------------------------------
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
                bool clicked = ImGui::Selectable("##row", is_selected,
                                                 ImGuiSelectableFlags_SpanAllColumns,
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

                if (clicked) {
                    selected_row_ = row;
                    if (on_select_) on_select_(idx);
                }
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

            // ---- Col 5: Node badges ----
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

            ImGui::PopID();
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
