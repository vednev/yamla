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
        {"serverStatus.connections.active",
            {"Connections Active", "count", false, std::numeric_limits<double>::quiet_NaN()}},

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
        {"serverStatus.wiredTiger.cache.bytes allocated for updates",
            {"WT Cache Updates", "bytes", false, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.cache.bytes belonging to the history store table in the cache",
            {"WT History Store Cache", "bytes", false, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.cache.pages evicted by application threads",
            {"WT App Thread Evictions", "pages/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.cache.application thread time evicting (usecs)",
            {"WT App Thread Eviction Time", "us/s", true, 0.0}},
        {"serverStatus.wiredTiger.cache.eviction worker thread evicting pages",
            {"WT Worker Evictions", "pages/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.cache.pages selected for eviction unable to be evicted",
            {"WT Eviction Failures", "pages/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.cache.forced eviction - pages selected count",
            {"WT Forced Eviction Selected", "pages/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.cache.forced eviction - pages selected unable to be evicted count",
            {"WT Forced Eviction Failed", "pages/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.cache.eviction currently operating in aggressive mode",
            {"WT Eviction Aggressive Mode", "count", false, 100.0}},
        {"serverStatus.wiredTiger.cache.hazard pointer blocked page eviction",
            {"WT Hazard Pointer Blocked", "count/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.cache.checkpoint blocked page eviction",
            {"WT Checkpoint Blocked Eviction", "count/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.cache.eviction empty score",
            {"WT Eviction Empty Score", "count", false, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.cache.the number of times full update inserted to history store",
            {"WT HS Full Updates", "ops/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.cache.history store table on-disk size",
            {"WT HS On-Disk Size", "bytes", false, std::numeric_limits<double>::quiet_NaN()}},

        // WT concurrentTransactions (tickets)
        {"serverStatus.wiredTiger.concurrentTransactions.read.available",
            {"WT Read Tickets Available", "count", false, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.concurrentTransactions.write.available",
            {"WT Write Tickets Available", "count", false, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.concurrentTransactions.read.out",
            {"WT Read Tickets In Use", "count", false, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.concurrentTransactions.write.out",
            {"WT Write Tickets In Use", "count", false, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.concurrentTransactions.read.totalTickets",
            {"WT Read Tickets Total", "count", false, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.concurrentTransactions.write.totalTickets",
            {"WT Write Tickets Total", "count", false, std::numeric_limits<double>::quiet_NaN()}},

        // WT log / journal
        {"serverStatus.wiredTiger.log.log bytes written",
            {"WT Log Written", "bytes/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.log.log sync operations",
            {"WT Log Syncs", "ops/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.log.log sync time duration (usecs)",
            {"WT Log Sync Duration", "us/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.log.yields waiting for previous log file close",
            {"WT Log Yield on Close", "ops/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.log.slot consolidation busy",
            {"WT Log Slot Busy", "ops/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.log.written slots coalesced",
            {"WT Log Slots Coalesced", "ops/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.log.log records compressed",
            {"WT Log Compressed", "records/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.log.log bytes of payload data",
            {"WT Log Payload Bytes", "bytes/s", true, std::numeric_limits<double>::quiet_NaN()}},

        // ---- WT checkpoint ----
        {"serverStatus.wiredTiger.checkpoint.most recent time (msecs)",
            {"Checkpoint Duration", "ms", false, 60000.0}},
        {"serverStatus.wiredTiger.checkpoint.max time (msecs)",
            {"Checkpoint Max Time", "ms", false, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.checkpoint.generation",
            {"Checkpoint Generation", "count", false, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.checkpoint.total time (msecs)",
            {"Checkpoint Total Time", "ms", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.checkpoint.prepare currently running",
            {"Checkpoint Prepare Running", "count", false, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.checkpoint.currently running",
            {"Checkpoint Running", "count", false, std::numeric_limits<double>::quiet_NaN()}},

        // ---- WT lock ----
        {"serverStatus.wiredTiger.lock.schema lock application thread wait time (usecs)",
            {"WT Schema Lock Wait", "us/s", true, 1000000.0}},

        // ---- WT data-handle ----
        {"serverStatus.wiredTiger.data-handle.connection data handles currently active",
            {"WT Active Data Handles", "count", false, 10000.0}},
        {"serverStatus.wiredTiger.data-handle.connection sweep dhandles closed",
            {"WT Sweep Handles Closed", "count/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.data-handle.connection sweep time-of-death sets",
            {"WT Sweep TOD Sets", "count/s", true, std::numeric_limits<double>::quiet_NaN()}},

        // ---- WT session ----
        {"serverStatus.wiredTiger.session.open session count",
            {"WT Open Sessions", "count", false, 19000.0}},

        // ---- WT cursor ----
        {"serverStatus.wiredTiger.cursor.cursor create calls",
            {"WT Cursor Creates", "ops/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.cursor.cursor insert calls",
            {"WT Cursor Inserts", "ops/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.cursor.cursor next calls",
            {"WT Cursor Nexts", "ops/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.cursor.cursor remove calls",
            {"WT Cursor Removes", "ops/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.cursor.cursor search calls",
            {"WT Cursor Searches", "ops/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.cursor.cursor update calls",
            {"WT Cursor Updates", "ops/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.cursor.cached cursor count",
            {"WT Cached Cursors", "count", false, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.cursor.cursor operation restarted",
            {"WT Cursor Restarts", "ops/s", true, std::numeric_limits<double>::quiet_NaN()}},

        // ---- WT transaction ----
        {"serverStatus.wiredTiger.transaction.transactions committed",
            {"WT Txns Committed", "txns/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.transaction.transactions rolled back",
            {"WT Txns Rolled Back", "txns/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.transaction.transaction begins",
            {"WT Txn Begins", "txns/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.transaction.update conflicts",
            {"WT Update Conflicts", "conflicts/s", true, 1000.0}},
        {"serverStatus.wiredTiger.transaction.failures due to history store",
            {"WT HS Failures", "ops/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.transaction.oldest pinned transaction ID rolled back for eviction",
            {"WT Oldest Pinned Txn Rolled Back", "count/s", true, 100.0}},
        {"serverStatus.wiredTiger.transaction.transaction range of timestamps currently pinned",
            {"WT Timestamp Range Pinned", "count", false, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.wiredTiger.transaction.transaction range of timestamps pinned by the oldest timestamp",
            {"WT Oldest TS Range Pinned", "count", false, std::numeric_limits<double>::quiet_NaN()}},

        // ---- WT thread-yield ----
        {"serverStatus.wiredTiger.thread-yield.application thread time waiting for cache (usecs)",
            {"WT App Thread Cache Wait", "us/s", true, std::numeric_limits<double>::quiet_NaN()}},

        // ---- serverStatus.tcmalloc ----
        {"serverStatus.tcmalloc.generic.current_allocated_bytes",
            {"tcmalloc Allocated", "bytes", false, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.tcmalloc.generic.heap_size",
            {"tcmalloc Heap Size", "bytes", false, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.tcmalloc.tcmalloc.pageheap_free_bytes",
            {"tcmalloc Pageheap Free", "bytes", false, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.tcmalloc.tcmalloc.total_free_bytes",
            {"tcmalloc Total Free", "bytes", false, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.tcmalloc.tcmalloc.central_cache_free_bytes",
            {"tcmalloc Central Cache Free", "bytes", false, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.tcmalloc.tcmalloc.transfer_cache_free_bytes",
            {"tcmalloc Transfer Cache Free", "bytes", false, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.tcmalloc.tcmalloc.thread_cache_free_bytes",
            {"tcmalloc Thread Cache Free", "bytes", false, std::numeric_limits<double>::quiet_NaN()}},

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

        // ---- serverStatus.storageEngine ----
        {"serverStatus.storageEngine.dropPendingIdents",
            {"Drop Pending Idents", "count", false, std::numeric_limits<double>::quiet_NaN()}},

        // ---- serverStatus.repl.buffer ----
        {"serverStatus.repl.buffer.count",
            {"Repl Buffer Count", "count", false, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.repl.buffer.sizeBytes",
            {"Repl Buffer Size", "bytes", false, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.repl.buffer.maxSizeBytes",
            {"Repl Buffer Max", "bytes", false, std::numeric_limits<double>::quiet_NaN()}},

        // ---- serverStatus.metrics.repl ----
        {"serverStatus.metrics.repl.apply.ops",
            {"Repl Apply Ops", "ops/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.metrics.repl.apply.batches.totalMillis",
            {"Repl Apply Batch Time", "ms/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.metrics.repl.network.getmores.num",
            {"Repl Getmores", "ops/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.metrics.repl.network.getmores.totalMillis",
            {"Repl Getmore Time", "ms/s", true, std::numeric_limits<double>::quiet_NaN()}},
        {"serverStatus.metrics.repl.network.bytes",
            {"Repl Network Bytes", "bytes/s", true, std::numeric_limits<double>::quiet_NaN()}},

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
        {"systemMetrics.cpu.nice_ms",
            {"CPU Nice", "ms/s", true, std::numeric_limits<double>::quiet_NaN()}},

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

// ------------------------------------------------------------
//  Disk I/O pattern-matching helpers
//  Disk device names are dynamic (sda, xvda, nvme0n1, etc.).
//  These helpers match "systemMetrics.disks.*.{suffix}" paths
//  so any device name is recognized without a static table entry.
// ------------------------------------------------------------
inline bool is_disk_metric(const std::string& path) {
    static const char* prefix = "systemMetrics.disks.";
    if (path.compare(0, 20, prefix) != 0) return false;
    // Find last dot — suffix is the metric name after the device
    auto last_dot = path.rfind('.');
    if (last_dot == std::string::npos || last_dot <= 20) return false;
    std::string_view suffix(path.data() + last_dot + 1, path.size() - last_dot - 1);
    return suffix == "reads" || suffix == "writes" ||
           suffix == "read_time_ms" || suffix == "write_time_ms" ||
           suffix == "io_queued_ms" || suffix == "io_time_ms" ||
           suffix == "sectors_read" || suffix == "sectors_written";
}

// Return a MetricDef for a dynamically-discovered disk metric.
// Called when the static table has no entry for a disk metric path.
inline MetricDef disk_metric_def(const std::string& path) {
    auto last_dot = path.rfind('.');
    std::string_view suffix(path.data() + last_dot + 1, path.size() - last_dot - 1);
    if (suffix == "reads")           return {"Disk Reads",      "ops/s",  true,  std::numeric_limits<double>::quiet_NaN()};
    if (suffix == "writes")          return {"Disk Writes",     "ops/s",  true,  std::numeric_limits<double>::quiet_NaN()};
    if (suffix == "read_time_ms")    return {"Disk Read Time",  "ms/s",   true,  std::numeric_limits<double>::quiet_NaN()};
    if (suffix == "write_time_ms")   return {"Disk Write Time", "ms/s",   true,  std::numeric_limits<double>::quiet_NaN()};
    if (suffix == "io_queued_ms")    return {"Disk I/O Queue",  "ms/s",   true,  500.0};
    if (suffix == "io_time_ms")      return {"Disk I/O Time",   "ms/s",   true,  std::numeric_limits<double>::quiet_NaN()};
    if (suffix == "sectors_read")    return {"Disk Sectors Read",    "sectors/s", true, std::numeric_limits<double>::quiet_NaN()};
    if (suffix == "sectors_written") return {"Disk Sectors Written", "sectors/s", true, std::numeric_limits<double>::quiet_NaN()};
    return {"Disk Metric", "count", false, std::numeric_limits<double>::quiet_NaN()};
}

// Lookup helpers — return fallback values if path not in table
inline const MetricDef* find_metric_def(const std::string& path) {
    const auto& t = metric_defs();
    auto it = t.find(path);
    if (it != t.end()) return &it->second;
    // Dynamic disk metric fallback
    if (is_disk_metric(path)) {
        static thread_local MetricDef tl_disk_def;
        tl_disk_def = disk_metric_def(path);
        return &tl_disk_def;
    }
    return nullptr;
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
//  Disk I/O dashboard constant
//  MetricTreeView detects this name and dynamically populates
//  the dashboard from the store's ordered_keys using is_disk_metric().
// ------------------------------------------------------------
inline constexpr const char* DISK_IO_DASHBOARD_NAME = "Disk I/O";

// ------------------------------------------------------------
//  Preset dashboard metric path lists — 15 curated categories
// ------------------------------------------------------------
inline const std::vector<PresetDashboard>& preset_dashboards() {
    static const std::vector<PresetDashboard> presets = {
        // Dashboard 1: Overview
        {
            "Overview",
            {
                "serverStatus.opcounters.insert",
                "serverStatus.opcounters.query",
                "serverStatus.opcounters.update",
                "serverStatus.opcounters.delete",
                "serverStatus.connections.current",
                "serverStatus.mem.resident",
                "serverStatus.wiredTiger.cache.bytes currently in the cache",
                "serverStatus.wiredTiger.cache.maximum bytes configured",
                "serverStatus.wiredTiger.cache.tracked dirty bytes in the cache",
                "systemMetrics.cpu.user_ms",
                "systemMetrics.cpu.iowait_ms",
                "serverStatus.opLatencies.reads.latency",
                "serverStatus.opLatencies.writes.latency",
                "serverStatus.wiredTiger.concurrentTransactions.write.available",
                "serverStatus.wiredTiger.concurrentTransactions.read.available",
            }
        },
        // Dashboard 2: CPU & System
        {
            "CPU & System",
            {
                "systemMetrics.cpu.user_ms",
                "systemMetrics.cpu.sys_ms",
                "systemMetrics.cpu.idle_ms",
                "systemMetrics.cpu.iowait_ms",
                "systemMetrics.cpu.steal_ms",
                "systemMetrics.cpu.irq_ms",
                "systemMetrics.cpu.softirq_ms",
                "systemMetrics.cpu.nice_ms",
            }
        },
        // Dashboard 3: Memory & tcmalloc
        {
            "Memory & tcmalloc",
            {
                "serverStatus.mem.resident",
                "serverStatus.mem.virtual",
                "serverStatus.tcmalloc.generic.current_allocated_bytes",
                "serverStatus.tcmalloc.generic.heap_size",
                "serverStatus.tcmalloc.tcmalloc.pageheap_free_bytes",
                "serverStatus.tcmalloc.tcmalloc.total_free_bytes",
                "serverStatus.tcmalloc.tcmalloc.central_cache_free_bytes",
                "serverStatus.tcmalloc.tcmalloc.transfer_cache_free_bytes",
                "serverStatus.tcmalloc.tcmalloc.thread_cache_free_bytes",
                "systemMetrics.memory.total_kb",
                "systemMetrics.memory.free_kb",
                "systemMetrics.memory.cached_kb",
                "systemMetrics.memory.buffers_kb",
            }
        },
        // Dashboard 4: WiredTiger Cache
        {
            "WiredTiger Cache",
            {
                "serverStatus.wiredTiger.cache.bytes currently in the cache",
                "serverStatus.wiredTiger.cache.maximum bytes configured",
                "serverStatus.wiredTiger.cache.tracked dirty bytes in the cache",
                "serverStatus.wiredTiger.cache.bytes allocated for updates",
                "serverStatus.wiredTiger.cache.bytes read into cache",
                "serverStatus.wiredTiger.cache.bytes written from cache",
                "serverStatus.wiredTiger.cache.pages read into cache",
                "serverStatus.wiredTiger.cache.pages written from cache",
                "serverStatus.wiredTiger.cache.bytes belonging to the history store table in the cache",
            }
        },
        // Dashboard 5: Eviction
        {
            "Eviction",
            {
                "serverStatus.wiredTiger.cache.unmodified pages evicted",
                "serverStatus.wiredTiger.cache.modified pages evicted",
                "serverStatus.wiredTiger.cache.pages evicted by application threads",
                "serverStatus.wiredTiger.cache.application thread time evicting (usecs)",
                "serverStatus.wiredTiger.cache.eviction worker thread evicting pages",
                "serverStatus.wiredTiger.cache.pages selected for eviction unable to be evicted",
                "serverStatus.wiredTiger.cache.forced eviction - pages selected count",
                "serverStatus.wiredTiger.cache.forced eviction - pages selected unable to be evicted count",
                "serverStatus.wiredTiger.cache.eviction currently operating in aggressive mode",
                "serverStatus.wiredTiger.cache.hazard pointer blocked page eviction",
                "serverStatus.wiredTiger.cache.checkpoint blocked page eviction",
                "serverStatus.wiredTiger.cache.eviction empty score",
                "serverStatus.wiredTiger.thread-yield.application thread time waiting for cache (usecs)",
            }
        },
        // Dashboard 6: Tickets / Admission Control
        {
            "Tickets",
            {
                "serverStatus.wiredTiger.concurrentTransactions.read.available",
                "serverStatus.wiredTiger.concurrentTransactions.read.out",
                "serverStatus.wiredTiger.concurrentTransactions.read.totalTickets",
                "serverStatus.wiredTiger.concurrentTransactions.write.available",
                "serverStatus.wiredTiger.concurrentTransactions.write.out",
                "serverStatus.wiredTiger.concurrentTransactions.write.totalTickets",
                "serverStatus.globalLock.currentQueue.readers",
                "serverStatus.globalLock.currentQueue.writers",
                "serverStatus.globalLock.activeClients.readers",
                "serverStatus.globalLock.activeClients.writers",
            }
        },
        // Dashboard 7: Operations
        {
            "Operations",
            {
                "serverStatus.opcounters.insert",
                "serverStatus.opcounters.query",
                "serverStatus.opcounters.update",
                "serverStatus.opcounters.delete",
                "serverStatus.opcounters.getmore",
                "serverStatus.opcounters.command",
                "serverStatus.opLatencies.reads.latency",
                "serverStatus.opLatencies.reads.ops",
                "serverStatus.opLatencies.writes.latency",
                "serverStatus.opLatencies.writes.ops",
                "serverStatus.opLatencies.commands.latency",
                "serverStatus.opLatencies.commands.ops",
                "serverStatus.metrics.document.inserted",
                "serverStatus.metrics.document.updated",
                "serverStatus.metrics.document.deleted",
                "serverStatus.metrics.document.returned",
                "serverStatus.metrics.operation.writeConflicts",
                "serverStatus.metrics.operation.scanAndOrder",
            }
        },
        // Dashboard 8: Checkpoints
        {
            "Checkpoints",
            {
                "serverStatus.wiredTiger.checkpoint.most recent time (msecs)",
                "serverStatus.wiredTiger.checkpoint.max time (msecs)",
                "serverStatus.wiredTiger.checkpoint.generation",
                "serverStatus.wiredTiger.checkpoint.total time (msecs)",
                "serverStatus.wiredTiger.checkpoint.currently running",
                "serverStatus.wiredTiger.checkpoint.prepare currently running",
                "serverStatus.wiredTiger.lock.schema lock application thread wait time (usecs)",
                "serverStatus.wiredTiger.data-handle.connection data handles currently active",
                "serverStatus.storageEngine.dropPendingIdents",
            }
        },
        // Dashboard 9: Replication
        {
            "Replication",
            {
                "serverStatus.opcountersRepl.insert",
                "serverStatus.opcountersRepl.update",
                "serverStatus.opcountersRepl.delete",
                "serverStatus.repl.buffer.count",
                "serverStatus.repl.buffer.sizeBytes",
                "serverStatus.repl.buffer.maxSizeBytes",
                "serverStatus.metrics.repl.apply.ops",
                "serverStatus.metrics.repl.apply.batches.totalMillis",
                "serverStatus.metrics.repl.network.getmores.num",
                "serverStatus.metrics.repl.network.getmores.totalMillis",
                "serverStatus.metrics.repl.network.bytes",
                "replSetGetStatus.members.0.optimeDate",
            }
        },
        // Dashboard 10: Network & Connections
        {
            "Network",
            {
                "serverStatus.network.bytesIn",
                "serverStatus.network.bytesOut",
                "serverStatus.network.numRequests",
                "serverStatus.connections.current",
                "serverStatus.connections.available",
                "serverStatus.connections.totalCreated",
                "serverStatus.connections.active",
            }
        },
        // Dashboard 11: Disk I/O (dynamic — MetricTreeView populates from store)
        {
            DISK_IO_DASHBOARD_NAME, {}
        },
        // Dashboard 12: Journal
        {
            "Journal",
            {
                "serverStatus.wiredTiger.log.log bytes written",
                "serverStatus.wiredTiger.log.log sync operations",
                "serverStatus.wiredTiger.log.log sync time duration (usecs)",
                "serverStatus.wiredTiger.log.yields waiting for previous log file close",
                "serverStatus.wiredTiger.log.slot consolidation busy",
                "serverStatus.wiredTiger.log.written slots coalesced",
                "serverStatus.wiredTiger.log.log records compressed",
                "serverStatus.wiredTiger.log.log bytes of payload data",
            }
        },
        // Dashboard 13: History Store
        {
            "History Store",
            {
                "serverStatus.wiredTiger.cache.bytes belonging to the history store table in the cache",
                "serverStatus.wiredTiger.cache.the number of times full update inserted to history store",
                "serverStatus.wiredTiger.cache.history store table on-disk size",
                "serverStatus.wiredTiger.transaction.transaction range of timestamps currently pinned",
                "serverStatus.wiredTiger.transaction.transaction range of timestamps pinned by the oldest timestamp",
            }
        },
        // Dashboard 14: Cursors & Data Handles
        {
            "Cursors & Handles",
            {
                "serverStatus.wiredTiger.data-handle.connection data handles currently active",
                "serverStatus.wiredTiger.data-handle.connection sweep dhandles closed",
                "serverStatus.wiredTiger.data-handle.connection sweep time-of-death sets",
                "serverStatus.wiredTiger.cursor.cursor create calls",
                "serverStatus.wiredTiger.cursor.cursor insert calls",
                "serverStatus.wiredTiger.cursor.cursor next calls",
                "serverStatus.wiredTiger.cursor.cursor remove calls",
                "serverStatus.wiredTiger.cursor.cursor search calls",
                "serverStatus.wiredTiger.cursor.cursor update calls",
                "serverStatus.wiredTiger.cursor.cached cursor count",
                "serverStatus.wiredTiger.cursor.cursor operation restarted",
                "serverStatus.wiredTiger.session.open session count",
            }
        },
        // Dashboard 15: Transactions
        {
            "Transactions",
            {
                "serverStatus.wiredTiger.transaction.transactions committed",
                "serverStatus.wiredTiger.transaction.transactions rolled back",
                "serverStatus.wiredTiger.transaction.transaction begins",
                "serverStatus.wiredTiger.transaction.update conflicts",
                "serverStatus.wiredTiger.transaction.failures due to history store",
                "serverStatus.wiredTiger.transaction.oldest pinned transaction ID rolled back for eviction",
                "serverStatus.metrics.operation.writeConflicts",
            }
        },
    };
    return presets;
}
