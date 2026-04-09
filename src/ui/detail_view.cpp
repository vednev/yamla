#include "detail_view.hpp"
#include "log_key_names.hpp"

#include <imgui.h>
#include <simdjson.h>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../parser/log_entry.hpp"

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <unistd.h>
#endif

// ------------------------------------------------------------
//  Thread-local parser for on-demand re-parse
// ------------------------------------------------------------
static thread_local simdjson::dom::parser tl_parser;

// ------------------------------------------------------------
//  Forward declarations — all helpers now carry `ctx`
// ------------------------------------------------------------
static void render_element(const char* key,
                            const simdjson::dom::element& el,
                            bool wrap,
                            const char* ctx);

// ------------------------------------------------------------
//  Context helpers
//  child_ctx() appends ".key" to the parent context in a
//  stack-allocated buffer and returns a pointer into it.
//  The buffer is local to the caller's scope.
// ------------------------------------------------------------
#define CHILD_CTX(parent_ctx, key_str, buf) \
    do { std::snprintf((buf), sizeof(buf), "%s.%s", (parent_ctx), (key_str)); } while(0)

// ------------------------------------------------------------
//  render_object / render_array
// ------------------------------------------------------------

static void render_object(const char* label,
                           const simdjson::dom::element& el,
                           bool wrap,
                           const char* ctx,
                           bool default_open = false)
{
    ImGuiTreeNodeFlags flags = default_open ? ImGuiTreeNodeFlags_DefaultOpen : 0;
    // The label shown on the tree node is the already-translated key
    if (ImGui::TreeNodeEx(label, flags)) {
        for (auto [k, v] : el.get_object().value()) {
            // Build the raw key as a null-terminated string
            char raw_key[256];
            std::snprintf(raw_key, sizeof(raw_key), "%.*s",
                          static_cast<int>(k.size()), k.data());

            // Build child context: parent_ctx.raw_key
            char child_ctx[512];
            CHILD_CTX(ctx, raw_key, child_ctx);

            // Translate raw_key → display label using the child context
            // (the label is what the user sees; raw_key drives context lookups)
            const char* display = log_key_label(raw_key, ctx);

            render_element(display, v, wrap, child_ctx);
        }
        ImGui::TreePop();
    }
}

static void render_array(const char* label,
                          const simdjson::dom::element& el,
                          bool wrap,
                          const char* ctx)
{
    if (ImGui::TreeNode(label)) {
        int idx = 0;
        for (auto item : el.get_array().value()) {
            char idx_buf[32];
            std::snprintf(idx_buf, sizeof(idx_buf), "[%d]", idx++);
            // Array elements keep parent ctx — their index isn't a key name
            render_element(idx_buf, item, wrap, ctx);
        }
        ImGui::TreePop();
    }
}

// ------------------------------------------------------------
//  render_leaf_impl
// ------------------------------------------------------------
static void render_leaf_impl(const char* key, ImVec4 value_color,
                              const char* value_str, bool wrap)
{
    if (!wrap) {
        ImGui::PushStyleColor(ImGuiCol_Text, value_color);
        ImGui::Text("%-36s  %s", key, value_str);
        ImGui::PopStyleColor();
    } else {
        // key in dim, value coloured, soft-wrapped
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
        ImGui::TextUnformatted(key);
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 0);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
        ImGui::TextUnformatted(":  ");
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 0);
        ImGui::PushStyleColor(ImGuiCol_Text, value_color);
        ImGui::TextWrapped("%s", value_str);
        ImGui::PopStyleColor();
    }
}

// ------------------------------------------------------------
//  render_element
// ------------------------------------------------------------
static void render_element(const char* key,
                            const simdjson::dom::element& el,
                            bool wrap,
                            const char* ctx)
{
    using T = simdjson::dom::element_type;
    switch (el.type()) {
        case T::OBJECT:
            render_object(key, el, wrap, ctx);
            break;
        case T::ARRAY:
            render_array(key, el, wrap, ctx);
            break;
        case T::STRING: {
            std::string_view sv;
            (void)el.get_string().get(sv);
            char small[512];
            const char* val_str;
            std::string heap_str;
            size_t needed = sv.size() + 3;
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
                            const std::string& file_path,
                            const StringTable* strings)
{
    entry_     = entry;
    file_path_ = file_path;
    strings_   = strings;
}

void DetailView::render_toolbar() {
    ImGui::Checkbox("Wrap text", &wrap_);
}

void DetailView::render_inner() {
    render_toolbar();
    ImGui::Separator();

    if (!entry_ || file_path_.empty()) {
        ImGui::TextDisabled("Click a log entry to inspect it.");
        return;
    }

    // Open the file, pread the log line, close immediately.
    // This is the on-demand approach: no mmap is kept open between calls.
    const uint64_t offset = entry_->raw_offset;
    const size_t   rlen   = entry_->raw_len;

    // Read rlen + SIMDJSON_PADDING bytes into a local buffer
    std::vector<char> buf(rlen + simdjson::SIMDJSON_PADDING, '\0');

#if defined(_WIN32)
    HANDLE fh = CreateFileA(file_path_.c_str(), GENERIC_READ,
                            FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
    if (fh == INVALID_HANDLE_VALUE) {
        ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "Cannot open file");
        return;
    }
    LARGE_INTEGER li; li.QuadPart = static_cast<LONGLONG>(offset);
    SetFilePointerEx(fh, li, nullptr, FILE_BEGIN);
    DWORD read_bytes = 0;
    ReadFile(fh, buf.data(), static_cast<DWORD>(rlen), &read_bytes, nullptr);
    CloseHandle(fh);
#else
    int fd = ::open(file_path_.c_str(), O_RDONLY);
    if (fd < 0) {
        ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "Cannot open file");
        return;
    }
    ::pread(fd, buf.data(), rlen, static_cast<off_t>(offset));
    ::close(fd);
#endif

    simdjson::padded_string padded(buf.data(), rlen);
    simdjson::dom::element doc;
    auto err = tl_parser.parse(padded).get(doc);
    if (err) {
        ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1),
                           "Parse error: %s", simdjson::error_message(err));
        return;
    }

    ImGuiWindowFlags scroll_flags = wrap_
        ? ImGuiWindowFlags_None
        : ImGuiWindowFlags_HorizontalScrollbar;

    ImGui::BeginChild("##detail_scroll", ImVec2(0, 0), false, scroll_flags);
    render_object("document", doc, wrap_, "document", /*default_open=*/true);
    ImGui::EndChild();
}

void DetailView::render() {
    ImGui::Begin("Entry Detail");
    render_inner();
    ImGui::End();
}
