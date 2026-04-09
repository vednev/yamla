#pragma once

#include <cstdint>
#include <string>

struct LogEntry;
class StringTable;

// ------------------------------------------------------------
//  DetailView
//
//  Renders a collapsible ImGui JSON tree for the selected entry.
//
//  The mmap is NOT held open between selections — instead the
//  file path and byte offset are stored, and the file is opened
//  on demand each time render_inner() is called.  On SSD this
//  is imperceptible (<1ms).  This frees the mmap's virtual
//  address space after parsing, enabling very large files.
// ------------------------------------------------------------
class DetailView {
public:
    DetailView() = default;

    // Set the entry to display. Pass nullptr to clear.
    // file_path: path to the original log file for this node.
    void set_entry(const LogEntry* entry,
                   const std::string& file_path,
                   const StringTable* strings);

    void render();
    void render_inner();

    bool has_entry() const { return entry_ != nullptr; }

private:
    void render_toolbar();

    const LogEntry*    entry_     = nullptr;
    std::string        file_path_;             // path to re-open on demand
    const StringTable* strings_   = nullptr;

    bool wrap_ = true;
};
