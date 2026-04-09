#include "app.hpp"

#include <SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <implot.h>

// ---- Platform-specific renderer backend --------------------
#if defined(__APPLE__)
    // macOS: SDL2 Renderer → Metal (SDL2 uses CAMetalLayer internally)
#   include <imgui_impl_sdlrenderer2.h>
#elif defined(_WIN32)
    // Windows: DirectX 11
#   include <imgui_impl_dx11.h>
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#   include <d3d11.h>
#   include <dxgi.h>
#   include <SDL_syswm.h>
#   pragma comment(lib, "d3d11")
#   pragma comment(lib, "dxgi")
#else
    // Linux: OpenGL 3.3
#   include <imgui_impl_opengl3.h>
#   include <SDL_opengl.h>
#endif

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <stdexcept>

#include "../core/prefs.hpp"
#include "../core/system_ram.hpp"

// ---- Windows DX11 helpers ----------------------------------
#if defined(_WIN32)
static bool create_dx11_device(HWND hwnd,
                                ID3D11Device**          out_device,
                                ID3D11DeviceContext**   out_context,
                                IDXGISwapChain**        out_swapchain)
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount                        = 2;
    sd.BufferDesc.Width                   = 0;
    sd.BufferDesc.Height                  = 0;
    sd.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags                              = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow                       = hwnd;
    sd.SampleDesc.Count                   = 1;
    sd.SampleDesc.Quality                 = 0;
    sd.Windowed                           = TRUE;
    sd.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL feature_level;
    const D3D_FEATURE_LEVEL feature_levels[] = {
        D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0
    };
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        0, feature_levels, 2,
        D3D11_SDK_VERSION, &sd,
        out_swapchain, out_device,
        &feature_level, out_context);
    return SUCCEEDED(hr);
}

static ID3D11RenderTargetView* create_dx11_rtv(IDXGISwapChain*  swapchain,
                                                ID3D11Device*    device)
{
    ID3D11Texture2D* back_buf = nullptr;
    swapchain->GetBuffer(0, IID_PPV_ARGS(&back_buf));
    if (!back_buf) return nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    device->CreateRenderTargetView(back_buf, nullptr, &rtv);
    back_buf->Release();
    return rtv;
}
#endif // _WIN32

// ---- Arena sizing ------------------------------------------
// Computed dynamically from system RAM and user preference.
// See compute_arena_budget() in start_load().

// ------------------------------------------------------------
//  Constructor / Destructor
// ------------------------------------------------------------

App::App() {
    log_view_.set_filter(&filter_);
    log_view_.set_on_select([this](size_t idx) {
        if (!cluster_ || cluster_->state() != LoadState::Ready) return;
        const LogEntry& e = cluster_->entries()[idx];
        // detail view re-opens the file on demand — just pass the path
        if (e.node_idx < cluster_->nodes().size()) {
            detail_view_.set_entry(&e,
                                   cluster_->nodes()[e.node_idx].path,
                                   &cluster_->strings());
        }
    });

    breakdown_view_.set_filter(&filter_);
    breakdown_view_.set_on_filter_changed([this] { on_filter_changed(); });
    breakdown_view_.set_prefs(&prefs_);

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

#if defined(__APPLE__)
    // macOS: SDL2 Renderer path — no GL attributes needed.
    // SDL2 will use Metal (CAMetalLayer) automatically on macOS 10.13+.
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");

    SDL_WindowFlags flags = static_cast<SDL_WindowFlags>(
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    window_ = SDL_CreateWindow("YAMLA — MongoDB Log Analyzer",
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                1920, 1080, flags);
    if (!window_) {
        std::fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError());
        return false;
    }

    renderer_ = SDL_CreateRenderer(window_, -1,
                                    SDL_RENDERER_ACCELERATED |
                                    SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) {
        std::fprintf(stderr, "SDL_CreateRenderer error: %s\n", SDL_GetError());
        return false;
    }

    // On HiDPI/Retina displays the renderer output size is larger than the
    // logical window size (e.g. 2x on Retina). Tell SDL2 Renderer to scale
    // its output so ImGui's logical-pixel draw calls fill the full physical
    // framebuffer. Without this, ImGui renders in one quarter of the window.
    {
        int ww, wh, rw, rh;
        SDL_GetWindowSize(window_, &ww, &wh);
        SDL_GetRendererOutputSize(renderer_, &rw, &rh);
        float scale_x = static_cast<float>(rw) / static_cast<float>(ww);
        float scale_y = static_cast<float>(rh) / static_cast<float>(wh);
        SDL_RenderSetScale(renderer_, scale_x, scale_y);
        SDL_RendererInfo rinfo{};
        SDL_GetRendererInfo(renderer_, &rinfo);
        std::fprintf(stderr, "Renderer: %s  scale=%.2fx%.2f\n",
                     rinfo.name, scale_x, scale_y);
    }

#elif defined(_WIN32)
    // Windows: create a plain window, then attach DX11 to its HWND.
    SDL_WindowFlags flags = static_cast<SDL_WindowFlags>(
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    window_ = SDL_CreateWindow("YAMLA — MongoDB Log Analyzer",
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                1920, 1080, flags);
    if (!window_) {
        std::fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError());
        return false;
    }

    SDL_SysWMinfo wmInfo{};
    SDL_VERSION(&wmInfo.version);
    SDL_GetWindowWMInfo(window_, &wmInfo);
    HWND hwnd = wmInfo.info.win.window;

    if (!create_dx11_device(hwnd, &d3d_device_, &d3d_context_, &d3d_swapchain_)) {
        std::fprintf(stderr, "DX11 device creation failed\n");
        return false;
    }
    d3d_rtv_ = create_dx11_rtv(d3d_swapchain_, d3d_device_);

#else
    // Linux: OpenGL 3.3 via SDL2
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
                                1920, 1080, flags);
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
    SDL_GL_SetSwapInterval(1);
#endif

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

    // ---- ImGui platform + renderer backend init ---------------
#if defined(__APPLE__)
    ImGui_ImplSDL2_InitForSDLRenderer(window_, renderer_);
    ImGui_ImplSDLRenderer2_Init(renderer_);
#elif defined(_WIN32)
    ImGui_ImplSDL2_InitForD3D(window_);
    ImGui_ImplDX11_Init(d3d_device_, d3d_context_);
#else
    ImGui_ImplSDL2_InitForOpenGL(window_, gl_ctx_);
    ImGui_ImplOpenGL3_Init("#version 330 core");
#endif

    // ---- Font manager: provide platform-appropriate texture callbacks ----
#if defined(__APPLE__)
    font_mgr_.set_callbacks(
        [] { ImGui_ImplSDLRenderer2_CreateFontsTexture(); },
        [] { ImGui_ImplSDLRenderer2_DestroyFontsTexture(); });
#elif defined(_WIN32)
    font_mgr_.set_callbacks(
        [] { ImGui_ImplDX11_CreateFontsTexture(); },
        [] { ImGui_ImplDX11_InvalidateDeviceObjects(); });
#else
    font_mgr_.set_callbacks(
        [] { ImGui_ImplOpenGL3_CreateFontsTexture(); },
        [] { ImGui_ImplOpenGL3_DestroyFontsTexture(); });
#endif

    // Load prefs and fonts
    prefs_ = PrefsManager::load();
    std::string vendor_dir = "vendor/fonts";
    font_mgr_.load(prefs_, vendor_dir);
    prefs_view_.set_available_fonts(&font_mgr_.available_fonts());

    return true;
}

// ------------------------------------------------------------
//  shutdown
// ------------------------------------------------------------
void App::shutdown() {
#if defined(__APPLE__)
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    if (ImPlot::GetCurrentContext()) ImPlot::DestroyContext();
    if (ImGui::GetCurrentContext())  ImGui::DestroyContext();
    if (renderer_) SDL_DestroyRenderer(renderer_);
#elif defined(_WIN32)
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    if (ImPlot::GetCurrentContext()) ImPlot::DestroyContext();
    if (ImGui::GetCurrentContext())  ImGui::DestroyContext();
    if (d3d_rtv_)      { d3d_rtv_->Release();      d3d_rtv_      = nullptr; }
    if (d3d_swapchain_){ d3d_swapchain_->Release(); d3d_swapchain_= nullptr; }
    if (d3d_context_)  { d3d_context_->Release();   d3d_context_  = nullptr; }
    if (d3d_device_)   { d3d_device_->Release();    d3d_device_   = nullptr; }
#else
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    if (ImPlot::GetCurrentContext()) ImPlot::DestroyContext();
    if (ImGui::GetCurrentContext())  ImGui::DestroyContext();
    if (gl_ctx_) SDL_GL_DeleteContext(gl_ctx_);
#endif
    if (window_) SDL_DestroyWindow(window_);
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
    // Reset all UI state that holds raw pointers into the old cluster/arena.
    // This must happen BEFORE cluster_ is destroyed (below) so no render
    // frame can access dangling pointers between destruction and the new
    // cluster becoming ready.
    filter_.clear();
    detail_view_.set_entry(nullptr, std::string{}, nullptr);
    log_view_.set_entries(nullptr, nullptr, nullptr);
    breakdown_view_.set_analysis(nullptr, nullptr);
    breakdown_view_.set_nodes(nullptr);
    total_file_bytes_   = 0;
    load_duration_s_    = 0.0;
    sample_mode_             = false;
    sample_ratio_            = 1.0f;
    sample_notice_dismissed_ = false;
    last_cluster_state_ = LoadState::Idle;
    load_start_         = std::chrono::steady_clock::now();

    // Measure total file size (stat only — don't mmap yet)
    size_t total_bytes = 0;
    for (const auto& p : paths) {
        try {
            MmapFile f(p); // stat to get size
            total_bytes += f.size();
            // MmapFile destructs here — we don't need to keep it
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "Cannot open %s: %s\n", p.c_str(), ex.what());
        }
    }
    total_file_bytes_ = total_bytes;

    // ---- Compute memory budget --------------------------------
    // Budget = min(user_limit, 60% of total RAM), at least 256 MB.
    // Needed = 1.5× file size (entries + interned strings overhead).
    // If needed > budget we enter sample mode.
    static constexpr size_t MIN_ARENA = 256ull * 1024 * 1024;
    size_t total_ram = query_total_ram();
    if (total_ram == 0) total_ram = 8ull * 1024 * 1024 * 1024; // fallback 8 GB

    size_t budget;
    if (prefs_.memory_limit_gb > 0) {
        budget = static_cast<size_t>(prefs_.memory_limit_gb) * 1024ull * 1024 * 1024;
        budget = std::min(budget, static_cast<size_t>(total_ram * 0.85)); // never > 85% RAM
    } else {
        budget = static_cast<size_t>(total_ram * 0.60); // auto: 60%
    }
    budget = std::max(budget, MIN_ARENA);

    size_t needed = std::max(MIN_ARENA, static_cast<size_t>(total_bytes * 1.5));

    sample_mode_  = (needed > budget);
    sample_ratio_ = sample_mode_
                    ? static_cast<float>(budget) / static_cast<float>(needed)
                    : 1.0f;

    // Cluster uses ArenaChain internally — no upfront arena size needed.
    // Budget is communicated via sample_ratio.
    (void)needed; // used only to compute sample_ratio above

    cluster_ = std::make_unique<Cluster>();
    cluster_->set_sample_ratio(sample_ratio_);
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
    float h           = avail.y;
    float vsplitter_w = 6.0f; // vertical (left-column-width) splitter

    // ---- Left column — single unified scrollable filter panel ----
    bool has_data = cluster_ && cluster_->state() == LoadState::Ready;
    ImGui::BeginChild("##left_col", ImVec2(left_w_, h), true);
    if (has_data)
        breakdown_view_.render();
    else
        ImGui::TextDisabled("Drop MongoDB log files here to begin.");
    ImGui::EndChild();

    // Splitter between left column and log view (controls left_w_)
    ImGui::SameLine(0, 0);
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1,1,1,0.20f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1,1,1,0.40f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0,0));
    ImGui::Button("##leftsplit", ImVec2(vsplitter_w, h));
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActive()) {
        float delta = ImGui::GetIO().MouseDelta.x;
        left_w_ += delta;
        left_w_  = std::max(180.0f, std::min(avail.x * 0.45f, left_w_));
    }

    ImGui::SameLine(0, 0);

    // Centre: log view — takes all remaining space
    float splitter_w = 6.0f; // right splitter width
    // left_w_ + left-vsplitter + center + right-vsplitter + right_w_
    float center_w_actual = avail.x - left_w_ - vsplitter_w - right_w_ - splitter_w - 2.0f;
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
        float max_w = avail.x - left_w_ - vsplitter_w - splitter_w - 120.0f;
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
    // If a load just finished → update UI views.
    // last_cluster_state_ is a member so start_load() can reset it to Idle,
    // ensuring the Ready transition always fires exactly once per load.
    if (cluster_) {
        LoadState cur = cluster_->state();
        if (cur != last_cluster_state_ && cur == LoadState::Ready) {
            auto now = std::chrono::steady_clock::now();
            load_duration_s_ = std::chrono::duration<double>(
                now - load_start_).count();

            const auto& nodes = cluster_->nodes();
            log_view_.set_entries(&cluster_->entries(),
                                   &cluster_->strings(), &nodes);
            breakdown_view_.set_analysis(&cluster_->analysis(),
                                          &cluster_->strings());
            breakdown_view_.set_nodes(&cluster_->nodes());
        }
        last_cluster_state_ = cur;
    }

    render_menu_bar();

    // Sample mode popup — centered, dismissable, shown once per load
    if (sample_mode_ && !sample_notice_dismissed_
        && cluster_ && cluster_->state() == LoadState::Ready)
    {
        ImGui::OpenPopup("##sample_notice");
    }

    if (ImGui::BeginPopupModal("##sample_notice", nullptr,
                                ImGuiWindowFlags_NoTitleBar |
                                ImGuiWindowFlags_NoResize   |
                                ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(
            ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
            ImGuiCond_Always, ImVec2(0.5f, 0.5f));

        // Amber warning header
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.80f, 0.15f, 1.0f));
        ImGui::Text("File too large for full load");
        ImGui::PopStyleColor();

        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextWrapped(
            "This file (%.1f GB) exceeds the available memory budget.\n\n"
            "Only %.0f%% of log entries have been loaded (every ~%d-th line).\n\n"
            "Breakdown counts and analysis are approximate.\n\n"
            "To load more entries, increase the memory limit in\n"
            "Edit > Preferences > Memory.",
            static_cast<double>(total_file_bytes_) / (1024.0 * 1024.0 * 1024.0),
            cluster_->sample_ratio() * 100.0f,
            std::max(1, static_cast<int>(1.0f / cluster_->sample_ratio() + 0.5f)));

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float btn_w = 120.0f;
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - btn_w) * 0.5f
                             + ImGui::GetCursorPosX());
        if (ImGui::Button("OK", ImVec2(btn_w, 0))) {
            sample_notice_dismissed_ = true;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    render_dockspace();
    render_loading_popup();
    prefs_view_.render();
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
#if defined(__APPLE__)
        ImGui_ImplSDLRenderer2_NewFrame();
#elif defined(_WIN32)
        ImGui_ImplDX11_NewFrame();
#else
        ImGui_ImplOpenGL3_NewFrame();
#endif
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        font_mgr_.push();

        render_frame();

        font_mgr_.pop();

        // Render
        ImGui::Render();

#if defined(__APPLE__)
        SDL_SetRenderDrawColor(renderer_,
            static_cast<Uint8>(clear_color.x * 255),
            static_cast<Uint8>(clear_color.y * 255),
            static_cast<Uint8>(clear_color.z * 255), 255);
        SDL_RenderClear(renderer_);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
        SDL_RenderPresent(renderer_);
#elif defined(_WIN32)
        {
            float cc[4] = { clear_color.x, clear_color.y,
                            clear_color.z, clear_color.w };
            d3d_context_->ClearRenderTargetView(d3d_rtv_, cc);
            d3d_context_->OMSetRenderTargets(1, &d3d_rtv_, nullptr);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            d3d_swapchain_->Present(1, 0); // vsync
        }
#else
        int w, h;
        SDL_GetWindowSize(window_, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window_);
#endif
    }

    if (load_thread_.joinable()) load_thread_.join();
    return 0;
}
