#include "app.hpp"

#include <SDL.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <stdexcept>

#include "../core/prefs.hpp"

// ---- Arena sizing ------------------------------------------
// Default: 2 GB slab. For files larger than ~1.3 GB combined
// the user should be prompted (future work). Arena exhaustion
// will assert loudly.
static constexpr size_t DEFAULT_ARENA_BYTES = 2ull * 1024 * 1024 * 1024;

// ------------------------------------------------------------
//  Constructor / Destructor
// ------------------------------------------------------------

App::App() {
    log_view_.set_filter(&filter_);
    log_view_.set_on_select([this](size_t idx) {
        if (!cluster_ || cluster_->state() != LoadState::Ready) return;
        const LogEntry& e = cluster_->entries()[idx];
        if (e.node_idx < node_files_.size() && node_files_[e.node_idx]) {
            detail_view_.set_entry(&e,
                                   node_files_[e.node_idx]->data(),
                                   &cluster_->strings());
        }
    });

    breakdown_view_.set_filter(&filter_);
    breakdown_view_.set_on_filter_changed([this] { on_filter_changed(); });

    filter_view_.set_filter(&filter_);
    filter_view_.set_on_filter_changed([this] { on_filter_changed(); });

    prefs_view_.set_prefs(&prefs_);
    prefs_view_.set_on_changed([this](const Prefs& p) {
        prefs_ = p;
        PrefsManager::save(p);
        font_mgr_.rebuild_pending = true;
    });
}

App::~App() {
    if (load_thread_.joinable()) load_thread_.join();
    shutdown();
}

// ------------------------------------------------------------
//  init
// ------------------------------------------------------------
bool App::init() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    SDL_WindowFlags flags = static_cast<SDL_WindowFlags>(
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    window_ = SDL_CreateWindow("YAMLA — MongoDB Log Analyzer",
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                1024, 768, flags);
    if (!window_) {
        std::fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError());
        return false;
    }

    gl_ctx_ = SDL_GL_CreateContext(window_);
    if (!gl_ctx_) {
        std::fprintf(stderr, "SDL_GL_CreateContext error: %s\n", SDL_GetError());
        return false;
    }
    SDL_GL_MakeCurrent(window_, gl_ctx_);
    SDL_GL_SetSwapInterval(1); // vsync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // ----------------------------------------------------------------
    //  CLI / terminal theme
    //  Black background, white text, white borders.
    //  Hover = inverted (white fill, black text).
    //  Zero rounding everywhere — sharp, crisp, old-school.
    // ----------------------------------------------------------------
    ImGui::StyleColorsDark();   // base reset
    ImGuiStyle& style = ImGui::GetStyle();

    // --- Geometry ---------------------------------------------------
    style.WindowRounding    = 0.0f;
    style.ChildRounding     = 0.0f;
    style.FrameRounding     = 0.0f;
    style.GrabRounding      = 0.0f;
    style.PopupRounding     = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.TabRounding       = 0.0f;

    // Border thickness: 1px crisp line on every frame/window
    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.FrameBorderSize   = 1.0f;   // buttons, inputs, combos get a border
    style.PopupBorderSize   = 1.0f;
    style.TabBorderSize     = 1.0f;

    // Generous padding for a clean, breathable layout
    style.FramePadding      = ImVec2(8, 5);
    style.WindowPadding     = ImVec2(10, 8);
    style.ItemSpacing       = ImVec2(8, 6);
    style.ItemInnerSpacing  = ImVec2(6, 4);
    style.CellPadding       = ImVec2(6, 4);
    style.IndentSpacing     = 16.0f;

    // --- Palette convenience ----------------------------------------
    const ImVec4 black      = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    const ImVec4 white      = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    const ImVec4 dim        = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);  // disabled/inactive
    const ImVec4 dark_gray  = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);  // child backgrounds
    const ImVec4 mid_gray   = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);  // active/pressed state
    const ImVec4 clear      = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    // --- Backgrounds ------------------------------------------------
    style.Colors[ImGuiCol_WindowBg]          = black;
    style.Colors[ImGuiCol_ChildBg]           = dark_gray;
    style.Colors[ImGuiCol_PopupBg]           = black;
    style.Colors[ImGuiCol_MenuBarBg]         = black;

    // --- Borders — medium gray, reduces visual noise vs pure white ---
    const ImVec4 border_gray = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
    style.Colors[ImGuiCol_Border]            = border_gray;
    style.Colors[ImGuiCol_BorderShadow]      = clear;

    // --- Text -------------------------------------------------------
    style.Colors[ImGuiCol_Text]              = white;
    style.Colors[ImGuiCol_TextDisabled]      = dim;
    style.Colors[ImGuiCol_TextSelectedBg]    = ImVec4(1.0f, 1.0f, 1.0f, 0.25f);

    // --- Frames (inputs, combos, sliders) ---------------------------
    style.Colors[ImGuiCol_FrameBg]           = black;
    style.Colors[ImGuiCol_FrameBgHovered]    = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    style.Colors[ImGuiCol_FrameBgActive]     = mid_gray;

    // --- Buttons ----------------------------------------------------
    // Bright-but-not-pure-white so white text label stays readable.
    style.Colors[ImGuiCol_Button]            = black;
    style.Colors[ImGuiCol_ButtonHovered]     = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    style.Colors[ImGuiCol_ButtonActive]      = mid_gray;

    // --- Headers (collapsing headers, table headers, selectables) ---
    // CollapsingHeader uses Text colour for its label — can't be inverted
    // per-state without reimplementing the widget, so use a bright but
    // slightly-off-white hover so white text stays readable over it.
    style.Colors[ImGuiCol_Header]            = black;
    style.Colors[ImGuiCol_HeaderHovered]     = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    style.Colors[ImGuiCol_HeaderActive]      = mid_gray;

    // --- Tabs -------------------------------------------------------
    style.Colors[ImGuiCol_Tab]               = black;
    style.Colors[ImGuiCol_TabHovered]        = white;
    style.Colors[ImGuiCol_TabActive]         = mid_gray;
    style.Colors[ImGuiCol_TabUnfocused]      = black;
    style.Colors[ImGuiCol_TabUnfocusedActive]= mid_gray;

    // --- Title bar --------------------------------------------------
    style.Colors[ImGuiCol_TitleBg]           = black;
    style.Colors[ImGuiCol_TitleBgActive]     = black;
    style.Colors[ImGuiCol_TitleBgCollapsed]  = black;

    // --- Scrollbar --------------------------------------------------
    style.Colors[ImGuiCol_ScrollbarBg]       = black;
    style.Colors[ImGuiCol_ScrollbarGrab]     = dim;
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = white;
    style.Colors[ImGuiCol_ScrollbarGrabActive]  = white;

    // --- Sliders / grabs --------------------------------------------
    style.Colors[ImGuiCol_SliderGrab]        = white;
    style.Colors[ImGuiCol_SliderGrabActive]  = white;

    // --- Check mark, radio ------------------------------------------
    style.Colors[ImGuiCol_CheckMark]         = white;

    // --- Resize grip ------------------------------------------------
    style.Colors[ImGuiCol_ResizeGrip]        = clear;
    style.Colors[ImGuiCol_ResizeGripHovered] = white;
    style.Colors[ImGuiCol_ResizeGripActive]  = white;

    // --- Separators — match border gray -----------------------------
    style.Colors[ImGuiCol_Separator]         = border_gray;
    style.Colors[ImGuiCol_SeparatorHovered]  = ImVec4(0.55f, 0.55f, 0.55f, 1.00f);
    style.Colors[ImGuiCol_SeparatorActive]   = ImVec4(0.70f, 0.70f, 0.70f, 1.00f);

    // --- Table — gray borders match child border gray ---------------
    style.Colors[ImGuiCol_TableHeaderBg]         = black;
    style.Colors[ImGuiCol_TableBorderStrong]      = border_gray;
    style.Colors[ImGuiCol_TableBorderLight]       = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    style.Colors[ImGuiCol_TableRowBg]             = black;
    style.Colors[ImGuiCol_TableRowBgAlt]          = dark_gray;

    // --- Nav highlight (keyboard focus) ----------------------------
    style.Colors[ImGuiCol_NavHighlight]      = white;

    // --- Plot colours (ImPlot picks these up via the ImGui palette) -
    // ImPlot has its own palette but we set the plot background here
    style.Colors[ImGuiCol_PlotLines]         = white;
    style.Colors[ImGuiCol_PlotLinesHovered]  = white;
    style.Colors[ImGuiCol_PlotHistogram]     = white;
    style.Colors[ImGuiCol_PlotHistogramHovered] = white;

    // ImGui pushes ImGuiCol_Text as the "active" text colour when a
    // button/header is hovered (white fill). We need to override text
    // to black on those inverted elements at render time — see below.
    // We store the inverted text colour in a custom variable so
    // render sites can push it around hovered selectables.
    // (ImGui has no per-state text colour override, so we use a push
    //  in the few places that need it — table rows, collapsing headers.)

    ImGui_ImplSDL2_InitForOpenGL(window_, gl_ctx_);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    // Load prefs and fonts
    prefs_ = PrefsManager::load();
    // vendor/fonts/ is relative to the working directory (project root)
    std::string vendor_dir = "vendor/fonts";
    font_mgr_.load(prefs_, vendor_dir);
    prefs_view_.set_available_fonts(&font_mgr_.available_fonts());

    return true;
}

// ------------------------------------------------------------
//  shutdown
// ------------------------------------------------------------
void App::shutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    if (ImPlot::GetCurrentContext())  ImPlot::DestroyContext();
    if (ImGui::GetCurrentContext())   ImGui::DestroyContext();
    if (gl_ctx_)  SDL_GL_DeleteContext(gl_ctx_);
    if (window_)  SDL_DestroyWindow(window_);
    SDL_Quit();
}

// ------------------------------------------------------------
//  handle_drop
// ------------------------------------------------------------
void App::handle_drop(const std::vector<std::string>& paths) {
    if (paths.empty()) return;
    if (load_thread_.joinable()) load_thread_.join(); // wait for previous load
    start_load(paths);
}

void App::start_load(const std::vector<std::string>& paths) {
    // Reset state
    filter_.clear();
    detail_view_.set_entry(nullptr, nullptr, nullptr);
    filter_view_.set_analysis(nullptr, nullptr); // clear stale data
    node_files_.clear();
    total_file_bytes_ = 0;
    load_duration_s_  = 0.0;
    load_start_       = std::chrono::steady_clock::now();

    // Compute total file size to size the arena
    size_t total_bytes = 0;
    for (const auto& p : paths) {
        try {
            MmapFile f(p);
            total_bytes += f.size();
            node_files_.push_back(std::make_unique<MmapFile>(std::move(f)));
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "Cannot open %s: %s\n", p.c_str(), ex.what());
        }
    }
    total_file_bytes_ = total_bytes;

    // Arena: 1.5× file size, at least 256 MB, at most DEFAULT_ARENA_BYTES
    size_t arena_bytes = std::max<size_t>(
        256ull * 1024 * 1024,
        std::min<size_t>(DEFAULT_ARENA_BYTES,
                         static_cast<size_t>(total_bytes * 1.5)));

    cluster_ = std::make_unique<Cluster>(arena_bytes);
    for (const auto& p : paths)
        cluster_->add_file(p);

    load_thread_ = std::thread([this] { cluster_->load(); });
}

// ------------------------------------------------------------
//  on_filter_changed
// ------------------------------------------------------------
void App::on_filter_changed() {
    log_view_.rebuild_filter_index();
}

// ------------------------------------------------------------
//  render_menu_bar
// ------------------------------------------------------------
void App::render_menu_bar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open Cluster (drag & drop files)")) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Alt+F4")) running_ = false;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Preferences...")) prefs_view_.show();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            ImGui::MenuItem("Drop one or more MongoDB log files onto this window");
            ImGui::EndMenu();
        }

        // Show load status — right-aligned, gray tone
        {
            const ImVec4 stat_color = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
            char stat_buf[128] = {};

            if (cluster_) {
                switch (cluster_->state()) {
                    case LoadState::Loading:
                        std::snprintf(stat_buf, sizeof(stat_buf), "Loading...");
                        break;
                    case LoadState::Ready: {
                        char size_buf[32];
                        if (total_file_bytes_ >= 1024ull * 1024 * 1024)
                            std::snprintf(size_buf, sizeof(size_buf), "%.2f GB",
                                static_cast<double>(total_file_bytes_) / (1024.0*1024.0*1024.0));
                        else
                            std::snprintf(size_buf, sizeof(size_buf), "%.1f MB",
                                static_cast<double>(total_file_bytes_) / (1024.0*1024.0));
                        std::snprintf(stat_buf, sizeof(stat_buf),
                            "%zu entries  |  %s  |  %.2fs",
                            cluster_->entries().size(), size_buf, load_duration_s_);
                        break;
                    }
                    case LoadState::Error:
                        std::snprintf(stat_buf, sizeof(stat_buf),
                            "Error: %s", cluster_->error().c_str());
                        break;
                    default:
                        std::snprintf(stat_buf, sizeof(stat_buf),
                            "Drop MongoDB log files here to begin");
                        break;
                }
            } else {
                std::snprintf(stat_buf, sizeof(stat_buf),
                    "Drop MongoDB log files here to begin");
            }

            float text_w = ImGui::CalcTextSize(stat_buf).x;
            float margin = 8.0f;
            ImGui::SetCursorPosX(ImGui::GetIO().DisplaySize.x - text_w - margin);
            ImGui::PushStyleColor(ImGuiCol_Text, stat_color);
            ImGui::TextUnformatted(stat_buf);
            ImGui::PopStyleColor();
        }

        ImGui::EndMainMenuBar();
    }
}

// ------------------------------------------------------------
//  render_dockspace — manual full-screen host window
// ------------------------------------------------------------
void App::render_dockspace() {
    // No actual docking — use a simple full-screen invisible host
    // so child windows have a parent to anchor to.
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);

    ImGuiWindowFlags host_flags =
        ImGuiWindowFlags_NoTitleBar    | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize      | ImGuiWindowFlags_NoMove     |
        ImGuiWindowFlags_NoScrollbar   | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::Begin("##host", nullptr, host_flags);
    ImGui::PopStyleVar(2);

    // Three-column layout: Breakdowns | Log View | Detail View
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float left_w = 280.0f;
    float h      = avail.y;

    // Left column: breakdowns (top 60%) + filter panel (bottom 40%)
    ImGui::BeginChild("##left_col", ImVec2(left_w, h), false,
                      ImGuiWindowFlags_NoScrollbar);
    {
        bool has_data = cluster_ && cluster_->state() == LoadState::Ready;
        float breakdown_h = h * 0.60f;
        float filter_h    = h - breakdown_h - 4.0f;

        ImGui::BeginChild("##breakdowns", ImVec2(-1, breakdown_h), true);
        if (has_data) breakdown_view_.render();
        ImGui::EndChild();

        ImGui::BeginChild("##filterpanel", ImVec2(-1, filter_h), true);
        if (has_data) filter_view_.render_inner();
        else          ImGui::TextDisabled("Load a cluster to see filters.");
        ImGui::EndChild();
    }
    ImGui::EndChild();

    ImGui::SameLine(0, 4);

    // Centre: log view — takes all space left after the right panel
    float splitter_w = 6.0f;
    float center_w_actual = avail.x - left_w - right_w_ - splitter_w - 8.0f;
    center_w_actual = std::max(center_w_actual, 120.0f);

    ImGui::BeginChild("##logview", ImVec2(center_w_actual, h), true);
    log_view_.render_inner();
    ImGui::EndChild();

    ImGui::SameLine(0, 0);

    // ---- Splitter between log view and detail panel ----
    // Transparent normally; bright white when dragging so the user can see it.
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.20f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1, 1, 1, 0.40f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::Button("##splitter", ImVec2(splitter_w, h));
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    if (ImGui::IsItemActive()) {
        float delta = ImGui::GetIO().MouseDelta.x;
        right_w_ -= delta; // dragging right → delta > 0 → shrink right panel
        float min_w = 180.0f;
        float max_w = avail.x - left_w - splitter_w - 120.0f;
        right_w_ = std::max(min_w, std::min(max_w, right_w_));
    }

    ImGui::SameLine(0, 0);

    // Right: detail view (resizable)
    ImGui::BeginChild("##detail", ImVec2(right_w_, h), true);
    detail_view_.render_inner();
    ImGui::EndChild();

    ImGui::End();
}

// ------------------------------------------------------------
//  render_loading_popup
// ------------------------------------------------------------
void App::render_loading_popup() {
    if (!cluster_ || cluster_->state() != LoadState::Loading) return;

    float progress = cluster_->progress();

    // Centre a fixed-size popup on screen
    ImGuiIO& io   = ImGui::GetIO();
    float pw = 420.0f, ph = 90.0f;
    ImGui::SetNextWindowPos(
        ImVec2((io.DisplaySize.x - pw) * 0.5f,
               (io.DisplaySize.y - ph) * 0.5f),
        ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(pw, ph), ImGuiCond_Always);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar    | ImGuiWindowFlags_NoResize    |
        ImGuiWindowFlags_NoMove        | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings;

    ImGui::Begin("##loading_popup", nullptr, flags);

    // Label
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);
    ImGui::SetCursorPosX((pw - ImGui::CalcTextSize("Loading...").x) * 0.5f);
    ImGui::TextUnformatted("Loading...");

    ImGui::Spacing();

    // Yellow progress bar — no overlay text passed to ProgressBar itself;
    // we draw the percentage manually so we can split colours at the fill edge.
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(1.0f, 0.95f, 0.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,       ImVec4(0.15f, 0.15f, 0.0f, 1.0f));

    float bar_x     = 12.0f;
    float bar_w     = pw - 24.0f;
    float bar_h     = 22.0f;
    ImGui::SetCursorPosX(bar_x);
    ImVec2 bar_tl   = ImGui::GetCursorScreenPos(); // absolute TL of bar
    ImGui::ProgressBar(progress, ImVec2(bar_w, bar_h), "");

    ImGui::PopStyleColor(2);

    // Draw the percentage text centred over the bar, split at the fill boundary.
    // Characters whose centres lie left of the fill edge get black text (yellow bg),
    // characters to the right get white text (dark bg).
    char pct[16];
    std::snprintf(pct, sizeof(pct), "%.0f%%", progress * 100.0f);

    ImDrawList* dl  = ImGui::GetWindowDrawList();
    ImFont*     fnt = ImGui::GetFont();
    float       fz  = ImGui::GetFontSize();

    float text_w    = fnt->CalcTextSizeA(fz, FLT_MAX, 0.0f, pct).x;
    float text_x    = bar_tl.x + (bar_w - text_w) * 0.5f;
    float text_y    = bar_tl.y + (bar_h - fz) * 0.5f;

    // X coordinate where the yellow fill ends
    float fill_end  = bar_tl.x + bar_w * progress;

    // Walk each glyph, decide colour by whether its centre is in the filled region
    const char* p   = pct;
    float cx        = text_x;
    while (*p) {
        unsigned int cp = (unsigned char)*p;
        const ImFontGlyph* glyph = fnt->FindGlyph((ImWchar)cp);
        float adv = glyph ? glyph->AdvanceX * (fz / fnt->FontSize) : fz * 0.5f;
        float char_centre = cx + adv * 0.5f;
        ImU32 col = (char_centre <= fill_end)
            ? IM_COL32(0, 0, 0, 255)      // black over yellow fill
            : IM_COL32(255, 255, 255, 255); // white over dark bg
        dl->AddText(fnt, fz, ImVec2(cx, text_y), col, p, p + 1);
        cx += adv;
        ++p;
    }

    ImGui::End();
}

// ------------------------------------------------------------
//  render_frame
// ------------------------------------------------------------
void App::render_frame() {
    // If a load just finished → update UI views
    static LoadState last_state = LoadState::Idle;
    if (cluster_) {
        LoadState cur = cluster_->state();
        if (cur != last_state && cur == LoadState::Ready) {
            auto now = std::chrono::steady_clock::now();
            load_duration_s_ = std::chrono::duration<double>(
                now - load_start_).count();

            const auto& entries = cluster_->entries();
            const auto& nodes   = cluster_->nodes();
            log_view_.set_entries(entries.data(), entries.size(),
                                   &cluster_->strings(), &nodes);
            breakdown_view_.set_analysis(&cluster_->analysis(),
                                          &cluster_->strings());
            filter_view_.set_analysis(&cluster_->analysis(),
                                       &cluster_->strings());
        }
        last_state = cur;
    }

    render_menu_bar();
    render_dockspace();       // three-column host with filter panel pinned in left column
    render_loading_popup();   // centered yellow progress popup while loading
    prefs_view_.render();     // floating preferences window (no-op when closed)
}

// ------------------------------------------------------------
//  run — main loop
// ------------------------------------------------------------
int App::run() {
    if (!init()) return 1;

    ImVec4 clear_color(0.00f, 0.00f, 0.00f, 1.00f);

    while (running_) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);

            switch (event.type) {
                case SDL_QUIT:
                    running_ = false;
                    break;

                case SDL_KEYDOWN:
                    if (event.key.keysym.mod & KMOD_ALT &&
                        event.key.keysym.sym == SDLK_F4)
                        running_ = false;
                    break;

                case SDL_DROPFILE: {
                    // SDL delivers one SDL_DROPFILE per file;
                    // accumulate until SDL_DROPCOMPLETE.
                    pending_drops_.emplace_back(event.drop.file);
                    SDL_free(event.drop.file);
                    break;
                }

                case SDL_DROPCOMPLETE: {
                    if (!pending_drops_.empty()) {
                        handle_drop(pending_drops_);
                        pending_drops_.clear();
                    }
                    break;
                }

                default: break;
            }
        }

        // Rebuild font atlas if the user clicked Apply in Preferences.
        // Must happen OUTSIDE a frame (before NewFrame).
        if (font_mgr_.rebuild_pending)
            font_mgr_.rebuild(prefs_, "vendor/fonts");

        // Start ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        font_mgr_.push(); // apply the loaded font globally for this frame

        render_frame();

        font_mgr_.pop();

        // Render
        ImGui::Render();
        int w, h;
        SDL_GetWindowSize(window_, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window_);
    }

    if (load_thread_.joinable()) load_thread_.join();
    return 0;
}
