#pragma once

#include <string>
#include <vector>
#include <functional>
#include "../core/prefs.hpp"

struct ImFont;

// ------------------------------------------------------------
//  FontManager
//
//  Manages the ImGui font atlas.  Call load() once at startup
//  and again (after marking rebuild_pending = true) whenever
//  the user changes font preferences.
//
//  The backend-specific texture upload and destroy operations
//  are provided as callbacks so FontManager has no dependency
//  on any particular rendering backend (OpenGL, Metal, DX11…).
//
//  App::init() provides the appropriate lambdas:
//    macOS  — ImGui_ImplSDLRenderer2_CreateFontsTexture / Destroy
//    Linux  — ImGui_ImplOpenGL3_CreateFontsTexture / Destroy
//    Windows — ImGui_ImplDX11_CreateFontsTexture / Destroy
// ------------------------------------------------------------
class FontManager {
public:
    using UploadCb  = std::function<void()>;
    using DestroyCb = std::function<void()>;

    FontManager() = default;

    // Must be called before load(). Provides platform-appropriate
    // font texture upload and destroy operations.
    void set_callbacks(UploadCb upload, DestroyCb destroy);

    // Load fonts from vendor_dir for the given prefs.
    // Must be called after ImGui::CreateContext() and after
    // set_callbacks(), but before the first frame.
    bool load(const Prefs& prefs, const std::string& vendor_dir);

    // Rebuild the atlas mid-session (outside a frame).
    // Destroys old texture, rebuilds atlas, uploads new texture.
    bool rebuild(const Prefs& prefs, const std::string& vendor_dir);

    // Push the active font for the current frame.
    void push() const;
    void pop()  const;

    bool rebuild_pending = false;

    const std::vector<std::string>& available_fonts() const { return available_; }
    const std::string& vendor_dir() const { return vendor_dir_; }

private:
    bool load_internal(const Prefs& prefs, const std::string& vendor_dir,
                       bool destroy_first);

    static const char* font_filename(const std::string& name);

    ImFont*                  active_font_ = nullptr;
    std::vector<std::string> available_;
    std::string              vendor_dir_;

    UploadCb  upload_cb_;
    DestroyCb destroy_cb_;
};
