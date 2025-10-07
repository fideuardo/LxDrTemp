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
- FR‑05: Samples include **timestamp (ns)** and **effective period (µs)**.
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

##### UC-09 Clear threshold alert (read-to-clear / ioctl)
**Primary Actor:** App  
**Preconditions:** Alert pending from UC-08.  
**Flow (read-to-clear):**  
1. App performs `read()`; the sample indicating the edge has `flags&THR_EDGE`.  
2. Pending alert is cleared.  
**Flow (ioctl):**  
1. App calls `SIMTEMP_IOC_CLR_ALERT` to clear without consuming data.  
**Trace:** FR-04, FR-06; IF-POLL-01, IF-IOCTL-01.

##### UC-10 Validate read payload fields (ts_ns, period_us, flags)
**Primary Actor:** Test Harness  
**Preconditions:** RUNNING with known `sampling_us`.  
**Flow:**  
1. Read a sequence of samples; compute deltas of `ts_ns`.  
2. Verify `period_us` field equals configured period (± tolerance).  
3. Verify `flags` reflect expected conditions (e.g., none set).  
**Acceptance:** Period error within implementation tolerance; no spurious flags.  
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
