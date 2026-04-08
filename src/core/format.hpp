#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

// ------------------------------------------------------------
//  Number formatting utilities
//
//  fmt_count(n)          → std::string  e.g. 1234567 → "1,234,567"
//  fmt_count_buf(n,b,l)  → const char*  writes into caller's buffer
//                          (for use inside snprintf label strings)
// ------------------------------------------------------------

// Write comma-separated integer into `dst` (null-terminated).
// Returns the number of characters written (excluding null terminator).
// dst must be at least 27 bytes (max uint64 with commas + null).
inline size_t fmt_count_into(uint64_t n, char* dst, size_t dst_len) {
    // Build digits right-to-left in a temporary buffer
    char tmp[27];
    size_t pos  = 0;
    size_t digits = 0;

    if (n == 0) {
        tmp[pos++] = '0';
    } else {
        while (n > 0) {
            if (digits > 0 && digits % 3 == 0)
                tmp[pos++] = ',';
            tmp[pos++] = static_cast<char>('0' + (n % 10));
            n /= 10;
            ++digits;
        }
    }

    // Reverse into dst
    size_t written = 0;
    for (size_t i = pos; i > 0 && written + 1 < dst_len; --i) {
        dst[written++] = tmp[i - 1];
    }
    if (written < dst_len) dst[written] = '\0';
    return written;
}

inline std::string fmt_count(uint64_t n) {
    char buf[27];
    fmt_count_into(n, buf, sizeof(buf));
    return buf;
}

// Convenience wrapper: writes into buf, returns buf pointer.
// Intended for use as a snprintf argument.
inline const char* fmt_count_buf(uint64_t n, char* buf, size_t buf_len) {
    fmt_count_into(n, buf, buf_len);
    return buf;
}

// ------------------------------------------------------------
//  fmt_compact — short SI-suffix form for axis / chart labels
//
//  Keeps labels short so they don't clobber each other when
//  counts reach the millions.
//
//    0–999           → "999"
//    1,000–999,999   → "1.2K"  (1 decimal, drop trailing .0)
//    1M–999M         → "1.2M"
//    1B+             → "1.2B"
//
//  Writes into caller-supplied buf (at least 16 bytes).
// ------------------------------------------------------------
inline const char* fmt_compact(uint64_t n, char* buf, size_t buf_len) {
    // Helper: format with 1 decimal, but strip trailing ".0"
    auto fmt1 = [&](double v, char suffix) {
        // At >= 100 the single decimal adds no useful precision
        if (v >= 100.0)
            std::snprintf(buf, buf_len, "%.0f%c", v, suffix);
        else if (static_cast<int>(v * 10 + 0.5) % 10 == 0)
            std::snprintf(buf, buf_len, "%.0f%c", v, suffix);
        else
            std::snprintf(buf, buf_len, "%.1f%c", v, suffix);
    };

    if      (n < 1000ULL)        std::snprintf(buf, buf_len, "%llu",
                                     static_cast<unsigned long long>(n));
    else if (n < 1000000ULL)     fmt1(static_cast<double>(n) / 1e3,  'K');
    else if (n < 1000000000ULL)  fmt1(static_cast<double>(n) / 1e6,  'M');
    else                          fmt1(static_cast<double>(n) / 1e9,  'B');
    return buf;
}
