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

    // Auto-fit: let ImGui compute the needed height each frame rather than
    // using a fixed height that clips content when padding/font size changes.
    ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(
        ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f - 190.0f,
               ImGui::GetIO().DisplaySize.y * 0.5f - 140.0f),
        ImGuiCond_FirstUseEver);

    // NoResize removed so the user can adjust if needed; height=0 means auto
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_AlwaysAutoResize;

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

    // ---- Memory limit ------------------------------------------
    ImGui::Spacing();
    ImGui::Text("Memory");
    ImGui::SameLine(80);
    ImGui::SetNextItemWidth(72);
    ImGui::InputInt("##mem_gb", &staging_.memory_limit_gb, 0, 0);
    staging_.memory_limit_gb = std::max(0, std::min(512, staging_.memory_limit_gb));
    ImGui::SameLine(0, 8);
    if (staging_.memory_limit_gb == 0)
        ImGui::TextDisabled("auto (60%% of RAM)");
    else
        ImGui::TextDisabled("GB limit (0 = auto)");

    // ---- Filter display preference ----------------------------
    ImGui::Spacing();
    ImGui::Text("Filters");
    ImGui::SameLine(80);
    ImGui::Checkbox("Prefer checkboxes over graphs", &staging_.prefer_checkboxes);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Show Severity and Op Type as checkbox lists\n"
                          "instead of bar charts");
    ImGui::Checkbox("Enable dedup (O(N^2), reload required)", &staging_.dedup_enabled);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Deduplicates identical log entries across files.\n"
            "Off by default for performance. Takes effect on next file load.");
    }

    // ---- LLM configuration ------------------------------------
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.8f, 1.0f, 1.0f));
    ImGui::Text("AI Assistant");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    ImGui::Text("API Key");
    ImGui::SameLine(80);
    ImGui::SetNextItemWidth(220);
    // Use password-style input for the API key
    static char key_buf[256] = {};
    if (ImGui::IsWindowAppearing()) {
        std::strncpy(key_buf, staging_.llm_api_key.c_str(), sizeof(key_buf) - 1);
        key_buf[sizeof(key_buf) - 1] = '\0';
    }
    if (ImGui::InputText("##llm_key", key_buf, sizeof(key_buf),
                         ImGuiInputTextFlags_Password))
        staging_.llm_api_key = key_buf;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Azure Foundry API key for the AI assistant");

    ImGui::Spacing();
    ImGui::Text("Endpoint");
    ImGui::SameLine(80);
    ImGui::SetNextItemWidth(220);
    static char endpoint_buf[256] = {};
    if (ImGui::IsWindowAppearing()) {
        std::strncpy(endpoint_buf, staging_.llm_endpoint.c_str(),
                     sizeof(endpoint_buf) - 1);
        endpoint_buf[sizeof(endpoint_buf) - 1] = '\0';
    }
    if (ImGui::InputText("##llm_endpoint", endpoint_buf, sizeof(endpoint_buf)))
        staging_.llm_endpoint = endpoint_buf;

    ImGui::Spacing();
    ImGui::Text("Model");
    ImGui::SameLine(80);
    ImGui::SetNextItemWidth(220);
    static char model_buf[128] = {};
    if (ImGui::IsWindowAppearing()) {
        std::strncpy(model_buf, staging_.llm_model.c_str(),
                     sizeof(model_buf) - 1);
        model_buf[sizeof(model_buf) - 1] = '\0';
    }
    if (ImGui::InputText("##llm_model", model_buf, sizeof(model_buf)))
        staging_.llm_model = model_buf;

    ImGui::Spacing();
    ImGui::Text("Max Tokens");
    ImGui::SameLine(80);
    ImGui::SetNextItemWidth(100);
    ImGui::InputInt("##llm_maxtok", &staging_.llm_max_tokens, 256, 1024);
    staging_.llm_max_tokens = std::max(256, std::min(32768, staging_.llm_max_tokens));

    ImGui::Spacing();
    ImGui::Text("Export Dir");
    ImGui::SameLine(80);
    ImGui::SetNextItemWidth(220);
    static char export_buf[512] = {};
    if (ImGui::IsWindowAppearing()) {
        std::strncpy(export_buf, staging_.export_dir.c_str(),
                     sizeof(export_buf) - 1);
        export_buf[sizeof(export_buf) - 1] = '\0';
    }
    if (ImGui::InputText("##export_dir", export_buf, sizeof(export_buf)))
        staging_.export_dir = export_buf;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Directory for exported AI responses.\n"
                          "Leave empty to disable export.");

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
