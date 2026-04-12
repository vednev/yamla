#include "ftdc_view.hpp"

#include <imgui.h>
#include <cstdio>
#include <algorithm>

#include "../ftdc/ftdc_analyzer.hpp"
#include "log_view.hpp"  // FilterState

// ============================================================
//  FtdcView
// ============================================================

FtdcView::~FtdcView() {
    if (load_thread_.joinable()) load_thread_.join();
}

void FtdcView::set_filter(FilterState* filter) {
    filter_ = filter;
    chart_panel_.set_filter(filter);
}

void FtdcView::set_log_data(const std::vector<const LogEntry*>* entries,
                             const StringTable*                  strings) {
    log_entries_ = entries;
    log_strings_ = strings;
    chart_panel_.set_log_data(entries, strings);
}

// ============================================================
//  start_load
// ============================================================
void FtdcView::start_load(const std::string& path) {
    if (load_thread_.joinable()) load_thread_.join();

    // Reset state
    tree_view_.clear_selection();
    chart_panel_.set_store(nullptr);
    chart_panel_.set_selected_metrics(nullptr);
    last_state_ = FtdcLoadState::Idle;

    cluster_ = std::make_unique<FtdcCluster>();
    cluster_->set_path(path);
    load_thread_ = std::thread([this] { cluster_->load(); });
}

// ============================================================
//  on_selection_changed
// ============================================================
void FtdcView::on_selection_changed() {
    chart_panel_.set_selected_metrics(&tree_view_.selected());
    chart_panel_.set_dashboard_groups(&tree_view_.active_dashboards());
    chart_panel_.set_custom_metrics(&tree_view_.custom_metrics());
}

// ============================================================
//  render_loading_popup
// ============================================================
void FtdcView::render_loading_popup() {
    if (!cluster_ || cluster_->state() != FtdcLoadState::Loading) return;

    float progress = cluster_->progress();

    ImGuiIO& io = ImGui::GetIO();
    float pw = 420.0f, ph = 90.0f;
    ImGui::SetNextWindowPos(
        ImVec2((io.DisplaySize.x - pw) * 0.5f,
               (io.DisplaySize.y - ph) * 0.5f),
        ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(pw, ph), ImGuiCond_Always);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar   | ImGuiWindowFlags_NoResize   |
        ImGuiWindowFlags_NoMove       | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings;

    ImGui::Begin("##ftdc_loading", nullptr, flags);

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);
    ImGui::SetCursorPosX((pw - ImGui::CalcTextSize("Loading FTDC...").x) * 0.5f);
    ImGui::TextUnformatted("Loading FTDC...");
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(1.0f, 0.95f, 0.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,       ImVec4(0.15f, 0.15f, 0.0f, 1.0f));

    float bar_x = 12.0f;
    float bar_w = pw - 24.0f;
    float bar_h = 22.0f;
    ImGui::SetCursorPosX(bar_x);
    ImVec2 bar_tl = ImGui::GetCursorScreenPos();
    ImGui::ProgressBar(progress, ImVec2(bar_w, bar_h), "");
    ImGui::PopStyleColor(2);

    // Percentage text split at fill edge
    char pct[16];
    std::snprintf(pct, sizeof(pct), "%.0f%%", progress * 100.0f);
    ImDrawList* dl  = ImGui::GetWindowDrawList();
    ImFont*     fnt = ImGui::GetFont();
    float       fz  = ImGui::GetFontSize();
    float text_w    = fnt->CalcTextSizeA(fz, FLT_MAX, 0.0f, pct).x;
    float text_x    = bar_tl.x + (bar_w - text_w) * 0.5f;
    float text_y    = bar_tl.y + (bar_h - fz) * 0.5f;
    float fill_end  = bar_tl.x + bar_w * progress;
    const char* p   = pct;
    float cx        = text_x;
    while (*p) {
        unsigned int cp = (unsigned char)*p;
        const ImFontGlyph* glyph = fnt->FindGlyph((ImWchar)cp);
        float adv = glyph ? glyph->AdvanceX * (fz / fnt->FontSize) : fz * 0.5f;
        ImU32 col = (cx + adv * 0.5f <= fill_end)
            ? IM_COL32(0, 0, 0, 255) : IM_COL32(255, 255, 255, 255);
        dl->AddText(fnt, fz, ImVec2(cx, text_y), col, p, p + 1);
        cx += adv;
        ++p;
    }

    ImGui::End();
}

// ============================================================
//  render
// ============================================================
void FtdcView::poll_state() {
    // Must be called every frame (even when FTDC tab is not visible)
    // to catch the Loading→Ready transition.
    if (!cluster_) return;
    FtdcLoadState cur = cluster_->state();
    if (cur != last_state_ && cur == FtdcLoadState::Ready) {
        FtdcAnalyzer::compute_all_rates(cluster_->store());
        tree_view_.set_store(&cluster_->store());
        tree_view_.set_on_selection_changed([this] { on_selection_changed(); });
        chart_panel_.set_store(&cluster_->store());
        chart_panel_.set_selected_metrics(&tree_view_.selected());
        chart_panel_.set_dashboard_groups(&tree_view_.active_dashboards());
        chart_panel_.set_custom_metrics(&tree_view_.custom_metrics());
        chart_panel_.set_filter(filter_);
        chart_panel_.set_log_data(log_entries_, log_strings_);
        on_selection_changed();
    }
    last_state_ = cur;
}

void FtdcView::render(float total_h) {
    // Before data is loaded, show a centered prompt instead of the two-column layout
    bool has_data = cluster_ && cluster_->state() == FtdcLoadState::Ready;
    if (!has_data) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        const char* msg = "Drop a diagnostic.data directory here";
        ImVec2 tsz = ImGui::CalcTextSize(msg);
        ImGui::SetCursorPos(ImVec2(
            (avail.x - tsz.x) * 0.5f + ImGui::GetCursorPosX(),
            (avail.y - tsz.y) * 0.5f + ImGui::GetCursorPosY()));
        ImGui::TextDisabled("%s", msg);
        return;
    }

    float vsplitter_w = 6.0f;
    float chart_w     = ImGui::GetContentRegionAvail().x - left_w_ - vsplitter_w;
    chart_w           = std::max(chart_w, 200.0f);

    // Left: metric tree
    ImGui::BeginChild("##ftdc_left", ImVec2(left_w_, total_h), true);
    tree_view_.render_inner();
    ImGui::EndChild();

    // Splitter (uses member left_w_ directly)
    ImGui::SameLine(0, 0);
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.20f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1, 1, 1, 0.40f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::Button("##ftdc_split", ImVec2(vsplitter_w, total_h));
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActive()) {
        float delta = ImGui::GetIO().MouseDelta.x;
        left_w_ += delta;
        left_w_ = std::max(180.0f, std::min(500.0f, left_w_));
    }

    // Right: chart panel
    ImGui::SameLine(0, 0);
    ImGui::BeginChild("##ftdc_charts", ImVec2(chart_w, total_h), true);
    chart_panel_.render_inner();
    ImGui::EndChild();
}
