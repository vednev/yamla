#pragma once

#include <string>
#include <vector>

// Forward declarations
class Cluster;
struct LogEntry;
class StringTable;
struct FilterState;

// ------------------------------------------------------------
//  LlmTools — tool schemas and execution
//
//  Provides Anthropic-format tool definitions and executes
//  tool calls against the in-memory Cluster data.
// ------------------------------------------------------------
class LlmTools {
public:
    explicit LlmTools() = default;

    // Set the data source. Must be called when cluster becomes Ready.
    // All pointers must remain valid for the lifetime of tool calls.
    void set_cluster(const Cluster* cluster);
    void set_selected_entry(const LogEntry* entry, const std::string& file_path);

    bool has_data() const { return cluster_ != nullptr; }

    // Return the tools[] JSON array string for the API request.
    // This is a static schema — doesn't change per request.
    static std::string tools_json();

    // Execute a tool call and return the result as a JSON string.
    // thread-safe for reads (cluster data is immutable after Ready).
    std::string execute(const std::string& tool_name,
                        const std::string& input_json) const;

private:
    std::string exec_get_analysis_summary() const;
    std::string exec_search_logs(const std::string& input_json) const;
    std::string exec_get_entry_detail(const std::string& input_json) const;
    std::string exec_get_slow_queries(const std::string& input_json) const;
    std::string exec_get_connections(const std::string& input_json) const;
    std::string exec_get_error_details(const std::string& input_json) const;

    const Cluster*  cluster_        = nullptr;
    const LogEntry* selected_entry_ = nullptr;
    std::string     selected_file_;
};
