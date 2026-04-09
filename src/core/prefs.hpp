#pragma once

#include <string>

// ------------------------------------------------------------
//  Prefs — user preferences persisted to disk
// ------------------------------------------------------------
struct Prefs {
    std::string font_name       = "Inter";  // display name, must match FontManager
    int         font_size       = 13;       // 10–20 pt
    int         memory_limit_gb = 0;        // 0 = auto (60% of total RAM)
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
