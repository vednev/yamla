#include "llm_client.hpp"
#include "../core/json_escape.hpp"

// cpp-httplib needs CPPHTTPLIB_OPENSSL_SUPPORT for HTTPS
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>

#include <simdjson.h>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <sys/stat.h>

// Build the JSON request body for the Anthropic Messages API
static std::string build_request_body(
    const std::string& model,
    int max_tokens,
    const std::string& system_prompt,
    const std::vector<ChatMessage>& history,
    const std::string& tools_json)
{
    std::string body;
    body.reserve(4096);

    body += "{\"model\":\"";
    body += json_escape(model);
    body += "\",\"max_tokens\":";
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", max_tokens);
    body += buf;

    body += ",\"stream\":true";

    // System prompt
    body += ",\"system\":\"";
    body += json_escape(system_prompt);
    body += "\"";

    // Messages array
    body += ",\"messages\":[";
    for (size_t i = 0; i < history.size(); ++i) {
        if (i > 0) body += ",";
        const auto& msg = history[i];

        body += "{\"role\":\"";
        body += json_escape(msg.role);
        body += "\",\"content\":[";

        for (size_t j = 0; j < msg.content.size(); ++j) {
            if (j > 0) body += ",";
            const auto& block = msg.content[j];

            switch (block.type) {
                case ContentBlock::Text:
                    body += "{\"type\":\"text\",\"text\":\"";
                    body += json_escape(block.text);
                    body += "\"}";
                    break;

                case ContentBlock::ToolUse:
                    body += "{\"type\":\"tool_use\",\"id\":\"";
                    body += json_escape(block.tool_use_id);
                    body += "\",\"name\":\"";
                    body += json_escape(block.tool_name);
                    body += "\",\"input\":";
                    body += block.tool_input_json; // already valid JSON
                    body += "}";
                    break;

                case ContentBlock::ToolResult:
                    body += "{\"type\":\"tool_result\",\"tool_use_id\":\"";
                    body += json_escape(block.tool_use_id);
                    body += "\",\"content\":\"";
                    body += json_escape(block.tool_result);
                    body += "\"";
                    if (block.tool_is_error)
                        body += ",\"is_error\":true";
                    body += "}";
                    break;
            }
        }

        body += "]}";
    }
    body += "]";

    // Tools
    if (!tools_json.empty()) {
        body += ",\"tools\":";
        body += tools_json;
    }

    body += "}";
    return body;
}

// ------------------------------------------------------------
//  LlmClient implementation
// ------------------------------------------------------------

LlmClient::LlmClient() = default;

void LlmClient::cancel_and_join() {
    cancel_.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(cli_mu_);
        if (cli_) cli_->stop();
    }
    if (worker_.joinable()) worker_.join();
}

LlmClient::~LlmClient() {
    cancel_and_join();
}

bool LlmClient::is_configured() const {
    return prefs_ && !prefs_->llm_api_key.empty() && !prefs_->llm_endpoint.empty();
}

std::string LlmClient::last_error() const {
    std::lock_guard<std::mutex> lock(error_mu_);
    return last_error_;
}

void LlmClient::clear() {
    conversation_.clear();
    {
        std::lock_guard<std::mutex> lock(error_mu_);
        last_error_.clear();
    }
}

void LlmClient::send(const std::string& user_message) {
    if (thinking_.load()) return;  // already processing

    // Push user message
    ChatMessage user_msg;
    user_msg.role = "user";
    ContentBlock text_block;
    text_block.type = ContentBlock::Text;
    text_block.text = user_message;
    user_msg.content.push_back(std::move(text_block));
    conversation_.push(std::move(user_msg));

    // Clear previous error
    {
        std::lock_guard<std::mutex> lock(error_mu_);
        last_error_.clear();
    }

    // Join any previous worker
    if (worker_.joinable()) worker_.join();

    thinking_.store(true, std::memory_order_release);
    worker_ = std::thread([this] { run_loop(); });
}

// ------------------------------------------------------------
//  run_loop — agentic tool-use loop on background thread
// ------------------------------------------------------------
void LlmClient::run_loop() {
    auto set_error = [this](const std::string& err) {
        std::lock_guard<std::mutex> lock(error_mu_);
        last_error_ = err;
    };

    if (!prefs_) {
        set_error("No preferences configured");
        thinking_.store(false, std::memory_order_release);
        return;
    }

    if (prefs_->llm_api_key.empty()) {
        set_error("API key not configured. Set it in Edit > Preferences.");
        thinking_.store(false, std::memory_order_release);
        return;
    }

    // Set up HTTPS client — stored as member so destructor can call stop()
    {
        std::lock_guard<std::mutex> lock(cli_mu_);
        std::string host = "https://" + prefs_->llm_endpoint;
        cli_ = std::make_unique<httplib::Client>(host);
        cli_->set_connection_timeout(10);   // 10s connect
        cli_->set_read_timeout(300);        // 5 min read — SSE streams can have long gaps
        cli_->set_write_timeout(10);

        // Load CA certificates for TLS verification.
        static const char* ca_paths[] = {
#if defined(__APPLE__)
            "/opt/homebrew/etc/ca-certificates/cert.pem",
            "/opt/homebrew/etc/openssl@3/cert.pem",
            "/etc/ssl/cert.pem",
#elif defined(_WIN32)
            nullptr,  // Windows: OpenSSL uses its own store
#else
            "/etc/ssl/certs/ca-certificates.crt",  // Debian/Ubuntu
            "/etc/pki/tls/certs/ca-bundle.crt",    // RHEL/Fedora
            "/etc/ssl/cert.pem",                    // Alpine/generic
#endif
            nullptr
        };
        for (const char** p = ca_paths; *p; ++p) {
            struct stat st;
            if (::stat(*p, &st) == 0) {
                cli_->set_ca_cert_path(*p);
                break;
            }
        }
    }

    std::string tools_json_str;
    if (tools_.has_data())
        tools_json_str = LlmTools::tools_json();

    for (int iter = 0; iter < MAX_ITERATIONS; ++iter) {
        if (cancel_.load(std::memory_order_acquire)) break;

        // Build request from current conversation state
        auto history = conversation_.copy();
        std::string req_body = build_request_body(
            prefs_->llm_model,
            prefs_->llm_max_tokens,
            system_prompt_,
            history,
            tools_json_str);

        httplib::Headers headers = {
            {"Content-Type",      "application/json"},
            {"anthropic-version", "2023-06-01"},
            {"api-key",           prefs_->llm_api_key}
        };

        std::string api_path = "/grove-foundry-prod/anthropic/v1/messages";

        // Push an empty assistant message that we'll stream into
        ChatMessage streaming_msg;
        streaming_msg.role = "assistant";
        conversation_.push(streaming_msg);

        // SSE parsing state
        struct StreamState {
            std::string line_buf;           // current line being assembled
            std::string event_type;         // from "event:" line
            bool has_error = false;
            std::string error_msg;

            // Current content block being assembled
            std::string current_block_type; // "text" or "tool_use"
            std::string tool_use_id;
            std::string tool_name;
            std::string tool_input_json;    // accumulated from input_json_delta

            // The assembled message (for post-processing tool calls)
            ChatMessage assembled_msg;
            bool message_complete = false;
        };
        StreamState ss;
        ss.assembled_msg.role = "assistant";

        // Reference to cancel flag and conversation for the lambda
        auto& cancel_ref = cancel_;
        auto& conv_ref   = conversation_;

        // Content receiver callback — called with chunks of SSE data
        auto content_receiver = [&ss, &cancel_ref, &conv_ref](
                const char* data, size_t len) -> bool
        {
            if (cancel_ref.load(std::memory_order_acquire)) return false;

            // Append to line buffer and process complete lines
            for (size_t i = 0; i < len; ++i) {
                char c = data[i];
                if (c == '\n') {
                    // Process the completed line
                    std::string& line = ss.line_buf;

                    if (line.empty()) {
                        // Empty line = end of SSE event block
                    } else if (line.compare(0, 6, "event:") == 0) {
                        size_t start = 6;
                        while (start < line.size() && line[start] == ' ') ++start;
                        ss.event_type = line.substr(start);
                    } else if (line.compare(0, 5, "data:") == 0) {
                        size_t start = 5;
                        while (start < line.size() && line[start] == ' ') ++start;
                        std::string json_str = line.substr(start);

                        simdjson::dom::parser parser;
                        simdjson::padded_string padded(json_str);
                        simdjson::dom::element doc;
                        if (parser.parse(padded).get(doc) != simdjson::SUCCESS) {
                            ss.line_buf.clear();
                            continue;
                        }

                        std::string_view type;
                        (void)doc["type"].get_string().get(type);

                        if (type == "content_block_start") {
                            simdjson::dom::element cb;
                            if (doc["content_block"].get(cb) == simdjson::SUCCESS) {
                                std::string_view cb_type;
                                (void)cb["type"].get_string().get(cb_type);
                                ss.current_block_type = std::string(cb_type);

                                if (cb_type == "tool_use") {
                                    std::string_view id, name;
                                    (void)cb["id"].get_string().get(id);
                                    (void)cb["name"].get_string().get(name);
                                    ss.tool_use_id = std::string(id);
                                    ss.tool_name = std::string(name);
                                    ss.tool_input_json.clear();
                                } else if (cb_type == "text") {
                                    ContentBlock tb;
                                    tb.type = ContentBlock::Text;
                                    ss.assembled_msg.content.push_back(std::move(tb));
                                }
                            }
                        }
                        else if (type == "content_block_delta") {
                            simdjson::dom::element delta;
                            if (doc["delta"].get(delta) == simdjson::SUCCESS) {
                                std::string_view delta_type;
                                (void)delta["type"].get_string().get(delta_type);

                                if (delta_type == "text_delta") {
                                    std::string_view text;
                                    if (delta["text"].get_string().get(text) == simdjson::SUCCESS) {
                                        std::string t(text);
                                        // Append to conversation for live UI update
                                        conv_ref.append_to_last(t);
                                        // Also append to assembled msg for post-processing
                                        if (!ss.assembled_msg.content.empty()) {
                                            auto& last = ss.assembled_msg.content.back();
                                            if (last.type == ContentBlock::Text)
                                                last.text += t;
                                        }
                                    }
                                }
                                else if (delta_type == "input_json_delta") {
                                    std::string_view pj;
                                    if (delta["partial_json"].get_string().get(pj) == simdjson::SUCCESS) {
                                        ss.tool_input_json += std::string(pj);
                                    }
                                }
                            }
                        }
                        else if (type == "content_block_stop") {
                            if (ss.current_block_type == "tool_use") {
                                ContentBlock tb;
                                tb.type = ContentBlock::ToolUse;
                                tb.tool_use_id = ss.tool_use_id;
                                tb.tool_name = ss.tool_name;
                                tb.tool_input_json = ss.tool_input_json.empty() ? "{}" : ss.tool_input_json;
                                ss.assembled_msg.content.push_back(std::move(tb));
                            }
                            ss.current_block_type.clear();
                        }
                        else if (type == "message_stop") {
                            ss.message_complete = true;
                        }
                        else if (type == "error") {
                            std::string_view err_msg;
                            simdjson::dom::element err_obj;
                            if (doc["error"].get(err_obj) == simdjson::SUCCESS) {
                                (void)err_obj["message"].get_string().get(err_msg);
                            }
                            ss.has_error = true;
                            ss.error_msg = err_msg.empty() ? "API streaming error" : std::string(err_msg);
                        }
                    }
                    ss.line_buf.clear();
                } else if (c != '\r') {
                    ss.line_buf += c;
                }
            }
            return true;
        };

        // Build request with streaming content receiver
        httplib::Request req;
        req.method = "POST";
        req.path   = api_path;
        req.headers = headers;
        req.body    = req_body;
        req.set_header("Content-Type", "application/json");
        req.content_receiver =
            [&content_receiver](const char* data, size_t len,
                                uint64_t /*offset*/, uint64_t /*total*/) -> bool {
                return content_receiver(data, len);
            };

        httplib::Response resp;
        httplib::Error    http_err = httplib::Error::Success;
        bool ok = cli_->send(req, resp, http_err);

        if (cancel_.load(std::memory_order_acquire)) break;

        if (!ok) {
            set_error(std::string("HTTP request failed: ") + httplib::to_string(http_err));
            break;
        }

        if (resp.status != 200) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "API returned HTTP %d: ", resp.status);
            std::string err_detail;
            if (!resp.body.empty()) {
                simdjson::dom::parser ep;
                simdjson::dom::element edoc;
                if (ep.parse(resp.body).get(edoc) == simdjson::SUCCESS) {
                    std::string_view em;
                    if (edoc["error"]["message"].get_string().get(em) == simdjson::SUCCESS)
                        err_detail = std::string(em);
                }
            }
            if (err_detail.empty() && !resp.body.empty())
                err_detail = resp.body.substr(0, 200);
            set_error(std::string(buf) + err_detail);
            break;
        }

        if (ss.has_error) {
            set_error(ss.error_msg);
            break;
        }

        // Check if the stream completed normally.
        // If message_stop was never received, the connection was likely
        // truncated by a proxy or network issue. The partial response
        // is already in the conversation — tell the user.
        if (!ss.message_complete && !cancel_.load(std::memory_order_acquire)) {
            // Only warn if we actually received some content
            bool has_content = false;
            for (const auto& b : ss.assembled_msg.content) {
                if (!b.text.empty() || b.type == ContentBlock::ToolUse) {
                    has_content = true;
                    break;
                }
            }
            if (has_content) {
                set_error("Response was cut short (connection closed). "
                          "Ask a follow-up to continue.");
            } else {
                set_error("No response received (stream ended unexpectedly).");
            }
            break;
        }

        // Check if the assembled message has tool_use blocks
        if (!ss.assembled_msg.has_tool_use()) {
            // Done — text was already streamed into the conversation
            break;
        }

        // Replace the streaming message with the full assembled message
        // (which includes tool_use blocks needed for conversation history)
        conversation_.replace_last(ss.assembled_msg);

        // Execute tool calls and feed results back
        ChatMessage tool_result_msg;
        tool_result_msg.role = "user";

        for (const auto& block : ss.assembled_msg.content) {
            if (block.type != ContentBlock::ToolUse) continue;

            std::string result;
            bool is_error = false;
            if (tools_.has_data()) {
                result = tools_.execute(block.tool_name, block.tool_input_json);
            } else {
                result = "{\"error\":\"No log data is currently loaded. Ask the user to load log files first.\"}";
                is_error = true;
            }

            ContentBlock rb;
            rb.type = ContentBlock::ToolResult;
            rb.tool_use_id = block.tool_use_id;
            rb.tool_result = std::move(result);
            rb.tool_is_error = is_error;
            tool_result_msg.content.push_back(std::move(rb));
        }

        conversation_.push(std::move(tool_result_msg));
        // Loop continues — model will see tool results and respond
    }

    // Release the HTTP client
    {
        std::lock_guard<std::mutex> lock(cli_mu_);
        cli_.reset();
    }

    thinking_.store(false, std::memory_order_release);
}
