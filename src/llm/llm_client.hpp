#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <functional>

#include "llm_types.hpp"
#include "llm_tools.hpp"
#include "../core/prefs.hpp"

// ------------------------------------------------------------
//  LlmClient — background-threaded Anthropic Messages API
//
//  Sends requests to the configured Azure Foundry endpoint
//  using the Anthropic Messages format. Handles the agentic
//  tool-use loop: if the model responds with tool_use blocks,
//  executes them via LlmTools and feeds results back.
//
//  All API calls run on a background thread. The UI thread
//  reads conversation state via ConversationState (lock-free
//  size check, mutex-protected snapshot).
// ------------------------------------------------------------
class LlmClient {
public:
    LlmClient();
    ~LlmClient();

    LlmClient(const LlmClient&)            = delete;
    LlmClient& operator=(const LlmClient&) = delete;

    // Configuration
    void set_prefs(const Prefs* prefs) { prefs_ = prefs; }
    void set_system_prompt(const std::string& prompt) { system_prompt_ = prompt; }

    // Data source for tool execution
    LlmTools&       tools()        { return tools_; }
    const LlmTools& tools() const  { return tools_; }

    // Conversation state (shared with UI)
    ConversationState& conversation() { return conversation_; }

    // Send a user message. Launches the background agentic loop.
    // No-op if already thinking.
    void send(const std::string& user_message);

    // Is the background thread currently processing?
    bool is_thinking() const { return thinking_.load(std::memory_order_acquire); }

    // Error from last request (empty if none)
    std::string last_error() const;

    // Clear conversation and error state
    void clear();

    // Check if API key is configured
    bool is_configured() const;

private:
    void run_loop();  // background thread entry point

    const Prefs*       prefs_ = nullptr;
    std::string        system_prompt_;
    LlmTools           tools_;
    ConversationState  conversation_;

    std::atomic<bool>  thinking_{false};
    std::thread        worker_;

    mutable std::mutex error_mu_;
    std::string        last_error_;

    // Max tool-use iterations per user message (prevent infinite loops)
    static constexpr int MAX_ITERATIONS = 10;
};
