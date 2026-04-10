#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstdint>

// ------------------------------------------------------------
//  ContentBlock — one piece of a chat message
//
//  Anthropic messages contain arrays of content blocks.
//  A block is either text, a tool_use request from the
//  assistant, or a tool_result we feed back.
// ------------------------------------------------------------
struct ContentBlock {
    enum Type { Text, ToolUse, ToolResult };

    Type        type          = Text;
    std::string text;                  // Text block content

    // ToolUse fields (assistant requesting a tool call)
    std::string tool_use_id;
    std::string tool_name;
    std::string tool_input_json;       // raw JSON of input params

    // ToolResult fields (our response to a tool call)
    std::string tool_result;
    bool        tool_is_error = false;
};

// ------------------------------------------------------------
//  ChatMessage — one turn in the conversation
// ------------------------------------------------------------
struct ChatMessage {
    std::string                role;     // "user", "assistant"
    std::vector<ContentBlock>  content;

    // Convenience: get concatenated text from all Text blocks
    std::string text() const {
        std::string out;
        for (const auto& b : content)
            if (b.type == ContentBlock::Text)
                out += b.text;
        return out;
    }

    // Convenience: check if message has any tool_use blocks
    bool has_tool_use() const {
        for (const auto& b : content)
            if (b.type == ContentBlock::ToolUse)
                return true;
        return false;
    }
};

// ------------------------------------------------------------
//  ConversationState — thread-safe conversation history
//
//  The background LLM thread appends messages; the UI thread
//  reads them for rendering. Access is mutex-protected, but
//  most frames skip the lock by checking the atomic size first.
// ------------------------------------------------------------
class ConversationState {
public:
    ConversationState() = default;

    // Append a message (called from LLM thread or UI thread for user msgs)
    void push(ChatMessage msg) {
        std::lock_guard<std::mutex> lock(mu_);
        history_.push_back(std::move(msg));
        size_.store(history_.size(), std::memory_order_release);
    }

    // Get current size without locking (for UI fast-path check)
    size_t size() const { return size_.load(std::memory_order_acquire); }

    // Copy out all messages (UI thread, only when size changed)
    std::vector<ChatMessage> snapshot() const {
        std::lock_guard<std::mutex> lock(mu_);
        return history_;
    }

    // Copy out for API serialization (LLM thread)
    std::vector<ChatMessage> copy() const {
        std::lock_guard<std::mutex> lock(mu_);
        return history_;
    }

    // Clear conversation
    void clear() {
        std::lock_guard<std::mutex> lock(mu_);
        history_.clear();
        size_.store(0, std::memory_order_release);
    }

    // Check if empty
    bool empty() const { return size() == 0; }

private:
    mutable std::mutex           mu_;
    std::vector<ChatMessage>     history_;
    std::atomic<size_t>          size_{0};
};
