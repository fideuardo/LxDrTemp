# REQUIREMENTS.md — NXP Systems Software Engineer Challenge

**Module:** `nxp_simtemp`  
**Author:** Fidel Eduardo Cabañas Castillo  
**Version:** Draft 1.0 (2025-10-14)  
**Scope:** Functional & Acceptance requirements for the virtual temperature sensor system.

---

## 1. Functional Requirements

| ID | Title | Description / Expected Behavior | Type |
|----|--------|----------------------------------|------|
| **RQ-CTRL-01** | Configuration Plane | The driver shall expose runtime configuration attributes via `sysfs` and/or `ioctl`. These must include: `sampling_ms`, `threshold_mC`, and `mode`. | Functional |
| **RQ-DATA-01** | Data Plane | The driver shall expose a character device `/dev/simtemp` that provides binary temperature samples via `read()`. | Functional |
| **RQ-EVT-01** | Event Notification | The driver shall support event notification using `poll()` or `epoll()`, signaling **new data** (POLLIN) and **threshold crossings** (POLLPRI). | Functional |
| **RQ-TIME-01** | Sampling Period | The driver shall generate temperature samples every *N* milliseconds using a high-resolution timer (`hrtimer`). The period shall be configurable within safe bounds (e.g. 5–5000 ms). | Functional |
| **RQ-THR-01** | Threshold Detection | The driver shall detect when the current temperature exceeds `threshold_mC` and mark the sample’s flag bit accordingly. | Functional |
| **RQ-MODE-01** | Temperature Generation Modes | The driver shall simulate temperature values using three selectable modes: `normal`, `noisy`, and `ramp`. | Functional |
| **RQ-STATE-01** | Status & Statistics | The driver shall expose read-only `stats` showing counts of samples, alerts, short reads, and last error. | Functional |
| **RQ-DT-01** | Device Tree Binding | The driver shall support `compatible = "nxp,simtemp"` and parse optional DT properties: `sampling-ms`, `threshold-mC`. | Functional |
| **RQ-SAFE-01** | Safe Unload | The driver shall correctly cancel timers, free memory, and unregister devices on removal, without leaks or kernel warnings. | Non-Functional |
| **RQ-CLI-01** | CLI Application | A Python or C++ user-space application shall configure and read data from `/dev/simtemp`, displaying formatted timestamps, temperature, and alert status. | Functional |
| **RQ-CLI-02** | CLI Test Mode | The CLI shall provide a `--test` option that sets a low threshold and validates that an alert is detected within two sampling periods. | Functional |
| **RQ-SCR-01** | Automation Scripts | Scripts shall exist for build (`build.sh`), demo (`run_demo.sh`), and optional lint (`lint.sh`). | Functional |
| **RQ-DOC-01** | Documentation | The repository shall include `README.md`, `DESIGN.md`, `TESTPLAN.md`, and `AI_NOTES.md`. | Non-Functional |
| **RQ-VID-01** | Demonstration Video | A 2–3 min video shall demonstrate load → configure → read → alert → unload sequence. | Non-Functional |

---

## 2. Derived Non-Functional Requirements

| ID | Title | Description |
|----|--------|-------------|
| **RQ-CONC-01** | Concurrency & Locking | Shared structures (ring buffer, state flags) must be protected by proper locking (spinlock for producer/consumer, waitqueue for readers). |
| **RQ-ERR-01** | Error Handling | Invalid sysfs writes or ioctl parameters must return standard errors (`-EINVAL`, `-EFAULT`, `-ENOTTY`). |
| **RQ-ROB-01** | Robustness | The module must handle fast sampling periods (down to 5 ms) without deadlocks or crashes. |
| **RQ-API-01** | ABI Contract | The binary record format must be stable, documented, and little-endian. Partial reads must be handled gracefully. |
| **RQ-MOD-01** | Modularity | Source code shall be divided logically into core, sysfs, ioctl, and DT components. |
| **RQ-SEC-01** | Security | Validate user input; avoid unbounded copies; use appropriate file permissions (e.g., `0644` for sysfs attributes). |

---

## 3. Acceptance Criteria (Mapping to Requirements)

| ID | Acceptance Criterion | Verification Method |
|----|----------------------|----------------------|
| **AC-01 (Build & Load)** | `build.sh` completes successfully; `insmod nxp_simtemp.ko` creates `/dev/simtemp` and sysfs attributes. | Manual test, console output |
| **AC-02 (Periodic Read)** | Reading `/dev/simtemp` returns valid binary records at the configured sampling period. | CLI read timing |
| **AC-03 (Poll/Alert)** | `poll()`/`epoll()` wake up on new samples and when the threshold is crossed (distinct event bits). | CLI test mode |
| **AC-04 (Sysfs Config)** | Writing `sampling_ms` or `threshold_mC` via sysfs updates runtime behavior; invalid values return `-EINVAL`. | Manual test |
| **AC-05 (Stats Reporting)** | `cat /sys/class/misc/simtemp/stats` shows coherent counters that increment correctly. | Visual inspection |
| **AC-06 (Safe Unload)** | `rmmod nxp_simtemp` produces no warnings, no OOPS, and frees all resources. | dmesg verification |
| **AC-07 (CLI PASS/FAIL)** | CLI `--test` exits 0 (PASS) when alert occurs within 2 periods, non-zero (FAIL) otherwise. | Automated run |
| **AC-08 (Documentation)** | `README.md`, `DESIGN.md`, `TESTPLAN.md`, `REQUIREMENTS.md`, and `AI_NOTES.md` are present, consistent, and up-to-date. | Review |
| **AC-09 (Git Hygiene)** | Repository has clean, logical commit history and tag `v1.0`. | Repository audit |
| **AC-10 (Video Proof)** | The demo video shows build, configuration, live readings, threshold alert, and module unload. | Visual review by NXP reviewers |

---

## 4. Traceability Matrix (summary)

| Requirement | Acceptance Criteria |
|--------------|---------------------|
| RQ-CTRL-01 | AC-04 |
| RQ-DATA-01 | AC-02, AC-03 |
| RQ-EVT-01 | AC-03 |
| RQ-TIME-01 | AC-02 |
| RQ-THR-01 | AC-03, AC-07 |
| RQ-MODE-01 | AC-02 |
| RQ-STATE-01 | AC-05 |
| RQ-DT-01 | AC-01 |
| RQ-SAFE-01 | AC-06 |
| RQ-CLI-01 | AC-02, AC-03 |
| RQ-CLI-02 | AC-07 |
| RQ-SCR-01 | AC-01 |
| RQ-DOC-01 | AC-08 |
| RQ-VID-01 | AC-10 |

---

## 5. References

- NXP Candidate Challenge — “Virtual Sensor + Alert Path” (v2025-09)
- Linux kernel driver API (v6.6+)
- Kernel Documentation: `Documentation/filesystems/sysfs.rst`, `hrtimer`, `miscdevice`
- POSIX: `poll(2)`, `epoll(7)`
