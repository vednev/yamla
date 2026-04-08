#pragma once

#include <string>
#include <vector>
#include "../core/prefs.hpp"

// Forward declarations — avoid pulling imgui.h into every header
struct ImFont;

// ------------------------------------------------------------
//  FontManager
//
//  Manages the ImGui font atlas.  Call load() once at startup
//  and again (after marking rebuild_pending = true) whenever
//  the user changes font preferences.
//
//  The rebuild must happen OUTSIDE an ImGui frame — the App
//  checks rebuild_pending at the top of the frame loop before
//  calling NewFrame.
//
//  Usage pattern in the frame loop:
//      if (font_mgr_.rebuild_pending) font_mgr_.rebuild();
//      ImGui_ImplOpenGL3_NewFrame();
//      ImGui_ImplSDL2_NewFrame();
//      ImGui::NewFrame();
//      font_mgr_.push();
//      ... render ...
//      font_mgr_.pop();
//      ImGui::Render();
// ------------------------------------------------------------
class FontManager {
public:
    FontManager() = default;

    // Load fonts from vendor_dir for the given prefs.
    // Must be called after ImGui::CreateContext() but before the first frame.
    bool load(const Prefs& prefs, const std::string& vendor_dir);

    // Rebuild the atlas mid-session (outside a frame).
    // Destroys old GL texture, rebuilds atlas, uploads new texture.
    bool rebuild(const Prefs& prefs, const std::string& vendor_dir);

    // Push the active font for the current frame.
    void push() const;
    void pop()  const;

    // Set to true by PrefsView when the user clicks Apply.
    // Checked and cleared by App at the top of the frame loop.
    bool rebuild_pending = false;

    // Font names available in this installation (from vendor_dir).
    const std::vector<std::string>& available_fonts() const { return available_; }

    // The vendor fonts directory path (stored so rebuild() can use it)
    const std::string& vendor_dir() const { return vendor_dir_; }

private:
    bool load_internal(const Prefs& prefs, const std::string& vendor_dir,
                       bool destroy_first);

    ImFont*                  active_font_ = nullptr;
    std::vector<std::string> available_;
    std::string              vendor_dir_;

    // Map display name → filename stem
    static const char* font_filename(const std::string& name);
};
