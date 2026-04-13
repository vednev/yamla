#include "prefs.hpp"
#include "json_escape.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

// ------------------------------------------------------------
//  Config path — ~/.config/yamla/prefs.json
// ------------------------------------------------------------
std::string PrefsManager::config_path() {
    const char* home = std::getenv("HOME");
    if (!home || !home[0]) home = "/tmp";
    return std::string(home) + "/.config/yamla/prefs.json";
}

// ------------------------------------------------------------
//  load — minimal hand-rolled JSON parse for two fields
// ------------------------------------------------------------
Prefs PrefsManager::load() {
    Prefs p;  // defaults from struct

    std::string path = config_path();
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return p;  // file not found — use defaults

    // Read whole file (it's tiny: ~200 bytes typically)
    char buf[4096] = {};
    size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
    std::fclose(f);
    buf[n] = '\0';

    // Parse "font":"..." — find key, read value (handles escaped quotes)
    auto parse_str = [&](const char* key, std::string& out) {
        const char* pos = std::strstr(buf, key);
        if (!pos) return;
        pos = std::strchr(pos, ':');
        if (!pos) return;
        // Skip to opening quote
        pos = std::strchr(pos, '"');
        if (!pos) return;
        ++pos; // skip the opening quote
        // Read until unescaped closing quote
        std::string result;
        while (*pos && *pos != '"') {
            if (*pos == '\\' && *(pos+1)) {
                ++pos;
                switch (*pos) {
                    case '"':  result += '"';  break;
                    case '\\': result += '\\'; break;
                    case 'n':  result += '\n'; break;
                    case 'r':  result += '\r'; break;
                    case 't':  result += '\t'; break;
                    default:   result += *pos; break;
                }
            } else {
                result += *pos;
            }
            ++pos;
        }
        out = result;
    };

    auto parse_int = [&](const char* key, int& out) {
        const char* pos = std::strstr(buf, key);
        if (!pos) return;
        pos = std::strchr(pos, ':');
        if (!pos) return;
        while (*pos == ':' || *pos == ' ') ++pos;
        if (*pos >= '0' && *pos <= '9')
            out = static_cast<int>(std::strtol(pos, nullptr, 10));
    };

    parse_str("\"font\"",   p.font_name);
    parse_int("\"size\"",   p.font_size);
    parse_int("\"mem_gb\"", p.memory_limit_gb);
    // prefer_checkboxes stored as 0/1 int
    int ckbox = p.prefer_checkboxes ? 1 : 0;
    parse_int("\"ckbox\"",  ckbox);
    p.prefer_checkboxes = (ckbox != 0);

    // LLM fields
    parse_str("\"llm_key\"",      p.llm_api_key);
    parse_str("\"llm_endpoint\"", p.llm_endpoint);
    parse_str("\"llm_model\"",    p.llm_model);
    parse_int("\"llm_maxtok\"",   p.llm_max_tokens);
    parse_str("\"export_dir\"",   p.export_dir);
    parse_int("\"chart_cols\"",  p.chart_layout_columns);

    // Clamp size to valid range
    if (p.font_size < 10) p.font_size = 10;
    if (p.font_size > 20) p.font_size = 20;
    if (p.llm_max_tokens < 256)  p.llm_max_tokens = 256;
    if (p.llm_max_tokens > 32768) p.llm_max_tokens = 32768;
    if (p.chart_layout_columns < 0 || p.chart_layout_columns > 4)
        p.chart_layout_columns = 0;

    return p;
}

// ------------------------------------------------------------
//  save
// ------------------------------------------------------------
void PrefsManager::save(const Prefs& p) {
    std::string path = config_path();
    std::string dir = path.substr(0, path.rfind('/'));
#if defined(_WIN32)
    _mkdir(dir.c_str());
#else
    ::mkdir(dir.c_str(), 0755);
#endif

    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return;
    std::fprintf(f,
        "{\"font\":\"%s\",\"size\":%d,\"mem_gb\":%d,\"ckbox\":%d,"
        "\"llm_key\":\"%s\",\"llm_endpoint\":\"%s\","
        "\"llm_model\":\"%s\",\"llm_maxtok\":%d,"
        "\"export_dir\":\"%s\",\"chart_cols\":%d}\n",
        json_escape(p.font_name).c_str(), p.font_size, p.memory_limit_gb,
        p.prefer_checkboxes ? 1 : 0,
        json_escape(p.llm_api_key).c_str(), json_escape(p.llm_endpoint).c_str(),
        json_escape(p.llm_model).c_str(), p.llm_max_tokens,
        json_escape(p.export_dir).c_str(), p.chart_layout_columns);
    std::fclose(f);

#if !defined(_WIN32)
    ::chmod(path.c_str(), 0600);
#endif
}
