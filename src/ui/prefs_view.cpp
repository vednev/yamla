#include "prefs_view.hpp"

#include <imgui.h>
#include <cstring>
#include <algorithm>

// ------------------------------------------------------------

void PrefsView::set_prefs(Prefs* prefs) {
    prefs_ = prefs;
}

void PrefsView::set_available_fonts(const std::vector<std::string>* fonts) {
    fonts_ = fonts;
}

void PrefsView::set_on_changed(ChangedCb cb) {
    on_changed_ = std::move(cb);
}

void PrefsView::show() {
    open_    = true;
    // Copy current prefs into staging so the user can edit without
    // immediately affecting the live font
    if (prefs_) staging_ = *prefs_;
}

// ------------------------------------------------------------
//  render
// ------------------------------------------------------------
void PrefsView::render() {
    if (!open_) return;

    ImGui::SetNextWindowSize(ImVec2(360, 210), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(
        ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f - 180.0f,
               ImGui::GetIO().DisplaySize.y * 0.5f - 105.0f),
        ImGuiCond_FirstUseEver);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

    if (!ImGui::Begin("Preferences", &open_, flags)) {
        ImGui::End();
        return;
    }

    // ---- Font family -------------------------------------------
    ImGui::Text("Font");
    ImGui::SameLine(80);
    ImGui::SetNextItemWidth(220);

    int current_idx = 0;
    if (fonts_ && !fonts_->empty()) {
        for (int i = 0; i < (int)fonts_->size(); ++i) {
            if ((*fonts_)[i] == staging_.font_name) {
                current_idx = i;
                break;
            }
        }

        if (ImGui::BeginCombo("##font_combo", staging_.font_name.c_str())) {
            for (int i = 0; i < (int)fonts_->size(); ++i) {
                bool selected = (i == current_idx);
                if (ImGui::Selectable((*fonts_)[i].c_str(), selected))
                    staging_.font_name = (*fonts_)[i];
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    } else {
        ImGui::TextDisabled("(no fonts found in vendor/fonts/)");
    }

    // ---- Font size ---------------------------------------------
    ImGui::Spacing();
    ImGui::Text("Size");
    ImGui::SameLine(80);

    // Decrement button
    if (ImGui::Button(" - ")) {
        if (staging_.font_size > 10) --staging_.font_size;
    }
    ImGui::SameLine(0, 4);

    // Size display / direct edit
    ImGui::SetNextItemWidth(48);
    ImGui::InputInt("##fsize", &staging_.font_size, 0, 0);
    staging_.font_size = std::max(10, std::min(20, staging_.font_size));
    ImGui::SameLine(0, 4);

    if (ImGui::Button(" + ")) {
        if (staging_.font_size < 20) ++staging_.font_size;
    }
    ImGui::SameLine(0, 8);
    ImGui::TextDisabled("(10–20 pt)");

    // ---- Preview line ------------------------------------------
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("Preview (active font):");
    ImGui::Text("The quick brown fox — 0123456789");
    ImGui::Text("COMMAND  |  find  |  1,234,567 entries");

    // ---- Buttons -----------------------------------------------
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    float btn_w = 80.0f;
    float total  = btn_w * 2 + 8.0f;
    ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - total) * 0.5f +
                          ImGui::GetCursorPosX());

    if (ImGui::Button("Apply", ImVec2(btn_w, 0))) {
        if (prefs_) *prefs_ = staging_;
        if (on_changed_) on_changed_(staging_);
        // Don't close — user may want to keep tweaking
    }
    ImGui::SameLine(0, 8);
    if (ImGui::Button("Close", ImVec2(btn_w, 0))) {
        open_ = false;
    }

    ImGui::End();
}
