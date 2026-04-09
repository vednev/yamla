#pragma once

#include <cstddef>

// ------------------------------------------------------------
//  query_total_ram()
//
//  Returns the total physical RAM installed in the machine, in
//  bytes.  Returns 0 if the query fails.
//
//  macOS:  sysctl hw.memsize
//  Linux:  parse /proc/meminfo MemTotal
//  Windows: GlobalMemoryStatusEx
// ------------------------------------------------------------

#if defined(__APPLE__)
#  include <sys/sysctl.h>
inline size_t query_total_ram() {
    int64_t mem = 0;
    size_t  len = sizeof(mem);
    if (::sysctlbyname("hw.memsize", &mem, &len, nullptr, 0) == 0)
        return static_cast<size_t>(mem);
    return 0;
}

#elif defined(__linux__)
#  include <cstdio>
inline size_t query_total_ram() {
    FILE* f = std::fopen("/proc/meminfo", "r");
    if (!f) return 0;
    char key[64];
    unsigned long long val = 0;
    while (std::fscanf(f, "%63s %llu %*s\n", key, &val) == 2) {
        if (std::strcmp(key, "MemTotal:") == 0) {
            std::fclose(f);
            return static_cast<size_t>(val) * 1024; // kB → bytes
        }
    }
    std::fclose(f);
    return 0;
}

#elif defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
inline size_t query_total_ram() {
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms))
        return static_cast<size_t>(ms.ullTotalPhys);
    return 0;
}

#else
inline size_t query_total_ram() { return 0; }
#endif
