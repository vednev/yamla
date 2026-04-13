---
name: ftdc-analyzer
description: Domain knowledge for interpreting MongoDB FTDC metrics, diagnosing performance issues, and identifying known bottleneck patterns. Includes integration with the ftdc-parser skill for end-to-end diagnostic workflows.
---

# FTDC Analyzer

Knowledge base for interpreting MongoDB FTDC (Full Time Diagnostic Data Capture) metrics. Use with the `ftdc-parser` skill to extract raw metrics, then apply this knowledge to diagnose issues.

## How to Use This Skill

**This skill teaches you how to think about FTDC diagnosis, not what to conclude.**

The patterns below are a starting point — not a checklist. New patterns appear constantly, and many real issues won't match anything documented here. Your job is to reason from first principles using these patterns as examples of diagnostic thinking.

### Key principles

1. **Think for yourself.** The patterns here show known diagnostic reasoning paths. When you encounter something unfamiliar, reason about what the metrics mean mechanically (e.g., "this counter measures X, and it's rising while Y is falling — what mechanism connects them?"). Don't stop investigating just because you can't find a matching pattern.

2. **Known bugs are teaching examples.** Many bugs listed here (WT-11300, WT-13283, etc.) may be fixed in the version you're analyzing. They're included to show *how* to investigate that class of problem — the diagnostic technique applies even after the specific bug is patched. A new eviction bug may show similar FTDC signatures to an old one.

3. **Metric names can change and do.** Use fuzzy matching (`--filter` with regex), not exact string lookup. If a metric name from this skill doesn't exist in the FTDC data, search for related terms — the metric may have been renamed or reorganized. Use `--list` to explore what's actually available.

4. **New metrics are always being added (and removed).** Don't assume this skill lists every relevant metric. Always run `--list --nonzero` and scan for metrics in the area you're investigating. A new metric may provide exactly the signal you need and this skill won't mention it.

5. **Correlate, don't conclude.** A single metric matching a pattern is not a diagnosis. Look for multiple independent signals pointing to the same root cause. State which metrics support your hypothesis AND which metrics you checked that did NOT show the expected pattern.

6. **If you have a blind spot, propose new metrics.** When existing metrics can't answer a diagnostic question, consider whether a new metric would help. Before proposing: inventory what already exists (check before duplicating), verify the metric can't be derived from existing ones, and confirm it would be on a hot path worth measuring (not a rare event better served by logs). Counters and histograms on high-frequency paths are valuable. Counters on rare events (like connection setup) usually aren't.

7. **Gauges sampled at 1Hz lie about sub-second dynamics.** FTDC samples once per second. A gauge like `sendQueueSize p50=95` means "at the instant FTDC sampled, the queue had 95 entries." It does NOT mean the queue is persistently deep. Between samples, the queue may fill and drain many times. To determine if a queue/buffer is actually backlogged, compare **throughput rates** (entries produced/s vs entries consumed/s), not instantaneous depth. If input rate ≈ output rate, the queue is healthy regardless of what the gauge shows at sample time.

---

## Scope and Limitations

- Patterns cover traditional MongoDB/WiredTiger (standalone, replica sets, sharded clusters) and **basic disaggregated storage** diagnosis
- Disaggregated storage coverage is limited to known eviction/checkpoint behavioral differences. Patterns not explicitly marked as disagg-aware may not apply. When in doubt, verify against source code.
- New features or architectures may exhibit different behavior for the same metrics
- When FTDC shows unfamiliar patterns:
  - Use `--list --nonzero` to discover what metrics exist in the data
  - Consult current source code for metric semantics
  - Reason about what the metric *mechanically measures* before interpreting
  - Do not assume patterns from this skill apply

---

## Metric Name Conventions

This skill uses abbreviated metric names for readability. When using the `ftdc-parser`, use the **full FTDC path**.

| Shorthand prefix | Full FTDC path prefix |
|---|---|
| `ss` | `serverStatus` |
| `ss wt` | `serverStatus.wiredTiger` |
| `ss wt cache` | `serverStatus.wiredTiger.cache` |
| `ss wt checkpoint` | `serverStatus.wiredTiger.checkpoint` |
| `ss queues execution` | `serverStatus.queues.execution` |
| `ss opcounters` | `serverStatus.opcounters` |
| `ss connections` | `serverStatus.connections` |
| `ss tcmalloc` | `serverStatus.tcmalloc` |

**Example:** `ss wt cache bytes currently in the cache` → `serverStatus.wiredTiger.cache.bytes currently in the cache`

To search for metrics in the parser, use `--filter` with a regex fragment:

```bash
# "ss wt cache eviction" → search with:
./ftdc_parser /path/ --list --nonzero --filter "wiredTiger\.cache\.eviction"
```

---

## Diagnostic Workflow

### Step 0: Confirm architecture with metadata

```bash
./ftdc_parser /path/to/diagnostic.data --metadata
```

From the metadata, extract and record:
- **Server version** (from `buildInfo.version`) — needed to check known bug applicability
- **Architecture** (from `hostInfo.system`) — cores, RAM, cpuArch
- **Storage engine configuration** (from `getCmdLineOpts.parsed.storage`)
- **Disaggregated storage** (from `getCmdLineOpts.parsed.setParameter.disaggregatedStorageEnabled`)
- **Topology** (from `getCmdLineOpts.parsed.replication`) — replica set name, standalone, etc.
- **syncdelay** (from `getCmdLineOpts.parsed.setParameter.syncdelay`)

⛔ **STOP — Record before proceeding:**
- Node count: ___
- Topology: standalone / replica set / sharded cluster
- Architecture: standard / disaggregated storage / Atlas / Serverless
- Server version: ___

If disaggregated storage is enabled, eviction and checkpoint patterns are fundamentally different — see Rule 5 in Avoiding Analytical Bias.

### Step 1: Read the summary

```bash
./ftdc_parser /path/to/diagnostic.data
```

The summary gives you: operations, connections, cache, checkpoints, admission control, replication state, top active metrics, and auto-detected issues. Read all of it.

### Step 1.5: Check system-level metrics FIRST

**Before diagnosing any WiredTiger or MongoDB issue, rule out system-level root causes.** If disks are saturated, every WT metric will look bad (slow checkpoints, slow eviction, ticket exhaustion) but the root cause is I/O, not MongoDB.

```bash
# Disk I/O
./ftdc_parser /path/ --stats --nonzero --filter "systemMetrics\.disks"

# CPU
./ftdc_parser /path/ --stats --nonzero --filter "systemMetrics\.cpu"

# System memory (not tcmalloc)
./ftdc_parser /path/ --stats --nonzero --filter "systemMetrics\.mem"
```

**What to check:**

| System metric | Healthy | Concern |
|---|---|---|
| `systemMetrics.disks.*.read_time_ms` / `reads` | < 10ms avg latency | > 20ms = slow disks |
| `systemMetrics.disks.*.write_time_ms` / `writes` | < 10ms avg latency | > 20ms = slow disks |
| `systemMetrics.cpu.idle_ms` (as % of total) | > 20% idle | < 5% = CPU saturated |
| `systemMetrics.cpu.iowait_ms` (as % of total) | < 5% | > 20% = I/O bottleneck |
| `systemMetrics.mem.available` | > 10% of total | near 0 = memory pressure |

**If disk latency is high or iowait is elevated, that is likely the root cause.** Do not attribute slowness to WiredTiger until system metrics are clean.

**Disaggregated storage: different I/O path.** For disagg deployments, local disk I/O is minimal — data is sent over the network to a log server. The "disk equivalent" metrics are:

```bash
./ftdc_parser /path/ --stats --nonzero --filter "disagg\.logServer|disagg\.getPageRequest|disagg\.paliRateLimiter"
```

| Disagg metric | What it means |
|---|---|
| `ss metrics disagg logServer totalBytesSent` | Network throughput to log server (the "write I/O") |
| `ss metrics disagg getPageRequest bytesRead` | Bytes read from remote storage (the "read I/O") |
| `ss metrics disagg getPageRequest numQueued` | Queued page requests — if high, remote storage is slow |
| `ss disagg paliRateLimiter successfulAdmissions` vs `attemptedAdmissions` | If attempts >> successes, rate limiter is throttling |

If disagg is enabled and local disk metrics are clean, check these metrics before blaming WiredTiger cache or eviction. High `numQueued` or rate limiter rejections point to the remote storage path, not local WT.

⛔ **STOP — Before blaming WiredTiger:**
- Are disks saturated? (or for disagg: is the log server / remote storage slow?) ___
- Is CPU saturated? ___
- Is the system swapping (low available memory)? ___
- If any answer is yes: diagnose the system issue first.

### Step 2: Get full metric statistics

```bash
./ftdc_parser /path/to/diagnostic.data --stats --nonzero
```

This outputs one NDJSON line per changed metric with: `name`, `kind`, `min`, `max`, `avg`, `p50`, `p99`, `first`, `last`, `delta`, `rate_ps`.

**How to read the output:**
- **`kind`**: `"counter"` (monotonically increasing) or `"gauge"` (fluctuates). The parser auto-classifies this.
- **Counters** (opcounters, bytes written, eviction counts): `delta` and `rate_ps` are the useful fields. Ignore `avg`/`p50` (midpoints of a cumulative counter are meaningless).
- **Gauges** (cache bytes, connections.current, tickets.available): `p50` (typical value), `p99` (near-worst), `min`, `max` are the useful fields. `avg` can be skewed by ramp-up/ramp-down periods — prefer `p50`.
- **p50 vs avg**: If `p50` and `avg` differ significantly, the metric has a skewed distribution (e.g., long ramp-up, or bimodal behavior). Investigate with `--json` to see the shape.
- **Counters where delta=0**: feature wasn't active during this window.

Focus on areas flagged by the summary, then scan for unexpected patterns.

### Step 3: Investigate temporal patterns

When stats reveal something worth investigating (e.g., ticket exhaustion, cache peaks near thresholds, high eviction counts), drill into the time-series:

```bash
# Per-second rates for counter metrics
./ftdc_parser /path/ --rates --filter "<pattern>"

# Raw values for gauge metrics
./ftdc_parser /path/ --json --filter "<pattern>"

# Zoom into a specific time window
./ftdc_parser /path/ --rates --filter "<pattern>" --from T1 --to T2
```

**When to use `--rates` vs `--json`:**
- `--rates` for **counters** (opcounters, bytes written, evictions) — shows ops/sec
- `--json` for **gauges** (cache bytes, tickets available, connections) — shows absolute values

### Step 4: Cross-reference and conclude

Apply the diagnostic patterns below. For every conclusion:
1. State which metrics support it
2. State which metrics you checked that did NOT show a problem
3. State what you did NOT check and why
4. Apply the Analytical Thinking Guidelines

---

## Temporal Pattern Detection Recipes

These are specific investigation procedures for patterns that cannot be detected from `--stats` alone.

### Detecting Cache Sawtooth

```bash
# Get cache fill over time — look for repeated rises/falls
./ftdc_parser /path/ --json --filter "cache\.(bytes currently|maximum bytes)" --nonzero
```

Feed through analysis: compute `(bytes currently / maximum bytes) * 100` per sample. Look for:
- Repeated oscillations between two ranges (e.g., 70%-85%)
- Whether falls correlate with checkpoint completion or app thread eviction
- Whether peaks exceed 95% (eviction trigger)

To correlate with eviction:
```bash
./ftdc_parser /path/ --rates --filter "cache\.(pages evicted by application|eviction worker)" --nonzero
```

### Detecting Burstiness

```bash
# Get insert/update rates over time
./ftdc_parser /path/ --rates --filter "opcounters\.(insert|update|delete)"
```

Compute burstiness score: `(ops in top 10% of samples) / (total ops) * 100`
- Score > 80%: Extreme burstiness
- Score 50-80%: High burstiness
- Score < 50%: Normal distribution

### Detecting 30-Second Oplog Stalls (SERVER-92554)

Only applicable to multi-node replica sets on Linux with glibc.

```bash
# Check for regular ~30s gaps in oplog timestamps
./ftdc_parser /path/ --json --filter "replSetGetStatus\.members.*optimeDate"
```

Look for exactly 30-second jumps in optime between consecutive samples.

### Detecting Ticket Exhaustion Patterns

```bash
# Raw ticket values over time
./ftdc_parser /path/ --json --filter "queues\.execution\.(write|read)\.(available|out|totalTickets)"
```

For each sample where `available == 0`:
- Check if `out > initial totalTickets` (dynamic scaling = system working as designed)
- Check if `totalTickets` is changing (admission control active)
- Compute % of samples exhausted and duration of exhaustion episodes

### Detecting Checkpoint Duration vs Interval

```bash
./ftdc_parser /path/ --json --filter "checkpoint\.(most recent time|generation)"
```

Track when `generation` increments (= new checkpoint completed). For each:
- Record the `most recent time (msecs)` at that sample
- Compute interval between consecutive generation changes
- If duration ≥ interval, checkpoints are running back-to-back

### Detecting and Interpreting FTDC Gaps

The summary mode auto-detects gaps > 5 seconds. For manual investigation:

```bash
./ftdc_parser /path/ --json --filter "start$"
```

Check timestamp spacing. Gaps > 5s indicate system-wide stalls (see Stalls & System Gaps section).

#### Why gaps matter

FTDC collects every ~1 second. When the collector thread can't run — because the server is stalled, the OS is swapping, or a checkpoint is blocking — you get a gap. **The gap itself IS the symptom.** A 30-second gap means 30 seconds where you have zero observability.

#### Interpreting data around gaps

⚠️ **Rates during gaps are averaged and may be misleading.**

When `--rates` computes rates across a gap, it divides the delta by the actual time elapsed (correct arithmetic), but this hides what happened WITHIN the gap. Example:

- Sample at T=0: `opcounters.insert = 100,000`
- Gap: 30 seconds with no samples
- Sample at T=30: `opcounters.insert = 200,000`
- Computed rate: `100,000 / 30 = 3,333/s`

But the actual pattern might have been:
- T=0 to T=2: 100,000 inserts at 50,000/s (the actual burst)
- T=2 to T=30: 0 inserts (server stalled for 28 seconds)

**You cannot distinguish these from FTDC alone.** When you see a gap coinciding with unusual rates, flag the uncertainty explicitly. Cross-reference with `mongod.log` to understand what happened during the gap.

#### Gap severity guide

| Gap duration | Severity | Common causes |
|---|---|---|
| 2-5s | Low | Momentary CPU pressure, brief checkpoint I/O burst |
| 5-15s | Medium | Heavy checkpoint, eviction pressure, disk I/O saturation |
| 15-30s | High | Severe stall — checkpoint + eviction + I/O compounding |
| > 30s | Critical | OOM kill/restart, hardware issue, kernel-level stall, bug |

#### What to check around gaps

1. **Check the sample immediately BEFORE the gap** — this is your last observation before the stall. Was cache fill high? Were tickets exhausted? Was a checkpoint running?
2. **Check the sample immediately AFTER the gap** — what changed? Did a checkpoint complete? Did cache drop (eviction ran)?
3. **Check system metrics for the same window** — was disk I/O elevated? CPU iowait?
4. **Cross-reference with `mongod.log`** — look for slow operations, elections, or errors timestamped within the gap

#### Gaps in `--stats` output

`--stats` computes min/max/avg/p50/p99 **per sample, not time-weighted**. If a metric is at value X for 100 samples (100 seconds) then at value Y during a 30-second gap (1 sample), the stats treat X and Y as equally weighted even though Y persisted 30x longer. Use `p50` to see the most common value. Use `--json` to see the actual timeline when stats seem inconsistent.

#### Fundamental FTDC limitations

These apply regardless of the parser:
- **1-second granularity**: sub-second spikes are invisible. Two events that happened 100ms apart appear in the same sample.
- **No event ordering within a sample**: if cache spiked AND tickets exhausted in the same second, you cannot tell which caused which.
- **Gaps are opaque**: you know WHAT changed across a gap but not WHEN or in what order.
- **Counter deltas across gaps**: a cumulative counter that jumped by 100K across a 30s gap tells you 100K events happened, but not the rate profile within those 30 seconds.
- **Gauge snapshots ≠ steady state**: a gauge (like queue depth) sampled at 1Hz captures one instant per second. A queue that fills and drains 100 times per second may show `p50=95` because FTDC consistently catches it mid-fill. To determine if a queue is actually backlogged, compare input rate vs output rate (counters), not instantaneous depth (gauge). If throughput in ≈ throughput out, the queue is healthy regardless of the snapshot.

### Detecting Memory Growth Trends

```bash
./ftdc_parser /path/ --json --filter "tcmalloc\.generic\.(current_allocated|heap_size)" --nonzero
```

If `current_allocated_bytes` grows steadily while `cache.bytes currently in the cache` is stable, the growth is non-cache memory (possible leak).

---

## Workload Characterization

Before diagnosing issues, characterize the workload to set appropriate expectations. What looks abnormal for a steady-state OLTP workload may be perfectly healthy for a bulk load.

**Classify from opcounters:**
```bash
./ftdc_parser /path/ --stats --filter "opcounters\." --nonzero
```

| Pattern | Workload type | Expected behavior |
|---|---|---|
| High insert, low everything else | Bulk load / data migration | Cache fills to 80%, checkpoints lengthen, tickets fluctuate — all normal |
| Balanced insert/query/update | Mixed OLTP | Cache should be stable, checkpoints short, tickets rarely exhausted |
| High query, low writes | Read-heavy | Low cache dirty %, minimal eviction, short checkpoints |
| High update, moderate query | Update-heavy OLTP | Watch for cache dirty pressure and history store growth |
| Periodic bursts with idle gaps | Batch / ETL | Sawtooth cache pattern expected, ticket exhaustion during bursts may be normal |

**What "healthy" looks like per workload type:**

| Metric | Bulk load | Steady OLTP | Read-heavy |
|---|---|---|---|
| Cache fill % (p50) | 75-85% (at eviction target) | 40-80% | 20-60% |
| Checkpoint duration | Can be 10s+ (growing with data) | < 30s | < 5s |
| Write tickets exhausted | Common during burst phases | Rare (< 5% samples) | Never |
| App thread eviction time | Some expected under sustained load | Should be near zero | Should be zero |
| Dirty cache % | 10-25% | 5-15% | < 5% |

**Why this matters:** If you see 14-second checkpoints during a bulk load of 60K inserts/sec, that is expected behavior — not a problem to diagnose. But the same 14-second checkpoints during light OLTP load would be a red flag.

---

## Avoiding Analytical Bias

**Every conclusion MUST be backed by multiple corroborating metrics. "Vibes-based" analysis is unacceptable.**

### Rule 1: Success Metrics Require Failure Metrics

Never conclude "X is fine" based only on a success/count metric. Always check the corresponding attempt/failure metrics.

| If you see... | You MUST also check... |
|---|---|
| `pages evicted by application threads = 0` | `application thread time evicting (usecs)`, forced eviction attempts/failures |
| `rejectedAdmissions = 0` | `attemptedAdmissions`, `successfulAdmissions` to confirm the limiter was active |
| `pages evicted = X` | `pages selected for eviction unable to be evicted`, `forced eviction failures` |
| `checkpoint most recent time = X ms` | `checkpoint.generation` to confirm checkpoints completed |

**Anti-pattern:** "App thread evictions = 0, so eviction kept pace" ❌
**Correct:** "App thread evictions = 0, BUT app thread eviction TIME was 3,013 seconds and worker failure rate was 64% - eviction was blocked, not unnecessary" ✓

### Rule 2: Validate Counter Magnitudes

Large counter values should be sanity-checked against related metrics:

- If a rate limiter shows billions of rejections, verify the success count is proportionally high
- If a counter shows more operations than the workload could produce, suspect parser/encoding issues
- Cross-reference with at least one independent metric (e.g., verify insert count against opcounters)

### Rule 3: Dynamic vs Static Configuration

Check if configuration values change over time before concluding exhaustion:

| Metric | What to check |
|---|---|
| `write.available = 0` | Does `write.out` exceed initial `write.totalTickets`? If yes, dynamic scaling is working |
| `read.available = 0` | Same - check if `out` exceeds configured `totalTickets` |
| Cache thresholds | Are they defaults or custom? Check `wiredTiger.cache.*configured` metrics |

**How to check with parser:**
```bash
./ftdc_parser /path/ --stats --filter "queues\.execution\.write\.(available|out|totalTickets)"
# If max(out) > min(totalTickets), dynamic scaling is active
```

**Anti-pattern:** "Tickets exhausted 40% of the time = starvation" ❌
**Correct:** "Tickets exhausted 40% of samples, but `out` reached 126 (initial config was 8) - dynamic admission control is scaling up, not starving" ✓

### Rule 4: State What You Did NOT Check

Before concluding, explicitly list:

1. Which related metrics you DID check
2. Which related metrics you did NOT check (and why)
3. What would change your conclusion if found

### Rule 5: Architecture-Specific Constraints

Before applying traditional patterns, confirm the architecture (from `--metadata` output):

| Architecture | Metrics that behave differently |
|---|---|
| Disaggregated storage | Eviction blocked by checkpoint constraints, PALI rate limiting, materialization frontier. **Local disk I/O is irrelevant** — data goes to log server over network. Check `disagg.logServer.*` instead of `systemMetrics.disks.*` |
| Atlas/Serverless | Different ticket defaults, external admission control |
| Sharded clusters | Per-shard vs aggregate metrics, router overhead |

**Example:** In disaggregated storage, millions of pages being "blocked from eviction because they can only be written by the next checkpoint" is an architectural constraint, not a bug. Similarly, 50% forced eviction failure rate is expected — pages must go through the checkpoint→log server pipeline and cannot be evicted locally.

**Disagg-specific metrics to check:**
```bash
./ftdc_parser /path/ --stats --nonzero --filter "disagg|materialization"
```

---

## Cache Pressure & Eviction

### Application Thread Eviction (Latency Spikes)

**Metrics:**

- `ss wt cache bytes currently in the cache` / `maximum bytes configured` — fill ratio
- `ss wt cache tracked dirty bytes in the cache` / `maximum bytes configured` — dirty ratio
- `ss wt cache bytes allocated for updates` / `maximum bytes configured` — updates ratio
- `ss wt cache application thread time evicting (usecs)` — app threads doing eviction
- `ss wt thread-yield application thread time waiting for cache` — threads blocked

**Parser command:**
```bash
./ftdc_parser /path/ --stats --nonzero --filter "cache\.(bytes currently|maximum bytes|tracked dirty|allocated for updates|application thread)"
```

**Thresholds (defaults, check for custom overrides in metadata):**

- `eviction_trigger`: 95% fill → eviction starts
- `eviction_dirty_trigger`: 20% dirty → dirty eviction starts
- `eviction_updates_trigger`: 10% updates → update eviction starts
- `eviction_target`: 80% → goal for eviction

**Pattern:** When cache hits triggers, background eviction threads work to reduce usage. If insufficient, application threads are "recruited" causing latency spikes.

**Known Issue (WT-11300):** Eviction queue empty despite cache pressure

- Updates ratio steadily increases to/exceeds 10%
- `ss wt cache eviction empty score` spikes to 100
- **Fix:** Upgrade to patched version (v7.0.15+, v8.0.4+)

### Aggressive Eviction Mode Bug (WT-13090 / WT-13283)

**Metrics:**

- `ss wt cache eviction currently operating in aggressive mode`
- `ss wt transaction oldest pinned transaction ID rolled back for eviction`

**Pattern:** Value should be 0 or very small. Large sustained values (thousands+) indicate integer overflow bug (WT-13090).

```bash
./ftdc_parser /path/ --stats --filter "aggressive mode|oldest pinned transaction ID rolled back"
# Check max value. Normal: 0-100. WT-13090: thousands or millions
```

- **Fix:** Restart mongod or upgrade to patched version

### Eviction Stuck — Not Making Progress (SERVER-83186)

**Pattern:** Dirty cache stays high, eviction makes no progress, system stalls. Application threads are pulled into eviction but can't evict anything because long-running uncommitted transactions pin content.

**Diagnostic (DTA Playbook):**

1. Check if workload has many threads performing medium-sized writes — look at writer tickets
2. Check for prepared transactions (cannot be rolled back)
3. Verify cache under pressure: `ss wt cache dirty fill ratio`, `ss wt cache allocated for updates ratio`
4. If pages are queued AND evicted AND restored — eviction is churning without progress:
   - `ss wt capacity bytes written for eviction` = 0 (no bytes actually written)
   - `ss wt cache pages queued for eviction` is high
   - `ss wt cache modified pages evicted` is high
   - `ss wt cache pages written requiring in-memory restoration` matches evicted pages
5. Check for aggressive score overflow (see above): very large `eviction currently operating in aggressive mode` + `oldest pinned transaction ID rolled back for eviction` = 0

```bash
./ftdc_parser /path/ --stats --nonzero --filter "cache\.(dirty fill|allocated for updates|pages queued|modified pages evicted|pages written requiring|aggressive mode)"
./ftdc_parser /path/ --stats --nonzero --filter "capacity\.bytes written for eviction|oldest pinned transaction ID"
```

**Workarounds:**
- Reduce writer tickets (start at 16-32)
- Increase cache size
- v8.0+: Increase `eviction_updates_trigger` to 40%, step up in increments of 10%
- If `eviction server skips pages that are written with transactions greater than the last running` is spiking, try fix from WT-9575

**Linked:** HELP-70986, HELP-71738, WT-13283, WT-12280, SERVER-83186, SPM-3546

### Eviction Analysis Checklist

**CRITICAL: Do not conclude eviction is healthy based on success metrics alone.**

```bash
# Get all eviction-related stats in one pass
./ftdc_parser /path/ --stats --nonzero --filter "cache\.(pages evicted|eviction|forced eviction|hazard|application thread|materialization|checkpoint blocked)"
```

| Metric Category | Success Metric | Failure/Attempt Metrics to Check |
|---|---|---|
| App threads | `pages evicted by application threads` | `application thread time evicting (usecs)`, `forced eviction - pages selected count` |
| Worker threads | `eviction worker thread evicting pages` | `pages selected for eviction unable to be evicted` |
| Forced eviction | `forced eviction - pages evicted that were dirty count` | `forced eviction - pages selected count`, `forced eviction - pages selected unable to be evicted count` |

**Failure Rate Calculation:**

```
Eviction failure rate = (forced selected unable) / (forced selected count) × 100
- < 10%: Normal
- 10-30%: Elevated, check reasons
- > 30%: Significant issue, investigate blocking reasons
```

**Common Eviction Block Reasons (check these counters):**

| Counter (full FTDC path) | Meaning |
|---|---|
| `ss wt cache hazard pointer blocked page eviction` | Page in active use by another thread |
| `ss wt cache page eviction blocked in disaggregated storage as it can only be written by the next checkpoint` | Disagg-specific: page must wait for checkpoint |
| `ss wt cache checkpoint blocked page eviction` | Checkpoint holding page |
| `ss wt cache pages selected for eviction unable to be evicted because of active children on an internal page` | Internal page with active children |
| `ss wt cache page eviction blocked due to materialization frontier` | Disagg-specific: tiered storage constraint |

---

## Bursty/Sawtooth Workload Patterns

### Identification

**Parser commands:**
```bash
# Step 1: Check op rate distribution
./ftdc_parser /path/ --rates --filter "opcounters\.(insert|update|delete)"

# Step 2: Check cache fill pattern
./ftdc_parser /path/ --json --filter "cache\.(bytes currently|maximum bytes)" --nonzero

# Step 3: Check ticket correlation
./ftdc_parser /path/ --json --filter "queues\.execution\.write\.(available|out)"
```

**Pattern indicators:**

- > 80% of operations occur in <20% of sampled time
- Cache fill shows rapid rise (>5%/sec) followed by plateau or fall
- Inter-burst intervals are regular (e.g., exactly 300 seconds = checkpoint interval)
- Write tickets exhausted during burst, recover during idle

**How to detect burstiness from `--rates` output:**

```
Burstiness Score = (ops in top 10% of samples) / (total ops) × 100
- Score > 80%: Extreme burstiness
- Score 50-80%: High burstiness
- Score < 50%: Normal distribution
```

### Cache Sawtooth Pattern

**Visual signature:** Cache fill % shows repeated rapid rises followed by gradual/sharp falls.

**Interpretation:**

- **Rise phase:** Burst writes filling cache faster than eviction can clear
- **Fall phase:** Background eviction catching up, or checkpoint flushing dirty data
- **Flat top at 80%:** Eviction target reached, system in equilibrium
- **Spikes to 95%+:** Eviction trigger hit, potential app thread recruitment

**When sawtooth is problematic:**

- Falls are driven by app thread eviction (check `--rates --filter "pages evicted by application"`)
- Peaks consistently hit 95%+ (eviction trigger threshold)
- Pattern frequency matches client operation timing (external cause)

**When sawtooth is acceptable:**

- Falls driven by background eviction threads
- Peaks stay below 80% (eviction target)
- System maintains throughput despite visual pattern

### What NOT to Assume

- Bursty patterns are not inherently bad if throughput is acceptable
- Customer workloads may legitimately require burst capacity
- Focus on whether MongoDB handles bursts gracefully, not eliminating them
- Sawtooth cache patterns may be normal for batch workloads

---

## Write Ticket Exhaustion

**Parser commands:**
```bash
# Stats overview
./ftdc_parser /path/ --stats --filter "queues\.execution\.write"

# Time-series to see exhaustion episodes
./ftdc_parser /path/ --json --filter "queues\.execution\.write\.(available|out|totalTickets)"
```

**Pattern:** Available drops to 0, queue grows, operations stall.

### Correlated Metrics

| Metric | What it tells you |
|---|---|
| `opcounters` rate during exhaustion | Whether exhaustion correlates with high throughput |
| `write.out` vs `write.totalTickets` | Whether configured tickets are fully utilized |
| `globalLock.currentQueue.writers` | Whether writes are queuing behind ticket exhaustion |
| `wt transactions` active/waiting | Whether long transactions are holding tickets |

### Write Conflict Storm (CPU Spike + Ticket Exhaustion)

**Pattern:** Application becomes unusable with high write latency, CPU at 100%, write ticket exhaustion. Caused by excessive write conflicts (often duplicate key violations) creating a chain reaction.

**Key FTDC metrics:**

```bash
./ftdc_parser /path/ --stats --nonzero --filter "transaction update conflicts|cursor.*insert.*error|writeConflicts|duplicateKeyErrors|cpu user"
```

| Metric | What to look for |
|---|---|
| `ss wt transaction update conflicts` | High rate — tracks conflicts in real time |
| `ss wt cursor cursor insert calls that return an error` | Near-identical spikes as update conflicts = duplicate key violations |
| `system cpu user` | CPU spike to ~100% (write conflict retries spinning) |
| `ss average latency writes` | Write latency spiking simultaneously |
| `ss metrics operation duplicateKeyErrors` (v8.3+) | Direct duplicate key counter |

**Caution:** `ss metrics operation writeConflicts` is incremented AFTER operation completes, not when conflict is encountered. Use `wt transaction update conflicts` for real-time tracking.

**Resolution:** Force failover. Investigate application logic for duplicate key patterns (PERF-7188).
**Linked:** HELP-81387, SERVER-111130, SERVER-111068, PERF-7188

### Write Ticket Exhaustion from Profiling (Low slowMs)

**Pattern:** Profiler enabled (level 1 with low slowMs or level 2) causes write ticket exhaustion. Every profiled operation writes to the capped `system.profile` collection, which serializes inserts+deletes.

**Diagnostic:**

```bash
./ftdc_parser /path/ --stats --nonzero --filter "global.*active.*writers|global.*queued.*writers|commands.*profile|catalogStats.*systemProfile"
```

| Metric | What to look for |
|---|---|
| `ss ss global: active writers` | Near or at `totalTickets` |
| `ss ss global: queued writers` | Growing (sawtooth on secondaries) |
| `ss wt cursor cursor insert calls` + `cursor remove calls` | Much higher than `ss ops writes` (unexplained WT writes = profiler) |
| `catalogStats systemProfile` | Non-zero = profile collections exist |
| `profiler activeWriters` (v8.0+) | Highly correlated with active/queued writers |

**On secondaries:** Also takes ParallelBatchWriterMode (PBWM) lock in IS mode, blocking oplog application (needs PBWM in X mode), causing replication lag.

**Resolution:** Turn off profiler (`db.setProfilingLevel(0)`) and restore slowMs to default (100ms).
**Linked:** HELP-73352, HELP-75709, SPM-3737

### Capped Collection Ticket Exhaustion (Pre-7.0)

**Pattern:** Heavy writes to capped collections (logging, metrics, profiling) cause write ticket exhaustion because capped writes are serialized via a metadata resource lock. Threads hold tickets while waiting for the lock.

**Diagnostic:** Confirm pre-7.0 version + write ticket pressure + capped collection write activity.

**Resolution:** Upgrade to 7.0+ (SERVER-82180) or replace capped collections with TTL-indexed regular collections.
**Linked:** HELP-85891, HELP-51123, SERVER-82180

---

## Checkpoints

### Long-Running Checkpoints

**Parser commands:**
```bash
# Stats overview
./ftdc_parser /path/ --stats --filter "checkpoint\."

# Time-series to see individual checkpoints
./ftdc_parser /path/ --json --filter "checkpoint\.(most recent time|generation)"
```

**Key metrics (full FTDC paths):**

- `ss wt checkpoint.most recent time (msecs)` — last checkpoint duration
- `ss wt checkpoint.max time (msecs)` — longest checkpoint
- `ss wt checkpoint.generation` — checkpoint count (increments per completion)
- `ss wt checkpoint.total time (msecs)` — cumulative checkpoint time

**Thresholds:**

- < 60s: Normal
- 60-300s: Monitor closely
- > 300s: Investigate (blocks schema operations)

**Duration vs Interval analysis:** (see Analytical Thinking Rule 2)
Compare `most recent time` against `syncdelay` from metadata. If checkpoint duration ≥ syncdelay, checkpoints are running back-to-back and reducing the interval has zero effect.

**Known Issue (WT-12798):** Large cache + large checkpoint target can cause very long checkpoints.

- **Fix:** Reduce `checkpoint=(wait=X)` or cache size

### Large Cache Causing Long Checkpoint (WT-12798)

**Pattern:** Cache is large (100GB+), dirty fill ratio looks low (5%), but absolute dirty bytes are huge (5% of 300GB = 15GB). Checkpoint must write all this data, taking many minutes.

**Diagnostic:**

```bash
./ftdc_parser /path/ --stats --nonzero --filter "cache\.(dirty fill|maximum bytes configured|tracked dirty bytes)"
./ftdc_parser /path/ --stats --filter "checkpoint\.(most recent time|currently running)"
./ftdc_parser /path/ --stats --filter "connection pthread mutex condition wait calls"
```

**Key insight:** Don't look at dirty fill *ratio* alone. Calculate absolute dirty bytes: `tracked dirty bytes in the cache`. If this is >10GB, checkpoints will be slow regardless of the ratio.

**Also check:** Whether dirty data is concentrated in few collections (`ss wt connection files currently open` low + `ss catalogStats collections` low). If so, eviction and checkpoint can't run concurrently on the same file, making it worse.

**Resolution:**
- Lower `eviction_dirty_target` to 1%
- Increase eviction worker threads to 8-16
- v8.0+: Upgrade for dedicated cleanup thread (WT-12657)
- Distribute writes across more collections if concentrated in few

**Linked:** WT-12798, HELP-86334, HELP-86942

### Checkpoint-dhandle/SchemaLock Contention

**Pattern:** Very high `schema lock application thread wait time` with many active dhandles. Checkpoint prepare phase contends with sweep server for dhandle list lock.

**Diagnostic:**

```bash
./ftdc_parser /path/ --stats --nonzero --filter "lock schema lock|data-handle.*currently active|data-handle.*sweep|checkpoint prepare"
```

| Metric | What to look for |
|---|---|
| `ss wt lock schema lock application thread wait time` | Very high values (seconds) |
| `ss wt data-handle connection data handles currently active` | Very high count (10K+) |
| `ss wt data-handle connection sweep dhandles closed` | Low relative to active count |
| `ss wt checkpoint prepare currently running` (v7.1+) | Extended periods |

**Resolution:**
- Reduce `close_idle_time` from 600s (default) to 15s to make sweep more aggressive
- v5.0+: Apply WT-7929 for better statistics during stalls
- **Linked:** HELP-53809, HELP-72481, WT-13663, WT-14140, WT-15762

### Checkpoint Latency with Very Low Bytes Written

**Pattern:** Checkpoint takes 30+ minutes but barely writes any data. Checkpoint cleanup is traversing massive btrees skipping obsolete pages.

**Diagnostic:**

```bash
./ftdc_parser /path/ --stats --nonzero --filter "cursor next calls that skip.*100|checkpoint-cleanup"
```

If `wt cursor cursor next calls that skip greater than or equal to 100 entries` has a sustained high rate during checkpoint, cleanup is the bottleneck.

**Resolution:** Reduce `minSnapshotHistoryWindowInSeconds`, apply WT-8771/WT-9502/WT-10807. Upgrade to v8.0 for dedicated cleanup thread (WT-12657).
**Linked:** HELP-60190, HELP-80559, WT-8771, WT-9502, WT-10807

### Post-Reboot Long Checkpoint (Out-of-Order Updates)

**Pattern:** After a system reboot, WiredTiger performs a full checkpoint. Out-of-order updates cause this checkpoint to take a very long time cleaning up metadata, leading to replication lag.

**Diagnostic:** Long `checkpoint most recent time` immediately after server restart + hot page contention metrics.

**Resolution:** Apply WT-12609 and WT-12657 fixes.
**Linked:** HELP-79144

### Checkpoint Blocking (Windows/Antivirus)

**Pattern:** Checkpoint stalls for exactly 60 seconds repeatedly.

- Windows Defender or antivirus scanning WiredTiger files
- **Fix:** Exclude dbPath from antivirus scanning

### Drops Blocked by Checkpoint

**Metrics:**

- `ss storageEngine dropPendingIdents` - Tables waiting to be dropped
- `ss wt session table drop successful calls` - Successful drops

**Pattern:** `dropPendingIdents` grows but successful drops = 0

- Long-running cursors/transactions block drops until checkpoint completes

---

## Replication

### 30-Second Oplog Stalls (SERVER-92554)

**Applicability:** Multi-node replica sets on Linux with glibc only.

**Pattern:** Replication lag jumps by exactly 30 seconds, repeatedly.

- Caused by glibc malloc arena contention in oplog visibility
- **Fix:** Set `MALLOC_ARENA_MAX=1` or upgrade to patched glibc

### Oplog Holes from Large Batch Inserts

**Metrics:**

- `ss wt log log sync operations` - Journal syncs
- Replication lag increases during large inserts

**Pattern:** Large multi-document inserts create oplog "holes" that block secondaries.

- **Fix:** Batch inserts into smaller chunks (< 1000 docs)

### replWriterThreadCount Exhaustion

**Pattern:** Oplog application slows on secondary despite low cache pressure and unsaturated CPU.

- Default `replWriterThreadCount=16` may be insufficient for high-throughput workloads
- **Fix:** Increase `replWriterThreadCount` (up to 256)

### ReplicationCoordinator Mutex Contention

**Pattern:** High heartbeat/ping time between nodes (symmetrically high both ways). CPU not fully utilized. Multiple operations contend for the ReplicationCoordinator mutex.

**Diagnostic:**

```bash
./ftdc_parser /path/ --stats --nonzero --filter "replSetUpdatePosition|repl.*waiters|log flush operations|network.*getmores"
```

| Metric | What to look for |
|---|---|
| `replSetGetStatus.members.*.pingMs` | High (>100ms) heartbeat time, symmetric |
| `ss metrics commands replSetUpdatePosition total` | Throughput drops on primary/sync source |
| `ss metrics repl waiters replication` | Increases significantly on primary |
| `ss wt log log flush operations` | Decreases on primary/sync source |
| `ss metrics repl network getmores num` | Slowness on secondary |

**Note:** `ss locks Mutex acquireCount` does NOT include the ReplicationCoordinator mutex.

**Resolution:** Reduce concurrency on primary, upgrade to larger machine, apply SERVER-90213/SERVER-89242/SERVER-84449.
**Linked:** HELP-63330, HELP-63535, HELP-61347

### Replication Lag from Network Issues

**Pattern:** Replication lag where the secondary's oplog buffer is at 0 (not application bottleneck) and getmores are low.

**Diagnostic:**

```bash
./ftdc_parser /path/ --stats --nonzero --filter "repl\.(buffer|network)"
```

| Metric | What to look for |
|---|---|
| `ss metrics repl buffer count/sizeBytes` | At 0 = not oplog application issue |
| `ss metrics repl network getmores num/numEmptyBatches` | Low getmores = secondary not receiving data |
| `ss metrics repl network getmores totalMillis` | High on secondary |
| `ss metrics repl network oplogGetMoresProcessed totalMillis` | Low on sync source (rules out sync source) |
| `system TcpExt TCPFastRetrans` | Network retransmissions between nodes |

**Resolution:** Investigate network path between nodes. This may not be a Replication issue — consider escalating to Networking.
**Linked:** HELP-63230, HELP-64589, HELP-68987

### Replication Lag from Cache Pressure on Secondaries

**Pattern:** Secondary oplog application threads stopped by eviction. Eviction walk skips leaf pages, failing to release cache pressure.

**Diagnostic:**

```bash
./ftdc_parser /path/ --stats --nonzero --filter "cache\.(fill ratio|eviction empty score|internal pages seen|pages seen by eviction|tracked.*leaf|thread-yield.*waiting for cache)"
```

| Metric | What to look for |
|---|---|
| `ss wt cache fill ratio` | >95% = high pressure |
| `ss wt thread-yield application thread time waiting for cache` | Any non-zero value |
| `ss wt cache eviction empty score` | Consistently high = pressure not released |
| `ss wt cache internal pages seen by eviction walk` ≈ `pages seen by eviction walk` | Leaf pages being skipped |

**Resolution:** Decrease `eviction_target` to 75, increase eviction threads.
**Linked:** HELP-57213

### Replication Lag from Compaction

**Pattern:** Compaction on a node generates dirty content faster than eviction can handle. Extended checkpoints + dirty cache → application threads stopped → replication lag.

**Diagnostic:**

```bash
./ftdc_parser /path/ --stats --nonzero --filter "session table compact running|cache dirty fill ratio|eviction empty score|history store.*on-disk"
```

**Resolution:** Avoid compaction during heavy write workloads. Use v8.0+ compaction improvements (PM-3091). Apply WT-13216.
**Linked:** HELP-59110, HELP-66936

### Replication Lag from Backup Cursor (dhandle Growth)

**Pattern:** Lag always around backup time, occurs after backupCursor is closed. Large number of dhandles and extra memory from backup.

**Diagnostic:**

```bash
./ftdc_parser /path/ --stats --nonzero --filter "data-handle.*currently active|estimated data handle memory|storageEngine backupCursorOpen"
```

**Resolution:** Upgrade to v5.0.19+ (WT-10253 makes dhandle sweep more responsive).
**Linked:** HELP-60868, WT-10253

### Replication Lag During dbCheck

**Pattern:** dbCheck operations are costly on secondaries. Primary does dbCheck non-blocking, but secondaries must apply batch-by-batch in blocking fashion.

**Diagnostic:**
- `ss metrics repl buffer sizeBytes` large on lagged secondary
- `ss metrics repl apply batches totalMillis` jumps during dbCheck

**Resolution:** Increase instance size or run dbCheck with `batchWriteConcern: {w: <all nodes>}` to throttle on primary.
**Linked:** HELP-72002

### Replication Lag on Delayed Secondary

**Pattern:** Persistent lag exceeding `secondaryDelaySecs`. Oplog entries with very different optimes in the same applier batch delay the entire batch.

**Resolution:** Upgrade to version with SERVER-105478 fix.
**Linked:** HELP-75709

### Replication Lag — Asymmetric

**Pattern:** One secondary lags while others keep up, despite performing similar amounts of work.

**Diagnostic (compare across nodes):**

```bash
# For each node, check:
./ftdc_parser /path/ --stats --nonzero --filter "repl\.(apply ops|buffer count)"
./ftdc_parser /path/ --stats --nonzero --filter "cache fill ratio|cpu user|disk.*utilization"
```

**Three sub-patterns from DTA Playbook:**

1. **Similar work, asymmetric lag:** Both secondaries apply similar ops, but one lags. Lagging node may have degraded hardware, memory fragmentation, or different B-tree shape. Try restarting/resyncing.

2. **Slight cache difference:** Lagging node has less cache (higher `ss mem resident` → less OS page cache). Buffer count oscillates full/empty. Configure caches to match.

3. **Maxed CPU and/or disk IOPs:** `system cpu user` near 100% or `disk average utilization` near 100% on lagging secondary. May need to upscale node.

**For all sub-patterns:** Ensure `replWriterThreadCount` is between #cores and 2*#cores.
**Linked:** HELP-52939, HELP-62594

### Replication Lag — Bottleneck on Oplog Applier Threads

**Pattern:** Primary has high concurrency but secondary can't keep up. No storage engine bottleneck on secondary, low CPU utilization.

**Diagnostic:**

```bash
./ftdc_parser /path/ --stats --filter "global.*active writers"
# Compare primary's active writers to replWriterThreadCount (default 16)
```

If primary's `ss global active writers` is significantly larger than `replWriterThreadCount`, secondary is bottlenecked.

**Resolution:** Set `replWriterThreadCount` to 32-48 (up to 2 * #cores). Requires restart.
**Linked:** HELP-79470

### j:false Write Feedback Loop (4.2/4.4)

**Pattern:** Using `{w:majority, j: false}` writes on v4.2/4.4 causes replication lag spiral. Less frequent journal flushes → larger oplog batches → slower replication → less journaling (negative feedback loop).

**Diagnostic:**

```bash
./ftdc_parser /path/ --stats --nonzero --filter "log flush operations|repl.*getmores.*numEmpty|repl.*waiters"
```

- `ss wt log log flush operations` drops (less journaling)
- `ss metrics repl network getmores numEmptyBatches` increases
- High heartbeat time (ReplicationCoordinator mutex contention, see above)

- **Fix:** Use `{j: true}` writes. Upgrade from 4.2 to 4.4+ (SPM-1456/SPM-1274)
**Linked:** HELP-52737, HELP-61389, SERVER-55606

### Commit Point / LSRT Stalling

**Pattern:** Commit point does not advance despite all services active. Usually caused by insufficient data-bearing voting nodes for write majority (e.g., 5-node config with arbiter + non-voting data node).

**Diagnostic:** Check replica set config for voting/non-voting/arbiter combinations. Check `rs writableVotingMembersCount`.

**Resolution:** Remove arbiter or give non-voting data-bearing node a vote.
**Linked:** HELP-84061

---

## Stalls & System Gaps

### FTDC Gaps (No Data)

**Detection:** The parser summary mode auto-detects gaps > 5 seconds.

**Pattern:** Missing samples in FTDC data indicates system-wide stall.

**Common Causes:**

1. **Checkpoint stall** - All threads blocked
2. **Compaction** - `compact` command blocks everything
3. **Backup cursor** - Holding resources too long
4. **OOM killer** - Check dmesg logs
5. **Disk I/O saturation** - Check iostat

### Compaction Blocking Backup

**Pattern:** Backup snapshot stall — backup cursor needs checkpoint lock but long checkpoint (caused by compaction) holds it.

```bash
./ftdc_parser /path/ --stats --filter "compact|checkpoint.*most recent time"
```

**Resolution:** Avoid concurrent compaction and backup. Increase `backupSocketTimeoutMs` to be larger than average checkpoint time.
**Linked:** HELP-63834, WT-13502, HELP-73460

### Compact Causes High Dirty Fill Ratios

**Pattern:** Compact marks in-memory pages as dirty when their disk blocks need to be moved. This pushes dirty content above `eviction_dirty_trigger`, pulling application threads into eviction. Compact fails to throttle itself.

```bash
./ftdc_parser /path/ --stats --nonzero --filter "cache dirty fill ratio|session table compact running|checkpoint.*most recent time"
```

**Resolution:** Don't run compact concurrently with heavy write workloads. Apply WT-13216 throttling fix.
**Linked:** HELP-71087, HELP-66936, WT-13216

### Compact Not Freeing Disk Space (Checkpoint Conflict)

**Pattern:** Compact logs "EBUSY due to an in-progress conflicting checkpoint." Compact walks the btree but if checkpoint interrupts, it restarts the walk. After 100 passes, may miss pages at the end of the file.

**Diagnostic:**

```bash
./ftdc_parser /path/ --stats --nonzero --filter "session table compact (passes|conflicted|running)"
```

**Resolution:** Apply WT-12846 (v8.1). Workaround: logical initial sync to reclaim space.
**Linked:** HELP-61261, HELP-73002, WT-12846

### Incremental Backup dhandle Growth

**Pattern:** Incremental backup opens a file cursor for every file in the database, causing dhandle count to grow and potentially exceed file descriptor limits.

```bash
./ftdc_parser /path/ --stats --nonzero --filter "data-handle.*currently active|storageEngine backupCursorOpen"
```

**Resolution:** Increase file descriptor limits to match object count. Full snapshots don't require opening all dhandles.
**Linked:** HELP-72382, WT-14322

### WT Unable to Drop Pending Idents (EBUSY Loop)

**Pattern:** `storageEngine dropPendingIdents` levels out and never returns to zero. `session table drop failed calls` grows. Disk usage increases because tables can't be dropped.

```bash
./ftdc_parser /path/ --stats --nonzero --filter "dropPendingIdents|session table.*drop"
```

**Cause:** Checkpoint cleanup threads dirty newly created empty tables, returning EBUSY on drop (WT-15225).

**Resolution:** Restart the node. Upgrade to v8.1+ for WT-15225 fix.
**Linked:** HELP-64945, HELP-80658, WT-14636, WT-15225

---

## Time-Series Collections

### Bucket Memory Pressure

**Metrics:**

- `ss timeseriesBucketStatistics numBucketsClosedDueToCachePressure`
- `ss timeseriesBucketStatistics numBucketsArchivedDueToMemoryThreshold`

**Pattern:** High values indicate memory pressure closing buckets prematurely.

- **Fix:** Increase `timeseriesIdleBucketExpiryMemoryUsageThreshold` (default 2.5%)

### High Cardinality Issues

**Pattern:** Too many unique metaField values creates bucket fragmentation.

- Each unique metaField value gets its own bucket
- Causes excessive memory usage and slow queries
- **Fix:** Reduce metaField cardinality or use secondary indexes

### TTL Monitor Not Deleting (Extended Range Dates)

**Pattern:** `ss metrics ttl deletedDocuments` is 0 despite eligible buckets. If the time-series collection contains dates outside the standard range (e.g., year 1970 from faulty sensor), the TTL Monitor skips the entire collection.

**Diagnostic:** Check logs for warning id 6679402: "Time-series collection contains dates outside the standard range."

**Resolution:** Upgrade to v8.0 patch (SERVER-97368). Remove extended range measurements.
**Linked:** SERVER-97368, SERVER-79864, HELP-70507

### Time-Series Write Latency — Cluster Configuration

**Common causes:**
- Shared cluster: time-series parameters apply server-wide, contending with regular collections. Dedicated cluster recommended.
- Incorrect `timeseriesIdleBucketExpiryMemoryUsageThreshold` competing with WT cache (default 2.5%, bumped to 5% in 8.0.5+). Combined WT cache + bucket catalog should not exceed 60%.
- Unbalanced cardinality across shards: check `numBucketsOpenedDueToMetadata` across nodes.

**Linked:** HELP-45638, HELP-68983, HELP-80234

### Time-Series Write Latency — Slow Bucket Reopening (v6.3+)

**Pattern:** Query-based bucket reopening increases bucket density but adds write latency. Some writes perform disk queries to find eligible buckets.

**Diagnostic:**

```bash
./ftdc_parser /path/ --stats --nonzero --filter "bucketCatalog\.(numBucketsQueried|numBucketsFetched|numBucketsReopened|numBucket.*Failed)"
```

**To disable (reduce latency, sacrifice density):** Delete the default `{<metaField>: 1, <timeField>: 1}` index.
**Linked:** HELP-45638, HELP-55479, HELP-68181

### Query Spilling Bottleneck on WiredTiger (v8.2+)

**Pattern:** Queries spill to disk using a separate WiredTiger instance (SPM-3313). If spilling becomes bottlenecked on eviction in that instance, check `spillWiredTiger` section in FTDC.

```bash
./ftdc_parser /path/ --stats --nonzero --filter "spillWiredTiger\.cache"
```

**Key metrics:** Same eviction/cache metrics as main WT but under `spillWiredTiger` prefix.

**Tuning parameters (startup-only):**
- `spillWiredTigerCacheSizePercentage` (default 2.5% of system memory)
- `spillWiredTigerEvictionThreadsMin/Max` (default 1)
- `spillWiredTigerSessionMax` (default 1024)

**Linked:** SPM-3313

---

## Memory & OOM

### tcmalloc Memory Growth

**Parser command:**
```bash
./ftdc_parser /path/ --json --filter "tcmalloc\.(generic|derived)" --nonzero
```

**Metrics:**

- `ss tcmalloc generic current_allocated_bytes`
- `ss tcmalloc generic heap_size`
- `ss tcmalloc derived allocated minus wt cache` — non-WT memory usage

**Pattern:** If `current_allocated_bytes` grows steadily while cache size is stable, indicates non-cache memory growth (possible leak).

### tcmallocReleaseRate Memory Inflation (v8.0+)

**Pattern:** On v8.0+ with custom `tcmallocReleaseRate`, monitoring shows inflated "system memory used by other processes." This is tcmalloc internal free memory NOT returned to OS — not actual memory pressure.

**Diagnostic:**
- `ss tcmalloc total_bytes_held` or `derived_total_free_bytes` is high
- `system memory anon total` growing
- No evidence of non-mongod process consuming memory

**Resolution:** Decrease or remove custom `tcmallocReleaseRate`. Review memory-based autoscaling thresholds.
**Linked:** CLOUDP-301473, HELP-84907, HELP-84602

### Index Build OOM (Bulk Cursor — WT-15594)

**Pattern:** OOM during index build. `ss tcmalloc derived allocated minus wt cache` growing steadily + `ss wt cursor bulk cursor count` = 1 persistently.

**Diagnostic:**

```bash
./ftdc_parser /path/ --stats --nonzero --filter "tcmalloc.*derived.*minus|cursor bulk cursor|indexBulkBuilder"
```

If heap profiler shows `__wt_modify_apply_item` → `__wt_modify_reconstruct_from_upd_list` in top memory paths, this is WT-15594.

**Resolution:** Increase `maxIndexBuildMemoryUsageMegabytes` (recommend 20% of RAM, 1-8GB range). Use FCBIS instead of LIS if applicable. Enable `tcMallocAggresiveDecommit`.
**Linked:** SERVER-68982, SERVER-106589, HELP-78562, WT-15594

### Sharded Cluster Primary Memory Leak (SERVER-73915)

**Pattern:** Linear increase in `ss tcmalloc derived allocated minus wt cache` on primary node running cross-shard transactions. Non-primary nodes don't show growth.

**Diagnostic:**

```bash
./ftdc_parser /path/ --stats --nonzero --filter "tcmalloc.*derived.*minus|commands.*coordinateCommitTransaction"
```

If `ss metrics commands coordinateCommitTransaction Total` has non-zero rate AND `tcmalloc derived allocated minus wt cache` grows linearly, this is SERVER-73915. ~650 bytes leaked per cross-shard transaction.

**Resolution:** Step down impacted node (frees all leaked memory, but deterministic — new primary will also leak). Upgrade to version with SERVER-103841.
**Linked:** HELP-73780, HELP-76164, SERVER-73915, SERVER-103841

### Session Max Exhaustion → Crash

**Pattern:** Primary crashes under high transaction load. WiredTiger error: "out of sessions, configured for 20030."

**Diagnostic:**

```bash
./ftdc_parser /path/ --stats --nonzero --filter "session (session_max|open_sessions)|current (open|active|inactive)"
```

| Metric | What to look for |
|---|---|
| `ss wt session open session count` | Approaching `session_max` |
| `ss current open/active/inactive` | Thousands of concurrent transactions |

**Resolution:** Increase `session_max`. Reduce concurrent multi-document transactions.
**Linked:** SERVER-82688, SERVER-108215

### TransactionTooLargeForCache Error

**Pattern:** Operations fail with "transaction is too large and will not fit in the storage engine cache" or "oldest pinned transaction ID rolled back for eviction."

MongoDB checks if `WT_STAT_SESSION_TXN_BYTES_DIRTY / WT_STAT_CONN_CACHE_BYTES_DIRTY_LEAF > transactionTooLargeForCacheThreshold` (default 0.75). If so, returns TransactionTooLargeForCache.

**Resolution:** Increase `eviction_dirty_trigger` or scale to a VM with more RAM.
**Linked:** HELP-70829

---

## Journal (Write-Ahead Log)

**Parser command:**
```bash
./ftdc_parser /path/ --stats --nonzero --filter "wiredTiger\.log\."
```

**Key metrics:**

| Metric | What it means |
|---|---|
| `ss wt log log sync time duration (usecs)` / `log sync operations` | Journal sync latency. High = slow disk or busy journal |
| `ss wt log log bytes written` | Journal write throughput |
| `ss wt log yields waiting for previous log file close` | Journal file rotation contention |
| `ss wt log slot consolidation busy` | Multiple threads competing for journal write slots |
| `ss wt log written slots coalesced` | How effectively writes are batched |

**Patterns:**

- **High journal sync latency** (> 10ms avg): Usually indicates disk I/O bottleneck. Cross-check with `systemMetrics.disks` latency.
- **Slot consolidation busy growing**: Multiple writers contending for journal — normal under heavy write load.
- **Yields for previous log file close**: Journal file rotation taking too long. May correlate with checkpoint activity.

---

## Block Manager & I/O

**Parser command:**
```bash
./ftdc_parser /path/ --stats --nonzero --filter "wiredTiger\.block-manager"
```

**Key metrics:**

| Metric | What it means |
|---|---|
| `ss wt block-manager bytes read` | Total data read from disk |
| `ss wt block-manager bytes written` | Total data written to disk |
| `ss wt block-manager blocks pre-loaded` | Prefetch effectiveness |
| `ss wt block-manager mapped bytes read` | Memory-mapped reads (if applicable) |

**Interpretation:** Block manager bytes written should roughly correlate with cache eviction + checkpoint writes. If block manager writes greatly exceed these, investigate fragmentation or compaction activity.

---

## Data Handles & Cursors

**Parser command:**
```bash
./ftdc_parser /path/ --stats --nonzero --filter "data-handle|cursor\.(cursor |session)"
```

**Key metrics:**

| Metric | What it means |
|---|---|
| `ss wt data-handle connection data handles currently active` | Open table handles. Grows with number of collections/indexes |
| `ss wt cursor cached cursor count` | Cursor cache size. High = many collections |
| `ss wt cursor cursor operation restarted` | Cursor conflicts requiring restart |
| `ss wt session open session count` | Active WT sessions |

**Patterns:**

- **dhandles growing unbounded**: Possible with incremental backup (see Stalls section). Can also indicate collection proliferation.
- **High cursor restarts**: Usually correlates with write conflicts or page splits.
- **Session count near configured max**: Can cause operation queuing.

---

## Transaction Timestamps & Pinning

**Parser command:**
```bash
./ftdc_parser /path/ --stats --nonzero --filter "wiredTiger\.transaction\.transaction (range|oldest|read timestamp)"
```

**Key metrics:**

| Metric | What it means |
|---|---|
| `ss wt transaction transaction range of timestamps currently pinned` | How far back the oldest reader is holding. Large = history store pressure |
| `ss wt transaction transaction range of timestamps pinned by the oldest timestamp` | Oldest timestamp lag |
| `ss wt transaction transaction read timestamp of the oldest active reader` | Identifies stale readers |

**Pattern:** If `range of timestamps currently pinned` grows over time, a long-running transaction or cursor is preventing cleanup. Correlate with history store growth.

---

## Schema Changes in FTDC

**Detection:** The parser summary mode auto-detects when the metric count changes between samples.

**Why it matters:** When new collections, indexes, or server features activate, the FTDC metric schema changes. This causes:
- New metrics appearing mid-capture (absent from earlier samples)
- `--stats` may show some metrics with fewer data points than others
- Chunk boundaries at schema changes

**Not a problem, but important context:** If you see metrics with different sample counts or unexpected min=0 values, check if a schema change occurred during the capture window.

## Index Builds

### Rolling vs Replicated Index Build Stalls

**Pattern:** Index build hangs waiting for commit quorum.

- Rolling builds on replicated collections can stall during initial sync
- **Fix:** Use `commitQuorum: 0` or wait for initial sync to complete

---

## Locks & Contention

### RSTL Acquisition Timeout — Deadlock with Read Ticket Exhaustion (SERVER-75205)

**Pattern:** Step up/stepdown fails with fassert. At the time of trying to step up/down, `ss wiredTiger concurrentTransactions read out` jumps to 128 (all tickets exhausted). CPU at 100%. Once RSTL is enqueued, everything halts.

**Cause:** Bug where operations acquire the global lock and RSTL out of order after yielding. Operations hold RSTL in IX mode waiting for read tickets, while the step thread waits for RSTL in X mode, while ticket holders are stuck behind the step thread.

**Resolution:** Upgrade to 4.4.20+, 5.0.16+, 6.0.6+, 7.0+.
**Linked:** HELP-43649, SERVER-75205

### RSTL Acquisition Timeout — OldestActiveTxnTimestamp Thread (SERVER-94771)

**Pattern:** RSTL held by `OldestActiveTxnTimestamp` thread which iterates `config.transactions` table. If that table is very large, iteration takes too long, blocking step up/down.

**Resolution:** Upgrade to v8.1 with SERVER-94771.
**Linked:** HELP-64278, SERVER-94771

### RSTL Acquisition Timeout — Backup Cursor (WT-16057)

**Pattern:** Backup cursor holds RSTL in MODE_IX while calling WiredTiger API. Long-running checkpoint holds `hot_backup_lock`, preventing backup cursor from completing.

**Resolution:** Avoid Atlas maintenance running concurrently with backups. Apply WT-16057.
**Linked:** HELP-83258, WT-16057, SERVER-114110

### Schema Lock Contention

**Pattern:** DDL operations (createIndex, drop) or sweep server contend with checkpoint for schema lock.

- Very high `ss wt lock schema lock application thread wait time`
- See Checkpoint-dhandle/SchemaLock Contention in the Checkpoints section for detailed diagnostic

---

## Hot Pages & Forced Eviction

**Parser command:**
```bash
./ftdc_parser /path/ --stats --nonzero --filter "forced eviction|hazard pointer|maximum page size|reconciliation maximum seconds|thread-yield.*page acquire"
```

**Key metrics:**

| Metric | What it means |
|---|---|
| `ss wt cache forced eviction - pages selected count` | Pages selected for force eviction |
| `ss wt cache forced eviction - pages selected unable to be evicted count` | Failed force evictions |
| `ss wt cache hazard pointer blocked page eviction` | Page locked by another thread |
| `ss wt cache maximum page size seen at eviction` | Hot pages usually >>10MB (default threshold) |
| `ss wt reconciliation maximum seconds spent in a reconciliation call` | Long reconciliation = hot page |
| `ss wt thread-yield page acquire eviction blocked` | Threads blocked trying to evict |
| `ss wt thread-yield page acquire locked blocked` | Threads blocked on page lock |
| `ss wt thread-yield page acquire time sleeping` | Threads sleeping waiting for page |

**Pattern:** When `forced eviction - pages selected count` ≈ `forced eviction - pages selected unable to be evicted count`, nearly ALL pages selected for force eviction cannot be evicted.

- Indicates hot page contention (many threads accessing same page)
- The threads spin retrying force eviction, causing read/write latency
- Low dirty ratio (<3%) or low `eviction_dirty_bytes` can worsen this by triggering force eviction too quickly

**Also check:** `ss wt cache maximum page size seen at eviction` — hot pages usually have size much larger than 10MB default threshold.

**Resolution:** No eviction parameter changes help. Pre-7.0: reduce read/write tickets to 64. Change data access pattern to avoid concentrating writes on few pages. Reconfigure OS-level `vm.dirty_ratio`/`vm.dirty_bytes`.
**Linked:** HELP-62420, HELP-58812, HELP-58076, HELP-64050

### Reconciliation Latency from History Store Page Eviction

**Pattern:** WiredTiger eviction slowed by lock contention during in-memory splits on history store pages. Forced eviction of HS pages fails repeatedly, blocking data store page reconciliation.

```bash
./ftdc_parser /path/ --stats --nonzero --filter "forced eviction.*history store|thread-yield.*page acquire.*(sleeping|locked|busy)"
```

| Metric | What to look for |
|---|---|
| `ss wt cache forced eviction - history store pages selected while session has history store cursor open` | High values |
| `ss wt cache forced eviction - history store pages failed to evict while session has history store cursor open` | Matching high values |
| `ss wt thread-yielding page acquire time sleeping (usecs)` | Sudden jumps |
| `ss wt thread-yielding page acquire locked blocked` | Sudden jumps |

**Resolution:** Reduce `maxTargetSnapshotHistoryWindowInSeconds` to 1 or 0. Upgrade to v5.0+ for WT-10759 fix.
**Linked:** WT-10759, HELP-42841, HELP-64050

---

## History Store Pressure

**Parser command:**
```bash
./ftdc_parser /path/ --stats --nonzero --filter "history.store|oldest timestamp|snapshot-window"
```

**Metrics:**

| Metric | What it means |
|---|---|
| `ss wt cache bytes belonging to the history store table in the cache` | HS cache footprint |
| `ss wt cache the number of times full update inserted to history store` | HS write rate — spikes reflect increased HS activity |
| `ss wt cache history store table on-disk size` | HS disk footprint |
| `ss wt snapshot-window-settings` | Current snapshot history window size |

**Pattern:** History store growing unbounded indicates long-running snapshots.

- Old snapshots prevent history cleanup
- Check HS bytes as ratio of total cache — if HS dominates, it competes with user data
- **Fix:** Close long-running cursors/transactions, reduce `minSnapshotHistoryWindowInSeconds`

### History Store Fragmentation (v8.0+, WT-14968)

**Pattern:** In v8.0, the checkpoint cleanup thread (WT-12657) can take hours to process databases with many collections/indexes. During this time, history store pages with frequent inserts/deletes become fragmented.

```bash
./ftdc_parser /path/ --stats --nonzero --filter "checkpoint cleanup.*(successfull|pages removed|pages visited)"
./ftdc_parser /path/ --stats --nonzero --filter "historyStorageStats.*block-manager.*available for reuse"
```

**Diagnostic:**
- Large delay (hours) between `checkpoint cleanup successfull calls`
- High `historyStorageStats block-manager file bytes available for reuse`
- Periodic spikes in `checkpoint pages removed during checkpoint cleanup` with low values between

**Resolution:** Apply WT-14968 fix. Autocompact can process history store (manual compact cannot). Workaround: logical initial sync.
**Linked:** HELP-78086, WT-14968

### Read/Write Latency After Document Deletions/TTL

**Pattern:** Heavy delete workloads cause cursors to skip large numbers of deleted keys. Deleted content remains for `minSnapshotHistoryWindowInSeconds` (default 300s). High CPU from cursor traversal.

```bash
./ftdc_parser /path/ --stats --nonzero --filter "cursor.*(entries skipped|deleted pages skipped|remove calls)|ttl\.(deleted|passes)|forced eviction.*deleted items"
```

| Metric | What to look for |
|---|---|
| `ss wt cursor Total number of entries skipped by cursor next/prev calls` | Very high rate |
| `ss wt cache forced eviction - pages selected because of too many deleted items count` | Forced eviction from deletes |
| `ss metrics ttl deletedDocuments` / `ss metrics ttl passes` | TTL activity |
| `ss extra_info system_time_us` | High CPU from cursor skips |

**Alternative cause (HELP-69023):** All data on page deleted, pages clean, but `wt cache fill ratio` below `eviction_target` (80%), so pages never evicted. Identified by very large `Total number of in-memory deleted pages skipped during tree walk rate`.

**Resolution:** Reduce `minSnapshotHistoryWindowInSeconds`. Spread deletes over time. Restart to clear deleted pages from cache.
**Linked:** HELP-59479, HELP-63300, WT-10807, WT-10424, WT-13450

### Slow TTL Deletes (High Index Key Count)

**Pattern:** TTL monitor deleting documents very slowly. Each document has thousands of index keys.

**Diagnostic:** Compute `ss wt cursor cursor insert calls` / `ss opcounters insert` = average index keys per document. If this ratio is in the thousands, each TTL delete does massive work.

**Resolution:** Use partial indexes to reduce key count. Shard the collection. Use faster disk.
**Linked:** HELP-70152

---

## Disk Usage

### Excessive WT Log Generation (WT-12012)

**Pattern:** WT eviction struggles for >4 minutes, system activates verbose logging for eviction/checkpoint. If the stall resolves without resetting verbosity, excessive logs fill disk → crash.

**Resolution:** Reset verbose logs manually:
```
db.adminCommand({"setParameter": 1, "wiredTigerEngineRuntimeConfig": "verbose=[recovery_progress:1,checkpoint_progress:1,compact_progress:1,backup:0,checkpoint:0,compact:0,evict:0,history_store:0,recovery:0,rts:0,salvage:0,tiered:0,timestamp:0,transaction:0,verify:0,log:0]"})
```
Upgrade to v7.0.21+ for WT-12012 fix.
**Linked:** HELP-74663, WT-12012

### Artificially Large Oplog Storage Size

**Pattern:** Oplog `storageSize` grows from write influx exceeding oplog capacity. After truncation catches up, file size remains inflated.

**Diagnostic:**

```bash
./ftdc_parser /path/ --stats --nonzero --filter "oplog.*(storageSize|freeStorageSize)|oplogTruncation|block-manager.*allocations.*extension"
```

**Resolution:** WT releases space when no active blocks at end of file. Otherwise, initial sync reclaims space.
**Linked:** SERVER-70884, HELP-81844

---

## Sharding & API Version

### API Version Mismatch During Failover (SERVER-106075 / SERVER-106138)

**Pattern:** Client sets `apiVersion: 1` in cross-shard transactions. During failover, shard returns "API Version Mismatch" error on commit. Transaction coordinator misinterprets this as success. Transaction remains in prepared state, blocking subsequent operations with WriteConflict errors.

**Impact by version:**
- **v5.0-7.0:** Prepared transaction remains indefinitely. Oplog grows unbounded. Persistent WriteConflict errors. (HELP-75525)
- **v8.0+:** Prepared transactions may be reaped after 30 minutes (SERVER-105751), leaving data in torn state across shards. Before timeout: secondary hangs, startup crashes on lagged secondary. (HELP-73998)

**Diagnostic:**
- Logs: "APIMismatchError" in two-phase commit context
- Log id 4640401: WriteConflictException during oplog application with high retry attempts
- Growing oplog size without corresponding write volume

**Resolution:** Version-specific. Check `config.transactions` for old unfinalized prepared transactions.
**Linked:** HELP-73998, HELP-75525, SERVER-106075, SERVER-106138, SERVER-105751

---

## Quick Reference: Key Metric Paths

| Symptom | Primary Metric (full FTDC path) | Parser filter |
|---|---|---|
| Cache pressure | `serverStatus.wiredTiger.cache.bytes currently in the cache` | `--filter "cache\.bytes currently"` |
| Dirty cache | `serverStatus.wiredTiger.cache.tracked dirty bytes in the cache` | `--filter "cache\.tracked dirty"` |
| Write tickets | `serverStatus.queues.execution.write.available` | `--filter "queues\.execution\.write"` |
| Read tickets | `serverStatus.queues.execution.read.available` | `--filter "queues\.execution\.read"` |
| Checkpoint time | `serverStatus.wiredTiger.checkpoint.most recent time (msecs)` | `--filter "checkpoint\.(most recent time\|generation)"` |
| Connections | `serverStatus.connections.current` | `--filter "connections\.(current\|total)"` |
| Op counters | `serverStatus.opcounters.insert/query/update/delete` | `--filter "opcounters\."` |
| Repl lag | `replSetGetStatus.members.*.optimeDate` | `--filter "replSetGetStatus.*optime"` |
| Memory | `serverStatus.tcmalloc.generic.current_allocated_bytes` | `--filter "tcmalloc\.generic"` |
| History store | `serverStatus.wiredTiger.cache.bytes belonging to the history store table in the cache` | `--filter "history store.*cache"` |
| App eviction | `serverStatus.wiredTiger.cache.application thread time evicting (usecs)` | `--filter "application thread"` |
| Eviction blocks | `serverStatus.wiredTiger.cache.*blocked*` | `--filter "cache.*(blocked\|unable)"` |

---

## Parameter Tuning Quick Reference

From the DTA HELP Playbook. **Consult WT experts before changing in production.**

| Parameter | Default | Increase effect | Decrease effect | Risk |
|---|---|---|---|---|
| `eviction_target` | 80 | Not recommended | Not recommended | — |
| `eviction_trigger` | 95 | Not recommended | Not recommended | — |
| `eviction_dirty_target` | 5 | More dirty pages accumulate | Proactive dirty page write; less checkpoint work. **Recommended for large caches (100GB+)** | Also changes `eviction_updates_target` |
| `eviction_dirty_trigger` | 20 | App threads recruited less often | App threads recruited sooner | Also changes `eviction_updates_trigger` |
| `eviction_updates_target` | 2.5 | Not recommended | Start evicting update pages sooner (1-2% range) | — |
| `eviction_updates_trigger` | 10 | Less app thread eviction for updates (20-30% range) | More app thread eviction | Memory fragmentation → OOM |
| `worker_thread_max` | 4 | More eviction throughput (if dirty/update fill ratio >20% sustained) | Not recommended | Higher CPU |
| `close_idle_time` | 600 (MDB) / 30 (WT) | Keep dhandles alive longer (reduce open/close churn) | More aggressive sweep (reduce schema lock contention) | Band-aid for large collection count |
| `minSnapshotHistoryWindowInSeconds` | 300 | — | Better eviction, less HS pressure, shorter checkpoint cleanup | May break `snapshot` read concern; unpredictable if history retained >5s |

---

## JIRA Reference Index

| Ticket | Issue | Versions Affected |
|---|---|---|
| **WiredTiger — Eviction** | | |
| WT-11300 | Eviction queue empty despite cache pressure | < v7.0.15, < v8.0.4 |
| WT-12280 | Eviction aggressive score calculation wrong | Various |
| WT-12708 | Cache eviction inefficiency fix | Backport pending |
| WT-13090 | Aggressive eviction integer overflow (uint32) | Various |
| WT-13283 | Eviction stuck — never rolls back oldest pinned txn | Various |
| WT-9575 | Queue pages with updates newer than oldest txn for eviction | Various |
| WT-15538 | Eviction inefficiency edge cases | Under investigation |
| **WiredTiger — Checkpoint** | | |
| WT-8771 | Checkpoint cleanup hazard pointer issue | Backported to 5.0 |
| WT-9502 | Avoid evicting modified pages in cleanup | Backported to 5.0 |
| WT-10807 | Skip deleted values during cursor traversal | Backported to 5.0, 6.0, 7.0 |
| WT-12609 | Checkpoint cleanup evicting internal pages too early | Various |
| WT-12657 | Dedicated checkpoint cleanup thread | v8.0+ |
| WT-12798 | Long checkpoints with large cache | Under investigation |
| WT-12846 | Compact conflict with checkpoint during tree walk | v8.1 |
| WT-13216 | Compact cache eviction throttling | Various |
| WT-13502 | Backup cursor blocked by concurrent compaction | Various |
| WT-14140 | Schema lock contention root cause fix | Various |
| WT-14968 | History store cleanup thread 1s sleep bottleneck | v8.0+ |
| WT-15225 | Empty table dirtied by cleanup → EBUSY drop loop | v8.3.0 |
| WT-15594 | Bulk cursor memory leak during index builds | 7.0, 8.0 |
| WT-15762 | Long checkpoint prepare phase | Various |
| WT-16057 | Backup cursor blocking stepdown via hot_backup_lock | All |
| **WiredTiger — Other** | | |
| WT-10253 | Dhandle sweep responsiveness after backup | Backported to 5.0.19 |
| WT-10759 | HS page reconciliation latency | Backported to 5.0 |
| WT-11460 | Page write generation number race | Various |
| WT-12012 | Excessive log verbosity not reset after eviction stall | < v7.0.21 |
| WT-14322 | Incremental backup dhandle overhead | All |
| WT-14636 | EBUSY drop loop investigation | Various |
| **Server — Replication** | | |
| SERVER-55606 | j:false write feedback loop | 4.2, 4.4 |
| SERVER-61185 | Oplog holes with unique indexes | Various |
| SERVER-75205 | Deadlock: RSTL + read ticket exhaustion | 4.4.15-4.4.19, 5.0.10-5.0.15, 6.0.0-6.0.5 |
| SERVER-82688 | Session_max limit exceeded under load | All |
| SERVER-83186 | Eviction stuck project | All |
| SERVER-89727 | Compact no longer acquires DB/collection locks | v7.1+ |
| SERVER-90213 | Reduce ReplicationCoordinator mutex contention | Backport pending |
| SERVER-92554 | 30-second oplog stalls (glibc condvar bug) | 4.2+, Linux |
| SERVER-94771 | RSTL timeout from OldestActiveTxnTimestamp | All < v8.1 |
| SERVER-102306 | FTDC session pulled into eviction (gaps) | All < v8.3 |
| SERVER-105478 | Delayed secondary lag from batch optimes | All |
| SERVER-105537 | Secondary TemporarilyUnavailable from many key ops | All |
| SERVER-105751 | Prepared transaction reaped prematurely (v8.0) | v8.0+ |
| SERVER-106075 | API Version Mismatch on commit after failover | All |
| SERVER-106138 | Coordinator misinterprets APIMismatchError | All |
| **Server — Storage/Memory** | | |
| SERVER-73915 | Sharded primary memory leak (cross-shard txns) | 7.0.x, 8.0.0-8.0.13 |
| SERVER-82180 | Capped collection metadata lock ticket exhaustion | Pre-v7.0 |
| SERVER-103841 | Fix for SERVER-73915 memory leak | v8.0.14+ |
| SERVER-111130 | `duplicateKeyErrors` metric added | v8.3.0 |
| SERVER-111068 | Slow query insert logs success after duplicate key | Various |
| **Server — Other** | | |
| SPM-3313 | Query spilling to separate WT instance | v8.2+ |
| SPM-3737 | Initiative to deprecate system.profile | Ongoing |
| PERF-7188 | withTransaction write conflict storm reproducer | Ongoing |

---

## Analytical Thinking Guidelines

**These guidelines prevent common reasoning errors when diagnosing performance issues.**

### Rule 1: Verify Mechanism Applicability to Topology

Before proposing any solution, confirm it applies to the deployment topology (from `--metadata`).

| Mechanism | Requirements |
|---|---|
| Flow Control | Multi-node replica set (monitors secondary lag) |
| Majority read concern | Requires secondaries |
| Oplog-based solutions | Replica set only |
| Shard balancer | Sharded cluster only |

**Anti-pattern:** "Enable Flow Control to throttle writes" for a 1-node replica set ❌
**Correct:** "Flow Control requires secondaries to monitor lag. For a 1-node set, we need a different throttling mechanism (e.g., client-side rate limiting, admission control)" ✓

### Rule 2: Distinguish Interval vs Duration

For any periodic operation (checkpoints, compaction, background jobs), distinguish between:

| Term | Meaning |
|---|---|
| **Interval** | How often the operation is *triggered* (e.g., `syncdelay` from metadata) |
| **Duration** | How long each execution *takes* (e.g., `checkpoint.most recent time (msecs)`) |

**Critical insight:** If duration ≥ interval, the operation runs continuously. Reducing the interval has **zero effect**.

⛔ **STOP — Answer before recommending any frequency change:**
- What is the configured interval? ___
- What is the observed duration? ___
- Is duration ≥ interval? ___ (If yes, frequency changes are meaningless)

### Rule 3: Check Current State Before Proposing Changes

Before suggesting "do X more often" or "increase Y," verify the current state using `--stats` or `--json`.

| Suggestion | Must verify first |
|---|---|
| "Checkpoint more frequently" | Are checkpoints already running continuously? |
| "Add more eviction threads" | Are existing threads blocked or idle? |
| "Increase ticket count" | Are current tickets being held, or is there a bottleneck elsewhere? |
| "Enable feature X" | Is it already enabled? What is the current configuration? |

### Rule 4: Check Existing Metrics Before Adding New Ones

Before proposing new instrumentation:

```bash
./ftdc_parser /path/ --list --nonzero --filter "<area>"
```

Check what metrics already exist in the area before suggesting new ones.

### Rule 5: Quantify Expected Impact

Every proposed solution must include:

1. **Expected improvement:** X% reduction in metric Y
2. **Confidence level:** How certain are you? (must be ≥90% to recommend)
3. **Assumptions:** What must be true for this to work?
4. **Risks:** What could go wrong?

⛔ **STOP — Answer before making any recommendation:**
- What is my confidence level? ___%
- If below 90%: What additional research would increase confidence? Do that research now.
- Do not recommend with low confidence. Research until confident or decline to recommend.

### Rule 6: Don't Hedge—Investigate

Avoid phrases like "if not already done" or "this might help." Instead, investigate using the parser or source code and return with facts.

---

## Disaggregated Storage (PALI) Diagnostics

Disaggregated storage replaces local disk with remote page and log servers. This fundamentally changes the performance characteristics — page reads take 1-5ms (network) instead of <100us (SSD). The FTDC metrics under `serverStatus.metrics.disagg.*` are critical for diagnosis.

### Architecture Context

- **Primary**: Writes to local WT cache → sends phylog + oplog to log server via PALI → page materializer writes to page server
- **Standby**: Reads oplog from log server via PALI ReadLog → applies to local WT (which reads pages from page server)
- **Both nodes read from the same page servers** — but their access patterns differ, leading to different performance characteristics

### Key Disagg Metric Groups

#### Page Server Reads (`disagg.pageServer.*`, `disagg.getPageRequest.*`)

| Metric | Type | What it tells you |
|--------|------|-------------------|
| `pageServer.local.success` | counter | Page reads from local-zone page server |
| `pageServer.nonLocal.success` | counter | Page reads from remote-zone page server (bad — cross-zone latency) |
| `getPageRequest.bytesRead` | counter | Total bytes read from page server |
| `getPageRequest.numQueued` | gauge | Pending (in-flight) page requests. High values (>10K) indicate page server can't keep up |
| `getPageRequest.withOpCtx` | counter | Foreground reads (user operations) — uses efficient deadline mechanism |
| `getPageRequest.withoutOpCtx` | counter | Background reads (eviction/checkpoint) — uses inefficient `whenAny()` fallback |
| `getPageRequest.duplicateRequests` | counter | Deduplicated requests (same table+page+LSN). Very low = dedup not helping |
| `pageServer.requestTimeout` | counter | Page reads that timed out. Any value > 0 is concerning |

**Key diagnostic**: Calculate `withoutOpCtx / (withOpCtx + withoutOpCtx)`. Values >20% indicate significant background read traffic (eviction/checkpoint reads that bypass rate limiting). The code comment says this "should be rare" but FTDC shows 49% on some workloads.

#### Page Read Latency Histogram (`disagg.pageServer.GetPageAtLSNStreaming.latencyMicros.<bucket>.count`)

| Bucket | Range | Interpretation |
|--------|-------|---------------|
| 0 | 0-1ms | Fast — likely cache hit on page server |
| 1 | 1-10ms | Normal for network round-trip to page server |
| 2 | 10-100ms | Slow — page server under load or delta chain reconstruction |
| 3 | 100ms-1s | Very slow — possible page server issue or materialization lag |

Calculate `% slow = (bucket1 + bucket2 + bucket3) / total`. Values >50% indicate the page server is the bottleneck, not PALI.

#### paliHandleGet Latency (`disagg.paliHandleGet.*`)

| Metric | Type | What it tells you |
|--------|------|-------------------|
| `paliHandleGetTotalLatencyUs` | gauge | End-to-end page read time from WT's perspective (includes queue + network + processing) |
| `postProcessingLatencyUs` | gauge | Time spent on decryption and delta reconstruction after response arrives |

If `postProcessingLatencyUs` is a significant fraction of `paliHandleGetTotalLatencyUs`, delta chain processing is a bottleneck.

#### Batch Size Histogram (`...requestWriterBatchSize.<bucket>.count`)

| Bucket | Size | What it tells you |
|--------|------|-------------------|
| 1 | 1 request per batch | No batching — requests arrive one at a time |
| 2-7 | 2-100+ per batch | Batching working — concurrent requests amortize gRPC overhead |

If >90% of requests are in bucket 1, it means page reads arrive serially from WT (common when each oplog applier thread blocks on its read). Batching only helps when multiple threads generate concurrent requests.

#### Log Server Metrics (`disagg.logServer.*`) — Primary Only

| Metric | Type | What it tells you |
|--------|------|-------------------|
| `totalBatchesSent` | counter | Batch send rate to log server |
| `totalEntriesSent` | counter | Entry send rate. `entries/batches` = average batch size |
| `noProviderReturned` | counter | Write loop idle cycles (1ms sleep each). High rate = loop mostly idle |
| `opLogStarved` | counter | Oplog starvation events (phylog prioritized over oplog). Values >10/s indicate backpressure |
| `batchSentQueueEmpty` | counter | % of sends where queue was empty. >95% is normal — it means the system drains fast |
| `sendQueueSize` | gauge | Entries waiting in send queue. p99 >1000 indicates burst queueing |

**Note on batching**: gRPC write overhead is ~40us per batch. Even at 840 batches/sec, total overhead is <4% of wall time. Empty queue batching is NOT a performance issue — it means the system processes faster than entries arrive.

#### Phylog Pipeline (`disagg.phylog.*`) — Primary Only

| Metric | Type | What it tells you |
|--------|------|-------------------|
| `generatedToSentLagMillis` | gauge | Queue time before sending to log server. Should be <5ms |
| `sentToCommittedLagMillis` | gauge | Log server acknowledgment time. Outside PALI control |
| `committedToMaterializedLagMillis` | gauge | Page materializer processing time. Outside PALI control |
| `generatedToMaterializedLagMillis` | gauge | End-to-end phylog pipeline. Includes all above. Used by flow control |
| `unmaterialized` | gauge | Entries awaiting materialization. Growing = materializer falling behind |
| `qentries` | gauge | Phylog queue depth |
| `unsent` | gauge | Entries in queue not yet sent to log server |

#### Oplog Application Buffers (`disagg.oplog_buffer.*`) — Standby Only

| Metric | Type | Limit | What it tells you |
|--------|------|-------|-------------------|
| `apply.count` | gauge | **10,000 ops** | Apply buffer depth. p99 >= 9,000 = saturated |
| `apply.sizeBytes` | gauge | **100 MB** | Apply buffer bytes |
| `write.count` | gauge | N/A | Write buffer entry count |
| `write.sizeBytes` | gauge | **256 MB** | Write buffer bytes. p99 >= 250MB = saturated |

When both buffers saturate simultaneously, the entire standby pipeline stalls: ReadLog reader → write buffer (FULL) → writer (blocked) → apply buffer (FULL) → applier threads (blocked on WT/SLS IO).

### Disagg-Specific Diagnostic Patterns

#### Pattern: Primary has worse page latency than standby

**How to detect**: Compare `paliHandleGet.paliHandleGetTotalLatencyUs` between mongod.0 and mongod.1 for the same workload phase.

**Example**: ecommerce primary avg=1,573us, standby avg=692us (2.3x worse).

**Root cause**: The primary's eviction-triggered reads (withoutOpCtx) compete with user reads for page server bandwidth. The standby reads recently materialized pages (hot in page server cache). Also, the "sad path" timeout mechanism for background reads adds per-request overhead.

#### Pattern: Standby pipeline saturated

**How to detect**: `oplog_buffer.apply.count` p99 >= 9,000 AND `oplog_buffer.write.sizeBytes` p99 >= 250MB.

**Affected workloads**: All write-heavy workloads (find_one_and_update, bulk_insert, ycsb updates).

**Root cause**: Standby oplog applier threads blocked on WT storage IO (page reads from SLS for key existence checks). Being addressed by WT-16102 (blind inserts on standby).

#### Pattern: High oplog starvation in phylog-heavy workloads

**How to detect**: `logServer.opLogStarved` >> 10/s.

**Example**: tpcc_out_of_cache shows 254/s while most workloads show <2/s.

**Root cause**: Phylog:oplog entry ratio is very high (e.g., 11:1 in tpcc). PALI's `_pickNextProvider()` algorithm starves oplog when materialization lag exceeds thresholds.

### Cross-Node Comparison Methodology

When analyzing a 2-node disagg replset:

1. **Always compare both nodes** — primary and standby have fundamentally different bottlenecks
2. **Primary bottleneck is usually page server read latency** — user reads + eviction reads compete
3. **Standby bottleneck is usually oplog application speed** — limited by WT stable table lookups
4. **Same page server serves both** — high primary read rate can increase standby read latency
5. **Standby reads/s > primary reads/s** is normal — standby reads base page + delta chains for key checks
6. **Calculate standby:primary read ratio** — values >2x indicate excessive standby reads (key existence checks)

### Related

- Use the `sys-perf-ftdc-analyzer` skill for automated cross-workload FTDC extraction from Evergreen patches
- Page server reader code: `src/mongo/db/modules/atlas/src/disagg_storage/pali/page_server_reader.cpp`
- Log server manager code: `src/mongo/db/modules/atlas/src/disagg_storage/pali/log_server_manager.cpp`
- Oplog application coordinator: `src/mongo/db/modules/atlas/src/disagg_storage/oplog_application_coordinator.cpp`

---

## Related Resources

- Use `ftdc-parser` skill to extract metrics from diagnostic.data files
- Use `sys-perf-ftdc-analyzer` skill for cross-workload analysis from Evergreen patches
- T2 Statistics visualization tool for visual pattern recognition
- MongoDB JIRA for ticket details (jira.mongodb.org)
