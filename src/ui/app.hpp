#pragma once

#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <chrono>

// Forward-declare SDL types to avoid polluting headers
struct SDL_Window;
typedef void* SDL_GLContext;

#include "../analysis/cluster.hpp"
#include "log_view.hpp"
#include "detail_view.hpp"
#include "breakdown_view.hpp"
#include "filter_view.hpp"

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

    // Drag-and-drop handler: receives a list of file paths
    void handle_drop(const std::vector<std::string>& paths);

    // Start async cluster load in a background thread
    void start_load(const std::vector<std::string>& paths);

    // Wired up to breakdown view filter changes
    void on_filter_changed();

    // SDL/GL
    SDL_Window*   window_  = nullptr;
    SDL_GLContext gl_ctx_  = nullptr;

    // Data
    std::unique_ptr<Cluster>    cluster_;
    std::thread                 load_thread_;

    // UI state
    FilterState     filter_;
    LogView         log_view_;
    DetailView      detail_view_;
    BreakdownView   breakdown_view_;
    FilterView      filter_view_;

    // Which mmap file data pointer to use for detail view
    // (We keep one MmapFile per node, but for v0.0.1 we re-open on demand
    //  since detail view re-parses raw bytes directly from the mmap data).
    // We store all open mmap files during a session.
    std::vector<std::unique_ptr<MmapFile>> node_files_;

    bool running_ = true;

    // Load statistics displayed in the menu bar
    size_t total_file_bytes_ = 0;
    double load_duration_s_  = 0.0;
    std::chrono::steady_clock::time_point load_start_;

    // Pending drag-and-drop file paths (accumulated between SDL_DROPFILE
    // and SDL_DROPCOMPLETE events)
    std::vector<std::string> pending_drops_;
};
