#include "font_manager.hpp"

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

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

// ------------------------------------------------------------
//  Probe vendor_dir to build available font list
// ------------------------------------------------------------
static bool file_exists(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0;
}

// ------------------------------------------------------------
//  load_internal — shared by load() and rebuild()
// ------------------------------------------------------------
bool FontManager::load_internal(const Prefs& prefs,
                                 const std::string& vendor_dir,
                                 bool destroy_first)
{
    ImGuiIO& io = ImGui::GetIO();

    if (destroy_first) {
        ImGui_ImplOpenGL3_DestroyFontsTexture();
        io.Fonts->Clear();
    }

    // Discover available fonts from disk
    available_.clear();
    static const char* known[] = {
        "Inter", "IBM Plex Sans", "Fira Code", "JetBrains Mono"
    };
    for (const char* n : known) {
        const char* fn = font_filename(n);
        if (!fn) continue;
        std::string full = vendor_dir + "/" + fn;
        if (file_exists(full))
            available_.push_back(n);
    }

    // Fall back to ImGui default if no fonts found
    if (available_.empty()) {
        io.Fonts->AddFontDefault();
        io.Fonts->Build();
        ImGui_ImplOpenGL3_CreateFontsTexture();
        active_font_ = nullptr;
        return false;
    }

    // Find the requested font; fall back to first available
    std::string target = prefs.font_name;
    bool found = false;
    for (const auto& a : available_) {
        if (a == target) { found = true; break; }
    }
    if (!found) target = available_[0];

    float size_px = static_cast<float>(prefs.font_size);

    // Build path
    const char* fn = font_filename(target);
    std::string path = vendor_dir + "/" + fn;

    // ImGui wants size in pixels; we treat the pref value directly as pt
    // (at 96 DPI, 1pt ≈ 1.33px — but for UI purposes pt == px is fine here)
    ImFontConfig cfg;
    cfg.OversampleH = 3;
    cfg.OversampleV = 2;
    cfg.PixelSnapH  = false;

    // Glyph ranges: each entry is a (first, last) inclusive pair.
    // The array MUST be terminated with a 0,0 pair — a single 0 is not enough.
    static const ImWchar glyph_ranges[] = {
        0x0020, 0x00FF,  // Basic Latin + Latin-1 Supplement
        0x2013, 0x2026,  // en-dash, em-dash, horizontal ellipsis
        0x2018, 0x201F,  // single/double typographic quotes
        0, 0,            // terminator pair
    };
    cfg.GlyphRanges = glyph_ranges;

    ImFont* font = io.Fonts->AddFontFromFileTTF(path.c_str(), size_px, &cfg);
    if (!font) {
        // Loading failed — fall back to ImGui's embedded default
        io.Fonts->Clear();
        io.Fonts->AddFontDefault();
    }

    io.Fonts->Build();
    // Always upload to the GPU — on the initial load path the backend
    // has not yet uploaded any font texture, so this is required.
    // On rebuild the old texture was already destroyed above.
    ImGui_ImplOpenGL3_CreateFontsTexture();

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
    if (active_font_)
        ImGui::PushFont(active_font_);
}

void FontManager::pop() const {
    if (active_font_)
        ImGui::PopFont();
}
