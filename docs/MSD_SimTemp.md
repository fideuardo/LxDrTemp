
# Module Software Design (MSD) — nxp_simtemp
**Version:** v0.3 (2025-10-14) — *Aligned with NXP Candidate Challenge*  
**License:** GPLv2 (SPDX: GPL-2.0-only)  
**Author:** Fidel Eduardo Cabañas Castillo

---

## Table of Contents
1. Introduction and Purpose  
2. Functional Requirements Overview  
3. Core Design Overview  
4. Software Module File Structure and Design Partitioning  
   4.1 Source File Overview  
   4.2 Threading & Concurrency Model  
   4.3 State Machine  
   4.4 Timing Model  
   4.5 Ring Buffer Design (SPSC Lockless)  
   4.6 File Tree Representation (Preliminary)  
   4.7 File-to-Requirement Traceability Matrix  
   4.8 Rationale & Trade-offs  
   4.9 Interface & Data Contract — **Design Freeze for MTS**  
5. UAPI/ABI v1  
   5.1 Binary Read Payload  
   5.2 IOCTL Interface (Optional, for batch/atomic config)  
   5.3 Sysfs ABI  
   5.4 Poll/Epoll Semantics  
   5.5 Overflow/Backpressure Policy  
6. Device Tree (DT) Binding  
7. Debugging & Tracepoints (optional)  
8. Testability Hooks for MTS  
9. Error Handling & Return Codes  
10. Acceptance Criteria (Core) — **Aligned with Challenge**  
11. Tooling, CLI & Demo Scripts  
12. Licensing and Intellectual Property  
13. Document History

---

## 1. Introduction and Purpose
This document specifies the detailed software design for the **nxp_simtemp** Linux kernel module. The module implements a high‑resolution **Producer–Consumer** sampling mechanism. It periodically produces simulated **temperature** samples and exposes them to user space via a character device `/dev/simtemp` with **binary reads** and **asynchronous I/O** via `poll()/epoll()`.

**Alignment deltas from earlier drafts**:
- Sysfs period unit is now **milliseconds** (`sampling_ms`) to match challenge wording (internal timekeeping remains µs for precision).
- DT compatible is **"nxp,simtemp"** (was `"simtemp,collector"`).
- A new **`stats`** sysfs attribute (RO) reports counters per acceptance criteria.
- Simulation **modes** are exposed as `mode = normal|noisy|ramp`. (One‑shot/continuous is handled as a **state** and start/stop control rather than a “mode”.)

---

## 2. Functional Requirements Overview
- FR‑01: Periodic sampling with configurable period in **ms** (min 5 ms, max 5000 ms default bounds).  
- FR‑02: Simulation **mode**: `normal`, `noisy`, `ramp`.  
- FR‑03: User‑space **read** returns binary samples (timestamp ns, temp mC, flags).  
- FR‑04: **poll/epoll** signals **data availability** (POLLIN) and **threshold crossing** (POLLPRI).  
- FR‑05: Samples include **timestamp (ns)** and status **flags**.  
- FR‑06: **Threshold** alert when temperature ≥ `threshold_mC`.  
- FR‑07: Ring buffer bounded; defined overrun policy.  
- FR‑08: Runtime configuration via **sysfs** (and optional **ioctl**). Defaults from **DT**.  
- FR‑09: **Stats** via sysfs (`stats` RO).

> See `docs/REQUIREMENTS.md` for the complete requirement set and acceptance mapping.

---

## 3. Core Design Overview
- **Device**: platform driver + miscdevice (creates `/dev/simtemp`).  
- **Producer**: `hrtimer` in REL mode → generate temperature → enqueue.  
- **Consumer**: `read()` drains ring; `poll()`/`epoll()` for readiness.  
- **Alerts**: threshold crossing sets a flag and wakes `POLLPRI`.  
- **Configuration**: sysfs for period/threshold/mode/stats; optional ioctl for batch config.

---

## 4. Software Module File Structure and Design Partitioning

### 4.1 Source File Overview
- `nxp_simtemp.c` — unified reference implementation (init/exit, hrtimer, fops, sysfs, ring, DT parsing).  
  *For larger code bases, split into: core, fops, sysfs, dt, ring.*
- `nxp_simtemp.h` — shared structs, enums, constants.  
- `nxp_simtemp_ioctl.h` — optional IOCTL ABI.  
- `dts/nxp-simtemp.dtsi` — DT example.

### 4.2 Threading & Concurrency Model
- **Producer**: hrtimer (softirq context) — single writer → SPSC.  
- **Consumer**: process context in `read()` — single reader per device (current scope).  
- **Synchronization**: SPSC **lockless ring** (acquire/release barriers) *or* spinlock (reference impl uses spinlock for simplicity; can evolve to lockless).  
- **Waiters**: waitqueue for `POLLIN`/`POLLPRI`.  
- **Control path**: serialize reconfiguration vs timer (`hrtimer_cancel()` before period change).

### 4.3 State Machine
```
      +-----------+      start()              +-----------+
      |  STOPPED  |  -----------------------> |  RUNNING  |
      +-----------+                           +-----------+
            ^                                         |
            |               stop()                    |
            +-----------------------------------------+
```
- **One‑shot** behavior (optional): produce 1 sample then transition to STOPPED (kept as extension; not required by challenge).

### 4.4 Timing Model
- Sysfs public unit: **ms** (`sampling_ms`).  
- Internal period: µs for precision. Reprogram sequence: cancel timer → update → re‑arm.  
- Default bounds: 5 ≤ `sampling_ms` ≤ 5000.

### 4.5 Ring Buffer Design (SPSC Lockless)
- Power‑of‑two capacity; head/tail indices wrap.  
- Overrun policy: **overwrite oldest** to favor recency. First post‑overrun sample carries `OVERFLOW` flag; `read()` may return `-EOVERFLOW` once.

### 4.6 File Tree Representation (Preliminary)
```
simtemp/
├─ kernel/
│  ├─ nxp_simtemp.c
│  ├─ nxp_simtemp.h
│  ├─ nxp_simtemp_ioctl.h
│  └─ dts/nxp-simtemp.dtsi
├─ user/cli/
│  └─ main.py
├─ scripts/
│  ├─ build.sh
│  ├─ run_demo.sh
│  └─ lint.sh
└─ docs/
   ├─ README.md
   ├─ DESIGN.md
   ├─ REQUIREMENTS.md
   ├─ TESTPLAN.md
   └─ AI_NOTES.md
```

### 4.7 File-to-Requirement Traceability Matrix
| Requirement | File(s) |
|---|---|
| FR‑01 (periodic sampling) | `nxp_simtemp.c` |
| FR‑02 (modes) | `nxp_simtemp.c` |
| FR‑03 (read) | `nxp_simtemp.c` |
| FR‑04 (poll/epoll) | `nxp_simtemp.c` |
| FR‑05 (timestamp/flags) | `nxp_simtemp.h`, `nxp_simtemp.c` |
| FR‑06 (threshold alert) | `nxp_simtemp.c` |
| FR‑07 (overrun policy) | `nxp_simtemp.c` |
| FR‑08 (sysfs/dt runtime) | `nxp_simtemp.c`, `dts/nxp-simtemp.dtsi` |
| FR‑09 (stats sysfs) | `nxp_simtemp.c` |

### 4.8 Rationale & Trade-offs
- SPSC ring buffer minimizes latency; spinlock fallback keeps code simpler for first delivery.  
- `POLLPRI` for alerts aligns with common Linux patterns (urgent/priority events).  
- Sysfs as canonical control plane; ioctl reserved for atomic/batch config.

### 4.9 Interface & Data Contract — **Design Freeze for MTS**
- IF‑RD‑01: `read()` returns integral multiples of `sizeof(struct simtemp_sample)`.  
- IF‑POLL‑01: `POLLIN` ⇒ ≥1 sample available; `POLLPRI` ⇒ threshold crossed.  
- IF‑SYSFS‑01: `sampling_ms`, `threshold_mC`, `mode`, `stats`.  
- IF‑IOCTL‑01 (optional): `SET/GET_CFG` for batch updates (sampling_ms, threshold_mC, mode).

---

## 5. UAPI/ABI v1

### 5.1 Binary Read Payload
```c
struct simtemp_sample {
    __u64 timestamp_ns;   // monotonic timestamp
    __s32 temp_mC;        // milli-degree Celsius (e.g., 44123 = 44.123 °C)
    __u32 flags;          // bit0=NEW_SAMPLE, bit1=THRESHOLD_CROSSED, bit2=OVERFLOW (optional)
} __attribute__((packed));
```

### 5.2 IOCTL Interface (Optional)
```c
#define SIMTEMP_IOC_MAGIC  's'
struct simtemp_cfg { __u32 sampling_ms; __s32 threshold_mC; __u32 mode; };
#define SIMTEMP_IOC_SET_CFG _IOW(SIMTEMP_IOC_MAGIC, 1, struct simtemp_cfg)
#define SIMTEMP_IOC_GET_CFG _IOR(SIMTEMP_IOC_MAGIC, 2, struct simtemp_cfg)
```

### 5.3 Sysfs ABI
- `sampling_ms` (RW, 5..5000) — sampling period in ms.  
- `threshold_mC` (RW, int) — threshold in m°C.  
- `mode` (RW: `normal|noisy|ramp`) — simulation waveform.  
- `stats` (RO) — `samples=<u64> overruns=<u32> alerts=<u32> alert_pending=<0|1> overflow_pending=<0|1> threshold_mC=<s32>`.

### 5.4 Poll/Epoll Semantics
- `POLLIN | POLLRDNORM`: new sample(s) available.  
- `POLLPRI`: threshold crossed on the latest sample (sticky until read).

### 5.5 Overflow/Backpressure Policy
- Overwrite oldest; mark first post‑overrun sample with `OVERFLOW` flag; optionally return `-EOVERFLOW` once.

---

## 6. Device Tree (DT) Binding
**compatible:** `"nxp,simtemp"`

**Properties:**
- `sampling-ms` (u32, default 100) — initial period in milliseconds.  
- `threshold-mC` (s32, default 45000) — threshold in m°C.  
- `status = "okay"`.

**Example:**
```dts
simtemp0: simtemp@0 {
    compatible = "nxp,simtemp";
    sampling-ms = <100>;
    threshold-mC = <45000>;
    status = "okay";
};
```

---

## 7. Debugging & Tracepoints (optional)
- `simtemp_enqueue(ts_ns, temp_mC, head, tail)`  
- `simtemp_overflow(capacity, drops)`  
- `simtemp_threshold(temp_mC, thr_mC)`

---

## 8. Testability Hooks for MTS
- Deterministic temperature generators for `normal/noisy/ramp`.  
- Edge cases: fast period (5 ms), threshold near mean, overflow, non‑blocking reads.

---

## 9. Error Handling & Return Codes
- `-EINVAL` invalid parameters; `-EAGAIN` non‑blocking empty read; `-EFAULT` copy errors; `-ENOTTY` unknown ioctl; `-EOVERFLOW` optional overrun signal.

---

## 10. Acceptance Criteria (Core) — **Aligned with Challenge**
- AC‑01 Build & Load: `build.sh` succeeds; `insmod` creates `/dev/simtemp` + sysfs attrs.  
- AC‑02 Data Path: `read()` returns documented binary record.  
- AC‑03 Events: `poll()` wakes on **new sample** and **threshold crossing** (distinct flags/bits).  
- AC‑04 Config Path: `echo 50 > .../sampling_ms` aumenta frecuencia; `echo 42000 > .../threshold_mC` afecta alertas; `cat stats` coherente.  
- AC‑05 Robustness: unload limpio (sin OOPS/warnings/leaks).  
- AC‑06 User App: CLI test mode configura, espera evento y reporta PASS/FAIL.

(Traceability: see `docs/REQUIREMENTS.md` AC mapping).

---

## 11. Tooling, CLI & Demo Scripts
- **CLI (`user/cli/main.py`)**: configura `sampling_ms`, `threshold_mC`, `mode`; lee `/dev/simtemp` con `poll`; `--test` valida alerta en ≤2 periodos.  
- **Scripts**: `scripts/build.sh`, `scripts/run_demo.sh`, `scripts/lint.sh` (opcional).  
- **Docs**: `README.md`, `DESIGN.md`, `REQUIREMENTS.md`, `TESTPLAN.md`, `AI_NOTES.md`.

---

## 12. Licensing and Intellectual Property
- SPDX in each source file: `// SPDX-License-Identifier: GPL-2.0`.

---

## 13. Document History
| Version | Date | Author | Notes |
|---|---|---|---|
| 0.3 | 2025-10-14 | F. E. Cabañas | Align with NXP: `sampling_ms`, `"nxp,simtemp"`, `stats` sysfs, GUI/CLI/scripts & AC listed. |
| 0.2 | 2025-10-06 | F. E. Cabañas | Pre‑alignment draft (µs units, alternate DT compatible). |
