#include "detail_view.hpp"

#include <imgui.h>
#include <simdjson.h>
#include <cstdio>

#include "../parser/log_entry.hpp"

// ------------------------------------------------------------
//  Thread-local parser for on-demand re-parse
// ------------------------------------------------------------
static thread_local simdjson::dom::parser tl_parser;

// ------------------------------------------------------------
//  Recursive JSON tree renderer
// ------------------------------------------------------------

static void render_element(const char* key,
                            const simdjson::dom::element& el);

static void render_object(const char* label,
                           const simdjson::dom::element& el,
                           bool default_open = false)
{
    ImGuiTreeNodeFlags flags = default_open ? ImGuiTreeNodeFlags_DefaultOpen : 0;
    if (ImGui::TreeNodeEx(label, flags)) {
        for (auto [k, v] : el.get_object().value()) {
            // Allocate label on the stack (key is a string_view)
            char key_buf[256];
            std::snprintf(key_buf, sizeof(key_buf), "%.*s",
                          static_cast<int>(k.size()), k.data());
            render_element(key_buf, v);
        }
        ImGui::TreePop();
    }
}

static void render_array(const char* label,
                          const simdjson::dom::element& el)
{
    if (ImGui::TreeNode(label)) {
        int idx = 0;
        for (auto item : el.get_array().value()) {
            char idx_buf[32];
            std::snprintf(idx_buf, sizeof(idx_buf), "[%d]", idx++);
            render_element(idx_buf, item);
        }
        ImGui::TreePop();
    }
}

static void render_element(const char* key,
                            const simdjson::dom::element& el)
{
    using T = simdjson::dom::element_type;
    switch (el.type()) {
        case T::OBJECT: {
            render_object(key, el);
            break;
        }
        case T::ARRAY: {
            render_array(key, el);
            break;
        }
        case T::STRING: {
            std::string_view sv;
            (void)el.get_string().get(sv);
            // Colour strings green
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.9f, 0.4f, 1.0f));
            ImGui::Text("%-24s : \"%.*s\"", key,
                        static_cast<int>(sv.size()), sv.data());
            ImGui::PopStyleColor();
            break;
        }
        case T::INT64: {
            int64_t v = 0; (void)el.get_int64().get(v);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.8f, 1.0f, 1.0f));
            ImGui::Text("%-24s : %lld", key, static_cast<long long>(v));
            ImGui::PopStyleColor();
            break;
        }
        case T::UINT64: {
            uint64_t v = 0; (void)el.get_uint64().get(v);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.8f, 1.0f, 1.0f));
            ImGui::Text("%-24s : %llu", key, static_cast<unsigned long long>(v));
            ImGui::PopStyleColor();
            break;
        }
        case T::DOUBLE: {
            double v = 0; (void)el.get_double().get(v);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.8f, 1.0f, 1.0f));
            ImGui::Text("%-24s : %g", key, v);
            ImGui::PopStyleColor();
            break;
        }
        case T::BOOL: {
            bool v = false; (void)el.get_bool().get(v);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.7f, 0.3f, 1.0f));
            ImGui::Text("%-24s : %s", key, v ? "true" : "false");
            ImGui::PopStyleColor();
            break;
        }
        case T::NULL_VALUE: {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
            ImGui::Text("%-24s : null", key);
            ImGui::PopStyleColor();
            break;
        }
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

void DetailView::render_inner() {
    if (!entry_ || !file_data_) {
        ImGui::TextDisabled("Click a log entry to inspect it.");
        return;
    }

    const char* raw  = file_data_ + entry_->raw_offset;
    size_t      rlen = entry_->raw_len;

    simdjson::dom::element doc;
    auto err = tl_parser.parse(raw, rlen).get(doc);
    if (err) {
        ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "Parse error: %s",
                           simdjson::error_message(err));
        return;
    }

    render_object("document", doc, /*default_open=*/true);
}

void DetailView::render() {
    ImGui::Begin("Entry Detail");
    render_inner();
    ImGui::End();
}
