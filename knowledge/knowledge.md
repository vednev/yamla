# MongoDB Log Internals and Technical Analysis Guide

This guide focuses purely on the **technical** aspects of MongoDB logs:

- How log entries are structured.
- What each field and component means.
- How MongoDB logs **slow operations**, replication behavior, sharding behavior, and security events.
- The most common **problems and anomalies** that surface in logs.
- Nuanced and version‑specific details that matter in real investigations.

It does **not** cover operational practices (how to obtain logs, internal processes, etc.).

---

## 1. Log Architecture and Structured JSON Format

### 1.1 Structured JSON Output

Modern `mongod` and `mongos` processes emit logs as **structured JSON**, one JSON document per log entry.

Each entry is a single line of JSON with a stable set of top‑level keys:

- `t` – timestamp document.
  - `t.$date` — ISO‑8601 datetime string.
- `s` – **severity** (single character).
- `c` – **component** (string, e.g. `"NETWORK"`, `"COMMAND"`).
- `id` – integer log ID, stable per event type across versions.
- `ctx` – context, often a thread or connection identifier (e.g. `"conn39250"`, `"OplogApplier-0"`).
- `svc` – service marker: `S` for shard server, `R` for router (`mongos`), `-` for none.
- `msg` – short message string describing the event (e.g. `"Slow query"`, `"client metadata"`, `"Listening on"`).
- `attr` – document of **event‑specific attributes** (command details, metrics, IPs, etc.).
- `tags` – optional string array with tags like `["startupWarnings"]`.
- `truncated` / `size` – appear when attributes were truncated due to size limits.

Because the schema is consistent, you can treat logs as a **queryable dataset** keyed by `s`, `c`, `id`, `msg`, and fields in `attr`.

### 1.2 Severity Levels (`s`)

Severity codes, from most to least severe:

- `F` — **Fatal**: unrecoverable condition; usually followed by process termination.
- `E` — **Error**: failed operation or serious problem.
- `W` — **Warning**: abnormal but non‑fatal condition.
- `I` — **Informational**: normal events; core server behavior at default verbosity.
- `D1`–`D5` — **Debug** levels 1–5, increasingly verbose debug information.

The mapping between `logLevel` / `systemLog.verbosity` and what is logged:

- `logLevel = 0` (or `verbosity: 0`) — informational messages and above; slow operations logged according to thresholds.
- `logLevel ≥ 1` — **all operations** are logged (subject to special cases like oplog application).

### 1.3 Components (`c`)

The `c` field classifies the source **subsystem** of the log entry.

A few especially important components:

- `CONTROL` — startup/shutdown, global configuration, feature compatibility.
- `NETWORK` — inbound/outbound connections, TLS, OCSP, heartbeats at socket level.
- `COMMAND` — read and write operations, commands, slow queries.
- `STORAGE` — storage engine operations (WiredTiger) at a coarse level.
- `WTBACKUP`, `WTCHKPT`, `WTWRTLOG` — WiredTiger backup, checkpoint, and internal logging subsystems.
- `REPL` — replication (steady‑state apply, sync, flow control).
- `ELECTION`, `REPL_HB`, `INITSYNC`, `ROLLBACK` — more granular replication phases.
- `SHARDING` — sharding metadata, migrations, and router behavior.
- `ACCESS`, `AUTH`, `AUDIT` — authentication, role/privilege checks, auditing.
- `FTDC` — full‑time diagnostic data capture.
- `REJECTED` — rejected queries when query rejection logging is enabled at high verbosity.

Filtering by `c` is often the **first step** in log analysis: for example, focus on `COMMAND` for slow operations, `REPL` for lag, `SHARDING` for imbalance, `NETWORK` for connection issues.

### 1.4 Escaping, Truncation, and log size

MongoDB ensures JSON validity:

- Control characters in `msg` and `attr` are escaped according to the **Relaxed Extended JSON v2.0** specification.
- Large attribute values may be truncated if they exceed `maxLogSizeKB` per attribute; truncated entries include a `truncated` object and optional `size` metadata with the original size.

This matters when:

- You’re trying to reconstruct the **exact command** from logs—very large command documents may be partially truncated.
- You parse logs with tools that assume no escaping/truncation; always treat `attr` as canonical, not just the serialized `command` string you might see in `msg`.

### 1.5 Version‑Specific Differences

Some differences to be aware of by version:

- **Pre‑4.4**: logs could be plain text or partially structured; 4.4 introduces full JSON logging for all components.
- **4.2+**: includes explicit `D1`–`D5` severity codes, not just `D`.
- **5.x+**: slow query logs include extra fields like `remote` (client IP) and more fine‑grained metrics.
- **6.1+**: slow query logs add cache refresh timing fields.
- **6.2+**: slow logs include `queryFramework` (`"classic"` vs `"sbe"`).
- **6.3+**:
  - Adds **session workflow** log messages (end‑to‑end breakdown of request handling).
  - Adds **Connection Acquisition To Wire** messages with network wait timings.
- **7.0+**: slow query log includes `catalogCacheIndexLookupDurationMillis` and renames some sharding cache timers.
- **8.0+**:
  - Slow query log focuses on **working time** (`workingMillis`) instead of pure latency for the main slow threshold.
  - Adds per‑queue timing in a `queues` document (`totalTimeQueuedMicros`).
- **8.1+**: audit logs and some connection logs gain fields capturing upstream load balancers and endpoints.

---

## 2. Logging of Slow Operations and Timing Metrics

### 2.1 Slow Operation Threshold and Sampling

MongoDB uses the **slow operation threshold** to decide which operations to log as “slow” in the diagnostic log and in the profiler:

Key concepts:

- `slowms` / `operationProfiling.slowOpThresholdMs`
  - Millisecond threshold; operations whose **work time** exceeds this value are considered “slow”.
- `slowOpSampleRate` / `sampleRate`
  - Fraction (0–1) of slow operations that are **actually recorded**. Useful for reducing log volume.

Behavior:

- Applies to both:
  - **Database profiler** (if enabled).
  - **Diagnostic logs** (even if profiler is disabled).
- At `logLevel = 0`:
  - Only operations exceeding `slowms` are logged, at a rate controlled by `slowOpSampleRate`.
- At `logLevel ≥ 1`:
  - All operations are logged in diagnostic logs, but the **profiler** still uses its own `mode` and thresholds.

Nuance: starting in MongoDB 8.0, the main slow threshold is evaluated on **working time** (`workingMillis`) rather than total observed latency; waiting for locks or flow control no longer determines whether an operation is considered slow.

### 2.2 Slow Query Log Fields (`COMMAND` Component)

A typical slow query log entry (`c: "COMMAND"`, `msg: "Slow query"`) contains:

- `attr.ns` — namespace (`"db.collection"`).
- `attr.type` — `"command"`, `"query"`, etc.
- `attr.command` — full command document showing collection, filter, update/aggregation pipeline, and options.
- `attr.planSummary` — high‑level plan, e.g.:
  - `"COLLSCAN"` — full collection scan.
  - `"IXSCAN { field: 1 }"` — simple index scan.
  - `"FETCH"` or mixed patterns for complex plans.
- `attr.docsExamined` — number of documents read by the plan.
- `attr.keysExamined` — number of index keys read.
- `attr.nreturned` — number of documents in the result set.
- `attr.durationMillis` or `attr.workingMillis` — time spent “working” on the operation; in newer versions this is the primary slow‑threshold metric.
- `attr.cpuNanos` — CPU nanoseconds consumed by the query (Linux only).
- `attr.appName` — application name as reported by the driver (e.g. connection string appName).
- `attr.remote` — client IP and port; in 5.0+ this appears in slow operation logs.
- `attr.queryFramework` — `"classic"` vs `"sbe"` (slot‑based engine).
- `attr.planCacheShapeHash` / `attr.queryHash` / `attr.planCacheKey` — identifiers for the query **shape** and the plan chosen by the optimizer.
- In 8.0+: `attr.queues` — per‑queue timing, e.g. `totalTimeQueuedMicros` for admission/ingress/execution queues.
- In 8.1+: per‑stage spill metrics: `<stage>Spills`, `<stage>SpilledBytes`, `<stage>SpilledRecords`, etc., indicating how often a query wrote temporary files to disk and how much data was spilled.

### 2.3 Session Workflow Log Messages

From MongoDB 6.3 onward, the server emits **session workflow** log entries when the time to send a response exceeds `slowms`:

These entries include a breakdown of times such as:

- Time spent:
  - Receiving the request.
  - Waiting in queues.
  - Executing the operation.
  - Marshaling and sending the response.

They are useful for distinguishing:

- **Server processing time** vs
- **Network and response overhead**.

Session workflow logs complement the regular slow query logs by giving a **request lifecycle view** across queues and subsystems.

### 2.4 Connection Acquisition To Wire Messages

Also new in 6.3 are **Connection Acquisition To Wire** messages, emitted when:

- The time between **acquiring a server connection** from the pool and **writing bytes** to that connection exceeds a threshold (default 1 millisecond).
- Fields include:
  - `durationMicros` — microseconds spent in this interval.
  - Details about the operation/connection context.

These logs are key for diagnosing:

- Application‑side **connection pool starvation**.
- Dispatcher/queue bottlenecks where a worker holds a connection but doesn’t immediately send data.

---

## 3. Query Shape, Plan Identification, and Nuanced Fields

### 3.1 Query Shape Identification

MongoDB introduces hash fields to identify **query shapes** independent of literal values:

- `queryHash` — legacy field, being deprecated in favor of `planCacheShapeHash`.
- `planCacheShapeHash` — hash of query “shape”.
- `planCacheKey` — key identifying the query plan in the plan cache.

Usage:

- All slow logs and profiler entries for a given shape share the same `planCacheShapeHash`.
- You can aggregate slow operations or plan changes **by shape** to see which types of queries are problematic.

Version nuance:

- In 8.0, `queryHash` is **duplicated** into `planCacheShapeHash`. Future versions will remove `queryHash`, so new analysis should rely on `planCacheShapeHash`.

### 3.2 Relationship to Query Settings and Plan Cache

The same `planCacheShapeHash` is also:

- Used in **query settings** (`$querySettings`) to apply behavior (e.g., index hints, engine selection) per query shape.
- Reflected in `explain()` output as `planCacheShapeHash` and `planCacheKey`.

This lets you:

1. Identify a problematic shape in logs (via `planCacheShapeHash`).
2. Use `explain()` with that shape to inspect the chosen plan.
3. Apply `setQuerySettings` to adjust index hints, engines, or block the shape as needed.

### 3.3 Spills and Memory‑Intensive Stages

From MongoDB 8.1+, slow query logs include per‑stage metrics when a query **spills to disk** (for example, sorting or grouping that exceeds memory limits):

- `<stage>Spills` — count of spill events for that stage.
- `<stage>SpilledBytes` — total bytes written as spill files.
- `<stage>SpilledRecords` — total records spilled.
- `<stage>SpilledDataStorageSize` — disk size of spill data.

Examples:

- `sortSpills`, `sortSpilledBytes` for SORT stage.
- `groupSpills` for GROUP stage.

Interpretation:

- Even if `durationMillis` is moderate, large spill counts and sizes can foretell problems under heavier load or with larger datasets.
- Repeated spills suggest:
  - Insufficient memory limits.
  - Need for better indexes (so less intermediate data must be sorted/aggregated).
  - Overly large result sets or poorly selective filters.

---

## 4. Replication‑Related Logging

### 4.1 Slow Oplog Application

Secondary members **log slow oplog application** entries when an oplog operation takes longer than the slow threshold to apply:

Characteristics:

- Component: `c: "REPL"`.
- Message: `"applied op: <oplog entry> took <num>ms"`.
- Logged on secondaries.
- **Independent** of:
  - Global log levels (`logLevel`, `logComponentVerbosity`).
  - Profiler settings.
- Affected by `slowOpSampleRate` to avoid logging every oplog entry under high write load.

Use:

- Correlating these with **replication lag** and **oplog volume** lets you see whether:
  - Lag is caused by expensive individual operations on secondaries (e.g. secondary‑only workload or large writes).
  - Or by network/disk constraints preventing timely apply of the oplog.

### 4.2 Election and Heartbeat Logs

Replication and election behavior surfaces under components like `REPL`, `ELECTION`, and `REPL_HB`:

Common log events:

- **Heartbeat failures** between replica set members.
- **Elections** being triggered (e.g., step down, priority changes).
- **Rollback** sequences (ROLLBACK component).

Nuanced points:

- In a write‑heavy cluster, repeated election messages often coincide with:
  - Long GC or disk stalls.
  - Heartbeat timeouts.
- `totalOplogSlotDurationMicros` (7.0+ and some patch releases) in slow query logs indicates time between a write obtaining a commit timestamp and actually committing; large values can affect replication, as secondaries must apply commits in timestamp order.

---

## 5. Sharding‑Related Logging

### 5.1 Query Fan‑out and Merging

On `mongos` and shard routers, slow query logs often contain:

- `attr.nShards` — number of shards involved in the operation.
- `attr.needsMerge` — whether the router performed a merge (e.g., after `$group` or `$sort`).

Patterns:

- `nShards > 1` + `needsMerge: true`:
  - Indicates **scatter‑gather** queries and/or merges across shards.
  - Often a sign that:
    - Filters or sort keys are not aligned with the **shard key**.
    - The query is forced to read/merge from many or all shards.

### 5.2 Sharding Metadata and Cache Refresh Timings

Sharded deployments log several timing fields for metadata lookups and cache refreshes, including:

- `catalogCacheIndexLookupDurationMillis` — time spent fetching index metadata from the catalog cache.
- `placementVersionRefreshDurationMillis` (7.0+; renamed from `shardVersionRefreshMillis`) — time to refresh placement information.

Interpretation:

- Large or frequent cache refresh durations can cause temporary latency spikes, especially when:
  - Chunks are frequently moved or split.
  - Shard catalog or config servers are overloaded.

---

## 6. Storage and WiredTiger Logs

### 6.1 Storage Component and WT Subcomponents

`c: "STORAGE"` and specialized `WT*` components cover events such as:

- Checkpoints (`WTCHKPT`) — timing and frequency of WiredTiger checkpoints.
- Backup operations (`WTBACKUP`).
- Write logging (`WTWRTLOG`) — internal log writing.

In combination with slow query logs and FTDC metrics, storage‑related log entries help diagnose:

- Long checkpoint pauses affecting latency.
- IO saturation issues during large compactions or index builds.
- Disk stalls or filesystem issues.

### 6.2 Oplog and Collection Stats via FTDC (Briefly)

While not “logs” in the same textual sense, FTDC includes periodic:

- `collStats` on `local.oplog.rs`.
- Disk/CPU/memory usage snapshots.

They complement storage logs by providing continuous **resource utilization curves** that you can line up with log events.

---

## 7. Network and Connection Logs

### 7.1 Connection Acceptance and Lifecycle

`c: "NETWORK"` logs include lifecycle events such as:

- `"connection accepted"` — new incoming client connection.
- `"connection ended"` or similar — connection closure.
- `"received client metadata"` / `"client metadata"` — metadata reported by drivers on first handshake, typically including:
  - Driver name and version.
  - OS type, name, architecture.
  - Application name (often from connection string `appName`).

A high rate of **connect/disconnect** events typically indicates:

- Clients not using connection pooling (e.g., serverless workloads that open/close per request).
- Misconfigured driver behavior (short timeouts, aggressive closing).
- Potential load balancer behavior (preemptively closing idle connections).

### 7.2 Client Metadata for Driver and OS Analysis

The **client metadata log entries** (under `NETWORK` with `msg: "client metadata"` or similar) contain nested documents like:

- `attr.doc.driver`:
  - `name`, `version` — driver identity.
- `attr.doc.os`:
  - `type`, `name`, `version`, `architecture`.
- `attr.doc.application`:
  - `name` — application name.

Uses:

- Correlating particular **driver versions** or OS types with problematic behaviors.
- Grouping load by application, to see which services generate the most IO (`group_bytes_read_by_driver`, etc.).

### 7.3 TLS and OCSP‑Related Fields (High‑level)

Certain logs specific to TLS/OCSP include:

- Records of **accepted TLS connections** and TLS handshake completion.
- OCSP (Online Certificate Status Protocol) verification logs, including messages such as “Completed client-side verification of OCSP request” that confirm successful certificate revocation checks.

These help diagnose:

- TLS misconfiguration or revoked certificates causing connection failures.
- Latency introduced by OCSP verification under some configurations.

---

## 8. Audit Logs

MongoDB Enterprise and Atlas can emit **audit logs** in two schemas: `mongo` (MongoDB‑defined) and `OCSF` (Open Cybersecurity Schema Framework).

### 8.1 mongo Schema Overview

In `mongo` schema, an audit event document includes fields such as:

- Operation metadata:
  - Action type (`atype`), e.g. `createUser`, `authCheck`, `insert`, `dropCollection`.
  - Timestamp.
- Participants and endpoints:
  - `local` (deprecated in favor of newer fields) — local endpoint.
  - `users` — array of `{ user, db }` documents.
  - For 8.1+, additional fields describing origin client and intermediaries:
    - Origin client IP and port.
    - Load balancer or proxy IP(s) and ports.
- Target resources (collections, databases, roles, etc.).
- Outcome (success/failure).

### 8.2 OCSF Schema Overview

In `OCSF` schema, the event has fields designed for easy ingestion into SIEM tools:

Key fields:

- `activity_id` — action type identifier (mapped from MongoDB atype).
- `category_uid`, `class_uid`, `type_uid` — categorize the event (e.g., configuration changes vs data access).
- `severity_id` — severity level.
- `time` — event time.
- `metadata` — additional metadata blob.
- `actor.user` — user performing the action (name, type, groups).
- `src_endpoint` — origin client IP/port (client machine).
- `dst_endpoint` — MongoDB server IP/port.
- `intermediate_ips` — array of intermediate load balancer or proxy endpoints for 8.1+.

Usage in analysis:

- Answer questions like:
  - “Which users changed cluster parameters?”
  - “Which IP ranges performed schema modifications?”
  - “What administrative operations occurred during an incident window?”
- Correlate with diagnostic logs using approximate time windows and operation types.

---

## 9. FTDC (Full Time Diagnostic Data Capture)

FTDC is a built‑in mechanism in `mongod` and `mongos` processes to **continuously capture diagnostic metrics** for later analysis.

Key points:

- Enabled by default; failures in FTDC threads are **fatal**, because losing diagnostics during production issues is unacceptable.
- Periodic captures include:
  - `serverStatus` (comprehensive server metrics).
  - `replSetGetStatus` (for replica sets).
  - `collStats` on `local.oplog.rs`.
  - `connPoolStats` on `mongos` nodes.
  - `getParameter` and `getClusterParameter` outputs.
- Utilization stats per host:
  - CPU, memory, disk (performance‑related).
  - Network throughput and some connection counters (but **not** packet payloads).

While FTDC is not text logs, it is tightly coupled with log analysis:

- FTDC provides **trends** (e.g., CPU or IO saturation, replication lag) that you line up with **specific anomalous log entries** in time.

---

## 10. Log Redaction and Sensitive Data

### 10.1 redactClientLogData and redactEncryptedFields

To reduce risk of sensitive data in logs, MongoDB supports log redaction:

- `redactClientLogData` (server parameter/config):
  - When `true`, any messages that would normally include client data (e.g., full documents or query filters) are **redacted**, leaving only metadata, file names, and line numbers.
- `redactEncryptedFields`:
  - Ensures that encrypted Binary data (for Queryable Encryption) is redacted from all logs, even if other data is not.

Effects:

- Without redaction, a verbose log might show:
  ```json
  "command": { "insert": "clients", "documents": [{ "name": "Joe", "PII": "Sensitive Information" }] }
  ```
- With `redactClientLogData`, the same log entry would hide the document contents, while still showing:
  - That an insert occurred.
  - Which namespace and command type were involved.

### 10.2 Queryable Encryption Redaction

When Queryable Encryption is enabled:

- Operations against encrypted collections are **omitted** from slow query logs, or sensitive encrypted data is redacted, depending on configuration.

Analysis implications:

- You often have full visibility into:
  - `planSummary`, `docsExamined`, timings like `durationMillis`.
- But value‑level predicates may be hidden, requiring you to reason in terms of **query shapes** and plan metrics rather than raw filter values.

---

## 11. Common Problems and Anomalies Visible in Logs

This section summarizes **frequent problem types** and what they look like in logs.

### 11.1 Slow Queries and Bad Index Usage

Symptoms in logs:

- `c: "COMMAND"`, `msg: "Slow query"` with:
  - `planSummary: "COLLSCAN"` and large `docsExamined` vs small `nreturned`.
  - Large `durationMillis` / `workingMillis`.
  - Frequent occurrences for the same `ns` and `planCacheShapeHash`.

Interpretation:

- Missing indexes or non‑selective filters causing full scans.
- Suboptimal sort patterns (server must sort large result sets in memory or on disk).
- Data skew or unbounded aggregations.

Nuanced details:

- Look at **ratio** `docsExamined / nreturned`:
  - Orders‑of‑magnitude difference usually indicates poor selectivity or no appropriate index.
- Look at `cpuNanos` for CPU‑heavy queries even if `durationMillis` is moderate; heavy CPU can be a concern when concurrency increases.
- Check `queryFramework` to know if SBE or classic engine handled the query, especially if you’re isolating engine‑specific regressions.

### 11.2 Replication Lag and Slow Oplog Application

Log signals:

- `REPL` component log entries such as:
  - Slow oplog apply messages: `"applied op: ... took <num>ms"`. Large `num` indicates heavy writes or secondary‑only workloads.
- Differences in `syncedTo` timestamps across members (via replication commands and logs, though that’s usually evaluated with shell helpers).

Interpretation:

- If secondaries frequently log slow apply times, consider:
  - Disk throughput differences.
  - Secondary performing heavy read workloads.
  - Complex writes (e.g., multi‑document operations) being replayed slowly.

### 11.3 Sharding Imbalance and Scatter‑Gather Queries

Characteristics:

- Slow query logs on `mongos` with:
  - `attr.nShards` large (often equal to number of shards).
  - `attr.needsMerge: true`.
  - `planSummary` often involving distributed cursors.

Interpretation:

- Poorly chosen shard key (e.g., low cardinality, skewed data).
- Query filters not aligned with shard key, causing **broadcast queries**.
- `$sort` and `$group` stages requiring router merges of large partial results.

### 11.4 Storage and Disk Latency

Often correlated behavior:

- Slow queries with:
  - High `docsExamined`, high `durationMillis`, and high `cpuNanos` combined with FTDC showing IO saturation.
- `STORAGE` and `WT*` logs with:
  - Warnings about slow checkpoints or eviction.
  - Repeated log messages indicating long flush times for journals or data files.

Effects:

- Elevated latency for all operations, including read/write commands and replication (overt or hidden in oplog apply times).

### 11.5 Connection Problems and Pool Starvation

Signs:

- `NETWORK` logs showing:
  - Bursts of `"connection accepted"` events and corresponding disconnects.
  - Repeated warnings about connection pool exhaustion (driver‑specific, but often visible in server logs as frequent connect/disconnect storms).
- Connection Acquisition To Wire logs with high `durationMicros` values.

Interpretation:

- Application is creating many **short‑lived connections** (chatty behavior or lack of pooling).
- Server may be under heavy load, causing time between acquiring a connection and actually sending data.

### 11.6 Authentication and Authorization Errors

Indicators:

- Diagnostic logs (non‑audit) containing repeated warnings/errors:
  - Failed authentication attempts.
  - Authorization failures when clients attempt disallowed operations.
- Audit logs with:
  - `atype: "authCheck"` failures.
  - Unexpected `dropCollection`, `createUser`, `grantRolesToUser`, etc., for particular users.

Security‑related patterns:

- Sudden spikes in failed logins from a specific IP or username may indicate:
  - Misconfigured application credentials.
  - Brute‑force attempts (if externally exposed).

---

## 12. Subtle and Nuanced Log Details to Be Aware Of

### 12.1 Syslog Time Stamps vs MongoDB Timestamps

When logging to `syslog`:

- The **syslog daemon** generates timestamps, not MongoDB itself, which can lead to misleading times under heavy system load.

Implication:

- When using syslog, the `t.$date` generated by syslog might not perfectly reflect when MongoDB issued the message, especially when logs are flushed in bursts.

### 12.2 Impact of `logLevel` and `logComponentVerbosity`

Some key constraints:

- Many **slow operation messages** (both queries and oplog apply) are **independent** of `logLevel`; they depend on slow thresholds and sampling instead.
- Setting `logLevel ≥ 1` logs **all operations** but does not cause secondaries to log all **oplog entries**; they still only log *slow* oplog application messages.

This explains why:

- Even at high log levels, you do *not* see a full textual mirror of the oplog; only slow ones appear.

### 12.3 Profiler vs Diagnostic Logs

Relationship between profiler and logs:

- The **same slow threshold and sampling parameters** (`slowms`, `sampleRate`) govern both.
- For `mongos`, `db.setProfilingLevel()` affects:
  - **Diagnostic logs** only; profiler is not available on mongos.

This matters when:

- You see slow operations in logs for mongos, but no profiler data (by design).
- Changing profiler settings on a database affects **all databases** in a `mongod` instance, since `slowms` is global per process, not per DB.

### 12.4 Log Size Limits per Attribute

The `maxLogSizeKB` parameter controls the maximum size of **individual attribute fields** in a log entry; data beyond that is truncated and referenced in the `truncated`/`size` metadata.

Tricky outcomes:

- Enormous commands (very large filters or aggregation stages) may be **incompletely** visible; ensure your tools can handle truncation or rely on `queryHash`/shape instead of full command text.

### 12.5 FTDC’s Perspective in Containers

If `mongod`/`mongos` runs inside a container:

- FTDC reports CPU, memory, and disk usage **from the container’s perspective**, not the host’s.

This can be confusing if:

- Host monitoring shows high CPU usage, but FTDC doesn’t—because the container was CPU‑limited and didn’t see the full host capacity.

---

## 13. Conclusion

MongoDB’s logging system provides **rich, structured telemetry** for understanding:

- Query performance and plan behavior.
- Replication and sharding health.
- Storage engine activity.
- Network and authentication patterns.
- Security‑relevant events.

The most important points for deep technical log analysis are:

- Master the **JSON structure** (fields like `s`, `c`, `id`, `ctx`, `attr`) and key metrics within `attr`.
- Understand the **slow operation pipeline**: how `slowms`, `slowOpSampleRate`, `workingMillis`, `remoteOpWaitMillis`, and per‑stage spill metrics interact.
- Use **query shape identifiers** (`planCacheShapeHash`, `planCacheKey`) and **engine markers** (`queryFramework`) to reason about problematic workloads independent of specific literals.
- Leverage **replication and sharding logs** (`REPL`, `SHARDING`, slow oplog apply, cache refresh timers) to diagnose distribution‑related anomalies.
- Keep in mind the **effects of redaction, truncation, and logging backends** (syslog vs file) so that you don’t misinterpret partial or delayed log entries.

With these details in hand, MongoDB logs become a **precise analytical tool** for troubleshooting and capacity planning, not just a passive record of events.



---

## Sources

- [Log Messages - Database Manual - MongoDB Docs](https://www.mongodb.com/docs/manual/reference/log-messages/)
- [Log Messages - Database Manual - MongoDB Docs](https://www.mongodb.com/docs/manual/reference/log-messages/)
- [Log Messages - Database Manual - MongoDB Docs](https://www.mongodb.com/docs/manual/reference/log-messages/)
- [MongoDB Server Parameters for a Self-Managed Deployment - Database Manual v6.0 - MongoDB Docs](https://www.mongodb.com/docs/v6.0/reference/parameters/)
- [Database Profiler - Database Manual - MongoDB Docs](https://www.mongodb.com/docs/manual/tutorial/manage-the-database-profiler/)
- [Database Profiler - MongoDB Manual v5.0 - MongoDB Docs](https://www.mongodb.com/docs/v5.0/tutorial/manage-the-database-profiler/)
- [Log Messages - Database Manual - MongoDB Docs](https://www.mongodb.com/docs/manual/reference/log-messages/)
- [Full Time Diagnostic Data Capture - Database Manual - MongoDB Docs](https://www.mongodb.com/docs/manual/administration/full-time-diagnostic-data-capture/)
- [Self-Managed Configuration File Options - Database Manual - MongoDB Docs](https://www.mongodb.com/docs/manual/reference/configuration-options/)
- [mongod - Database Manual - MongoDB Docs](https://www.mongodb.com/docs/manual/reference/program/mongod/)
- [Log Messages - Database Manual v8.0 - MongoDB Docs](https://www.mongodb.com/docs/v8.0/reference/log-messages/)
- [Log Messages - MongoDB Manual v5.0 - MongoDB Docs](https://www.mongodb.com/docs/v5.0/reference/log-messages/)
- [Log Messages - MongoDB Manual v5.0 - MongoDB Docs](https://www.mongodb.com/docs/v5.0/reference/log-messages/)
- [mongo Schema Audit Messages - Database Manual - MongoDB Docs](https://www.mongodb.com/docs/manual/reference/audit-message/mongo/)
- [Database Profiler - Database Manual v7.0 - MongoDB Docs](https://www.mongodb.com/docs/v7.0/tutorial/manage-the-database-profiler/)
- [Troubleshoot Replica Sets - Database Manual - MongoDB Docs](https://www.mongodb.com/docs/manual/tutorial/troubleshoot-replica-sets/)
- [CE Guide S&P](https://docs.google.com/document/d/1u_d-E_rUCme3Lh8nalHrzpRleqrWOXSap8IK69Xzd90)
- [FAQ: Self-Managed MongoDB Diagnostics - Database Manual - MongoDB Docs](https://www.mongodb.com/docs/manual/faq/diagnostics/)
- [System Event Audit Messages - Database Manual - MongoDB Docs](https://www.mongodb.com/docs/manual/reference/audit-message/)
- [MongoDB Query Performance Troubleshooting Guideline_20260401](https://docs.google.com/document/d/1LDptnEeApdE7cRsRoiyaFVhB_o9WyvxTDEjrpNd8eUU)
