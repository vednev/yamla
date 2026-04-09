#include "font_manager.hpp"

#include <imgui.h>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

// ------------------------------------------------------------
//  Callbacks
// ------------------------------------------------------------
void FontManager::set_callbacks(UploadCb upload, DestroyCb destroy) {
    upload_cb_  = std::move(upload);
    destroy_cb_ = std::move(destroy);
}

// ------------------------------------------------------------
//  Font name → TTF filename mapping
// ------------------------------------------------------------
const char* FontManager::font_filename(const std::string& name) {
    if (name == "Inter")           return "Inter-Regular.ttf";
    if (name == "JetBrains Mono")  return "JetBrainsMono-Regular.ttf";
    if (name == "Fira Code")       return "FiraCode-Regular.ttf";
    if (name == "IBM Plex Sans")   return "IBMPlexSans-Regular.ttf";
    return nullptr;
}

static bool file_exists(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0;
}

// ------------------------------------------------------------
//  load_internal
// ------------------------------------------------------------
bool FontManager::load_internal(const Prefs& prefs,
                                 const std::string& vendor_dir,
                                 bool destroy_first)
{
    ImGuiIO& io = ImGui::GetIO();

    if (destroy_first) {
        if (destroy_cb_) destroy_cb_();
        io.Fonts->Clear();
    }

    available_.clear();
    static const char* known[] = {
        "Inter", "IBM Plex Sans", "Fira Code", "JetBrains Mono"
    };
    for (const char* n : known) {
        const char* fn = font_filename(n);
        if (!fn) continue;
        if (file_exists(vendor_dir + "/" + fn))
            available_.push_back(n);
    }

    if (available_.empty()) {
        io.Fonts->AddFontDefault();
        io.Fonts->Build();
        if (upload_cb_) upload_cb_();
        active_font_ = nullptr;
        return false;
    }

    std::string target = prefs.font_name;
    bool found = false;
    for (const auto& a : available_) {
        if (a == target) { found = true; break; }
    }
    if (!found) target = available_[0];

    float size_px = static_cast<float>(prefs.font_size);

    const char* fn = font_filename(target);
    std::string path = vendor_dir + "/" + fn;

    ImFontConfig cfg;
    cfg.OversampleH = 3;
    cfg.OversampleV = 2;
    cfg.PixelSnapH  = false;

    static const ImWchar glyph_ranges[] = {
        0x0020, 0x00FF,
        0x2013, 0x2026,
        0x2018, 0x201F,
        0, 0,
    };
    cfg.GlyphRanges = glyph_ranges;

    ImFont* font = io.Fonts->AddFontFromFileTTF(path.c_str(), size_px, &cfg);
    if (!font) {
        io.Fonts->Clear();
        io.Fonts->AddFontDefault();
    }

    io.Fonts->Build();
    if (upload_cb_) upload_cb_();   // platform-provided texture upload

    active_font_ = font;
    vendor_dir_  = vendor_dir;
    return font != nullptr;
}

// ------------------------------------------------------------
//  Public API
// ------------------------------------------------------------
bool FontManager::load(const Prefs& prefs, const std::string& vendor_dir) {
    vendor_dir_ = vendor_dir;
    return load_internal(prefs, vendor_dir, /*destroy_first=*/false);
}

bool FontManager::rebuild(const Prefs& prefs, const std::string& vendor_dir) {
    rebuild_pending = false;
    return load_internal(prefs, vendor_dir, /*destroy_first=*/true);
}

void FontManager::push() const {
    if (active_font_) ImGui::PushFont(active_font_);
}

void FontManager::pop() const {
    if (active_font_) ImGui::PopFont();
}
