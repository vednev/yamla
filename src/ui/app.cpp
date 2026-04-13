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
#include <fstream>
#include <sstream>

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
//  Session destructor
// ------------------------------------------------------------
Session::~Session() {
    if (load_thread.joinable()) load_thread.join();
    llm_client.cancel_and_join();
    cluster.reset();
}

// ------------------------------------------------------------
//  Constructor / Destructor
// ------------------------------------------------------------

App::App() {
    prefs_view_.set_prefs(&prefs_);
    prefs_view_.set_on_changed([this](const Prefs& p) {
        prefs_ = p;
        PrefsManager::save(p);
        font_mgr_.rebuild_pending = true;
        // Re-configure LLM client with updated prefs
        setup_llm();
    });

    create_session();  // start with one empty session
}

App::~App() {
    sessions_.clear();  // Session destructors handle thread joining
    shutdown();
}

// ------------------------------------------------------------
//  Session lifecycle helpers
// ------------------------------------------------------------

Session& App::active_session() {
    // Bounds-check (T-05-01)
    if (active_session_idx_ < 0)
        active_session_idx_ = 0;
    if (active_session_idx_ >= static_cast<int>(sessions_.size()))
        active_session_idx_ = static_cast<int>(sessions_.size()) - 1;
    return *sessions_[active_session_idx_];
}

void App::create_session() {
    auto s = std::make_unique<Session>();
    wire_session(*s);
    sessions_.push_back(std::move(s));
    active_session_idx_ = static_cast<int>(sessions_.size()) - 1;
}

void App::wire_session(Session& s) {
    s.log_view.set_filter(&s.filter);
    s.log_view.set_on_select([&s](size_t idx, uint16_t node_idx) {
        if (!s.cluster || s.cluster->state() != LoadState::Ready) return;
        const LogEntry& e = s.cluster->entries()[idx];
        const auto& nodes = s.cluster->nodes();
        if (node_idx >= nodes.size()) return;

        uint64_t offset = e.raw_offset;
        uint32_t len    = e.raw_len;
        s.cluster->get_node_raw(idx, node_idx, offset, len);

        const std::string& path = nodes[node_idx].path;
        s.detail_view.set_entry(&e, path, &s.cluster->strings(),
                                offset, len);
        s.llm_client.tools().set_selected_entry(&e, path);
    });

    s.breakdown_view.set_filter(&s.filter);
    s.breakdown_view.set_on_filter_changed([&s] { s.log_view.rebuild_filter_index(); });
    s.breakdown_view.set_prefs(&prefs_);

    s.ftdc_view.set_filter(&s.filter);

    s.chat_view.set_llm_client(&s.llm_client);
    s.chat_view.set_prefs(&prefs_);
}

void App::close_session(int idx) {
    if (idx < 0 || idx >= static_cast<int>(sessions_.size())) return;
    sessions_.erase(sessions_.begin() + idx);
    if (sessions_.empty()) {
        create_session();  // always have at least one tab
        active_session_idx_ = 0;
    } else if (active_session_idx_ >= static_cast<int>(sessions_.size())) {
        active_session_idx_ = static_cast<int>(sessions_.size()) - 1;
    } else if (active_session_idx_ > idx) {
        active_session_idx_--;
    }
}

std::string App::compute_tab_title(const Session& s) {
    bool has_log  = s.cluster && s.cluster->state() == LoadState::Ready;
    bool has_ftdc = s.ftdc_view.load_state() == FtdcLoadState::Ready;
    if (has_log && has_ftdc) {
        const auto& nodes = s.cluster->nodes();
        std::string log_name = "log";
        if (!nodes.empty()) {
            const auto& p = nodes[0].path;
            auto pos = p.find_last_of("/\\");
            log_name = (pos != std::string::npos) ? p.substr(pos + 1) : p;
        }
        return log_name + " + FTDC";
    }
    if (has_log) {
        const auto& nodes = s.cluster->nodes();
        if (!nodes.empty()) {
            const auto& p = nodes[0].path;
            auto pos = p.find_last_of("/\\");
            return (pos != std::string::npos) ? p.substr(pos + 1) : p;
        }
        return "Log";
    }
    if (has_ftdc) return "FTDC";
    return "New Session";
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
    ImPlot::GetStyle().Use24HourClock = true;

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

    // Load knowledge base and configure LLM
    load_knowledge();
    setup_llm();

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
//  load_knowledge — read knowledge/*.md at startup
// ------------------------------------------------------------
void App::load_knowledge() {
    // Read the single knowledge file
    const char* path = "knowledge/knowledge.md";
    std::ifstream f(path);
    if (!f.is_open()) {
        std::fprintf(stderr, "Warning: cannot open knowledge file: %s\n", path);
        return;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    knowledge_text_ = ss.str();
}

// ------------------------------------------------------------
//  setup_llm — configure LLM clients for ALL sessions
// ------------------------------------------------------------
void App::setup_llm() {
    // Build system prompt: persona + knowledge
    std::string system;
    system += "You are a MongoDB log analysis assistant integrated into YAMLA "
              "(a desktop MongoDB log analyzer tool). You help the user understand "
              "patterns, diagnose issues, and find relevant log entries in their "
              "loaded log data.\n\n"
              "You have access to tools that can query the loaded log data: search "
              "for entries, get analysis summaries, inspect slow queries, examine "
              "connections, and find errors. Use these tools proactively to answer "
              "the user's questions with concrete data.\n\n"
              "When presenting findings, be concise but thorough. Reference specific "
              "entry indices so the user can look them up. Use markdown formatting "
              "for readability.\n\n"
              "--- MONGODB LOG KNOWLEDGE BASE ---\n\n";
    system += knowledge_text_;

    for (auto& sp : sessions_) {
        sp->llm_client.set_prefs(&prefs_);
        sp->llm_client.set_system_prompt(system);
    }
}

// Detect if a dropped path is FTDC data (diagnostic.data dir or metrics.* file)
static bool is_ftdc_path(const std::string& path) {
    if (path.find("diagnostic.data") != std::string::npos) return true;
    // Check basename starts with "metrics"
    auto sep = path.rfind('/');
#if defined(_WIN32)
    auto sep2 = path.rfind('\\');
    if (sep2 != std::string::npos && (sep == std::string::npos || sep2 > sep))
        sep = sep2;
#endif
    std::string basename = (sep != std::string::npos) ? path.substr(sep + 1) : path;
    return basename.size() >= 7 && basename.substr(0, 7) == "metrics";
}

// ------------------------------------------------------------
//  handle_drop — routes to active session
// ------------------------------------------------------------
void App::handle_drop(const std::vector<std::string>& paths) {
    if (paths.empty()) return;
    Session& s = active_session();

    // Auto-detect FTDC data: route to FTDC view
    if (is_ftdc_path(paths[0])) {
        s.ftdc_view.start_load(paths[0]);
        s.active_tab = 1;
        s.force_tab_switch = true;
        return;
    }

    if (s.load_thread.joinable()) s.load_thread.join();

    // If LLM is currently thinking, wait for it to finish before
    // mutating cluster data. This prevents data races.
    if (s.llm_client.is_thinking()) {
        // Fall through to start_load which creates a new cluster
    }

    if (s.cluster && s.cluster->state() == LoadState::Ready && !s.llm_client.is_thinking()) {
        append_load(paths);
    } else {
        start_load(paths);
    }
}

void App::start_load(const std::vector<std::string>& paths) {
    Session& s = active_session();

    // Reset all UI state that holds raw pointers into the old cluster/arena.
    // This must happen BEFORE cluster is destroyed (below) so no render
    // frame can access dangling pointers between destruction and the new
    // cluster becoming ready.
    s.filter.clear();
    s.detail_view.set_entry(nullptr, std::string{}, nullptr);
    s.log_view.set_entries(nullptr, nullptr, nullptr);
    s.breakdown_view.set_analysis(nullptr, nullptr);
    s.breakdown_view.set_nodes(nullptr);
    s.total_file_bytes   = 0;
    s.load_duration_s    = 0.0;
    s.sample_mode             = false;
    s.sample_ratio            = 1.0f;
    s.sample_notice_dismissed = false;
    s.last_cluster_state = LoadState::Idle;
    s.load_start         = std::chrono::steady_clock::now();

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
    s.total_file_bytes = total_bytes;

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

    s.sample_mode  = (needed > budget);
    s.sample_ratio = s.sample_mode
                     ? static_cast<float>(budget) / static_cast<float>(needed)
                     : 1.0f;

    // Cluster uses ArenaChain internally — no upfront arena size needed.
    // Budget is communicated via sample_ratio.
    (void)needed; // used only to compute sample_ratio above

    s.cluster = std::make_unique<Cluster>();
    s.cluster->set_sample_ratio(s.sample_ratio);
    for (const auto& p : paths)
        s.cluster->add_file(p);

    s.load_thread = std::thread([&s] { s.cluster->load(); });
}

// ------------------------------------------------------------
//  append_load — add files to an already-loaded cluster
// ------------------------------------------------------------
void App::append_load(const std::vector<std::string>& paths) {
    Session& s = active_session();

    // Clear view pointers — entries will be re-sorted so all indices
    // become invalid. The views will be re-wired when state transitions
    // back to Ready in render_frame().
    s.filter.clear();
    s.detail_view.set_entry(nullptr, std::string{}, nullptr);
    s.log_view.set_entries(nullptr, nullptr, nullptr);
    s.breakdown_view.set_analysis(nullptr, nullptr);
    s.breakdown_view.set_nodes(nullptr);
    s.last_cluster_state = LoadState::Idle;
    s.load_start         = std::chrono::steady_clock::now();

    // Accumulate total file bytes (existing + new)
    for (const auto& p : paths) {
        try {
            MmapFile f(p);
            s.total_file_bytes += f.size();
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "Cannot open %s: %s\n", p.c_str(), ex.what());
        }
    }

    // Capture the paths for the background thread
    std::vector<std::string> new_paths = paths;
    s.load_thread = std::thread([&s, new_paths = std::move(new_paths)] {
        s.cluster->append_files(new_paths);
    });
}

// ------------------------------------------------------------
//  on_filter_changed
// ------------------------------------------------------------
void App::on_filter_changed() {
    active_session().log_view.rebuild_filter_index();
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
            if (ImGui::MenuItem("AI Assistant", "Ctrl+A"))
                active_session().chat_view.toggle();
            ImGui::EndMenu();
        }

        // Show load status — right-aligned, gray tone (for active session)
        {
            Session& s = active_session();
            const ImVec4 stat_color = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
            char stat_buf[128] = {};

            if (s.cluster) {
                switch (s.cluster->state()) {
                    case LoadState::Loading:
                        std::snprintf(stat_buf, sizeof(stat_buf), "Loading...");
                        break;
                    case LoadState::Ready: {
                        char size_buf[32];
                        if (s.total_file_bytes >= 1024ull * 1024 * 1024)
                            std::snprintf(size_buf, sizeof(size_buf), "%.2f GB",
                                static_cast<double>(s.total_file_bytes) / (1024.0*1024.0*1024.0));
                        else
                            std::snprintf(size_buf, sizeof(size_buf), "%.1f MB",
                                static_cast<double>(s.total_file_bytes) / (1024.0*1024.0));
                        std::snprintf(stat_buf, sizeof(stat_buf),
                            "%zu entries  |  %s  |  %.2fs",
                            s.cluster->entries().size(), size_buf, s.load_duration_s);
                        break;
                    }
                    case LoadState::Error:
                        std::snprintf(stat_buf, sizeof(stat_buf),
                            "Error: %s", s.cluster->error().c_str());
                        break;
                    default:
                        break;
                }
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

    // ---- Outer session tab bar (D-40) ----
    if (ImGui::BeginTabBar("##sessions", ImGuiTabBarFlags_AutoSelectNewTabs |
                                          ImGuiTabBarFlags_FittingPolicyResizeDown)) {
        for (int i = 0; i < static_cast<int>(sessions_.size()); ++i) {
            auto& si = *sessions_[i];
            // Update tab title dynamically (D-41)
            si.title = compute_tab_title(si);
            std::string label = si.title + "###session_" + std::to_string(i);

            // D-42: close button via p_open parameter
            bool tab_open = si.open;
            if (ImGui::BeginTabItem(label.c_str(), &tab_open, ImGuiTabItemFlags_None)) {
                active_session_idx_ = i;
                ImGui::EndTabItem();
            }
            if (!tab_open && si.open) {
                // User clicked close button — show confirmation dialog (D-42)
                show_close_confirm_ = true;
                close_confirm_idx_  = i;
                si.open = true;  // keep open until confirmed
            }
        }

        // D-43: "+" button at end of tab bar
        if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing |
                                       ImGuiTabItemFlags_NoTooltip)) {
            create_session();
        }

        ImGui::EndTabBar();
    }

    // ---- Active session's views (scoped by session index for unique IDs, D-51) ----
    ImGui::PushID(active_session_idx_);
    Session& s = active_session();

    // ---- Inner tab bar: show only when FTDC data is loaded (per D-06) ----
    bool show_tab_bar = (s.ftdc_view.load_state() != FtdcLoadState::Idle);

    if (show_tab_bar) {
        if (ImGui::BeginTabBar("##main_tabs", ImGuiTabBarFlags_None)) {
            ImGuiTabItemFlags logs_flags = (s.active_tab == 0 && s.force_tab_switch)
                ? ImGuiTabItemFlags_SetSelected : 0;
            ImGuiTabItemFlags ftdc_flags = (s.active_tab == 1 && s.force_tab_switch)
                ? ImGuiTabItemFlags_SetSelected : 0;
            if (s.force_tab_switch) s.force_tab_switch = false;

            if (ImGui::BeginTabItem("Logs", nullptr, logs_flags)) {
                s.active_tab = 0;
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("FTDC", nullptr, ftdc_flags)) {
                s.active_tab = 1;
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float h = avail.y;

    if (!show_tab_bar || s.active_tab == 0) {
        // ---- Logs tab: existing three-column layout ----
        float vsplitter_w = 6.0f; // vertical (left-column-width) splitter

        // ---- Left column — single unified scrollable filter panel ----
        bool has_data = s.cluster && s.cluster->state() == LoadState::Ready;
        ImGui::BeginChild("##left_col", ImVec2(left_w_, h), true);
        if (has_data)
            s.breakdown_view.render();
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
        s.log_view.render_inner();
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
        s.detail_view.render_inner();
        ImGui::EndChild();
    }

    if (show_tab_bar && s.active_tab == 1) {
        // ---- FTDC tab: two-column layout ----
        s.ftdc_view.render(h);
    }

    ImGui::PopID();  // PushID(active_session_idx_)
    ImGui::End();
}

// ------------------------------------------------------------
//  render_loading_popup — checks ALL sessions (D-50)
// ------------------------------------------------------------
void App::render_loading_popup() {
    Session& active = active_session();

    // ---- Log cluster loading popup (active session only — only one
    //      centered popup at a time makes sense) ----
    if (active.cluster && active.cluster->state() == LoadState::Loading) {
        float progress = active.cluster->progress();

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

    // ---- FTDC loading popups for ALL sessions (D-50) ----
    for (int i = 0; i < static_cast<int>(sessions_.size()); ++i) {
        ImGui::PushID(i);
        sessions_[i]->ftdc_view.render_loading_popup();
        ImGui::PopID();
    }
}

// ------------------------------------------------------------
//  render_frame
// ------------------------------------------------------------
void App::render_frame() {
    // Poll ALL sessions for load completion (D-49: inactive sessions
    // still detect transitions)
    for (auto& sp : sessions_) {
        Session& s = *sp;
        if (s.cluster) {
            LoadState cur = s.cluster->state();
            if (cur != s.last_cluster_state && cur == LoadState::Ready) {
                auto now = std::chrono::steady_clock::now();
                s.load_duration_s = std::chrono::duration<double>(
                    now - s.load_start).count();

                const auto& nodes = s.cluster->nodes();
                s.log_view.set_entries(&s.cluster->entries(),
                                       &s.cluster->strings(), &nodes);
                s.breakdown_view.set_analysis(&s.cluster->analysis(),
                                               &s.cluster->strings());
                s.breakdown_view.set_nodes(&s.cluster->nodes());

                // Wire LLM tools to the new cluster data
                s.llm_client.tools().set_cluster(s.cluster.get());
                // Clear conversation — old data context is stale
                s.llm_client.clear();

                // Build flat pointer list for FTDC annotation markers
                s.log_entry_ptrs.clear();
                const auto& entries = s.cluster->entries();
                s.log_entry_ptrs.reserve(entries.size());
                for (size_t i = 0; i < entries.size(); ++i)
                    s.log_entry_ptrs.push_back(&entries[i]);
                s.ftdc_view.set_log_data(&s.log_entry_ptrs, &s.cluster->strings());
            }
            s.last_cluster_state = cur;
        }
        // Poll FTDC for all sessions (D-49)
        s.ftdc_view.poll_state();
    }

    render_menu_bar();

    // Sample mode popup — active session only (centered, dismissable, shown once per load)
    {
        Session& s = active_session();
        if (s.sample_mode && !s.sample_notice_dismissed
            && s.cluster && s.cluster->state() == LoadState::Ready)
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
                static_cast<double>(s.total_file_bytes) / (1024.0 * 1024.0 * 1024.0),
                s.cluster->sample_ratio() * 100.0f,
                std::max(1, static_cast<int>(1.0f / s.cluster->sample_ratio() + 0.5f)));

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            float btn_w = 120.0f;
            ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - btn_w) * 0.5f
                                 + ImGui::GetCursorPosX());
            if (ImGui::Button("OK", ImVec2(btn_w, 0))) {
                s.sample_notice_dismissed = true;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    render_dockspace();
    render_loading_popup();
    prefs_view_.render();
    active_session().chat_view.render();

    // ---- Close session confirmation dialog (D-42) ----
    if (show_close_confirm_) {
        ImGui::OpenPopup("##close_session_confirm");
        show_close_confirm_ = false;
    }
    if (ImGui::BeginPopupModal("##close_session_confirm", nullptr,
                                ImGuiWindowFlags_NoTitleBar |
                                ImGuiWindowFlags_NoResize |
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Close session?");
        ImGui::Separator();
        ImGui::TextWrapped("Chat history and loaded data will be lost.");
        ImGui::Spacing();

        float btn_w = 80.0f;
        if (ImGui::Button("Close", ImVec2(btn_w, 0))) {
            close_session(close_confirm_idx_);
            close_confirm_idx_ = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(btn_w, 0))) {
            close_confirm_idx_ = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
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
                    // Ctrl+A: toggle AI assistant chat
                    if ((event.key.keysym.mod & KMOD_CTRL) &&
                        event.key.keysym.sym == SDLK_a &&
                        !ImGui::GetIO().WantTextInput)
                        active_session().chat_view.toggle();
                    // Escape: close AI assistant chat
                    if (event.key.keysym.sym == SDLK_ESCAPE &&
                        active_session().chat_view.is_open())
                        active_session().chat_view.close();
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

    // Session destructors handle thread joining via sessions_.clear() in ~App
    return 0;
}
