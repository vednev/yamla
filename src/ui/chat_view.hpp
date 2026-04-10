#pragma once

#include <string>
#include <vector>
#include <cstddef>

#include "../llm/llm_client.hpp"
#include "../llm/llm_types.hpp"

// ------------------------------------------------------------
//  ChatView — non-modal floating ImGui chat window
//
//  Renders conversation history with markdown-formatted
//  assistant responses. Input box at bottom for user messages.
//  Ctrl+A to open, Escape to close.
// ------------------------------------------------------------
struct Prefs;

class ChatView {
public:
    ChatView() = default;

    void set_llm_client(LlmClient* client) { client_ = client; }
    void set_prefs(const Prefs* prefs) { prefs_ = prefs; }

    bool is_open() const { return open_; }
    void show()  { open_ = true; }
    void close() { open_ = false; }
    void toggle() { open_ = !open_; }

    // Call once per frame from App::render_frame()
    void render();

private:
    // Render a single message
    void render_message(const ChatMessage& msg, size_t msg_idx);

    // Render markdown text using md4c
    void render_markdown(const std::string& text);

    // Render a collapsed summary of tool calls within an assistant turn
    void render_tool_summary(const ChatMessage& assistant_msg,
                             const ChatMessage* result_msg,
                             size_t msg_idx);

    // Export an assistant response as a text file
    void export_response(const ChatMessage& msg);

    // Render the error modal (called every frame)
    void render_error_modal();

    LlmClient*    client_ = nullptr;
    const Prefs*  prefs_  = nullptr;
    bool          open_   = false;

    // Input buffer
    char input_buf_[4096] = {};

    // Track rendered count for auto-scroll
    size_t last_rendered_count_ = 0;
    bool   scroll_to_bottom_    = false;

    // Cached snapshot of conversation (avoid locking every frame)
    std::vector<ChatMessage> cached_messages_;
    size_t                   cached_size_ = 0;

    // Error modal state
    bool        show_error_modal_ = false;
    std::string error_modal_text_;
};
