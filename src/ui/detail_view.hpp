#pragma once

#include <cstdint>
#include <string>

// Forward declarations
struct LogEntry;
class StringTable;
class MmapFile;

// ------------------------------------------------------------
//  DetailView
//
//  Renders a collapsible ImGui JSON tree for the currently
//  selected LogEntry. The raw JSON is re-parsed from the
//  mmap'd file bytes on demand (only when selection changes).
//
//  Uses a thread-local simdjson parser (no allocations visible
//  to the caller).
// ------------------------------------------------------------
class DetailView {
public:
    DetailView() = default;

    // Set the entry to display. Pass nullptr to clear.
    // `file_data` must remain valid for the lifetime of the view.
    void set_entry(const LogEntry* entry,
                   const char* file_data,
                   const StringTable* strings);

    // Render as a standalone ImGui window.
    void render();

    // Render only the contents (no Begin/End) — use inside a child window.
    void render_inner();

    bool has_entry() const { return entry_ != nullptr; }

private:
    void render_toolbar();

    // Render one key/value leaf — respects wrap_ flag.
    void render_leaf(const char* key, const char* color_tag,
                     const char* value_str) const;

    const LogEntry*    entry_     = nullptr;
    const char*        file_data_ = nullptr;
    const StringTable* strings_   = nullptr;

    bool wrap_ = false;  // text-wrap toggle controlled by toolbar checkbox
};
