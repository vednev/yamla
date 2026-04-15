#pragma once

#include <cstddef>
#include <cstdint>

// Forward declarations — avoid pulling in heavy headers
class ArenaChain;
class StringTable;
struct MetricStore;
struct TimingStats;

// ------------------------------------------------------------
//  DebugPanel — developer-only memory + timing overlay (D-13)
//
//  Hidden by default. Toggled via Help → Debug Panel menu item
//  (only built into yamla-debug; gated by YAMLA_DEBUG_BUILD in App).
//  Non-owning pointers to data sources; data must outlive panel.
// ------------------------------------------------------------
class DebugPanel {
public:
    DebugPanel() = default;

    void set_sources(const ArenaChain* string_chain,
                     const ArenaChain* entry_chain,
                     const StringTable* strings,
                     const MetricStore* ftdc_store,
                     const TimingStats* timing);

    void render_inner(); // content only — caller wraps with Begin/End

    bool visible = false;
    void toggle() { visible = !visible; }

private:
    const ArenaChain*  string_chain_ = nullptr;
    const ArenaChain*  entry_chain_  = nullptr;
    const StringTable* strings_      = nullptr;
    const MetricStore* ftdc_store_   = nullptr;
    const TimingStats* timing_       = nullptr;
};
