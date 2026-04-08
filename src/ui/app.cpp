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
        // Find the node file for this entry
        if (e.node_idx < node_files_.size() && node_files_[e.node_idx]) {
            detail_view_.set_entry(&e,
                                   node_files_[e.node_idx]->data(),
                                   &cluster_->strings());
        }
    });

    breakdown_view_.set_filter(&filter_);
    breakdown_view_.set_on_filter_changed([this] { on_filter_changed(); });
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
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED);
    window_ = SDL_CreateWindow("YAMLA — MongoDB Log Analyzer",
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                1440, 900, flags);
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

    // Dark theme with custom accent
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding    = 4.0f;
    style.FrameRounding     = 3.0f;
    style.GrabRounding      = 3.0f;
    style.ScrollbarRounding = 3.0f;
    style.TabRounding       = 3.0f;
    style.Colors[ImGuiCol_WindowBg]       = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    style.Colors[ImGuiCol_Header]         = ImVec4(0.20f, 0.35f, 0.55f, 0.80f);
    style.Colors[ImGuiCol_HeaderHovered]  = ImVec4(0.25f, 0.45f, 0.65f, 0.90f);
    style.Colors[ImGuiCol_HeaderActive]   = ImVec4(0.30f, 0.55f, 0.75f, 1.00f);

    ImGui_ImplSDL2_InitForOpenGL(window_, gl_ctx_);
    ImGui_ImplOpenGL3_Init("#version 330 core");

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
        if (ImGui::BeginMenu("Help")) {
            ImGui::MenuItem("Drop one or more MongoDB log files onto this window");
            ImGui::EndMenu();
        }

        // Show load status in menu bar
        if (cluster_) {
            ImGui::SetCursorPosX(ImGui::GetIO().DisplaySize.x - 460.0f);
            switch (cluster_->state()) {
                case LoadState::Loading: {
                    float p = cluster_->progress();
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "Loading... %.0f%%", p * 100.0f);
                    ImGui::ProgressBar(p, ImVec2(200, 14), buf);
                    break;
                }
                case LoadState::Ready: {
                    size_t n = cluster_->entries().size();
                    // Format file size as MB or GB
                    char size_buf[32];
                    if (total_file_bytes_ >= 1024ull * 1024 * 1024) {
                        std::snprintf(size_buf, sizeof(size_buf), "%.2f GB",
                            static_cast<double>(total_file_bytes_) / (1024.0 * 1024.0 * 1024.0));
                    } else {
                        std::snprintf(size_buf, sizeof(size_buf), "%.1f MB",
                            static_cast<double>(total_file_bytes_) / (1024.0 * 1024.0));
                    }
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
                                       "%zu entries  |  %s  |  %.2fs",
                                       n, size_buf, load_duration_s_);
                    break;
                }
                case LoadState::Error:
                    ImGui::TextColored(ImVec4(1,0.3f,0.3f,1),
                                       "Error: %s", cluster_->error().c_str());
                    break;
                default: break;
            }
        } else {
            ImGui::SetCursorPosX(ImGui::GetIO().DisplaySize.x - 320.0f);
            ImGui::TextDisabled("Drop MongoDB log files here to begin");
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
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.10f, 1.0f));
    ImGui::Begin("##host", nullptr, host_flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    // Three-column layout: Breakdowns | Log View | Detail View
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float left_w   = 280.0f;
    float right_w  = 420.0f;
    float center_w = avail.x - left_w - right_w - 12.0f; // 3 × 4px padding
    float h        = avail.y;

    // Left: breakdowns
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.10f, 0.13f, 1.0f));
    ImGui::BeginChild("##breakdowns", ImVec2(left_w, h), true);
    ImGui::PopStyleColor();
    if (cluster_ && cluster_->state() == LoadState::Ready)
        breakdown_view_.render();
    ImGui::EndChild();

    ImGui::SameLine(0, 4);

    // Centre: log view
    ImGui::BeginChild("##logview", ImVec2(center_w, h), true);
    log_view_.render_inner();
    ImGui::EndChild();

    ImGui::SameLine(0, 4);

    // Right: detail view
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.10f, 0.13f, 1.0f));
    ImGui::BeginChild("##detail", ImVec2(right_w, h), true);
    ImGui::PopStyleColor();
    detail_view_.render_inner();
    ImGui::EndChild();

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
        }
        last_state = cur;
    }

    render_menu_bar();
    render_dockspace(); // handles all three panels internally
}

// ------------------------------------------------------------
//  run — main loop
// ------------------------------------------------------------
int App::run() {
    if (!init()) return 1;

    ImVec4 clear_color(0.08f, 0.08f, 0.10f, 1.00f);

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

        // Start ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        render_frame();

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
