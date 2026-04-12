#include "ftdc_cluster.hpp"
#include "ftdc_parser.hpp"

#include <cstdio>
#include <algorithm>
#include <string>
#include <vector>

// Platform directory listing
#if defined(_WIN32)
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#else
#   include <dirent.h>
#   include <sys/stat.h>
#endif

// ---- Platform directory scanner ----
static std::vector<std::string> scan_ftdc_dir(const std::string& dir_path) {
    std::vector<std::string> files;

#if defined(_WIN32)
    std::string pattern = dir_path + "\\*";
    WIN32_FIND_DATAA fd{};
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return files;
    do {
        std::string name = fd.cFileName;
        // FTDC files: "metrics.*" and "metrics.interim"
        if (name.substr(0, 7) == "metrics")
            files.push_back(dir_path + "\\" + name);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* d = opendir(dir_path.c_str());
    if (!d) return files;
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        std::string name = ent->d_name;
        // Accept "metrics", "metrics.*", "metrics.interim"
        if (name.size() >= 7 && name.substr(0, 7) == "metrics")
            files.push_back(dir_path + "/" + name);
    }
    closedir(d);
#endif

    // Sort lexicographically — FTDC filenames sort chronologically
    std::sort(files.begin(), files.end());
    return files;
}

// ---- Check if path is a directory ----
static bool is_directory(const std::string& path) {
#if defined(_WIN32)
    DWORD attr = GetFileAttributesA(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) &&
           (attr & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st{};
    if (stat(path.c_str(), &st) != 0) return false;
    return S_ISDIR(st.st_mode);
#endif
}

// ============================================================
//  FtdcCluster::load
// ============================================================
void FtdcCluster::load() {
    state_.store(FtdcLoadState::Loading);
    progress_.store(0.0f);
    error_msg_.clear();

    store_ = std::make_unique<MetricStore>();

    // Build list of files to parse
    std::vector<std::string> files;
    if (is_directory(path_)) {
        files = scan_ftdc_dir(path_);
        if (files.empty()) {
            error_msg_ = "No metrics.* files found in: " + path_;
            state_.store(FtdcLoadState::Error);
            return;
        }
    } else {
        files.push_back(path_);
    }

    FtdcParser parser;
    size_t total_files = files.size();
    size_t done        = 0;
    size_t fail_count  = 0;

    for (const auto& f : files) {
        parser.set_progress_cb([&](size_t bytes_done, size_t bytes_total) {
            float file_frac = (bytes_total > 0)
                ? static_cast<float>(bytes_done) / static_cast<float>(bytes_total)
                : 1.0f;
            float overall = (static_cast<float>(done) + file_frac)
                          / static_cast<float>(total_files);
            progress_.store(overall);
        });

        std::string err;
        if (!parser.parse_file(f, *store_, err))
            ++fail_count;

        ++done;
        progress_.store(static_cast<float>(done) / static_cast<float>(total_files));
    }

    // If ALL files failed, report error
    if (fail_count == total_files) {
        error_msg_ = "All " + std::to_string(fail_count) + " FTDC file(s) failed to parse";
        state_.store(FtdcLoadState::Error);
        return;
    }

    // Sort all series by timestamp (in case files were out of order)
    for (auto& kv : store_->series) {
        auto& ms = kv.second;
        if (ms.size() <= 1) continue;
        // Build index sort
        std::vector<size_t> idx(ms.size());
        for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
        std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
            return ms.timestamps_ms[a] < ms.timestamps_ms[b];
        });
        // Apply permutation
        std::vector<int64_t> ts2(ms.size());
        std::vector<double>  v2(ms.size());
        for (size_t i = 0; i < idx.size(); ++i) {
            ts2[i] = ms.timestamps_ms[idx[i]];
            v2[i]  = ms.values[idx[i]];
        }
        ms.timestamps_ms = std::move(ts2);
        ms.values        = std::move(v2);
    }

    store_->update_time_range();

    progress_.store(1.0f);
    state_.store(FtdcLoadState::Ready);
}
