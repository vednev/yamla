#include "detail_view.hpp"

#include <imgui.h>
#include <simdjson.h>
#include <cstdio>
#include <cstring>

#include "../parser/log_entry.hpp"

// ------------------------------------------------------------
//  Thread-local parser for on-demand re-parse
// ------------------------------------------------------------
static thread_local simdjson::dom::parser tl_parser;

// ------------------------------------------------------------
//  Forward declarations for recursive helpers
// ------------------------------------------------------------
static void render_element(const char* key,
                            const simdjson::dom::element& el,
                            bool wrap);

// ------------------------------------------------------------
//  render_object / render_array — recursive tree nodes
// ------------------------------------------------------------

static void render_object(const char* label,
                           const simdjson::dom::element& el,
                           bool wrap,
                           bool default_open = false)
{
    ImGuiTreeNodeFlags flags = default_open ? ImGuiTreeNodeFlags_DefaultOpen : 0;
    if (ImGui::TreeNodeEx(label, flags)) {
        for (auto [k, v] : el.get_object().value()) {
            char key_buf[256];
            std::snprintf(key_buf, sizeof(key_buf), "%.*s",
                          static_cast<int>(k.size()), k.data());
            render_element(key_buf, v, wrap);
        }
        ImGui::TreePop();
    }
}

static void render_array(const char* label,
                          const simdjson::dom::element& el,
                          bool wrap)
{
    if (ImGui::TreeNode(label)) {
        int idx = 0;
        for (auto item : el.get_array().value()) {
            char idx_buf[32];
            std::snprintf(idx_buf, sizeof(idx_buf), "[%d]", idx++);
            render_element(idx_buf, item, wrap);
        }
        ImGui::TreePop();
    }
}

// ------------------------------------------------------------
//  render_leaf — a single scalar value row.
//
//  No-wrap: fixed-width key column, value on same line.
//           Uses ImGui::Text with a %-24s format.
//  Wrap:    "key: value" on one line; if the value is long,
//           ImGui::TextWrapped lets it flow onto the next line.
//           The key is rendered in a dimmer colour so it's
//           visually distinct from the value.
// ------------------------------------------------------------
static void render_leaf_impl(const char* key, ImVec4 value_color,
                              const char* value_str, bool wrap)
{
    if (!wrap) {
        // Fixed-column, no wrapping
        ImGui::PushStyleColor(ImGuiCol_Text, value_color);
        ImGui::Text("%-24s : %s", key, value_str);
        ImGui::PopStyleColor();
    } else {
        // Wrap mode: "key: " in dim + value in colour, soft-wrapped
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
        ImGui::TextUnformatted(key);
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 0);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
        ImGui::TextUnformatted(": ");
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 0);
        ImGui::PushStyleColor(ImGuiCol_Text, value_color);
        ImGui::TextWrapped("%s", value_str);
        ImGui::PopStyleColor();
    }
}

static void render_element(const char* key,
                            const simdjson::dom::element& el,
                            bool wrap)
{
    using T = simdjson::dom::element_type;
    switch (el.type()) {
        case T::OBJECT:
            render_object(key, el, wrap);
            break;
        case T::ARRAY:
            render_array(key, el, wrap);
            break;
        case T::STRING: {
            std::string_view sv;
            (void)el.get_string().get(sv);
            // Build the quoted string in a stack buffer; fall back to
            // heap only for very long strings
            char small[512];
            const char* val_str;
            std::string heap_str;
            size_t needed = sv.size() + 3; // " + content + " + null
            if (needed <= sizeof(small)) {
                small[0] = '"';
                std::memcpy(small + 1, sv.data(), sv.size());
                small[sv.size() + 1] = '"';
                small[sv.size() + 2] = '\0';
                val_str = small;
            } else {
                heap_str.reserve(needed);
                heap_str = '"';
                heap_str.append(sv.data(), sv.size());
                heap_str += '"';
                val_str = heap_str.c_str();
            }
            render_leaf_impl(key, ImVec4(0.4f, 0.9f, 0.4f, 1.0f), val_str, wrap);
            break;
        }
        case T::INT64: {
            int64_t v = 0; (void)el.get_int64().get(v);
            char buf[32]; std::snprintf(buf, sizeof(buf), "%lld",
                                        static_cast<long long>(v));
            render_leaf_impl(key, ImVec4(0.6f, 0.8f, 1.0f, 1.0f), buf, wrap);
            break;
        }
        case T::UINT64: {
            uint64_t v = 0; (void)el.get_uint64().get(v);
            char buf[32]; std::snprintf(buf, sizeof(buf), "%llu",
                                        static_cast<unsigned long long>(v));
            render_leaf_impl(key, ImVec4(0.6f, 0.8f, 1.0f, 1.0f), buf, wrap);
            break;
        }
        case T::DOUBLE: {
            double v = 0; (void)el.get_double().get(v);
            char buf[32]; std::snprintf(buf, sizeof(buf), "%g", v);
            render_leaf_impl(key, ImVec4(0.6f, 0.8f, 1.0f, 1.0f), buf, wrap);
            break;
        }
        case T::BOOL: {
            bool v = false; (void)el.get_bool().get(v);
            render_leaf_impl(key, ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                             v ? "true" : "false", wrap);
            break;
        }
        case T::NULL_VALUE:
            render_leaf_impl(key, ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "null", wrap);
            break;
    }
}

// ------------------------------------------------------------
//  DetailView public API
// ------------------------------------------------------------

void DetailView::set_entry(const LogEntry* entry,
                            const char* file_data,
                            const StringTable* strings)
{
    entry_     = entry;
    file_data_ = file_data;
    strings_   = strings;
}

void DetailView::render_toolbar() {
    ImGui::Checkbox("Wrap text", &wrap_);
}

void DetailView::render_inner() {
    // Toolbar strip at the top of the panel
    render_toolbar();
    ImGui::Separator();

    if (!entry_ || !file_data_) {
        ImGui::TextDisabled("Click a log entry to inspect it.");
        return;
    }

    const char* raw  = file_data_ + entry_->raw_offset;
    size_t      rlen = entry_->raw_len;

    simdjson::padded_string padded(raw, rlen);
    simdjson::dom::element doc;
    auto err = tl_parser.parse(padded).get(doc);
    if (err) {
        ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1),
                           "Parse error: %s", simdjson::error_message(err));
        return;
    }

    // Scrollable child — horizontal scrollbar only shown when not wrapping
    ImGuiWindowFlags scroll_flags = wrap_
        ? ImGuiWindowFlags_None
        : ImGuiWindowFlags_HorizontalScrollbar;

    ImGui::BeginChild("##detail_scroll", ImVec2(0, 0), false, scroll_flags);
    render_object("document", doc, wrap_, /*default_open=*/true);
    ImGui::EndChild();
}

void DetailView::render() {
    ImGui::Begin("Entry Detail");
    render_inner();
    ImGui::End();
}
