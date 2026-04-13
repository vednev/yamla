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
#include "ftdc_view.hpp"

// ------------------------------------------------------------
//  Session — one independent analysis session
//
//  Each session owns its own log data, FTDC data, views,
//  filters, chat, and LLM client. Multiple sessions live
//  in App::sessions_ as independent tabs.
// ------------------------------------------------------------
struct Session {
    // Data
    std::unique_ptr<Cluster>    cluster;
    std::thread                 load_thread;

    // UI state
    FilterState     filter;
    LogView         log_view;
    DetailView      detail_view;
    BreakdownView   breakdown_view;

    // LLM chat integration
    LlmClient       llm_client;
    ChatView         chat_view;

    // FTDC view
    FtdcView        ftdc_view;

    // Inner tab: 0 = Logs, 1 = FTDC
    int  active_tab       = 0;
    bool force_tab_switch = false;

    // Flat pointer list for FTDC annotation markers
    std::vector<const LogEntry*> log_entry_ptrs;

    // Load state tracking
    LoadState last_cluster_state = LoadState::Idle;
    bool      sample_mode             = false;
    float     sample_ratio            = 1.0f;
    bool      sample_notice_dismissed = false;
    size_t    total_file_bytes = 0;
    double    load_duration_s  = 0.0;
    std::chrono::steady_clock::time_point load_start;

    // Tab title (computed from loaded filenames)
    std::string title = "New Session";
    bool        open  = true;  // false when user clicks close (after confirm)

    // Non-copyable (owns threads)
    Session() = default;
    ~Session();
    Session(const Session&)            = delete;
    Session& operator=(const Session&) = delete;
};

// ------------------------------------------------------------
//  App
//
//  Owns the SDL2 window, OpenGL context, ImGui/ImPlot state,
//  and orchestrates all UI panels. Holds multiple sessions as
//  independent tabs.
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

    // Session lifecycle
    Session& active_session();
    void create_session();          // creates empty session, wires callbacks
    void close_session(int idx);    // removes session, adjusts active index
    void wire_session(Session& s);  // sets up callbacks and shared state

    // Compute tab title from loaded data (D-41)
    static std::string compute_tab_title(const Session& s);

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

    // Shared UI state (D-38: stays in App, not per-session)
    FontManager     font_mgr_;
    PrefsView       prefs_view_;
    Prefs           prefs_;

    bool      running_ = true;

    // Panel dimensions — all user-draggable, shared across sessions (D-39)
    float right_w_ = 420.0f;  // right (detail) panel width
    float left_w_  = 280.0f;  // left column width

    // Pending drag-and-drop file paths (accumulated between SDL_DROPFILE
    // and SDL_DROPCOMPLETE events)
    std::vector<std::string> pending_drops_;

    // Sessions (D-37, D-38)
    std::vector<std::unique_ptr<Session>> sessions_;
    int active_session_idx_ = 0;

    // Knowledge text loaded once at startup, passed to each session's LlmClient (D-48)
    std::string knowledge_text_;

    // Close confirmation dialog state (D-42)
    bool  show_close_confirm_ = false;
    int   close_confirm_idx_  = -1;

    void load_knowledge();          // reads knowledge/*.md into knowledge_text_
    void setup_llm();               // configure client after prefs load
};
