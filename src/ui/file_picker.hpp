#pragma once

#include <string>
#include <vector>

// PendingPick is defined in app.hpp — forward-declare to avoid circular include.
// The file picker produces PendingPick items via its picks_ vector.
struct PendingPick;

// ------------------------------------------------------------
//  FilePicker — ImGui-based modal file/directory browser
//
//  Replaces the native NFD dialogs with a custom picker that
//  runs inside the ImGui render loop. Supports multi-select
//  of log files, log directories, and FTDC directories with
//  pill/chip display of selections before loading.
// ------------------------------------------------------------
class FilePicker {
public:
    FilePicker();

    // Open the picker modal.  start_dir is used if no cached directory.
    void open(const std::string& start_dir = "");

    // Call every frame.  Returns true if Load was clicked (paths ready).
    bool render();

    // Get the resolved paths (after render() returns true).
    // Log directories are expanded to individual .log/.json files.
    std::vector<std::string> take_paths();

    // Is the picker currently open?
    bool is_open() const;

    // Set the cached directory (from prefs)
    void set_cached_directory(const std::string& dir);

    // Get current directory (to cache in prefs)
    std::string current_directory() const;

private:
    struct DirEntry {
        std::string name;
        std::string full_path;
        bool is_dir;
        size_t size;
    };

    bool open_ = false;
    bool just_opened_ = false;         // trigger OpenPopup on first render
    std::string current_dir_;
    std::string cached_dir_;
    char path_buf_[2048] = {};         // editable path input
    std::vector<DirEntry> entries_;    // current directory listing
    int selected_idx_ = -1;            // highlighted entry
    bool show_hidden_ = false;         // show dotfiles toggle
    std::vector<PendingPick> picks_;   // selected items (pills)
    std::vector<std::string> result_paths_;  // resolved on Load

    void scan_directory();
    void navigate_to(const std::string& dir);
    void add_selected();               // add highlighted entry as file
    void add_directory();              // add highlighted directory to picks
};
