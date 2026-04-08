#pragma once

#include <cstdint>
#include "../analysis/cluster.hpp"

// ------------------------------------------------------------
//  Color utilities for the UI
// ------------------------------------------------------------

// Convert NodeColor (float RGBA) to ImGui packed u32
inline uint32_t node_color_u32(const NodeColor& c) {
    auto f2b = [](float f) -> uint32_t {
        return static_cast<uint32_t>(f * 255.0f + 0.5f) & 0xFF;
    };
    // ImGui packs as ABGR
    return (f2b(c.a) << 24) | (f2b(c.b) << 16) | (f2b(c.g) << 8) | f2b(c.r);
}

// Dim a NodeColor for backgrounds
inline NodeColor dim(const NodeColor& c, float alpha = 0.25f) {
    return { c.r, c.g, c.b, alpha };
}

// Severity colors
inline uint32_t severity_color_u32(Severity s) {
    switch (s) {
        case Severity::Fatal:   return 0xFF2020FF; // bright red
        case Severity::Error:   return 0xFF4040FF; // red
        case Severity::Warning: return 0xFF00AAFF; // orange-yellow
        case Severity::Info:    return 0xFFCCCCCC; // light grey
        case Severity::Debug:   return 0xFF888888; // grey
        default:                return 0xFFAAAAAA;
    }
}
