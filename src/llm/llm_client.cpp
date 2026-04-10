#include "llm_client.hpp"

// cpp-httplib needs CPPHTTPLIB_OPENSSL_SUPPORT for HTTPS
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>

#include <simdjson.h>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <sys/stat.h>

// ------------------------------------------------------------
//  JSON helpers (local to this TU)
// ------------------------------------------------------------
static std::string json_esc(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

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
    body += json_esc(model);
    body += "\",\"max_tokens\":";
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", max_tokens);
    body += buf;

    // System prompt
    body += ",\"system\":\"";
    body += json_esc(system_prompt);
    body += "\"";

    // Messages array
    body += ",\"messages\":[";
    for (size_t i = 0; i < history.size(); ++i) {
        if (i > 0) body += ",";
        const auto& msg = history[i];

        body += "{\"role\":\"";
        body += json_esc(msg.role);
        body += "\",\"content\":[";

        for (size_t j = 0; j < msg.content.size(); ++j) {
            if (j > 0) body += ",";
            const auto& block = msg.content[j];

            switch (block.type) {
                case ContentBlock::Text:
                    body += "{\"type\":\"text\",\"text\":\"";
                    body += json_esc(block.text);
                    body += "\"}";
                    break;

                case ContentBlock::ToolUse:
                    body += "{\"type\":\"tool_use\",\"id\":\"";
                    body += json_esc(block.tool_use_id);
                    body += "\",\"name\":\"";
                    body += json_esc(block.tool_name);
                    body += "\",\"input\":";
                    body += block.tool_input_json; // already valid JSON
                    body += "}";
                    break;

                case ContentBlock::ToolResult:
                    body += "{\"type\":\"tool_result\",\"tool_use_id\":\"";
                    body += json_esc(block.tool_use_id);
                    body += "\",\"content\":\"";
                    body += json_esc(block.tool_result);
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

// Parse the Anthropic Messages API response into a ChatMessage
static bool parse_response(const std::string& response_body,
                            ChatMessage& out_msg,
                            std::string& out_error)
{
    simdjson::dom::parser parser;
    simdjson::dom::element doc;
    auto err = parser.parse(response_body).get(doc);
    if (err) {
        out_error = "Failed to parse API response JSON";
        return false;
    }

    // Check for API error
    std::string_view resp_type;
    if (doc["type"].get_string().get(resp_type) == simdjson::SUCCESS) {
        if (resp_type == "error") {
            std::string_view err_msg;
            if (doc["error"]["message"].get_string().get(err_msg) == simdjson::SUCCESS)
                out_error = std::string(err_msg);
            else
                out_error = "API returned an error";
            return false;
        }
    }

    out_msg.role = "assistant";

    // Parse content array
    simdjson::dom::array content_arr;
    if (doc["content"].get_array().get(content_arr) != simdjson::SUCCESS) {
        out_error = "Response missing content array";
        return false;
    }

    for (auto item : content_arr) {
        std::string_view block_type;
        if (item["type"].get_string().get(block_type) != simdjson::SUCCESS)
            continue;

        ContentBlock block;

        if (block_type == "text") {
            block.type = ContentBlock::Text;
            std::string_view text;
            if (item["text"].get_string().get(text) == simdjson::SUCCESS)
                block.text = std::string(text);
            out_msg.content.push_back(std::move(block));
        }
        else if (block_type == "tool_use") {
            block.type = ContentBlock::ToolUse;
            std::string_view id, name;
            if (item["id"].get_string().get(id) == simdjson::SUCCESS)
                block.tool_use_id = std::string(id);
            if (item["name"].get_string().get(name) == simdjson::SUCCESS)
                block.tool_name = std::string(name);

            // Serialize input back to JSON string
            simdjson::dom::element input_el;
            if (item["input"].get(input_el) == simdjson::SUCCESS) {
                block.tool_input_json = simdjson::minify(input_el);
            } else {
                block.tool_input_json = "{}";
            }
            out_msg.content.push_back(std::move(block));
        }
    }

    return true;
}

// ------------------------------------------------------------
//  LlmClient implementation
// ------------------------------------------------------------

LlmClient::LlmClient() = default;

LlmClient::~LlmClient() {
    if (worker_.joinable()) worker_.join();
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

    // Set up HTTPS client
    std::string host = "https://" + prefs_->llm_endpoint;
    httplib::Client cli(host);
    cli.set_connection_timeout(10);  // 10s connect
    cli.set_read_timeout(120);       // 120s read (LLM can be slow)
    cli.set_write_timeout(10);

    // Load CA certificates for TLS verification.
    // Homebrew OpenSSL on macOS doesn't use the system keychain —
    // we must point it at a CA bundle explicitly.
    {
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
                cli.set_ca_cert_path(*p);
                break;
            }
        }
    }

    std::string tools_json_str;
    if (tools_.has_data())
        tools_json_str = LlmTools::tools_json();

    for (int iter = 0; iter < MAX_ITERATIONS; ++iter) {
        // Build request from current conversation state
        auto history = conversation_.copy();
        std::string req_body = build_request_body(
            prefs_->llm_model,
            prefs_->llm_max_tokens,
            system_prompt_,
            history,
            tools_json_str);

        // Make the API call
        httplib::Headers headers = {
            {"Content-Type",      "application/json"},
            {"anthropic-version", "2023-06-01"},
            {"api-key",           prefs_->llm_api_key}
        };

        std::string api_path = "/grove-foundry-prod/anthropic/v1/messages";

        auto res = cli.Post(api_path, headers, req_body, "application/json");

        if (!res) {
            auto err_val = res.error();
            set_error(std::string("HTTP request failed: ") + httplib::to_string(err_val));
            break;
        }

        if (res->status != 200) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "API returned HTTP %d: ", res->status);
            // Try to extract error message from response
            std::string err_detail;
            simdjson::dom::parser ep;
            simdjson::dom::element edoc;
            if (ep.parse(res->body).get(edoc) == simdjson::SUCCESS) {
                std::string_view em;
                if (edoc["error"]["message"].get_string().get(em) == simdjson::SUCCESS)
                    err_detail = std::string(em);
            }
            if (err_detail.empty()) err_detail = res->body.substr(0, 200);
            set_error(std::string(buf) + err_detail);
            break;
        }

        // Parse response
        ChatMessage assistant_msg;
        std::string parse_err;
        if (!parse_response(res->body, assistant_msg, parse_err)) {
            set_error(parse_err);
            break;
        }

        // Push assistant message to conversation
        conversation_.push(assistant_msg);

        // Check if assistant wants to use tools
        if (!assistant_msg.has_tool_use()) {
            // Done — assistant produced a final text response
            break;
        }

        // Execute tool calls and feed results back
        ChatMessage tool_result_msg;
        tool_result_msg.role = "user";

        for (const auto& block : assistant_msg.content) {
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

    thinking_.store(false, std::memory_order_release);
}
