#pragma once

#include <cstring>
#include <cstdio>

// ------------------------------------------------------------
//  MongoDB 4.4+ structured log key semantic names
//
//  log_key_label(key, context)
//
//  Returns a display label for a JSON key given its position in
//  the document tree.  `context` is the dot-separated path of
//  ancestor keys as built by the recursive renderer, e.g.:
//
//    "document"                          → top-level key
//    "document.attr"                     → inside attr object
//    "document.attr.locks"               → inside locks object
//    "document.attr.locks.Global.acquireCount" → lock mode key
//
//  Return format:
//    Known key  → "semantic name (raw)"   e.g. "timestamp (t)"
//    Unknown    → "raw"                   unchanged, no suffix
//
//  All returned pointers are into static storage or the input
//  buffer — no heap allocation.
// ------------------------------------------------------------

namespace detail {

// Returns the last path component of a dot-separated context string.
inline const char* last_component(const char* ctx) {
    if (!ctx || !*ctx) return "";
    const char* last = ctx;
    for (const char* p = ctx; *p; ++p)
        if (*p == '.' ) last = p + 1;
    return last;
}

// True if `ctx` contains the substring `sub` as a full path component.
inline bool ctx_contains(const char* ctx, const char* sub) {
    if (!ctx || !sub) return false;
    const char* p = ctx;
    size_t slen = std::strlen(sub);
    while ((p = std::strstr(p, sub)) != nullptr) {
        // Check that the match is bounded by '.' or start/end
        bool left_ok  = (p == ctx || *(p - 1) == '.');
        bool right_ok = (p[slen] == '\0' || p[slen] == '.');
        if (left_ok && right_ok) return true;
        ++p;
    }
    return false;
}

// True if `ctx` ends with the given component.
inline bool ctx_ends_with(const char* ctx, const char* suffix) {
    if (!ctx || !suffix) return false;
    return std::strcmp(last_component(ctx), suffix) == 0;
}

// Scratch buffer used to build "semantic name (raw)" labels.
// Single-threaded (main thread only for rendering) — safe.
static char g_label_buf[256];

// Build and return "semantic (raw)" label.
inline const char* make_label(const char* semantic, const char* raw) {
    std::snprintf(g_label_buf, sizeof(g_label_buf), "%s  (%s)", semantic, raw);
    return g_label_buf;
}

} // namespace detail


inline const char* log_key_label(const char* key, const char* context = nullptr) {
    if (!key || !*key) return key;

    const char* ctx = context ? context : "";

    // ---- Lock mode single-character codes ----------------------
    // These appear as keys inside acquireCount / acquireWaitCount /
    // timeAcquiringMicros objects nested under locks.<resource>.
    bool in_lock_stat = detail::ctx_ends_with(ctx, "acquireCount")   ||
                        detail::ctx_ends_with(ctx, "acquireWaitCount")||
                        detail::ctx_ends_with(ctx, "timeAcquiringMicros");
    if (in_lock_stat && key[1] == '\0') {
        switch (key[0]) {
            case 'r': return detail::make_label("IS - Intent Shared",     "r");
            case 'w': return detail::make_label("IX - Intent Exclusive",  "w");
            case 'R': return detail::make_label("S  - Shared",            "R");
            case 'W': return detail::make_label("X  - Exclusive",         "W");
        }
    }

    // ---- Keys inside locks.<resource> objects ------------------
    if (detail::ctx_contains(ctx, "locks") &&
        !detail::ctx_ends_with(ctx, "locks"))
    {
        if (!std::strcmp(key, "acquireCount"))
            return detail::make_label("times acquired",                   "acquireCount");
        if (!std::strcmp(key, "acquireWaitCount"))
            return detail::make_label("times waited to acquire",          "acquireWaitCount");
        if (!std::strcmp(key, "timeAcquiringMicros"))
            return detail::make_label("wait time (us)",                   "timeAcquiringMicros");
        if (!std::strcmp(key, "deadlockCount"))
            return detail::make_label("deadlocks",                        "deadlockCount");
    }

    // ---- Lock resource type names (direct children of locks) ---
    if (detail::ctx_ends_with(ctx, "locks")) {
        if (!std::strcmp(key, "ParallelBatchWriterMode"))
            return detail::make_label("Parallel Batch Writer Mode lock",  "ParallelBatchWriterMode");
        if (!std::strcmp(key, "ReplicationStateTransition"))
            return detail::make_label("Replication State Transition lock","ReplicationStateTransition");
        if (!std::strcmp(key, "Global"))
            return detail::make_label("Global lock",                      "Global");
        if (!std::strcmp(key, "Database"))
            return detail::make_label("Database lock",                    "Database");
        if (!std::strcmp(key, "Collection"))
            return detail::make_label("Collection lock",                  "Collection");
        if (!std::strcmp(key, "Mutex"))
            return detail::make_label("Mutex lock",                       "Mutex");
        if (!std::strcmp(key, "oplog"))
            return detail::make_label("Oplog lock",                       "oplog");
    }

    // ---- Keys inside attr.storage ------------------------------
    if (detail::ctx_contains(ctx, "storage")) {
        if (!std::strcmp(key, "data"))
            return detail::make_label("storage I/O stats",                "data");
        if (!std::strcmp(key, "bytesRead"))
            return detail::make_label("bytes read",                       "bytesRead");
        if (!std::strcmp(key, "bytesWritten"))
            return detail::make_label("bytes written",                    "bytesWritten");
        if (!std::strcmp(key, "timeReadingMicros"))
            return detail::make_label("read time (us)",                   "timeReadingMicros");
        if (!std::strcmp(key, "timeWritingMicros"))
            return detail::make_label("write time (us)",                  "timeWritingMicros");
        if (!std::strcmp(key, "timeWaitingMicros"))
            return detail::make_label("storage wait time (us)",           "timeWaitingMicros");
        if (!std::strcmp(key, "cache"))
            return detail::make_label("WT cache wait (us)",               "cache");
        if (!std::strcmp(key, "schemaLock"))
            return detail::make_label("WT schema lock wait (us)",         "schemaLock");
        if (!std::strcmp(key, "handleLock"))
            return detail::make_label("WT handle lock wait (us)",         "handleLock");
    }

    // ---- Keys inside attr.queues -------------------------------
    if (detail::ctx_contains(ctx, "queues")) {
        if (!std::strcmp(key, "admissions"))
            return detail::make_label("queue admissions",                 "admissions");
        if (!std::strcmp(key, "totalTimeQueuedMicros"))
            return detail::make_label("total queue wait (us)",            "totalTimeQueuedMicros");
        if (!std::strcmp(key, "ingress"))
            return detail::make_label("ingress queue",                    "ingress");
        if (!std::strcmp(key, "execution"))
            return detail::make_label("execution queue",                  "execution");
    }

    // ---- Keys inside attr.flowControl --------------------------
    if (detail::ctx_contains(ctx, "flowControl")) {
        if (!std::strcmp(key, "acquireCount"))
            return detail::make_label("ticket acquisitions",              "acquireCount");
        if (!std::strcmp(key, "acquireWaitCount"))
            return detail::make_label("times waited for ticket",          "acquireWaitCount");
        if (!std::strcmp(key, "timeAcquiringMicros"))
            return detail::make_label("ticket wait time (us)",            "timeAcquiringMicros");
    }

    // ---- attr children (direct children of the attr object) ----
    if (detail::ctx_ends_with(ctx, "attr") ||
        detail::ctx_ends_with(ctx, "document")) // also top-level attr-like fields
    {
        // Query identity
        if (!std::strcmp(key, "ns"))            return detail::make_label("namespace",                   "ns");
        if (!std::strcmp(key, "type"))          return detail::make_label("operation type",               "type");
        if (!std::strcmp(key, "appName"))       return detail::make_label("application name",             "appName");
        if (!std::strcmp(key, "remote"))        return detail::make_label("remote address",               "remote");
        if (!std::strcmp(key, "client"))        return detail::make_label("client connection",            "client");
        if (!std::strcmp(key, "connectionId"))  return detail::make_label("connection ID",                "connectionId");
        if (!std::strcmp(key, "protocol"))      return detail::make_label("wire protocol",                "protocol");
        if (!std::strcmp(key, "collectionType"))return detail::make_label("collection type",              "collectionType");
        if (!std::strcmp(key, "isFromUserConnection"))
            return detail::make_label("from user connection",             "isFromUserConnection");

        // Command / query content
        if (!std::strcmp(key, "command"))       return detail::make_label("command document",             "command");
        if (!std::strcmp(key, "originatingCommand"))
            return detail::make_label("originating command",              "originatingCommand");
        if (!std::strcmp(key, "planSummary"))   return detail::make_label("query plan summary",           "planSummary");
        if (!std::strcmp(key, "queryFramework"))return detail::make_label("query framework",              "queryFramework");
        if (!std::strcmp(key, "readConcern"))   return detail::make_label("read concern",                 "readConcern");
        if (!std::strcmp(key, "writeConcern"))  return detail::make_label("write concern",                "writeConcern");
        if (!std::strcmp(key, "lsid"))          return detail::make_label("logical session ID",           "lsid");
        if (!std::strcmp(key, "resolvedViews")) return detail::make_label("resolved views",               "resolvedViews");

        // Plan cache keys
        if (!std::strcmp(key, "queryHash"))     return detail::make_label("query shape hash (deprecated)","queryHash");
        if (!std::strcmp(key, "planCacheShapeHash"))
            return detail::make_label("plan cache shape hash",            "planCacheShapeHash");
        if (!std::strcmp(key, "planCacheKey"))  return detail::make_label("plan cache key",               "planCacheKey");
        if (!std::strcmp(key, "queryShapeHash"))return detail::make_label("query shape hash",             "queryShapeHash");

        // Execution counters
        if (!std::strcmp(key, "keysExamined"))  return detail::make_label("index keys examined",          "keysExamined");
        if (!std::strcmp(key, "docsExamined"))  return detail::make_label("documents examined",           "docsExamined");
        if (!std::strcmp(key, "nreturned"))     return detail::make_label("documents returned",           "nreturned");
        if (!std::strcmp(key, "nBatches"))      return detail::make_label("batches returned",             "nBatches");
        if (!std::strcmp(key, "numYields"))     return detail::make_label("lock yields",                  "numYields");
        if (!std::strcmp(key, "reslen"))        return detail::make_label("response length (bytes)",      "reslen");
        if (!std::strcmp(key, "ninserted"))     return detail::make_label("documents inserted",           "ninserted");
        if (!std::strcmp(key, "nMatched"))      return detail::make_label("documents matched",            "nMatched");
        if (!std::strcmp(key, "nModified"))     return detail::make_label("documents modified",           "nModified");
        if (!std::strcmp(key, "ndeleted"))      return detail::make_label("documents deleted",            "ndeleted");
        if (!std::strcmp(key, "nUpserted"))     return detail::make_label("documents upserted",           "nUpserted");
        if (!std::strcmp(key, "nShards"))       return detail::make_label("shards contacted",             "nShards");
        if (!std::strcmp(key, "cursorid"))      return detail::make_label("cursor ID",                    "cursorid");
        if (!std::strcmp(key, "cursorExhausted"))
            return detail::make_label("cursor exhausted",                 "cursorExhausted");
        if (!std::strcmp(key, "hasSortStage"))  return detail::make_label("has in-memory sort stage",     "hasSortStage");
        if (!std::strcmp(key, "fromMultiPlanner"))
            return detail::make_label("evaluated multiple plans",         "fromMultiPlanner");
        if (!std::strcmp(key, "replanned"))     return detail::make_label("query was replanned",          "replanned");
        if (!std::strcmp(key, "replanReason"))  return detail::make_label("replan reason",                "replanReason");
        if (!std::strcmp(key, "ordered"))       return detail::make_label("ordered bulk write",           "ordered");
        if (!std::strcmp(key, "multi"))         return detail::make_label("multi-document update",        "multi");
        if (!std::strcmp(key, "upsert"))        return detail::make_label("is upsert",                   "upsert");

        // Timing fields
        if (!std::strcmp(key, "durationMillis"))
            return detail::make_label("total duration (ms)",              "durationMillis");
        if (!std::strcmp(key, "workingMillis"))
            return detail::make_label("active working time (ms)",         "workingMillis");
        if (!std::strcmp(key, "cpuNanos"))
            return detail::make_label("CPU time (ns)",                    "cpuNanos");
        if (!std::strcmp(key, "planningTimeMicros"))
            return detail::make_label("query planning time (us)",         "planningTimeMicros");
        if (!std::strcmp(key, "remoteOpWaitMillis"))
            return detail::make_label("remote shard wait (ms)",           "remoteOpWaitMillis");
        if (!std::strcmp(key, "waitForWriteConcernDurationMillis"))
            return detail::make_label("write concern wait (ms)",          "waitForWriteConcernDurationMillis");
        if (!std::strcmp(key, "prepareConflictDurationMillis"))
            return detail::make_label("prepare conflict wait (ms)",       "prepareConflictDurationMillis");
        if (!std::strcmp(key, "totalOplogSlotDurationMicros"))
            return detail::make_label("oplog slot commit delay (us)",     "totalOplogSlotDurationMicros");

        // Sub-objects
        if (!std::strcmp(key, "locks"))         return detail::make_label("lock statistics",              "locks");
        if (!std::strcmp(key, "storage"))       return detail::make_label("storage engine stats",         "storage");
        if (!std::strcmp(key, "queues"))        return detail::make_label("admission queues",             "queues");
        if (!std::strcmp(key, "flowControl"))   return detail::make_label("flow control",                 "flowControl");
        if (!std::strcmp(key, "authorization")) return detail::make_label("authorization stats",          "authorization");

        // Error fields
        if (!std::strcmp(key, "errCode"))       return detail::make_label("error code",                   "errCode");
        if (!std::strcmp(key, "errName"))       return detail::make_label("error name",                   "errName");
        if (!std::strcmp(key, "errMsg"))        return detail::make_label("error message",                "errMsg");
        if (!std::strcmp(key, "exception"))     return detail::make_label("exception detail",             "exception");

        // Sharding / catalog cache
        if (!std::strcmp(key, "catalogCacheDatabaseLookupDurationMillis"))
            return detail::make_label("catalog cache DB lookup (ms)",     "catalogCacheDatabaseLookupDurationMillis");
        if (!std::strcmp(key, "catalogCacheCollectionLookupDurationMillis"))
            return detail::make_label("catalog cache collection lookup (ms)", "catalogCacheCollectionLookupDurationMillis");
        if (!std::strcmp(key, "placementVersionRefreshDurationMillis"))
            return detail::make_label("placement version refresh (ms)",   "placementVersionRefreshDurationMillis");
        if (!std::strcmp(key, "shardVersionRefreshMillis"))
            return detail::make_label("shard version refresh (ms)",       "shardVersionRefreshMillis");

        // Network / connection
        if (!std::strcmp(key, "connectionCount"))
            return detail::make_label("open connection count",            "connectionCount");
        if (!std::strcmp(key, "address"))       return detail::make_label("bind address",                 "address");
        if (!std::strcmp(key, "doc"))           return detail::make_label("client metadata",              "doc");
        if (!std::strcmp(key, "durationMicros"))return detail::make_label("duration (us)",                "durationMicros");
    }

    // ---- Top-level envelope keys (context = "document") --------
    if (detail::ctx_ends_with(ctx, "document")) {
        if (!std::strcmp(key, "t"))    return detail::make_label("timestamp",          "t");
        if (!std::strcmp(key, "s"))    return detail::make_label("severity",           "s");
        if (!std::strcmp(key, "c"))    return detail::make_label("component",          "c");
        if (!std::strcmp(key, "id"))   return detail::make_label("log message ID",     "id");
        if (!std::strcmp(key, "ctx"))  return detail::make_label("thread context",     "ctx");
        if (!std::strcmp(key, "svc"))  return detail::make_label("service",            "svc");
        if (!std::strcmp(key, "msg"))  return detail::make_label("message",            "msg");
        if (!std::strcmp(key, "attr")) return detail::make_label("attributes",         "attr");
        if (!std::strcmp(key, "tags")) return detail::make_label("tags",               "tags");
        if (!std::strcmp(key, "truncated")) return detail::make_label("truncation info","truncated");
        if (!std::strcmp(key, "size")) return detail::make_label("original size",      "size");
    }

    // Unknown key — return raw unchanged
    return key;
}
