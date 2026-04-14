#include "file_picker.hpp"
#include "app.hpp"           // PendingPick, classify_pick helpers

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// POSIX directory scanning (macOS + Linux)
#if !defined(_WIN32)
#   include <dirent.h>
#   include <sys/stat.h>
#   include <unistd.h>
#endif

// File-local helpers reused from app.cpp (duplicated here to keep
// the file picker self-contained — these are tiny and stable).

// ------------------------------------------------------------
//  is_ftdc_path — detect FTDC directories/files
// ------------------------------------------------------------
static bool is_ftdc_path(const std::string& path) {
    if (path.find("diagnostic.data") != std::string::npos) return true;
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
//  path_is_directory
// ------------------------------------------------------------
static bool fp_path_is_directory(const std::string& path) {
#if defined(_WIN32)
    DWORD attr = GetFileAttributesA(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st{};
    if (stat(path.c_str(), &st) != 0) return false;
    return S_ISDIR(st.st_mode);
#endif
}

// ------------------------------------------------------------
//  count_log_files_in_dir
// ------------------------------------------------------------
// Check if a filename looks like a MongoDB log file.
// Matches: .log, .json (exact extension), and also rotated logs
// like mongod.log.2024-01-15T00-00-00 (contain ".log." in name).
static bool is_log_filename(const std::string& name) {
    // Check for exact .log or .json extension
    size_t dot = name.rfind('.');
    if (dot != std::string::npos) {
        std::string ext = name.substr(dot);
        if (ext == ".log" || ext == ".json") return true;
    }
    // Check for rotated logs: name contains ".log." (e.g., mongod.log.2024-01-15)
    if (name.find(".log.") != std::string::npos) return true;
    if (name.find(".json.") != std::string::npos) return true;
    return false;
}

static int fp_count_log_files(const std::string& dir_path) {
    std::string dp = dir_path;
    while (dp.size() > 1 && (dp.back() == '/' || dp.back() == '\\'))
        dp.pop_back();

    int count = 0;
#if defined(_WIN32)
    std::string pattern = dp + "\\*";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::string name(fd.cFileName);
        if (is_log_filename(name)) ++count;
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
#else
    DIR* d = opendir(dp.c_str());
    if (!d) return 0;
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        std::string full = dp + "/" + ent->d_name;
        struct stat st{};
        if (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) continue;
        std::string name(ent->d_name);
        if (is_log_filename(name)) ++count;
    }
    closedir(d);
#endif
    return count;
}

// ------------------------------------------------------------
//  collect_log_files_in_dir
// ------------------------------------------------------------
static std::vector<std::string> fp_collect_log_files(const std::string& dir_path) {
    std::string dp = dir_path;
    while (dp.size() > 1 && (dp.back() == '/' || dp.back() == '\\'))
        dp.pop_back();

    std::vector<std::string> files;
#if defined(_WIN32)
    std::string pattern = dp + "\\*";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return files;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::string name(fd.cFileName);
        if (is_log_filename(name))
            files.push_back(dp + "\\" + name);
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
#else
    DIR* d = opendir(dp.c_str());
    if (!d) return files;
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        std::string full = dp + "/" + ent->d_name;
        struct stat st{};
        if (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) continue;
        std::string name(ent->d_name);
        if (is_log_filename(name))
            files.push_back(full);
    }
    closedir(d);
#endif
    std::sort(files.begin(), files.end());
    return files;
}

// Case-insensitive compare for sorting
static bool ci_less(const std::string& a, const std::string& b) {
    size_t len = std::min(a.size(), b.size());
    for (size_t i = 0; i < len; ++i) {
        char ca = static_cast<char>(std::tolower(static_cast<unsigned char>(a[i])));
        char cb = static_cast<char>(std::tolower(static_cast<unsigned char>(b[i])));
        if (ca != cb) return ca < cb;
    }
    return a.size() < b.size();
}

// Format file size for display
static std::string format_size(size_t bytes) {
    char buf[32];
    if (bytes >= 1024ull * 1024 * 1024)
        std::snprintf(buf, sizeof(buf), "%.1f GB",
            static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    else if (bytes >= 1024ull * 1024)
        std::snprintf(buf, sizeof(buf), "%.1f MB",
            static_cast<double>(bytes) / (1024.0 * 1024.0));
    else if (bytes >= 1024)
        std::snprintf(buf, sizeof(buf), "%.1f KB",
            static_cast<double>(bytes) / 1024.0);
    else
        std::snprintf(buf, sizeof(buf), "%zu B", bytes);
    return buf;
}

// ============================================================
//  FilePicker implementation
// ============================================================

FilePicker::FilePicker() = default;

void FilePicker::open(const std::string& start_dir) {
    if (open_) return;  // already open

    // Determine starting directory
    if (!cached_dir_.empty() && fp_path_is_directory(cached_dir_)) {
        current_dir_ = cached_dir_;
    } else if (!start_dir.empty() && fp_path_is_directory(start_dir)) {
        current_dir_ = start_dir;
    } else {
        const char* home = std::getenv("HOME");
        current_dir_ = (home && home[0]) ? home : "/";
    }

    selected_idx_ = -1;
    picks_.clear();
    result_paths_.clear();
    open_ = true;
    just_opened_ = true;
    scan_directory();
}

bool FilePicker::is_open() const {
    return open_;
}

void FilePicker::set_cached_directory(const std::string& dir) {
    cached_dir_ = dir;
}

std::string FilePicker::current_directory() const {
    return current_dir_;
}

std::vector<std::string> FilePicker::take_paths() {
    return std::move(result_paths_);
}

// ------------------------------------------------------------
//  scan_directory — refresh entries_ from current_dir_
// ------------------------------------------------------------
void FilePicker::scan_directory() {
    entries_.clear();
    selected_idx_ = -1;

    // Copy current_dir_ into the editable path buffer
    std::strncpy(path_buf_, current_dir_.c_str(), sizeof(path_buf_) - 1);
    path_buf_[sizeof(path_buf_) - 1] = '\0';

#if defined(_WIN32)
    std::string pattern = current_dir_ + "\\*";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        std::string name(fd.cFileName);
        if (name == "." || name == "..") continue;
        if (!show_hidden_ && name[0] == '.') continue;

        DirEntry e;
        e.name = name;
        e.full_path = current_dir_ + "\\" + name;
        e.is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        e.size = static_cast<size_t>(
            (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow);
        entries_.push_back(std::move(e));
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
#else
    DIR* d = opendir(current_dir_.c_str());
    if (!d) return;  // permission denied etc.

    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        std::string name(ent->d_name);
        if (name == "." || name == "..") continue;
        if (!show_hidden_ && !name.empty() && name[0] == '.') continue;

        DirEntry e;
        e.name = name;
        e.full_path = current_dir_ + "/" + name;
        e.is_dir = false;
        e.size = 0;

        struct stat st{};
        if (stat(e.full_path.c_str(), &st) == 0) {
            e.is_dir = S_ISDIR(st.st_mode);
            e.size = static_cast<size_t>(st.st_size);
        }
        entries_.push_back(std::move(e));
    }
    closedir(d);
#endif

    // Sort: directories first, then files. Both case-insensitive alphabetical.
    std::sort(entries_.begin(), entries_.end(),
        [](const DirEntry& a, const DirEntry& b) {
            if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
            return ci_less(a.name, b.name);
        });
}

// ------------------------------------------------------------
//  navigate_to
// ------------------------------------------------------------
void FilePicker::navigate_to(const std::string& dir) {
    current_dir_ = dir;
    // Normalize: strip trailing slash (except root "/")
    while (current_dir_.size() > 1 && current_dir_.back() == '/')
        current_dir_.pop_back();
    scan_directory();
}

// ------------------------------------------------------------
//  add_selected — add highlighted entry (file) to picks
// ------------------------------------------------------------
void FilePicker::add_selected() {
    if (selected_idx_ < 0 || selected_idx_ >= static_cast<int>(entries_.size()))
        return;

    const DirEntry& e = entries_[selected_idx_];

    // Deduplicate
    for (const auto& p : picks_) {
        if (p.path == e.full_path) return;
    }

    PendingPick pick;
    pick.path = e.full_path;
    pick.is_ftdc = is_ftdc_path(e.full_path);
    pick.file_count = 1;

    if (pick.is_ftdc) {
        // Enforce 1 FTDC — replace existing
        picks_.erase(
            std::remove_if(picks_.begin(), picks_.end(),
                           [](const PendingPick& pp) { return pp.is_ftdc; }),
            picks_.end());
        pick.label = "FTDC: " + e.name;
    } else if (e.is_dir) {
        int n = fp_count_log_files(e.full_path);
        pick.file_count = n;
        if (n > 0)
            pick.label = "LOGS: " + std::to_string(n) + " files";
        else
            pick.label = "DIR: " + e.name + " (empty)";
    } else {
        pick.label = "LOG: " + e.name;
    }

    picks_.push_back(std::move(pick));
}

// ------------------------------------------------------------
//  add_directory — add highlighted directory to picks
// ------------------------------------------------------------
void FilePicker::add_directory() {
    if (selected_idx_ < 0 || selected_idx_ >= static_cast<int>(entries_.size()))
        return;

    const DirEntry& e = entries_[selected_idx_];
    if (!e.is_dir) return;

    // Deduplicate
    for (const auto& p : picks_) {
        if (p.path == e.full_path) return;
    }

    PendingPick pick;
    pick.path = e.full_path;
    pick.is_ftdc = is_ftdc_path(e.full_path);
    pick.file_count = 1;

    if (pick.is_ftdc) {
        // Enforce 1 FTDC — replace existing
        picks_.erase(
            std::remove_if(picks_.begin(), picks_.end(),
                           [](const PendingPick& pp) { return pp.is_ftdc; }),
            picks_.end());
        pick.label = "FTDC: " + e.name;
    } else {
        int n = fp_count_log_files(e.full_path);
        pick.file_count = n;
        if (n > 0)
            pick.label = "LOGS: " + std::to_string(n) + " files";
        else
            pick.label = "DIR: " + e.name + " (empty)";
    }

    picks_.push_back(std::move(pick));
}

// ------------------------------------------------------------
//  render — main render function, called every frame
//  Returns true when Load is clicked (paths ready to collect).
// ------------------------------------------------------------
bool FilePicker::render() {
    if (!open_) return false;

    bool load_clicked = false;

    // Open the popup on first frame after open()
    if (just_opened_) {
        ImGui::OpenPopup("Open Files###file_picker");
        just_opened_ = false;
    }

    // Centre and size the modal
    ImGuiIO& io = ImGui::GetIO();
    float modal_w = std::min(860.0f, io.DisplaySize.x * 0.85f);
    float modal_h = std::min(560.0f, io.DisplaySize.y * 0.80f);
    ImGui::SetNextWindowSize(ImVec2(modal_w, modal_h), ImGuiCond_Always);
    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
        ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Open Files###file_picker", nullptr,
                                ImGuiWindowFlags_NoResize |
                                ImGuiWindowFlags_NoMove)) {
        float line_h = ImGui::GetTextLineHeightWithSpacing();

        // ---- Top bar: path input + Up button + hidden toggle ----
        {
            float up_btn_w = ImGui::CalcTextSize("Up").x + 16.0f;
            float hidden_btn_w = ImGui::CalcTextSize(".*").x + 16.0f;
            float input_w = ImGui::GetContentRegionAvail().x
                          - up_btn_w - hidden_btn_w - 16.0f;

            ImGui::PushItemWidth(input_w);
            if (ImGui::InputText("##path", path_buf_, sizeof(path_buf_),
                                  ImGuiInputTextFlags_EnterReturnsTrue)) {
                std::string new_dir(path_buf_);
                if (fp_path_is_directory(new_dir))
                    navigate_to(new_dir);
            }
            ImGui::PopItemWidth();

            ImGui::SameLine(0, 4.0f);
            if (ImGui::Button("Up", ImVec2(up_btn_w, 0))) {
                // Navigate to parent
                std::string parent = current_dir_;
                auto sep = parent.rfind('/');
                if (sep != std::string::npos && sep > 0)
                    parent = parent.substr(0, sep);
                else
                    parent = "/";
                navigate_to(parent);
            }

            ImGui::SameLine(0, 4.0f);
            if (ImGui::Button(show_hidden_ ? "[.*]" : " .* ",
                              ImVec2(hidden_btn_w, 0))) {
                show_hidden_ = !show_hidden_;
                scan_directory();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(show_hidden_ ? "Hide hidden files"
                                               : "Show hidden files");
        }

        ImGui::Separator();

        // ---- File list area ----
        // Compute height: leave space for pills + bottom buttons
        float pills_h = picks_.empty() ? 0.0f : (line_h + 12.0f);
        float bottom_h = line_h + 16.0f;  // Load/Cancel buttons
        float list_h = ImGui::GetContentRegionAvail().y - pills_h - bottom_h - 12.0f;
        list_h = std::max(list_h, 100.0f);

        if (ImGui::BeginChild("##file_list", ImVec2(0, list_h), true)) {
            // Table: Icon | Name | Size
            if (ImGui::BeginTable("##files", 3,
                                   ImGuiTableFlags_RowBg |
                                   ImGuiTableFlags_ScrollY)) {
                ImGui::TableSetupColumn("##icon", ImGuiTableColumnFlags_WidthFixed, 28.0f);
                ImGui::TableSetupColumn("Name",   ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Size",   ImGuiTableColumnFlags_WidthFixed, 80.0f);

                for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
                    const auto& e = entries_[i];
                    ImGui::PushID(i);
                    ImGui::TableNextRow();

                    // Icon column
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled(e.is_dir ? "[D]" : "[F]");

                    // Name column — selectable spanning all columns
                    ImGui::TableSetColumnIndex(1);
                    bool is_selected = (i == selected_idx_);
                    if (ImGui::Selectable(e.name.c_str(), is_selected,
                                           ImGuiSelectableFlags_SpanAllColumns |
                                           ImGuiSelectableFlags_AllowOverlap)) {
                        selected_idx_ = i;
                    }

                    // Double-click handling
                    if (ImGui::IsItemHovered() &&
                        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        if (e.is_dir) {
                            // Navigate into directory
                            navigate_to(e.full_path);
                            ImGui::PopID();
                            ImGui::EndTable();
                            ImGui::EndChild();
                            goto end_modal;
                        } else {
                            // Double-click file → add to picks
                            selected_idx_ = i;
                            add_selected();
                        }
                    }

                    // Size column
                    ImGui::TableSetColumnIndex(2);
                    if (!e.is_dir) {
                        ImGui::TextDisabled("%s", format_size(e.size).c_str());
                    }

                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
        ImGui::EndChild();

        // ---- Selected pills area ----
        if (!picks_.empty()) {
            ImGui::Spacing();
            float avail_w = ImGui::GetContentRegionAvail().x;
            float cursor_x = ImGui::GetCursorPosX();
            float start_x = cursor_x;
            float spacing = 6.0f;
            int remove_idx = -1;

            for (int i = 0; i < static_cast<int>(picks_.size()); ++i) {
                const auto& pick = picks_[i];
                std::string chip_text = pick.label + "  x";
                float chip_w = ImGui::CalcTextSize(chip_text.c_str()).x + 16.0f;
                float chip_h = line_h + 2.0f;

                // Wrap to next line if overflow
                if (i > 0 && cursor_x + chip_w > start_x + avail_w) {
                    cursor_x = start_x;
                }
                if (i > 0 && cursor_x > start_x) {
                    ImGui::SameLine(0, spacing);
                }
                if (cursor_x == start_x && i > 0) {
                    ImGui::SetCursorPosX(start_x);
                }

                ImGui::PushID(i + 5000);

                // Pill styling
                ImVec4 chip_bg     = pick.is_ftdc ? ImVec4(0.12f, 0.15f, 0.20f, 1.0f)
                                                  : ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
                ImVec4 chip_hover  = pick.is_ftdc ? ImVec4(0.18f, 0.22f, 0.28f, 1.0f)
                                                  : ImVec4(0.20f, 0.20f, 0.20f, 1.0f);
                ImVec4 chip_active = pick.is_ftdc ? ImVec4(0.22f, 0.28f, 0.35f, 1.0f)
                                                  : ImVec4(0.25f, 0.25f, 0.25f, 1.0f);
                ImGui::PushStyleColor(ImGuiCol_Button,        chip_bg);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  chip_hover);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,   chip_active);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);

                if (ImGui::Button(chip_text.c_str(), ImVec2(chip_w, chip_h))) {
                    remove_idx = i;
                }

                ImGui::PopStyleVar();
                ImGui::PopStyleColor(3);

                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", pick.path.c_str());

                ImGui::PopID();
                cursor_x += chip_w + spacing;
            }

            if (remove_idx >= 0) {
                picks_.erase(picks_.begin() + remove_idx);
            }
        }

        // ---- Bottom bar: Add + Add Directory + Load + Cancel ----
        {
            ImGui::Separator();
            ImGui::Spacing();

            float btn_h = line_h + 6.0f;

            // "Add" button — adds highlighted file entry to picks
            bool can_add = (selected_idx_ >= 0 &&
                           selected_idx_ < static_cast<int>(entries_.size()));
            if (!can_add) ImGui::BeginDisabled();
            if (ImGui::Button("Add", ImVec2(80.0f, btn_h))) {
                add_selected();
            }
            if (!can_add) ImGui::EndDisabled();

            // "Add Directory" button — adds highlighted directory to picks
            ImGui::SameLine(0, 8.0f);
            bool can_add_dir = can_add &&
                               entries_[selected_idx_].is_dir;
            if (!can_add_dir) ImGui::BeginDisabled();
            if (ImGui::Button("Add Directory", ImVec2(120.0f, btn_h))) {
                add_directory();
            }
            if (!can_add_dir) ImGui::EndDisabled();

            // Right-aligned: Load + Cancel
            float load_w = 80.0f;
            float cancel_w = 80.0f;
            float right_edge = ImGui::GetContentRegionAvail().x
                             + ImGui::GetCursorPosX();
            float load_x = right_edge - load_w - cancel_w - 12.0f;

            ImGui::SameLine(load_x);

            bool can_load = !picks_.empty();
            if (!can_load) ImGui::BeginDisabled();
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.10f, 0.25f, 0.10f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.15f, 0.35f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.20f, 0.45f, 0.20f, 1.0f));
            if (ImGui::Button("Load", ImVec2(load_w, btn_h))) {
                // Resolve picks → paths
                result_paths_.clear();
                for (const auto& pick : picks_) {
                    if (pick.is_ftdc) {
                        result_paths_.push_back(pick.path);
                    } else if (fp_path_is_directory(pick.path)) {
                        auto files = fp_collect_log_files(pick.path);
                        for (auto& f : files)
                            result_paths_.push_back(std::move(f));
                    } else {
                        result_paths_.push_back(pick.path);
                    }
                }
                load_clicked = true;
                cached_dir_ = current_dir_;
                picks_.clear();
                open_ = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor(3);
            if (!can_load) ImGui::EndDisabled();

            ImGui::SameLine(0, 8.0f);
            if (ImGui::Button("Cancel", ImVec2(cancel_w, btn_h))) {
                picks_.clear();
                result_paths_.clear();
                open_ = false;
                ImGui::CloseCurrentPopup();
            }
        }

end_modal:
        ImGui::EndPopup();
    } else {
        // Popup was closed externally (e.g. clicking outside)
        if (open_) {
            picks_.clear();
            result_paths_.clear();
            open_ = false;
        }
    }

    return load_clicked;
}
