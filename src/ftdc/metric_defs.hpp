#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <cmath>    // NAN
#include <limits>

// ------------------------------------------------------------
//  MetricDef — static definition for a known MongoDB FTDC metric
// ------------------------------------------------------------
struct MetricDef {
    const char* display_name;  // human-readable label
    const char* unit;          // "bytes", "ms", "count", "ratio", "%", "ops/s"
    bool        is_cumulative; // true = delta/s is the primary view
    double      threshold;     // anomaly threshold (NAN = no threshold)
    // Threshold meaning: value > threshold is anomalous (for gauges)
    //                    rate  > threshold is anomalous (for cumulative)
};

// ------------------------------------------------------------
//  Preset dashboard definitions
// ------------------------------------------------------------
struct PresetDashboard {
    const char* name;
    std::vector<const char*> metric_paths;
};

// ------------------------------------------------------------
//  Global metric definition table
//  Key: FTDC metric path (dot-separated), Value: MetricDef
// ------------------------------------------------------------
inline const std::unordered_map<std::string, MetricDef>& metric_defs() {
    static const std::unordered_map<std::string, MetricDef> table = {
        // ---- serverStatus.host / process ----
        {"serverStatus.uptime",
            {"Uptime", "s", false, std::numeric_limits<double>::quiet_NaN()}},

        // ---- serverStatus.opcounters ----
        {"serverStatus.opcounters.insert",
            {"Inserts", "ops/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.opcounters.query",
            {"Queries", "ops/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.opcounters.update",
            {"Updates", "ops/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.opcounters.delete",
            {"Deletes", "ops/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.opcounters.getmore",
            {"Getmore", "ops/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.opcounters.command",
            {"Commands", "ops/s", true, std::numeric_limits<double>::quiet_NaN()}},

        // ---- serverStatus.opcountersRepl ----
        {"serverStatus.opcountersRepl.insert",
            {"Repl Inserts", "ops/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.opcountersRepl.update",
            {"Repl Updates", "ops/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.opcountersRepl.delete",
            {"Repl Deletes", "ops/s", true, std::numeric_limits<double>::quiet_NaN()}},

        // ---- serverStatus.connections ----
        {"serverStatus.connections.current",
            {"Connections Current", "count", false, 5000.0}},
        {"serverStatus.connections.available",
            {"Connections Available", "count", false, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.connections.totalCreated",
            {"Connections Created", "conns/s", true, std::numeric_limits<double>::quiet_NaN()}},

        // ---- serverStatus.network ----
        {"serverStatus.network.bytesIn",
            {"Network In", "bytes/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.network.bytesOut",
            {"Network Out", "bytes/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.network.numRequests",
            {"Network Requests", "req/s", true, std::numeric_limits<double>::quiet_NaN()}},

        // ---- serverStatus.mem (values are in megabytes) ----
        {"serverStatus.mem.resident",
            {"RSS Memory", "MB", false, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.mem.virtual",
            {"Virtual Memory", "MB", false, std::numeric_limits<double>::quiet_NaN()}},

        // ---- serverStatus.globalLock ----
        {"serverStatus.globalLock.currentQueue.readers",
            {"Queued Readers", "count", false, 10.0}},
        {"serverStatus.globalLock.currentQueue.writers",
            {"Queued Writers", "count", false, 10.0}},
        {"serverStatus.globalLock.activeClients.readers",
            {"Active Readers", "count", false, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.globalLock.activeClients.writers",
            {"Active Writers", "count", false, std::numeric_limits<double>::quiet_NaN()}},

        // ---- serverStatus.opLatencies ----
        {"serverStatus.opLatencies.reads.latency",
            {"Read Latency", "us", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.opLatencies.reads.ops",
            {"Read Ops", "ops/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.opLatencies.writes.latency",
            {"Write Latency", "us", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.opLatencies.writes.ops",
            {"Write Ops", "ops/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.opLatencies.commands.latency",
            {"Command Latency", "us", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.opLatencies.commands.ops",
            {"Command Ops", "ops/s", true, std::numeric_limits<double>::quiet_NaN()}},

        // ---- serverStatus.wiredTiger.cache ----
        {"serverStatus.wiredTiger.cache.bytes currently in the cache",
            {"WT Cache Used", "bytes", false, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.cache.maximum bytes configured",
            {"WT Cache Max", "bytes", false, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.cache.tracked dirty bytes in the cache",
            {"WT Cache Dirty", "bytes", false, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.cache.bytes read into cache",
            {"WT Cache Reads", "bytes/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.cache.bytes written from cache",
            {"WT Cache Writes", "bytes/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.cache.pages read into cache",
            {"WT Pages Read", "pages/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.cache.pages written from cache",
            {"WT Pages Written", "pages/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.cache.unmodified pages evicted",
            {"WT Pages Evicted (clean)", "pages/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.cache.modified pages evicted",
            {"WT Pages Evicted (dirty)", "pages/s", true, std::numeric_limits<double>::quiet_NaN()}},

        // WT concurrentTransactions (tickets)
        {"serverStatus.wiredTiger.concurrentTransactions.read.available",
            {"WT Read Tickets Available", "count", false, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.concurrentTransactions.write.available",
            {"WT Write Tickets Available", "count", false, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.concurrentTransactions.read.out",
            {"WT Read Tickets In Use", "count", false, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.concurrentTransactions.write.out",
            {"WT Write Tickets In Use", "count", false, std::numeric_limits<double>::quiet_NaN()}},

        // WT log
        {"serverStatus.wiredTiger.log.log bytes written",
            {"WT Log Written", "bytes/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.log.log sync operations",
            {"WT Log Syncs", "ops/s", true, std::numeric_limits<double>::quiet_NaN()}},

        // ---- serverStatus.metrics.document ----
        {"serverStatus.metrics.document.deleted",
            {"Docs Deleted", "docs/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.metrics.document.inserted",
            {"Docs Inserted", "docs/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.metrics.document.returned",
            {"Docs Returned", "docs/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.metrics.document.updated",
            {"Docs Updated", "docs/s", true, std::numeric_limits<double>::quiet_NaN()}},

        // ---- serverStatus.metrics.operation ----
        {"serverStatus.metrics.operation.scanAndOrder",
            {"Scan and Order", "ops/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.metrics.operation.writeConflicts",
            {"Write Conflicts", "ops/s", true, 100.0}},

        // ---- serverStatus.repl.buffer ----
        {"serverStatus.repl.buffer.count",
            {"Repl Buffer Count", "count", false, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.repl.buffer.sizeBytes",
            {"Repl Buffer Size", "bytes", false, std::numeric_limits<double>::quiet_NaN()}},

        // ---- replSetGetStatus.members (lag) ----
        {"replSetGetStatus.members.0.optimeDate",
            {"Primary Optime", "ms", false, std::numeric_limits<double>::quiet_NaN()}},

        // ---- systemMetrics.cpu ----
        {"systemMetrics.cpu.user_ms",
            {"CPU User", "ms/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"systemMetrics.cpu.sys_ms",
            {"CPU Sys", "ms/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"systemMetrics.cpu.idle_ms",
            {"CPU Idle", "ms/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"systemMetrics.cpu.iowait_ms",
            {"CPU I/O Wait", "ms/s", true, 200.0}},
        {"systemMetrics.cpu.irq_ms",
            {"CPU IRQ", "ms/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"systemMetrics.cpu.softirq_ms",
            {"CPU SoftIRQ", "ms/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"systemMetrics.cpu.steal_ms",
            {"CPU Steal", "ms/s", true, 100.0}},  // steal > 0 = VM contention

        // ---- systemMetrics.memory (values are in kilobytes) ----
        {"systemMetrics.memory.total_kb",
            {"Total Memory", "KB", false, std::numeric_limits<double>::quiet_NaN()}},
        {"systemMetrics.memory.free_kb",
            {"Free Memory", "KB", false, std::numeric_limits<double>::quiet_NaN()}},
        {"systemMetrics.memory.cached_kb",
            {"Cached Memory", "KB", false, std::numeric_limits<double>::quiet_NaN()}},
        {"systemMetrics.memory.buffers_kb",
            {"Buffer Memory", "KB", false, std::numeric_limits<double>::quiet_NaN()}},

        // ---- systemMetrics.disk ----
        {"systemMetrics.disks.sda.read_time_ms",
            {"Disk Read Time", "ms/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"systemMetrics.disks.sda.write_time_ms",
            {"Disk Write Time", "ms/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"systemMetrics.disks.sda.reads",
            {"Disk Reads", "ops/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"systemMetrics.disks.sda.writes",
            {"Disk Writes", "ops/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"systemMetrics.disks.sda.io_queued_ms",
            {"Disk I/O Queue", "ms/s", true, 500.0}},
    };
    return table;
}

// Lookup helpers — return fallback values if path not in table
inline const MetricDef* find_metric_def(const std::string& path) {
    const auto& t = metric_defs();
    auto it = t.find(path);
    return it != t.end() ? &it->second : nullptr;
}

// Return a human-readable display name for any metric path.
// If the path is in the table, use the table's name.
// Otherwise, derive a name from the last two path components.
inline std::string metric_display_name(const std::string& path) {
    if (const MetricDef* d = find_metric_def(path))
        return d->display_name;
    // Fallback: last two dot-separated components
    auto p = path.rfind('.');
    if (p != std::string::npos && p > 0) {
        auto p2 = path.rfind('.', p - 1);
        if (p2 != std::string::npos)
            return path.substr(p2 + 1);
    }
    return path;
}

inline std::string metric_unit(const std::string& path) {
    if (const MetricDef* d = find_metric_def(path))
        return d->unit;
    return "count";
}

inline bool metric_is_cumulative(const std::string& path) {
    if (const MetricDef* d = find_metric_def(path))
        return d->is_cumulative;
    return false;
}

inline double metric_threshold(const std::string& path) {
    if (const MetricDef* d = find_metric_def(path))
        return d->threshold;
    return std::numeric_limits<double>::quiet_NaN();
}

// ------------------------------------------------------------
//  Preset dashboard metric path lists
// ------------------------------------------------------------
inline const std::vector<PresetDashboard>& preset_dashboards() {
    static const std::vector<PresetDashboard> presets = {
        {
            "Overview",
            {
                "systemMetrics.cpu.user_ms",
                "systemMetrics.cpu.iowait_ms",
                "serverStatus.mem.resident",
                "serverStatus.connections.current",
                "serverStatus.opcounters.insert",
                "serverStatus.opcounters.query",
                "serverStatus.opcounters.update",
                "serverStatus.opcounters.delete",
            }
        },
        {
            "CPU & System",
            {
                "systemMetrics.cpu.user_ms",
                "systemMetrics.cpu.sys_ms",
                "systemMetrics.cpu.iowait_ms",
                "systemMetrics.cpu.steal_ms",
                "systemMetrics.cpu.idle_ms",
                "systemMetrics.cpu.irq_ms",
            }
        },
        {
            "Memory",
            {
                "serverStatus.mem.resident",
                "serverStatus.mem.virtual",
                "systemMetrics.memory.free_kb",
                "systemMetrics.memory.cached_kb",
                "serverStatus.wiredTiger.cache.bytes currently in the cache",
                "serverStatus.wiredTiger.cache.tracked dirty bytes in the cache",
                "serverStatus.wiredTiger.cache.maximum bytes configured",
            }
        },
        {
            "Replication",
            {
                "serverStatus.opcountersRepl.insert",
                "serverStatus.opcountersRepl.update",
                "serverStatus.opcountersRepl.delete",
                "serverStatus.repl.buffer.count",
                "serverStatus.repl.buffer.sizeBytes",
                "serverStatus.metrics.operation.writeConflicts",
            }
        },
        {
            "WiredTiger",
            {
                "serverStatus.wiredTiger.cache.bytes currently in the cache",
                "serverStatus.wiredTiger.cache.tracked dirty bytes in the cache",
                "serverStatus.wiredTiger.cache.bytes read into cache",
                "serverStatus.wiredTiger.cache.bytes written from cache",
                "serverStatus.wiredTiger.cache.modified pages evicted",
                "serverStatus.wiredTiger.cache.unmodified pages evicted",
                "serverStatus.wiredTiger.concurrentTransactions.read.out",
                "serverStatus.wiredTiger.concurrentTransactions.write.out",
                "serverStatus.wiredTiger.log.log bytes written",
            }
        },
        {
            "Operations",
            {
                "serverStatus.opcounters.insert",
                "serverStatus.opcounters.query",
                "serverStatus.opcounters.update",
                "serverStatus.opcounters.delete",
                "serverStatus.opcounters.command",
                "serverStatus.opLatencies.reads.latency",
                "serverStatus.opLatencies.writes.latency",
                "serverStatus.opLatencies.commands.latency",
                "serverStatus.globalLock.currentQueue.readers",
                "serverStatus.globalLock.currentQueue.writers",
            }
        },
        {
            "Network",
            {
                "serverStatus.network.bytesIn",
                "serverStatus.network.bytesOut",
                "serverStatus.network.numRequests",
                "serverStatus.connections.current",
                "serverStatus.connections.available",
                "serverStatus.connections.totalCreated",
            }
        },
    };
    return presets;
}
