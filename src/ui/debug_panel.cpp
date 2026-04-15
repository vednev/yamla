#include "debug_panel.hpp"

#include "../core/arena_chain.hpp"
#include "../core/timing.hpp"
#include "../parser/log_entry.hpp"   // StringTable
#include "../ftdc/metric_store.hpp"  // MetricStore

#include <imgui.h>
#include <cstdio>

// ------------------------------------------------------------
//  DebugPanel implementation
// ------------------------------------------------------------

void DebugPanel::set_sources(const ArenaChain* string_chain,
                              const ArenaChain* entry_chain,
                              const StringTable* strings,
                              const MetricStore* ftdc_store,
                              const TimingStats* timing) {
    string_chain_ = string_chain;
    entry_chain_  = entry_chain;
    strings_      = strings;
    ftdc_store_   = ftdc_store;
    timing_       = timing;
}

void DebugPanel::render_inner() {
    if (!visible) return;

    ImGui::Text("--- Memory ---");
    if (string_chain_) {
        ImGui::Text("String arena: %zu slabs, ~%.2f MB used (%.2f MB cap)",
            string_chain_->slab_count(),
            static_cast<double>(string_chain_->approximate_used()) / (1024.0 * 1024.0),
            static_cast<double>(string_chain_->total_capacity())  / (1024.0 * 1024.0));
    }
    if (entry_chain_) {
        ImGui::Text("Entry arena:  %zu slabs, ~%.2f MB used (%.2f MB cap)",
            entry_chain_->slab_count(),
            static_cast<double>(entry_chain_->approximate_used()) / (1024.0 * 1024.0),
            static_cast<double>(entry_chain_->total_capacity())  / (1024.0 * 1024.0));
    }
    if (strings_) {
        ImGui::Text("StringTable:  %zu interned strings", strings_->size());
    }
    if (ftdc_store_) {
        size_t total_samples = 0;
        for (const auto& kv : ftdc_store_->series)
            total_samples += kv.second.size();
        ImGui::Text("FTDC store:   %zu series, %zu samples",
            ftdc_store_->series.size(), total_samples);
    }

    ImGui::Separator();
    ImGui::Text("--- Timing ---");
    if (timing_) {
        ImGui::Text("Parse:  %.1f ms", timing_->parse_ms);
        ImGui::Text("Filter: %.1f ms", timing_->filter_ms);
        ImGui::Text("Frame:  %.2f ms (%.0f FPS)",
            timing_->frame_ms,
            timing_->frame_ms > 0.0 ? 1000.0 / timing_->frame_ms : 0.0);
        ImGui::Text("Memory snapshot: %.2f MB",
            static_cast<double>(timing_->memory_bytes) / (1024.0 * 1024.0));
    } else {
        ImGui::Text("No timing data yet");
    }
}
