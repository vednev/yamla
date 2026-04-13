#pragma once

#include <string>
#include <vector>

// ------------------------------------------------------------
//  Prefs — user preferences persisted to disk
// ------------------------------------------------------------
struct Prefs {
    std::string font_name         = "Inter"; // display name, must match FontManager
    int         font_size         = 13;      // 10–20 pt
    int         memory_limit_gb   = 0;       // 0 = auto (60% of total RAM)
    bool        prefer_checkboxes = false;   // show bar charts as checkbox lists

    // LLM integration
    std::string llm_api_key;                                        // Azure Foundry API key
    std::string llm_endpoint   = "grove-gateway-prod.azure-api.net"; // host (no scheme)
    std::string llm_model      = "claude-opus-4-6";                // model / deployment
    int         llm_max_tokens = 4096;                              // max response tokens
    std::string export_dir;                                         // directory for exported responses

    // Chart layout: 0=auto-detect, 1=list, 2/3/4=column count
    int         chart_layout_columns = 0;

    // Recent files: full paths, most recent first, max 10 managed by App
    std::vector<std::string> recent_files;
};

// ------------------------------------------------------------
//  PrefsManager — load / save ~/.config/yamla/prefs.json
// ------------------------------------------------------------
class PrefsManager {
public:
    static Prefs       load();
    static void        save(const Prefs& p);
    static std::string config_path();  // full path to the JSON file
};
