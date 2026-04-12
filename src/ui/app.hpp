#pragma once

#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <chrono>

// Forward-declare SDL types to avoid polluting headers
struct SDL_Window;
struct SDL_Renderer;
typedef void* SDL_GLContext;

// Forward-declare DX11 types (Windows only)
#if defined(_WIN32)
struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;
struct ID3D11RenderTargetView;
#endif

#include "../analysis/cluster.hpp"
#include "../core/prefs.hpp"
#include "../llm/llm_client.hpp"
#include "log_view.hpp"
#include "detail_view.hpp"
#include "breakdown_view.hpp"
#include "font_manager.hpp"
#include "prefs_view.hpp"
#include "chat_view.hpp"

// ------------------------------------------------------------
//  App
//
//  Owns the SDL2 window, OpenGL context, ImGui/ImPlot state,
//  and orchestrates all UI panels.
// ------------------------------------------------------------
class App {
public:
    App();
    ~App();

    // Non-copyable
    App(const App&)            = delete;
    App& operator=(const App&) = delete;

    // Run the main loop. Returns 0 on clean exit.
    int run();

private:
    // SDL + GL setup / teardown
    bool init();
    void shutdown();

    // Per-frame rendering
    void render_frame();
    void render_menu_bar();
    void render_dockspace();
    void render_loading_popup();

    // Drag-and-drop handler: receives a list of file paths
    void handle_drop(const std::vector<std::string>& paths);

    // Start async cluster load in a background thread
    void start_load(const std::vector<std::string>& paths);

    // Append new files to the existing cluster (additive load)
    void append_load(const std::vector<std::string>& paths);

    // Wired up to breakdown view filter changes
    void on_filter_changed();

    // SDL window — always present
    SDL_Window*   window_  = nullptr;

    // Platform/renderer handles — platform-specific
#if defined(__APPLE__)
    // macOS: SDL2 Renderer (Metal-backed via CAMetalLayer)
    SDL_Renderer* renderer_ = nullptr;
#elif defined(_WIN32)
    // Windows: DirectX 11
    SDL_Renderer*          renderer_        = nullptr; // unused, kept for SDL init path
    ID3D11Device*          d3d_device_      = nullptr;
    ID3D11DeviceContext*   d3d_context_     = nullptr;
    IDXGISwapChain*        d3d_swapchain_   = nullptr;
    ID3D11RenderTargetView* d3d_rtv_        = nullptr;
#else
    // Linux: OpenGL via SDL2
    SDL_GLContext gl_ctx_  = nullptr;
#endif

    // Data
    std::unique_ptr<Cluster>    cluster_;
    std::thread                 load_thread_;

    // UI state
    FilterState     filter_;
    LogView         log_view_;
    DetailView      detail_view_;
    BreakdownView   breakdown_view_;
    FontManager     font_mgr_;
    PrefsView       prefs_view_;
    Prefs           prefs_;

    // node_files_ removed — mmaps are dropped after parsing; detail view
    // re-opens files on demand via pread.

    bool      running_            = true;
    LoadState last_cluster_state_ = LoadState::Idle;
    bool      sample_mode_              = false;  // true when file exceeded memory budget
    float     sample_ratio_             = 1.0f;   // fraction of entries loaded (1.0 = full)
    bool      sample_notice_dismissed_  = false;  // user dismissed the sample popup

    // Panel dimensions — all user-draggable
    float right_w_ = 420.0f;  // right (detail) panel width
    float left_w_  = 280.0f;  // left column width

    // Load statistics displayed in the menu bar
    size_t total_file_bytes_ = 0;
    double load_duration_s_  = 0.0;
    std::chrono::steady_clock::time_point load_start_;

    // Pending drag-and-drop file paths (accumulated between SDL_DROPFILE
    // and SDL_DROPCOMPLETE events)
    std::vector<std::string> pending_drops_;

    // LLM chat integration
    LlmClient   llm_client_;
    ChatView     chat_view_;
    std::string  knowledge_text_;   // loaded from knowledge/ at startup

    void load_knowledge();          // reads knowledge/*.md into knowledge_text_
    void setup_llm();               // configure client after prefs load
};
