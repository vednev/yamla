#pragma once

#include <functional>
#include <string>
#include <vector>
#include "../core/prefs.hpp"

// ------------------------------------------------------------
//  PrefsView
//
//  A modal-style floating window for font + size preferences.
//  Changes are only applied (and persisted) when the user
//  clicks "Apply".
// ------------------------------------------------------------
class PrefsView {
public:
    using ChangedCb = std::function<void(const Prefs&)>;

    PrefsView() = default;

    // Must be called before render().
    void set_prefs(Prefs* prefs);
    void set_available_fonts(const std::vector<std::string>* fonts);
    void set_on_changed(ChangedCb cb);

    void show();
    bool is_open() const { return open_; }

    // Call once per frame.
    void render();

private:
    Prefs*                        prefs_     = nullptr;
    const std::vector<std::string>* fonts_   = nullptr;
    ChangedCb                     on_changed_;
    bool                          open_      = false;

    // Staging area — edits held here until Apply is clicked
    Prefs staging_;
};
