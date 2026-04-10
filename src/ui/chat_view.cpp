#include "chat_view.hpp"

#include <imgui.h>
#include <md4c.h>

#include "../core/prefs.hpp"

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cfloat>
#include <ctime>
#include <algorithm>
#include <string>
#include <vector>

// ============================================================
//  Markdown renderer using md4c -> ImGui
//
//  md4c parses markdown via callbacks. We accumulate text in
//  segments, each tagged with its inline style, then flush a
//  whole block at once. This avoids the problem where ImGui
//  text calls each start a new line.
// ============================================================

namespace {

// ------------------------------------------------------------
//  sanitize_text — strip UTF-8 codepoints outside loaded glyph ranges
//
//  The font atlas only covers:
//    0x0020–0x00FF  (Basic Latin + Latin Supplement)
//    0x2013–0x2026  (en-dash, em-dash, ellipsis, etc.)
//    0x2018–0x201F  (curly quotes)
//  Any codepoint outside these ranges renders as '?' (ImGui FallbackChar).
//  This function strips those characters so they never appear.
// ------------------------------------------------------------
static bool is_renderable_codepoint(uint32_t cp) {
    if (cp >= 0x0020 && cp <= 0x00FF) return true;
    if (cp >= 0x2013 && cp <= 0x2026) return true;
    if (cp >= 0x2018 && cp <= 0x201F) return true;
    return false;
}

static std::string sanitize_text(const char* text, size_t len) {
    std::string out;
    out.reserve(len);
    size_t i = 0;
    while (i < len) {
        uint8_t c = static_cast<uint8_t>(text[i]);
        uint32_t cp = 0;
        size_t seq_len = 0;

        if (c < 0x80) {
            // ASCII
            cp = c;
            seq_len = 1;
        } else if ((c & 0xE0) == 0xC0) {
            // 2-byte
            if (i + 1 < len) {
                cp = ((c & 0x1F) << 6) | (static_cast<uint8_t>(text[i+1]) & 0x3F);
                seq_len = 2;
            } else {
                i++; continue;
            }
        } else if ((c & 0xF0) == 0xE0) {
            // 3-byte
            if (i + 2 < len) {
                cp = ((c & 0x0F) << 12) |
                     ((static_cast<uint8_t>(text[i+1]) & 0x3F) << 6) |
                     (static_cast<uint8_t>(text[i+2]) & 0x3F);
                seq_len = 3;
            } else {
                i++; continue;
            }
        } else if ((c & 0xF8) == 0xF0) {
            // 4-byte
            if (i + 3 < len) {
                cp = ((c & 0x07) << 18) |
                     ((static_cast<uint8_t>(text[i+1]) & 0x3F) << 12) |
                     ((static_cast<uint8_t>(text[i+2]) & 0x3F) << 6) |
                     (static_cast<uint8_t>(text[i+3]) & 0x3F);
                seq_len = 4;
            } else {
                i++; continue;
            }
        } else {
            // Invalid leading byte, skip
            i++; continue;
        }

        if (is_renderable_codepoint(cp)) {
            out.append(text + i, seq_len);
        }
        // else: silently drop the character
        i += seq_len;
    }
    return out;
}

// A segment of text within one block, tagged with style info.
struct TextSegment {
    std::string text;
    bool bold      = false;
    bool italic    = false;
    bool code_span = false;
};

struct MdRenderState {
    // Inline style state
    bool bold       = false;
    bool italic     = false;
    bool code_span  = false;
    bool code_block = false;
    int  heading    = 0;
    int  list_depth = 0;
    int  ol_index   = 0;      // current ordered list item number
    bool in_ol      = false;  // inside an OL (vs UL)

    // Table state
    bool in_table      = false;
    bool in_thead      = false;
    int  table_col     = 0;    // current column index within a row
    int  table_cols    = 0;    // total columns (from MD_BLOCK_TABLE_DETAIL)
    int  table_id_ctr  = 0;    // unique ID counter for multiple tables

    // Accumulated segments for the current block
    std::vector<TextSegment> segments;

    // Unique ID counter for code block children
    int codeblock_id = 0;

    // Push current accumulated text as a segment
    std::string pending;

    void push_pending() {
        if (pending.empty()) return;
        segments.push_back({std::move(pending), bold, italic, code_span});
        pending.clear();
    }

    // Pick the color for a segment based on its style flags.
    static ImVec4 segment_color(const TextSegment& seg) {
        if (seg.code_span) return ImVec4(0.4f, 0.9f, 0.4f, 1.0f);
        if (seg.bold)      return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        if (seg.italic)    return ImVec4(0.75f, 0.85f, 0.95f, 1.0f);
        return ImGui::GetStyleColorVec4(ImGuiCol_Text);
    }

    // Flush all accumulated segments as a single block.
    //
    // Concatenates all segment text into one string and renders
    // it with a single TextUnformatted call so wrapping works
    // correctly. Bold regions get a faux-bold DrawList overlay
    // computed from character offsets within the combined string.
    void flush_block() {
        push_pending();
        if (segments.empty()) return;

        // Build the combined string and record bold ranges
        struct BoldRange { size_t start; size_t len; };
        std::string combined;
        std::vector<BoldRange> bold_ranges;
        bool has_code = false;
        for (auto& seg : segments) {
            if (seg.bold)
                bold_ranges.push_back({combined.size(), seg.text.size()});
            if (seg.code_span) has_code = true;
            combined += seg.text;
        }
        segments.clear();

        if (combined.empty()) return;

        // If the entire block is a single code span, render in code color
        // Otherwise use default text color (bold regions get overlay below)
        ImVec4 text_color = (segments.empty() && has_code && bold_ranges.empty())
            ? ImVec4(0.4f, 0.9f, 0.4f, 1.0f)
            : ImGui::GetStyleColorVec4(ImGuiCol_Text);

        float wrap_x = ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX();
        ImGui::PushTextWrapPos(wrap_x);
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::PushStyleColor(ImGuiCol_Text, text_color);
        ImGui::TextUnformatted(combined.c_str(),
                                combined.c_str() + combined.size());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();

        // Faux-bold overlay for bold ranges (first line only — sufficient
        // since most bold spans are short inline phrases)
        if (!bold_ranges.empty()) {
            ImFont* font = ImGui::GetFont();
            float fsize  = ImGui::GetFontSize();
            ImU32 bold_col = ImGui::ColorConvertFloat4ToU32(
                ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            float wrap_w = wrap_x - pos.x;

            for (auto& br : bold_ranges) {
                // Compute x-offset of this bold range within the first line
                float x_start = font->CalcTextSizeA(fsize, FLT_MAX, 0.0f,
                    combined.c_str(), combined.c_str() + br.start).x;
                // Only overlay if it's on the first line (within wrap width)
                if (x_start < wrap_w) {
                    ImGui::GetWindowDrawList()->AddText(
                        font, fsize,
                        ImVec2(pos.x + x_start + 1.0f, pos.y), bold_col,
                        combined.c_str() + br.start,
                        combined.c_str() + br.start + br.len,
                        wrap_w - x_start);
                }
            }
        }
    }
};

// md4c callbacks
static int md_enter_block(MD_BLOCKTYPE type, void* detail, void* userdata) {
    auto* st = static_cast<MdRenderState*>(userdata);

    switch (type) {
        case MD_BLOCK_H: {
            auto* hd = static_cast<MD_BLOCK_H_DETAIL*>(detail);
            st->heading = hd->level;
            break;
        }
        case MD_BLOCK_CODE: {
            st->code_block = true;
            // Render code blocks with a background rect drawn manually
            ImGui::Spacing();
            break;
        }
        case MD_BLOCK_UL:
            st->list_depth++;
            st->in_ol = false;
            break;
        case MD_BLOCK_OL:
            st->list_depth++;
            st->in_ol = true;
            st->ol_index = 0;
            break;
        case MD_BLOCK_LI:
            st->flush_block();
            st->ol_index++;
            break;
        case MD_BLOCK_TABLE: {
            auto* td = static_cast<MD_BLOCK_TABLE_DETAIL*>(detail);
            st->in_table = true;
            st->table_cols = static_cast<int>(td->col_count);
            st->table_col = 0;
            // Unique ID per table so multiple tables don't share column state
            char table_id[32];
            std::snprintf(table_id, sizeof(table_id), "##md_tbl_%d", st->table_id_ctr++);
            // Start an ImGui table — SizingFixedFit so it doesn't stretch to fill width
            ImGui::PushStyleColor(ImGuiCol_TableBorderStrong,
                                  ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_TableBorderLight,
                                  ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
            ImGui::BeginTable(table_id, st->table_cols,
                              ImGuiTableFlags_Borders |
                              ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingFixedFit |
                              ImGuiTableFlags_NoHostExtendX);
            // Set up columns so each auto-fits to content
            for (int c = 0; c < st->table_cols; ++c)
                ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed);
            break;
        }
        case MD_BLOCK_THEAD:
            st->in_thead = true;
            break;
        case MD_BLOCK_TBODY:
            st->in_thead = false;
            break;
        case MD_BLOCK_TR:
            st->table_col = 0;
            ImGui::TableNextRow();
            break;
        case MD_BLOCK_TH:
        case MD_BLOCK_TD:
            // Clamp to valid range to avoid garbled layout
            if (st->table_col < st->table_cols)
                ImGui::TableSetColumnIndex(st->table_col);
            break;
        case MD_BLOCK_P:
            break;
        default:
            break;
    }
    return 0;
}

static int md_leave_block(MD_BLOCKTYPE type, void* /*detail*/, void* userdata) {
    auto* st = static_cast<MdRenderState*>(userdata);

    switch (type) {
        case MD_BLOCK_H:
            // Render heading text as faux-bold in accent color
            {
                st->push_pending();
                std::string combined;
                for (auto& seg : st->segments) combined += seg.text;
                st->segments.clear();

                ImVec4 hcolor = (st->heading <= 2)
                    ? ImVec4(0.55f, 0.75f, 1.0f, 1.0f)
                    : ImVec4(0.65f, 0.80f, 1.0f, 1.0f);

                // Capture wrap width and position BEFORE rendering
                float wrap_w = ImGui::GetContentRegionAvail().x;
                ImVec2 pos = ImGui::GetCursorScreenPos();

                ImGui::PushTextWrapPos(wrap_w + ImGui::GetCursorPosX());
                ImGui::PushStyleColor(ImGuiCol_Text, hcolor);
                ImGui::TextUnformatted(combined.c_str(),
                                        combined.c_str() + combined.size());
                ImGui::PopStyleColor();
                ImGui::PopTextWrapPos();

                // Faux-bold overlay using the pre-captured wrap width
                ImU32 hcol32 = ImGui::ColorConvertFloat4ToU32(hcolor);
                ImGui::GetWindowDrawList()->AddText(
                    ImGui::GetFont(), ImGui::GetFontSize(),
                    ImVec2(pos.x + 1.0f, pos.y), hcol32,
                    combined.c_str(), combined.c_str() + combined.size(),
                    wrap_w);
            }
            st->heading = 0;
            ImGui::Spacing();
            break;

        case MD_BLOCK_CODE: {
            // Render the accumulated code block text with a background
            st->push_pending();
            std::string code_text;
            for (auto& seg : st->segments) code_text += seg.text;
            st->segments.clear();
            st->code_block = false;

            // Use a child with a dark background for the code block
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.07f, 0.07f, 0.07f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 3.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));

            // Calculate height needed
            float avail_w = ImGui::GetContentRegionAvail().x - 16.0f;
            ImVec2 tsize = ImGui::GetFont()->CalcTextSizeA(
                ImGui::GetFontSize(), avail_w, 0.0f,
                code_text.c_str(), code_text.c_str() + code_text.size());
            float child_h = tsize.y + 14.0f; // padding top + bottom

            ImGui::BeginChild(ImGui::GetID(code_text.c_str()),
                              ImVec2(0, child_h), true,
                              ImGuiWindowFlags_NoScrollbar);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.9f, 0.4f, 1.0f));
            ImGui::PushTextWrapPos(avail_w);
            ImGui::TextUnformatted(code_text.c_str(),
                                    code_text.c_str() + code_text.size());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            ImGui::EndChild();

            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor();
            ImGui::Spacing();
            break;
        }

        case MD_BLOCK_TABLE:
            ImGui::EndTable();
            ImGui::PopStyleColor(2);
            st->in_table = false;
            ImGui::Spacing();
            break;
        case MD_BLOCK_THEAD:
            st->in_thead = false;
            break;
        case MD_BLOCK_TBODY:
            break;
        case MD_BLOCK_TR:
            break;
        case MD_BLOCK_TH: {
            // Render header cell text as faux-bold using standard ImGui text
            st->push_pending();
            std::string combined;
            for (auto& seg : st->segments) combined += seg.text;
            st->segments.clear();
            if (!combined.empty()) {
                ImVec4 white(1.0f, 1.0f, 1.0f, 1.0f);
                ImVec2 pos = ImGui::GetCursorScreenPos();
                ImGui::PushStyleColor(ImGuiCol_Text, white);
                ImGui::TextUnformatted(combined.c_str(),
                                        combined.c_str() + combined.size());
                ImGui::PopStyleColor();
                // Faux-bold overlay
                ImU32 col = ImGui::ColorConvertFloat4ToU32(white);
                ImGui::GetWindowDrawList()->AddText(
                    ImGui::GetFont(), ImGui::GetFontSize(),
                    ImVec2(pos.x + 1.0f, pos.y), col,
                    combined.c_str(), combined.c_str() + combined.size());
            }
            st->table_col++;
            break;
        }
        case MD_BLOCK_TD: {
            // Render data cell text using standard ImGui text
            st->push_pending();
            for (auto& seg : st->segments) {
                if (seg.text.empty()) continue;
                ImVec4 color = MdRenderState::segment_color(seg);
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::TextUnformatted(seg.text.c_str(),
                                        seg.text.c_str() + seg.text.size());
                ImGui::PopStyleColor();
            }
            st->segments.clear();
            st->table_col++;
            break;
        }

        case MD_BLOCK_UL:
        case MD_BLOCK_OL:
            st->list_depth = std::max(0, st->list_depth - 1);
            break;

        case MD_BLOCK_LI: {
            // Flush remaining segments into the list item line
            st->push_pending();

            // Build the bullet/number prefix
            float indent = 16.0f * static_cast<float>(st->list_depth);
            ImGui::Indent(indent);

            std::string bullet;
            if (st->in_ol) {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%d. ", st->ol_index);
                bullet = buf;
            } else {
                bullet = "- ";
            }

            // Build the full line: bullet + all segment text
            // Render as a single wrapped TextColored call for simplicity
            std::string full_line = bullet;
            for (auto& seg : st->segments) full_line += seg.text;
            st->segments.clear();

            ImGui::PushStyleColor(ImGuiCol_Text,
                ImGui::GetStyleColorVec4(ImGuiCol_Text));
            ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x +
                                   ImGui::GetCursorPosX());
            ImGui::TextUnformatted(full_line.c_str(),
                                    full_line.c_str() + full_line.size());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();

            ImGui::Unindent(indent);
            break;
        }

        case MD_BLOCK_P:
            st->flush_block();
            ImGui::Spacing();
            break;

        default:
            st->flush_block();
            break;
    }
    return 0;
}

static int md_enter_span(MD_SPANTYPE type, void* /*detail*/, void* userdata) {
    auto* st = static_cast<MdRenderState*>(userdata);

    switch (type) {
        case MD_SPAN_STRONG:
            st->push_pending();
            st->bold = true;
            break;
        case MD_SPAN_EM:
            st->push_pending();
            st->italic = true;
            break;
        case MD_SPAN_CODE:
            st->push_pending();
            st->code_span = true;
            break;
        default:
            break;
    }
    return 0;
}

static int md_leave_span(MD_SPANTYPE type, void* /*detail*/, void* userdata) {
    auto* st = static_cast<MdRenderState*>(userdata);

    switch (type) {
        case MD_SPAN_STRONG:
            st->push_pending();
            st->bold = false;
            break;
        case MD_SPAN_EM:
            st->push_pending();
            st->italic = false;
            break;
        case MD_SPAN_CODE:
            st->push_pending();
            st->code_span = false;
            break;
        default:
            break;
    }
    return 0;
}

// Decode a simple HTML entity to its UTF-8 character(s).
// Returns the decoded string, or the raw entity if unknown.
static std::string decode_entity(const char* text, MD_SIZE size) {
    std::string raw(text, size);
    if (raw == "&amp;")  return "&";
    if (raw == "&lt;")   return "<";
    if (raw == "&gt;")   return ">";
    if (raw == "&quot;") return "\"";
    if (raw == "&apos;") return "'";
    if (raw == "&#39;")  return "'";
    if (raw == "&nbsp;") return " ";
    if (raw == "&mdash;" || raw == "&mdash") return "--";
    if (raw == "&ndash;" || raw == "&ndash") return "-";
    if (raw == "&hellip;") return "...";
    // Numeric character references: &#NNN; or &#xHHH;
    if (size > 3 && text[0] == '&' && text[1] == '#') {
        unsigned long cp = 0;
        if (text[2] == 'x' || text[2] == 'X')
            cp = std::strtoul(text + 3, nullptr, 16);
        else
            cp = std::strtoul(text + 2, nullptr, 10);
        if (cp > 0 && cp < 128) {
            return std::string(1, static_cast<char>(cp));
        }
        // Non-ASCII codepoints are outside our glyph range — drop them
        return "";
    }
    return raw;  // unknown entity — pass through raw
}

static int md_text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata) {
    auto* st = static_cast<MdRenderState*>(userdata);

    switch (type) {
        case MD_TEXT_NORMAL:
        case MD_TEXT_CODE:
            st->pending += sanitize_text(text, size);
            break;
        case MD_TEXT_SOFTBR:
            st->pending += ' ';
            break;
        case MD_TEXT_BR:
            st->push_pending();
            break;
        case MD_TEXT_NULLCHAR:
            // Skip null chars entirely
            break;
        case MD_TEXT_ENTITY:
            st->pending += decode_entity(text, size);
            break;
        case MD_TEXT_HTML:
            // Skip raw HTML
            break;
        default:
            st->pending += sanitize_text(text, size);
            break;
    }
    return 0;
}

} // anonymous namespace

// ============================================================
//  ChatView implementation
// ============================================================

void ChatView::render_markdown(const std::string& text) {
    MdRenderState state;

    MD_PARSER parser_cfg = {};
    parser_cfg.abi_version = 0;
    parser_cfg.flags       = MD_FLAG_NOHTML | MD_FLAG_TABLES;  // GFM tables + no raw HTML
    parser_cfg.enter_block = md_enter_block;
    parser_cfg.leave_block = md_leave_block;
    parser_cfg.enter_span  = md_enter_span;
    parser_cfg.leave_span  = md_leave_span;
    parser_cfg.text        = md_text;

    md_parse(text.c_str(), static_cast<MD_SIZE>(text.size()), &parser_cfg, &state);

    // Flush any remaining text
    state.flush_block();
}

// Render a collapsed one-line tool summary for an assistant message
// that contains tool_use blocks, paired with the following tool_result message.
void ChatView::render_tool_summary(const ChatMessage& assistant_msg,
                                    const ChatMessage* result_msg,
                                    size_t msg_idx)
{
    // Count tool calls
    int tool_count = 0;
    std::string tool_names;
    for (const auto& b : assistant_msg.content) {
        if (b.type == ContentBlock::ToolUse) {
            if (tool_count > 0) tool_names += ", ";
            tool_names += b.tool_name;
            ++tool_count;
        }
    }
    if (tool_count == 0) return;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.45f, 0.45f, 1.0f));

    // One-line summary, expandable
    char summary[256];
    if (tool_count == 1)
        std::snprintf(summary, sizeof(summary), "Used tool: %s###tools_%d",
                      tool_names.c_str(), static_cast<int>(msg_idx));
    else
        std::snprintf(summary, sizeof(summary), "Used %d tools: %s###tools_%d",
                      tool_count, tool_names.c_str(), static_cast<int>(msg_idx));

    if (ImGui::TreeNode(summary)) {
        // Show each tool call and its result paired together
        for (const auto& b : assistant_msg.content) {
            if (b.type != ContentBlock::ToolUse) continue;

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            ImGui::TextUnformatted(b.tool_name.c_str());
            ImGui::PopStyleColor();

            // Show truncated input
            if (!b.tool_input_json.empty() && b.tool_input_json != "{}") {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
                if (b.tool_input_json.size() > 80) {
                    std::string trunc = b.tool_input_json.substr(0, 80) + "...";
                    ImGui::TextUnformatted(trunc.c_str());
                } else {
                    ImGui::TextUnformatted(b.tool_input_json.c_str());
                }
                ImGui::PopStyleColor();
            }

            // Find matching result
            if (result_msg) {
                for (const auto& rb : result_msg->content) {
                    if (rb.type == ContentBlock::ToolResult &&
                        rb.tool_use_id == b.tool_use_id)
                    {
                        // Show a very brief excerpt of the result
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
                        char result_label[64];
                        std::snprintf(result_label, sizeof(result_label),
                                      "  result (%zu bytes)###r_%s",
                                      rb.tool_result.size(), rb.tool_use_id.c_str());
                        if (ImGui::TreeNode(result_label)) {
                            ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x +
                                                   ImGui::GetCursorPosX());
                            if (rb.tool_result.size() > 500) {
                                std::string t = rb.tool_result.substr(0, 500) + "\n... (truncated)";
                                ImGui::TextUnformatted(t.c_str());
                            } else {
                                ImGui::TextUnformatted(rb.tool_result.c_str());
                            }
                            ImGui::PopTextWrapPos();
                            ImGui::TreePop();
                        }
                        ImGui::PopStyleColor();
                        break;
                    }
                }
            }
        }
        ImGui::TreePop();
    }

    ImGui::PopStyleColor();
}

void ChatView::render_message(const ChatMessage& msg, size_t msg_idx) {
    bool is_user = (msg.role == "user");

    // Check if this is purely a tool_result message (from agentic loop).
    // These are rendered as part of the preceding assistant tool_use message,
    // so skip them entirely here.
    bool all_tool_results = true;
    for (const auto& b : msg.content) {
        if (b.type != ContentBlock::ToolResult) {
            all_tool_results = false;
            break;
        }
    }
    if (is_user && all_tool_results) return;

    // Check if this assistant message is purely tool_use (no text).
    // These get folded into the tool summary — don't render a header.
    bool is_assistant = (msg.role == "assistant");
    bool has_text = false;
    bool has_tool_use = false;
    for (const auto& b : msg.content) {
        if (b.type == ContentBlock::Text && !b.text.empty()) has_text = true;
        if (b.type == ContentBlock::ToolUse) has_tool_use = true;
    }

    // Pure tool_use assistant messages: render just the collapsed summary
    if (is_assistant && has_tool_use && !has_text) {
        ImGui::PushID(static_cast<int>(msg_idx));
        // Find the following tool_result message
        const ChatMessage* result_msg = nullptr;
        if (msg_idx + 1 < cached_messages_.size()) {
            const auto& next = cached_messages_[msg_idx + 1];
            if (next.role == "user") {
                bool all_results = true;
                for (const auto& b : next.content)
                    if (b.type != ContentBlock::ToolResult) { all_results = false; break; }
                if (all_results) result_msg = &next;
            }
        }
        render_tool_summary(msg, result_msg, msg_idx);
        ImGui::PopID();
        return;
    }

    ImGui::PushID(static_cast<int>(msg_idx));

    // Message header
    if (is_user) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.8f, 1.0f, 1.0f));
        ImGui::TextUnformatted("You:");
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.9f, 0.4f, 1.0f));
        ImGui::TextUnformatted("Assistant:");
        ImGui::PopStyleColor();

        // Export button for assistant text messages
        if (has_text && prefs_ && !prefs_->export_dir.empty()) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            char btn_label[32];
            std::snprintf(btn_label, sizeof(btn_label), "Export##exp_%d",
                          static_cast<int>(msg_idx));
            if (ImGui::SmallButton(btn_label))
                export_response(msg);
            ImGui::PopStyleColor(2);
        }
    }

    // Render content blocks
    for (const auto& block : msg.content) {
        switch (block.type) {
            case ContentBlock::Text:
                if (is_user) {
                    ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x +
                                           ImGui::GetCursorPosX());
                    ImGui::TextUnformatted(block.text.c_str(),
                                            block.text.c_str() + block.text.size());
                    ImGui::PopTextWrapPos();
                } else {
                    render_markdown(block.text);
                }
                break;

            case ContentBlock::ToolUse:
                // Tool calls within a mixed message (text + tools):
                // show a one-line collapsed summary
                {
                    const ChatMessage* result_msg = nullptr;
                    if (msg_idx + 1 < cached_messages_.size()) {
                        const auto& next = cached_messages_[msg_idx + 1];
                        if (next.role == "user") {
                            bool all_r = true;
                            for (const auto& b : next.content)
                                if (b.type != ContentBlock::ToolResult) { all_r = false; break; }
                            if (all_r) result_msg = &next;
                        }
                    }
                    render_tool_summary(msg, result_msg, msg_idx);
                }
                // Only render the summary once for all tool_use blocks
                goto done_blocks;

            case ContentBlock::ToolResult:
                // Should not appear in assistant messages
                break;
        }
    }
    done_blocks:

    ImGui::PopID();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
}

void ChatView::export_response(const ChatMessage& msg) {
    if (!prefs_ || prefs_->export_dir.empty()) {
        error_modal_text_ = "Export directory not configured.\nSet it in Edit > Preferences.";
        show_error_modal_ = true;
        return;
    }

    // Build the text content from all text blocks
    std::string content;
    for (const auto& b : msg.content) {
        if (b.type == ContentBlock::Text && !b.text.empty())
            content += b.text;
    }
    if (content.empty()) return;

    // Generate filename with timestamp
    auto now = std::time(nullptr);
    struct tm t;
#if defined(_WIN32)
    localtime_s(&t, &now);
#else
    localtime_r(&now, &t);
#endif
    char fname[128];
    std::snprintf(fname, sizeof(fname),
                  "yamla-export-%04d%02d%02d-%02d%02d%02d.txt",
                  t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                  t.tm_hour, t.tm_min, t.tm_sec);

    std::string path = prefs_->export_dir;
    // Ensure trailing separator
    if (!path.empty() && path.back() != '/' && path.back() != '\\')
        path += '/';
    path += fname;

    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) {
        error_modal_text_ = "Failed to write export file:\n" + path +
                            "\n\nCheck that the directory exists and is writable.";
        show_error_modal_ = true;
        return;
    }
    std::fwrite(content.data(), 1, content.size(), f);
    std::fclose(f);
}

void ChatView::render_error_modal() {
    if (show_error_modal_) {
        ImGui::OpenPopup("Export Error");
        show_error_modal_ = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Export Error", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::PushTextWrapPos(400.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
        ImGui::TextUnformatted(error_modal_text_.c_str());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float btn_w = 80.0f;
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - btn_w) * 0.5f +
                              ImGui::GetCursorPosX());
        if (ImGui::Button("OK", ImVec2(btn_w, 0)))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
}

void ChatView::render() {
    if (!open_ || !client_) return;

    // Set initial position and size on first appearance
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x * 0.5f - 300.0f,
               io.DisplaySize.y * 0.5f - 250.0f),
        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;

    if (!ImGui::Begin("AI Assistant###chat_window", &open_, flags)) {
        ImGui::End();
        return;
    }

    // --- Toolbar ---
    {
        bool is_configured = client_->is_configured();
        if (!is_configured) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.7f, 0.3f, 1.0f));
            ImGui::TextWrapped("API key not configured. Set it in Edit > Preferences.");
            ImGui::PopStyleColor();
            ImGui::Spacing();
        }

        if (ImGui::Button("Clear")) {
            client_->clear();
            cached_messages_.clear();
            cached_size_ = 0;
            last_rendered_count_ = 0;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Ctrl+A to toggle | Esc to close");
    }

    ImGui::Separator();

    // --- Message area ---
    {
        // Update cache if conversation changed
        size_t cur_size = client_->conversation().size();
        if (cur_size != cached_size_) {
            cached_messages_ = client_->conversation().snapshot();
            cached_size_ = cur_size;
            scroll_to_bottom_ = true;
        }

        // Calculate height: total - toolbar - input area
        float input_area_height = 40.0f;
        float avail = ImGui::GetContentRegionAvail().y - input_area_height;
        if (avail < 50.0f) avail = 50.0f;

        ImGui::BeginChild("##chat_messages", ImVec2(0, avail), true);

        if (cached_messages_.empty()) {
            ImGui::TextDisabled("Ask me about the loaded MongoDB logs.");
            ImGui::TextDisabled("I can search logs, analyze errors, find slow queries, and more.");
        } else {
            for (size_t i = 0; i < cached_messages_.size(); ++i) {
                render_message(cached_messages_[i], i);
            }
        }

        // Show thinking indicator
        if (client_->is_thinking()) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            // Animated dots
            double t = ImGui::GetTime();
            int dots = static_cast<int>(t * 2.0) % 4;
            char thinking[32] = "Thinking";
            for (int d = 0; d < dots; ++d)
                thinking[8 + d] = '.';
            thinking[8 + dots] = '\0';
            ImGui::TextUnformatted(thinking);
            ImGui::PopStyleColor();
        }

        // Show error if any
        std::string err = client_->last_error();
        if (!err.empty()) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x +
                                   ImGui::GetCursorPosX());
            ImGui::TextUnformatted(err.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }

        // Auto-scroll to bottom when new messages arrive
        if (scroll_to_bottom_) {
            ImGui::SetScrollHereY(1.0f);
            scroll_to_bottom_ = false;
        }

        ImGui::EndChild();
    }

    // --- Input area ---
    {
        bool can_send = client_->is_configured() && !client_->is_thinking();

        float btn_w = 60.0f;
        float spacing = 8.0f;
        float input_w = ImGui::GetContentRegionAvail().x - btn_w - spacing;
        if (input_w < 100.0f) input_w = 100.0f;

        ImGui::SetNextItemWidth(input_w);

        // Enter to send
        bool enter_pressed = false;
        ImGuiInputTextFlags input_flags = ImGuiInputTextFlags_EnterReturnsTrue;
        if (ImGui::InputText("##chat_input", input_buf_, sizeof(input_buf_), input_flags)) {
            enter_pressed = true;
        }

        // Auto-focus the input when window opens
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere(-1);

        ImGui::SameLine(0, spacing);

        if (!can_send) ImGui::BeginDisabled();
        bool send_clicked = ImGui::Button("Send", ImVec2(btn_w, 0));
        if (!can_send) ImGui::EndDisabled();

        if ((enter_pressed || send_clicked) && can_send) {
            std::string msg(input_buf_);
            // Trim whitespace
            size_t start = msg.find_first_not_of(" \t\n\r");
            if (start != std::string::npos) {
                msg = msg.substr(start);
                size_t end = msg.find_last_not_of(" \t\n\r");
                if (end != std::string::npos) msg = msg.substr(0, end + 1);

                client_->send(msg);
                input_buf_[0] = '\0';
                scroll_to_bottom_ = true;
            }
        }
    }

    ImGui::End();

    // Error modal must be rendered outside the chat window
    render_error_modal();
}
