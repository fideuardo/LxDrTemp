# Module Software Design (MSD)
## Sampler Data Collector (simtemp)

**License:** GPLv2 (SPDX: GPL-2.0-only)  
**Author:** Fidel Eduardo Cabañas Castillo  
**Version:** Draft 0.2 
**Date:** 2025-10-06

---

## Table of Contents
1. [Introduction and Purpose](#introduction-and-purpose)  
2. [Functional Requirements Overview](#functional-requirements-overview) 
    2.1. [Uses Cases](#21-uses-cases)
3. [Core Design Overview](#core-design-overview)  
4. [Software Module File Structure and Design Partitioning](#software-module-file-structure-and-design-partitioning)  
   4.1. [Source File Overview](#source-file-overview)  
   4.2. [Threading & Concurrency Model](#threading--concurrency-model)  
   4.3. [State Machine](#state-machine)  
   4.4. [Timing Model](#timing-model)  
   4.5. [Ring Buffer Design (SPSC Lockless)](#ring-buffer-design-spsc-lockless)  
   4.6. [File Tree Representation (Preliminary)](#file-tree-representation-preliminary)  
   4.7. [File-to-Requirement Traceability Matrix](#file-to-requirement-traceability-matrix)  
   4.8. [Rationale & Trade-offs](#rationale--trade-offs)  
   4.9. [Interface & Data Contract — **Design Freeze for MTS**](#interface--data-contract--design-freeze-for-mts)  
5. [UAPI/ABI v1](#uapiabi-v1)  
   5.1. [Binary Read Payload](#binary-read-payload)  
   5.2. [IOCTL Interface](#ioctl-interface)  
   5.3. [Sysfs ABI](#sysfs-abi)  
   5.4. [Poll/Epoll Semantics](#pollepoll-semantics)  
   5.5. [Overflow/Backpressure Policy](#overflowbackpressure-policy)  
6. [Device Tree (DT) Binding](#device-tree-dt-binding)  
7. [Debugging & Tracepoints](#debugging--tracepoints)  
8. [Testability Hooks for MTS](#testability-hooks-for-mTS)  
9. [Error Handling & Return Codes](#error-handling--return-codes)  
10. [Licensing and Intellectual Property](#licensing-and-intellectual-property)  
11. [Future Work (Next Design Iteration)](#future-work-next-design-iteration)  
12. [Document History](#document-history)

---

## 1. Introduction and Purpose
This document specifies the detailed software design for the **Sampler Data Collector (simtemp)** Linux kernel module. The module implements a high‑resolution **Producer–Consumer** sampling mechanism. It periodically produces simulated **temperature** samples and exposes them to user space via a character device `/dev/simtemp` with **binary reads** and **asynchronous I/O** via `poll()/epoll()`.

**Key goals:**
- Deterministic sampling using **hrtimer** (µs resolution).
- Efficient zero‑copy-ish delivery from kernel to user through an **SPSC lockless ring buffer**.
- Configurability at runtime through **sysfs** and **ioctl()**.
- Threshold alerting using `POLLPRI` with well-defined **edge semantics**.
- **DT binding** to provide sane defaults and platform integration.

---

## 2. Functional Requirements Overview
- FR‑01: Periodic sampling with configurable period (min 1000 µs, max 10 s).
- FR‑02: Support **modes**: `continuous` and `one-shot`.
- FR‑03: User‑space **read** returns binary samples in FIFO order.
- FR‑04: **poll/epoll** signals data availability and threshold alerts.
- FR‑05: Samples include **timestamp (ns)** and status **flags**.
- FR‑06: **Threshold** alerting with rising/falling/both edge policies.
- FR‑07: Ring buffer protects against overrun with defined policy.
- FR‑08: Runtime configuration via **sysfs** + **ioctl**, defaults from **DT**.

### 2.1 Use Cases (UC)

This section defines the **system use cases** consistent with the frozen design in v0.8. Each UC includes actors, preconditions, flows, postconditions, and traceability to **FR** (Functional Requirements) and **Interface/Design Contracts** (IF/DS). These UCs will feed the **Module Test Specification (MTS)**.

#### 2.1.1 Actors

- **App** — A user-space application (CLI or daemon) interacting with `/dev/simtemp`, `poll/epoll`, and `/sys` attributes.
- **Integrator** — System integrator configuring defaults via Device Tree (DT).
- **Kernel (simtemp)** — The kernel module under test as a black box.
- **Test Harness** — Automated test runner that validates timing/flags and error codes.

#### 2.1.2 Use Case Matrix (Traceability Overview)

| UC ID | Title | Primary Actor | Traces to FR |
|---|---|---|---|
| UC-01 | Configure period & start continuous sampling | App | FR-01, FR-02, FR-08 |
| UC-02 | Change period while STOPPED | App | FR-01, FR-08 |
| UC-03 | Attempt to change period while RUNNING (error) | App | FR-08 |
| UC-04 | One-shot sample capture | App | FR-02, FR-03 |
| UC-05 | Blocking read of samples | App | FR-03, FR-05 |
| UC-06 | Non-blocking read returns immediately | App | FR-03 |
| UC-07 | `poll/epoll` for data availability (POLLIN) | App | FR-04 |
| UC-08 | Threshold alert via `POLLPRI` (rising/falling/both) | App | FR-04, FR-06 |
| UC-09 | Clear threshold alert (read-to-clear / ioctl) | App | FR-04, FR-06 |
| UC-10 | Validate read payload fields (ts_ns, period_us, flags) | Test Harness | FR-05 |
| UC-11 | Overrun behavior (overwrite-oldest + EOVERFLOW) | Test Harness | FR-07 |
| UC-12 | Boot defaults from Device Tree | Integrator | FR-08 |
| UC-13 | Switch modes via ioctl (continuous ↔ one-shot) | App | FR-02, FR-08 |
| UC-14 | Reject invalid sysfs values with -EINVAL | App | FR-08 |
| UC-15 | Remove & re-probe driver (lifecycle) | Integrator | FR-08 |

---

##### UC-01 Configure period & start continuous sampling
**Primary Actor:** App  
**Preconditions:** Driver is loaded; state = STOPPED; device node `/dev/simtemp` present.  
**Trigger:** App sets `sampling_us` and calls `SIMTEMP_IOC_START`.  
**Main Success Flow:**  
1. App writes `sampling_us=5000` (5 ms).  
2. App issues `SIMTEMP_IOC_START`.  
3. Kernel arms `hrtimer` and transitions to RUNNING.  
4. Samples are produced and enqueued periodically.  
**Alternate/Exceptions:**  
- A1: `sampling_us` outside `[1000..10_000_000]` → `-EINVAL`.  
**Postconditions:** state = RUNNING; period_us=5000.  
**Trace:** FR-01, FR-02, FR-08; IF-IOCTL-01, IF-SYSFS-01; DS-RB-01.

##### UC-02 Change period while STOPPED
**Primary Actor:** App  
**Preconditions:** state = STOPPED.  
**Trigger:** App writes new `sampling_us`.  
**Main Success Flow:**  
1. App writes `sampling_us=20000`.  
2. Kernel validates and updates internal `period_us`.  
**Exceptions:**  
- E1: Value invalid → `-EINVAL`.  
**Postconditions:** `period_us=20000`.  
**Trace:** FR-01, FR-08; IF-SYSFS-01.

##### UC-03 Attempt to change period while RUNNING (error)
**Primary Actor:** App  
**Preconditions:** state = RUNNING.  
**Trigger:** App writes `sampling_us=10000`.  
**Flow:**  
1. Write attempt rejected with `-EBUSY`.  
**Postconditions:** No change to `period_us`.  
**Trace:** FR-08; IF-SYSFS-01.

##### UC-04 One-shot sample capture
**Primary Actor:** App  
**Preconditions:** state = STOPPED; mode = one-shot.  
**Trigger:** App sets mode one-shot and starts.  
**Main Success Flow:**  
1. App: `operation_mode="one-shot"`; `SIMTEMP_IOC_START`.  
2. Kernel produces exactly **one** sample, enqueues it, and transitions to STOPPED.  
3. App `read()` gets exactly one `simtemp_sample_v1` with `flags&ONESHOT_DONE`.  
**Postconditions:** state = STOPPED; exactly one sample produced.  
**Trace:** FR-02, FR-03; IF-IOCTL-01, IF-RD-01.

##### UC-05 Blocking read of samples
**Primary Actor:** App  
**Preconditions:** state = RUNNING; fd open without `O_NONBLOCK`.  
**Trigger:** App calls `read(fd, buf, N * sizeof(sample))`.  
**Main Success Flow:**  
1. If RB empty, task sleeps on waitqueue.  
2. On new sample arrival, `read()` copies ≥1 sample(s) as available.  
**Postconditions:** App receives FIFO-ordered samples.  
**Trace:** FR-03, FR-05; IF-RD-01, IF-POLL-01, DS-RB-01.

##### UC-06 Non-blocking read returns immediately
**Primary Actor:** App  
**Preconditions:** fd open with `O_NONBLOCK`.  
**Flow:**  
1. If RB empty, `read()` returns `-EAGAIN` immediately.  
2. If data present, returns number of bytes for whole samples.  
**Trace:** FR-03; IF-RD-01.

##### UC-07 `poll/epoll` for data availability (POLLIN)
**Primary Actor:** App  
**Preconditions:** state = RUNNING; App registered with `poll/epoll`.  
**Flow:**  
1. When RB has ≥1 sample, kernel sets `POLLIN`.  
2. App wakes, calls `read()`, drains samples; `POLLIN` cleared once empty.  
**Trace:** FR-04; IF-POLL-01, DS-RB-01.

##### UC-08 Threshold alert via `POLLPRI` (rising/falling/both)
**Primary Actor:** App  
**Preconditions:** Threshold configured; `alert_policy` set.  
**Main Success Flow:**  
1. Sample crosses threshold according to policy (edge-based).  
2. Kernel sets pending alert; `poll()` reports `POLLPRI`.  
3. App handles urgent path (e.g., logging, UI, safety action).  
**Alternates:**  
- A1: Flapping near threshold is mitigated by 50 mC hysteresis.  
**Trace:** FR-04, FR-06; IF-POLL-01.

##### UC-09 Clear threshold alert (ioctl)
**Primary Actor:** App  
**Preconditions:** Alert pending from UC-08.  
**Flow (read-to-clear - DEPRECATED):**  
1. App performs `read()`; a sample with `flags&THR_EDGE` clears the alert.  
**Flow (ioctl):**  
1. App calls `SIMTEMP_IOC_CLR_ALERT` to clear without consuming data.  
**Trace:** FR-04, FR-06; IF-POLL-01, IF-IOCTL-01.

##### UC-10 Validate read payload fields (ts_ns, period_us, flags)
**Primary Actor:** Test Harness  
**Preconditions:** RUNNING with known `sampling_us`.  
**Flow:**  
1. Read a sequence of samples; compute deltas of `ts_ns`.  
2. Verify `period_us` field equals configured period (± tolerance).  
3. Verify `flags` reflect expected conditions (e.g., `OK` bit set).  
**Acceptance:** Timestamp deltas are consistent with configured period; no spurious flags.  
**Trace:** FR-05; IF-RD-01.

##### UC-11 Overrun behavior (overwrite-oldest + EOVERFLOW)
**Primary Actor:** Test Harness  
**Preconditions:** Small RB capacity; consumer intentionally slow.  
**Flow:**  
1. Producer fills RB; on next enqueue, kernel advances tail (overwrite-oldest).  
2. The **first** sample read after loss reports `flags&OVERFLOW`; `read()` may return `-EOVERFLOW` **once**.  
**Postconditions:** System continues delivering **recent** samples.  
**Trace:** FR-07; DS-RB-01, IF-RD-01.

##### UC-12 Boot defaults from Device Tree
**Primary Actor:** Integrator  
**Preconditions:** DT node present with properties.  
**Flow:**  
1. On probe, driver parses `sample-period-us`, `threshold-mC`, `operation-mode`, `alert-policy`.  
2. These act as **defaults** until overridden by sysfs/ioctl.  
**Trace:** FR-08; IF-SYSFS-01.

##### UC-13 Switch modes via ioctl (continuous ↔ one-shot)
**Primary Actor:** App  
**Preconditions:** state = STOPPED.  
**Flow:**  
1. App calls `SIMTEMP_IOC_SET_MODE`.  
2. If RUNNING, return `-EBUSY`; otherwise accept and persist.  
**Trace:** FR-02, FR-08; IF-IOCTL-01.

##### UC-14 Reject invalid sysfs values with -EINVAL
**Primary Actor:** App  
**Preconditions:** N/A.  
**Flow:**  
1. App writes out-of-range `sampling_us` or malformed string.  
2. Kernel rejects with `-EINVAL`.  
**Trace:** FR-08; IF-SYSFS-01.

##### UC-15 Remove & re-probe driver (lifecycle)
**Primary Actor:** Integrator  
**Preconditions:** Driver is loaded and has been used.  
**Flow:**  
1. Remove module or unbind platform device (`remove`).  
2. Re-insert module or re-bind device (`probe`).  
3. Verify resources reclaimed and re-initialized (RB empty, state STOPPED).  
**Trace:** FR-08.

---

## 3. Core Design Overview
- **Platform driver** + **miscdevice** (`/dev/simtemp`, `MISC_DYNAMIC_MINOR`).
- **Producer**: hrtimer callback → compute temp → enqueue sample.
- **Consumer**: user `read()` drains ring; `poll()`/`epoll()` for readiness.
- **Alert path**: edge detection vs configured threshold → set alert flag and wake `POLLPRI` waiters.
- **Configuration**: sysfs for period, threshold, mode; ioctl for state, ABI, and fast control.

---

## 4. Software Module File Structure and Design Partitioning

### 4.1. Source File Overview
- `simtemp_core.c/.h` — timer, sample generation, threshold edge detection, state machine.
- `simtemp_ringbuf.c/.h` — SPSC lockless ring buffer (atomic head/tail + barriers).
- `simtemp_fops.c` — `open/read/poll/unlocked_ioctl/release`.
- `simtemp_sysfs.c` — attributes: `sampling_us`, `threshold_mC`, `operation_mode`, `alert_policy`, `rb_capacity` (ro).
- `simtemp_dt.c` — OF parsing: `sample-period-us`, `threshold-mC`, `operation-mode`, `alert-policy`.
- `simtemp_trace.h` — tracepoints.
- `simtemp_debugfs.c` (optional) — stats/inspection.
- `simtemp.h` — shared structs, enums, constants.
- `Kbuild/Makefile` — module build.

### 4.2. Threading & Concurrency Model
- **Producer**: hrtimer (softirq context) — single writer to ring buffer.
- **Consumer**: process context in `read()` — single reader.
- Model is **SPSC** ⇒ lockless feasible with acquire/release barriers; no spinlocks in the ring buffer path.
- Control paths (sysfs/ioctl) take a **state_mutex** to serialize reconfiguration with respect to timer (uses `hrtimer_cancel()` before changes).

### 4.3. State Machine
```
      +-----------+      SIMTEMP_IOC_START         +-----------+
      |  STOPPED  |  ----------------------------> |  RUNNING  |
      +-----------+                                 +-----------+
            ^                                             |
            |                one-shot sample done         |
            +---------------------------------------------+
```
- **Mode** is orthogonal to **State**:
  - `operation_mode ∈ {continuous, one-shot}`.
  - In **one-shot**, the first successful enqueue transitions to **STOPPED**.
- Illegal transitions:
  - Changing `operation_mode` while **RUNNING** → `-EBUSY` (use state STOP first).

### 4.4. Timing Model
- Internal unit is **microseconds** (`*_us`). Sysfs exposes **`sampling_us`**; dt uses `sample-period-us`.
- Reprogramming sequence: `mutex_lock(state_mutex)` → `hrtimer_cancel()` → update `period_us` → `hrtimer_start()` if state was RUNNING → `mutex_unlock()`.
- Effective period stored per sample (`period_us` field) for diagnostics.

### 4.5. Ring Buffer Design (SPSC Lockless)
- Head (producer) and tail (consumer) are `u32` indices modulo `capacity` power‑of‑two.
- Memory ordering:
  - Producer: write sample → `smp_store_release(head, new_head)`.
  - Consumer: `head = smp_load_acquire(head)` → read sample.
- **No spinlocks** in RB path. Overrun policy defined in §5.5.

### 4.6. File Tree Representation (Preliminary)
```
simtemp/
├─ include/
│  └─ simtemp.h
├─ core/
│  ├─ simtemp_core.c
│  ├─ simtemp_ringbuf.c
│  └─ simtemp_dt.c
├─ uapi/
│  └─ simtemp_uapi.h
├─ io/
│  ├─ simtemp_fops.c
│  └─ simtemp_sysfs.c
├─ debug/
│  ├─ simtemp_debugfs.c (optional)
│  └─ simtemp_trace.h
├─ Kbuild
└─ Makefile
```

### 4.7. File-to-Requirement Traceability Matrix
| Requirement | File(s) |
|---|---|
| FR‑01 (periodic sampling) | `simtemp_core.c`, `simtemp_dt.c`, `simtemp_sysfs.c` |
| FR‑02 (modes) | `simtemp_core.c`, `simtemp_sysfs.c` |
| FR‑03 (read) | `simtemp_ringbuf.c`, `simtemp_fops.c` |
| FR‑04 (poll/epoll) | `simtemp_fops.c` |
| FR‑05 (timestamp/period) | `simtemp_core.c`, `simtemp_uapi.h` |
| FR‑06 (threshold + edges) | `simtemp_core.c`, `simtemp_sysfs.c`, `simtemp_uapi.h` |
| FR‑07 (overrun policy) | `simtemp_ringbuf.c`, `simtemp_uapi.h` |
| FR‑08 (sysfs/dt runtime) | `simtemp_sysfs.c`, `simtemp_dt.c` |

### 4.8. Rationale & Trade-offs
- **SPSC lockless** chosen for low‑latency and simplicity (single producer: hrtimer; single consumer: one reader per fd). If multiple readers are needed, add a fan‑out layer later.
- **µs** internally to match hrtimer granularity; expose **us** in sysfs for 1:1 mapping and avoid rounding confusion.
- `POLLPRI` edge notifications + explicit **edge policy** provide deterministic app behavior.

### 4.9. Interface & Data Contract — **Design Freeze for MTS**
- IF‑RD‑01: `read()` returns integral multiples of `sizeof(struct simtemp_sample_v1)`.
- IF‑POLL‑01: `POLLIN` ⇒ at least one sample available. `POLLPRI` ⇒ threshold edge pending (see §5.4 for clear rules).
- IF‑IOCTL‑01: See §5.2 (names and semantics frozen).
- IF‑SYSFS‑01: See §5.3 (attributes and ranges frozen).
- DS‑RB‑01: RB is SPSC lockless; no spinlocks in hot path.

---

## 5. UAPI/ABI v1

### 5.1. Binary Read Payload
```c
/* uapi/simtemp_uapi.h */
#include <linux/types.h>

#define SIMTEMP_ABI_MAJOR 1
#define SIMTEMP_ABI_MINOR 0

struct simtemp_sample {
    __u64 timestamp_ns;   // monotonic timestamp
    __s32 temp_mC;        // milli-degree Celsius (e.g., 44123 = 44.123 °C)
    __u32 flags;          // bit0=OK, bit1=OVERFLOW, bit2=THR_EDGE, bit3=ONESHOT_DONE
} __attribute__((packed));
```

### 5.2. IOCTL Interface
```c
#define SIMTEMP_IOC_MAGIC   'T'

/* Get ABI version */
#define SIMTEMP_IOC_GET_ABI        _IOR(SIMTEMP_IOC_MAGIC, 0x00, struct { __u16 major; __u16 minor; })

/* Start/Stop state machine */
#define SIMTEMP_IOC_START          _IO(SIMTEMP_IOC_MAGIC,  0x01)
#define SIMTEMP_IOC_STOP           _IO(SIMTEMP_IOC_MAGIC,  0x02)

/* Get/Set operation mode: 0=continuous, 1=one-shot */
#define SIMTEMP_IOC_GET_MODE       _IOR(SIMTEMP_IOC_MAGIC, 0x10, __u32)
#define SIMTEMP_IOC_SET_MODE       _IOW(SIMTEMP_IOC_MAGIC, 0x11, __u32)

/* Get/Set alert edge policy: 0=rising,1=falling,2=both */
#define SIMTEMP_IOC_GET_ALERT_POL  _IOR(SIMTEMP_IOC_MAGIC, 0x12, __u32)
#define SIMTEMP_IOC_SET_ALERT_POL  _IOW(SIMTEMP_IOC_MAGIC, 0x13, __u32)

/* Clear pending threshold alert (read-to-clear alternative) */
#define SIMTEMP_IOC_CLR_ALERT      _IO(SIMTEMP_IOC_MAGIC,  0x20)
```

### 5.3. Sysfs ABI
All attributes under the platform device:
- `sampling_us` (RW, `1000..10_000_000`): internal period in microseconds. Writing while RUNNING returns `-EBUSY`; change state to STOP first.
- `threshold_mC` (RW, `-40000..150000`): s32 in milli‑Celsius.
- `operation_mode` (RW, `"continuous"|"one-shot"`): may be changed only when STOPPED.
- `alert_policy` (RW, `"rising"|"falling"|"both"`).
- `rb_capacity` (RO): number of samples the RB can hold.

**Precedence:** DT defaults < sysfs runtime.

### 5.4. Poll/Epoll Semantics
- `POLLIN`: set when RB has ≥1 sample; auto‑clears when drained.
- `POLLPRI` (threshold alert):
  - Raised on **edge** as per `alert_policy` when sample crosses threshold.
  - **Clear policy**: **read-to-clear**; a successful `read()` that observes the sample with the edge sets `flags&THR_EDGE` and clears the pending alert. Alternatively, `SIMTEMP_IOC_CLR_ALERT` clears without read.
  - Debounce/hysteresis (optional): internal fixed **50 mC** to avoid flapping near threshold.

### 5.5. Overflow/Backpressure Policy
- Policy: **Overwrite oldest** (ring moves tail forward on overrun) to favor **recency** in telemetry/monitoring.
- `flags&OVERFLOW` is set on the **first** sample after an overrun.
- `read()` may return `-EOVERFLOW` once to signal data loss even if samples are still delivered.

---

## 6. Device Tree (DT) Binding
Compatible: `"simtemp,collector"`

Properties:
- `sample-period-us` (u32, default 100000): initial period in µs.
- `threshold-mC` (s32, default 25000).
- `operation-mode` (`"continuous"` | `"one-shot"`, default `"continuous"`).
- `alert-policy` (`"rising"` | `"falling"` | `"both"`, default `"rising"`).

**Example:**
```dts
simtemp0: simtemp@0 {
    compatible = "simtemp,collector";
    sample-period-us = <5000>;      /* 200 Hz */
    threshold-mC = <30000>;         /* 30.0°C */
    operation-mode = "continuous";
    alert-policy = "rising";
};
```

---

## 7. Debugging & Tracepoints
- `simtemp_enqueue(__u64 ts_ns, __s32 temp_mC, __u32 head, __u32 tail)`
- `simtemp_overflow(__u32 capacity, __u32 drops)`
- `simtemp_threshold(__s32 temp_mC, __s32 thr_mC, __u32 edge)`

---

## 8. Testability Hooks for MTS
- Exported (static inline or symbol) test targets:
  - `simtemp_core_start() / simtemp_core_stop()`
  - `simtemp_timer_cb()` (injectable via test shim)
  - `simtemp_rb_enqueue()/simtemp_rb_dequeue()`
  - `simtemp_read()/simtemp_poll()/simtemp_ioctl()`
- Deterministic cases:
  - Edge policies (rising/falling/both) including hysteresis.
  - Overflow at RB capacities {64, 256, 1024}.
  - Period change sequences and `-EBUSY` behavior.
  - One‑shot completion path and flags.

---

## 9. Error Handling & Return Codes
- `-EINVAL` out‑of‑range sysfs values.
- `-EBUSY` reconfiguration while RUNNING (where forbidden).
- `-EOVERFLOW` reported once after overrun until read.
- `-EIO` unexpected ring invariants breach (debug builds).

---

## 10. Licensing and Intellectual Property
- SPDX tag in each source file: `// SPDX-License-Identifier: GPL-2.0-only`.
- Copyright © 2025 Fidel Eduardo Cabañas.

---

## 11. Future Work (Next Design Iteration)
- Multi‑reader fan‑out (per‑fd cursors).
- `debugfs` live histogram of latencies (timer to enqueue to wake).
- Power management hooks (suspend/resume).
- Optional random/thermistor‑like temperature model parameters via sysfs.

---

## 12. Document History
| Version | Date | Author | Notes |
|---|---|---|---|
| 0.8 | 2025-10-06 | F. E. Cabañas | Units unified to µs; SPSC lockless ring; ABI v1 (timestamp+flags); poll semantics & edge policy; overflow = overwrite‑oldest; DT/sysfs alignment; state machine & ranges frozen for MTS. |
| 0.7 | 2025-10-05 | F. E. Cabañas | Draft prior to freeze. |

---

## 13. Function Inventory (Implementation Plan)

This section enumerates the functions to be implemented, their **location (file)**, and a short **purpose/contract** description. It is aligned with the frozen design of v0.8 and serves as the handoff list for coding and for building the initial MTS.

### include/simtemp.h (shared headers)
- `enum simtemp_mode { SIMTEMP_MODE_CONTINUOUS, SIMTEMP_MODE_ONESHOT };` — Operating modes definition.
- `enum simtemp_edge { SIMTEMP_EDGE_RISING, SIMTEMP_EDGE_FALLING, SIMTEMP_EDGE_BOTH };` — Threshold edge policy.
- `struct simtemp_sample_v1` — ABI v1 read payload (`ts_ns`, `temperature_mC`, `period_us`, `flags`).
- `struct simtemp_rb` — SPSC ring buffer state (buffer pointer, capacity, head/tail indices).
- `struct simtemp_dev` — Device state: hrtimer, waitqueues, miscdev, sysfs refs, ring buffer, locks, configuration (period_us, threshold_mC, mode, edge), flags, and stats.

### core/simtemp_core.c (timer, state machine, generation, alerts)
- `int simtemp_core_init(struct simtemp_dev *sd);` — Initialize timer, waitqueues, flags (does not start sampling). Returns `0/-errno`.
- `void simtemp_core_exit(struct simtemp_dev *sd);` — Cancel timer, tear down core resources.
- `int simtemp_core_start(struct simtemp_dev *sd);` — Transition to RUNNING, arm hrtimer using `period_us`. `-EBUSY` if already running.
- `int simtemp_core_stop(struct simtemp_dev *sd);` — Cancel hrtimer and go to STOPPED.
- `enum hrtimer_restart simtemp_timer_cb(struct hrtimer *t);` — Producer path in softirq: compute sample, enqueue, detect threshold edge, wake readers, and rearm if needed.
- `static inline s32 simtemp_core_compute_temp_mC(struct simtemp_dev *sd, u64 ts_ns);` — Deterministic temperature model for tests.
- `void simtemp_core_handle_threshold(struct simtemp_dev *sd, s32 temp_mC);` — Edge detection versus configured threshold with hysteresis; sets pending alert.
- `bool simtemp_core_enqueue_sample(struct simtemp_dev *sd, s32 temp_mC, u64 ts_ns);` — Lockless enqueue into ring buffer; reports overflow event.
- `void simtemp_core_signal_readers(struct simtemp_dev *sd, bool data_ready, bool pri_ready);` — Wake up waiters for `POLLIN`/`POLLPRI`.
- `int simtemp_core_set_period_us(struct simtemp_dev *sd, u32 period_us);` — Validate range and update period; called under `state_mutex` with timer canceled.
- `int simtemp_core_set_mode(struct simtemp_dev *sd, enum simtemp_mode mode);` — Change operation mode (only while STOPPED).
- `int simtemp_core_set_threshold(struct simtemp_dev *sd, s32 threshold_mC);` — Update threshold; reset internal edge state if applicable.
- `int simtemp_core_set_alert_edge(struct simtemp_dev *sd, enum simtemp_edge edge);` — Set threshold edge policy.

### core/simtemp_ringbuf.c (SPSC lockless ring buffer)
- `int simtemp_rb_init(struct simtemp_rb *rb, size_t capacity_pow2);` — Allocate buffer with power-of-two capacity; init indices. `0/-ENOMEM/-EINVAL`.
- `void simtemp_rb_reset(struct simtemp_rb *rb);` — Reset indices and flags.
- `void simtemp_rb_free(struct simtemp_rb *rb);` — Free memory.
- `bool simtemp_rb_try_enqueue(struct simtemp_rb *rb, const struct simtemp_sample_v1 *s, bool *did_overwrite);` — Write sample with `smp_store_release`; on full, overwrite oldest (advance tail) and set `did_overwrite`.
- `ssize_t simtemp_rb_read(struct simtemp_rb *rb, char __user *ubuf, size_t bytes);` — Copy out complete samples from tail using `smp_load_acquire`. Returns bytes read.
- `size_t simtemp_rb_count(const struct simtemp_rb *rb);` — Number of elements available.
- `bool simtemp_rb_empty(const struct simtemp_rb *rb);` / `bool simtemp_rb_full(const struct simtemp_rb *rb);` — Helpers.

### core/simtemp_dt.c (Device Tree parsing)
- `int simtemp_of_parse(struct simtemp_dev *sd, struct device *dev);` — Parse DT properties: `sample-period-us`, `threshold-mC`, `operation-mode`, `alert-policy`; apply defaults; return `0/-EINVAL`.

### io/simtemp_fops.c (char device operations)
- `int simtemp_fops_open(struct inode *ino, struct file *filp);` — Bind `simtemp_dev` to `private_data`.
- `ssize_t simtemp_fops_read(struct file *filp, char __user *ubuf, size_t len, loff_t *ppos);` — Blocking/non-blocking read of whole samples; returns `-EOVERFLOW` once after an overrun.
- `__poll_t simtemp_fops_poll(struct file *filp, struct poll_table_struct *wait);` — `POLLIN` if samples available; `POLLPRI` if threshold alert is pending.
- `long simtemp_fops_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);` — Implements `GET_ABI`, `START/STOP`, `GET/SET_MODE`, `GET/SET_ALERT_POL`, `CLR_ALERT`.
- `int simtemp_fops_release(struct inode *ino, struct file *filp);` — Per-fd cleanup; placeholder for future fan-out.
- `static int simtemp_copy_sample_to_user(const struct simtemp_sample_v1 *s, char __user *ubuf);` — ABI-safe copy helper.

### io/simtemp_sysfs.c (sysfs attributes under the platform device)
- `ssize_t sampling_us_show(struct device *dev, struct device_attribute *attr, char *buf);`
- `ssize_t sampling_us_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);` — Validate range; if RUNNING return `-EBUSY` unless state change sequence is followed.
- `ssize_t threshold_mC_show(struct device *dev, struct device_attribute *attr, char *buf);`
- `ssize_t threshold_mC_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);`
- `ssize_t operation_mode_show(struct device *dev, struct device_attribute *attr, char *buf);`
- `ssize_t operation_mode_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);` — Only STOPPED.
- `ssize_t alert_policy_show(struct device *dev, struct device_attribute *attr, char *buf);`
- `ssize_t alert_policy_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);`
- `ssize_t rb_capacity_show(struct device *dev, struct device_attribute *attr, char *buf);`
- `int simtemp_sysfs_create(struct simtemp_dev *sd);` / `void simtemp_sysfs_remove(struct simtemp_dev *sd);` — Create/remove device attributes.

### debug/simtemp_debugfs.c (optional diagnostics)
- `int simtemp_debugfs_init(struct simtemp_dev *sd);` / `void simtemp_debugfs_exit(struct simtemp_dev *sd);` — Setup/remove debugfs entries.
- `ssize_t simtemp_dbg_read_stats(struct file *f, char __user *ubuf, size_t len, loff_t *ppos);` — Dump internal stats.

### debug/simtemp_trace.h (tracepoints)
- `TRACE_EVENT(simtemp_enqueue, TP_PROTO(u64 ts_ns, s32 temp_mC, u32 head, u32 tail), ...);`
- `TRACE_EVENT(simtemp_overflow, TP_PROTO(u32 capacity, u32 drops), ...);`
- `TRACE_EVENT(simtemp_threshold, TP_PROTO(s32 temp_mC, s32 thr_mC, u32 edge), ...);`

### uapi/simtemp_uapi.h (ABI helpers for ioctl)
- `int simtemp_get_abi(struct simtemp_dev *sd, u16 *major, u16 *minor);` — Return `{1,0}`.

### Platform driver integration (probe/remove) — may live in `simtemp_core.c` or `simtemp_platform.c`
- `int simtemp_probe(struct platform_device *pdev);` — Allocate `simtemp_dev`, init ring/core, parse DT, register miscdev, create sysfs, (optionally) debugfs. Rollback on failure.
- `int simtemp_remove(struct platform_device *pdev);` — Stop/exit core, remove sysfs, deregister miscdev, exit debugfs, free ring and device.
- `static int __init simtemp_module_init(void);` / `static void __exit simtemp_module_exit(void);` — Register/unregister platform driver.
