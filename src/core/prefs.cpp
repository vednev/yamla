#include "prefs.hpp"

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

    // Read whole file (it's tiny: ~40 bytes)
    char buf[512] = {};
    size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
    std::fclose(f);
    buf[n] = '\0';

    // Parse "font":"..." — find key, read value
    auto parse_str = [&](const char* key, std::string& out) {
        const char* pos = std::strstr(buf, key);
        if (!pos) return;
        pos = std::strchr(pos, ':');
        if (!pos) return;
        while (*pos == ':' || *pos == ' ' || *pos == '"') ++pos;
        const char* end = std::strchr(pos, '"');
        if (!end) return;
        out.assign(pos, end);
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

    // Clamp size to valid range
    if (p.font_size < 10) p.font_size = 10;
    if (p.font_size > 20) p.font_size = 20;

    return p;
}

// ------------------------------------------------------------
//  save
// ------------------------------------------------------------
void PrefsManager::save(const Prefs& p) {
    std::string path = config_path();

    // Ensure directory exists
    std::string dir = path.substr(0, path.rfind('/'));
#if defined(_WIN32)
    _mkdir(dir.c_str());
#else
    ::mkdir(dir.c_str(), 0755);
#endif

    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return;
    std::fprintf(f, "{\"font\":\"%s\",\"size\":%d,\"mem_gb\":%d,\"ckbox\":%d}\n",
                 p.font_name.c_str(), p.font_size, p.memory_limit_gb,
                 p.prefer_checkboxes ? 1 : 0);
    std::fclose(f);
}
