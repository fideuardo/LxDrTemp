# Module Test Specification (MTS)
## Sampler Data Collector (nxp_simtemp)

**License:** GPLv2  
**Author:** Fidel Eduardo Cabañas Castillo  
**Version:** Draft v0.3  
**Date:** 2025-10-15

---

> **Summary:**  
> This document specifies the test cases for the "nxp_simtemp" Linux kernel module. The tests are designed to verify the functional requirements and interfaces defined in the Module Software Design (MSD) document (`MSD_SimTemp.md` v0.3).

---

## Table of Contents

1. Introduction and Purpose
2. Referenced Documents
3. Test Strategy
4. Test Environment
5. Test Case Specification
    5.1. FR-01: Sampling Frequency
    5.2. FR-02/FR-03: User Interface & Data Format
    5.3. FR-04/FR-06: Asynchronous Events & Thresholds
    5.4. FR-08/FR-09: Configuration & Stats Interface (SysFS/DT)
    5.5. FR-07: Synchronization & Buffering
    5.6. Module Lifecycle & Licensing
    5.7. UAPI and IOCTLs
6. Document History

---

## 1. Introduction and Purpose

This document provides a comprehensive set of test cases to validate the `nxp_simtemp` kernel module. Its purpose is to ensure that the module meets all specified requirements, functions correctly, and is robust. All test cases are traceable to a requirement or design element in the MSD.

## 2. Referenced Documents

| ID        | Document Name         |
| :-------- | :-------------------- |
| **MSD-01**| `MSD_SimTemp.md` v0.3 |

## 3. Test Strategy

The testing will be performed through a combination of automated scripts and manual procedures.
- **Unit/Integration Tests:** Where possible, kernel unit testing frameworks could be used to test internal APIs like the ring buffer. (Future work)
- **System/Functional Tests:** The primary focus will be on black-box and gray-box testing from user space using shell scripts and Python applications. These tests will interact with the driver via the `/dev/nxp_simtemp` character device and the SysFS interface.
- **Static Analysis:** Code will be checked with `sparse`, `checkpatch.pl`, and other static analysis tools.

## 4. Test Environment

- **Hardware:** NXP i.MX 8M Mini EVK (or a QEMU environment emulating it).
- **Software:**
    - Linux Kernel (version 5.10+).
    - A cross-compilation toolchain.
    - User space test scripts (`bash`, `python3`).
    - `dtc` (Device Tree Compiler).

---

## 5. Test Case Specification

### 5.1. FR-01: Sampling Frequency

| Test Case ID      | Requirement ID | Test Title                                  | Test Description                                                                                                                            | Expected Results                                                                                                                            | Test Type | Priority |
| :---------------- | :------------- | :------------------------------------------ | :------------------------------------------------------------------------------------------------------------------------------------------ | :------------------------------------------------------------------------------------------------------------------------------------------ | :-------- | :------- |
| **MTS-FR-01-01**  | FR-01          | Verify Sampling Frequency (e.g., 100 Hz)    | 1. Set `sampling_ms` to `10` via SysFS. <br> 2. Start the sampler. <br> 3. Read a large number of samples (e.g., 1000). <br> 4. Analyze the timestamps of the collected samples. | The delta between consecutive timestamps should be approximately 10 ms. The average frequency should be close to 100 Hz, within an acceptable tolerance (e.g., +/- 5%). | System    | High     |
| **MTS-FR-01-02**  | FR-01, FR-08   | Verify Dynamic Frequency Change             | 1. Stop the sampler. <br> 2. Set `sampling_ms` to `100`. <br> 3. Start sampler, read a few samples and verify the timestamp delta is ~100 ms. <br> 4. Stop sampler, set `sampling_ms` to `20`. <br> 5. Start sampler, read a few more samples. | The timestamp delta should now be ~20 ms. The driver must not crash or hang. The change must be rejected with `-EBUSY` if the sampler is running. | System    | High     |

### 5.2. FR-02/FR-03: User Interface & Data Format

| Test Case ID      | Requirement ID | Test Title                                  | Test Description                                                                                                                            | Expected Results                                                                                                                            | Test Type | Priority |
| :---------------- | :------------- | :------------------------------------------ | :------------------------------------------------------------------------------------------------------------------------------------------ | :------------------------------------------------------------------------------------------------------------------------------------------ | :-------- | :------- |
| **MTS-FR-02-01**  | FR-08          | Verify Device Node Creation                 | 1. Load the `nxp_simtemp` module. <br> 2. Check for the existence of the device node.                                                        | The character device `/dev/nxp_simtemp` is created with major/minor numbers assigned. Permissions should be appropriate (e.g., 0666).       | System    | High     |
| **MTS-FR-03-01**  | FR-03, FR-05   | Verify Binary Read Format                   | 1. Read one sample from `/dev/nxp_simtemp`. <br> 2. Verify the size of the data read.                                                          | The number of bytes read must equal `sizeof(struct simtemp_sample_v1)`. The content should be parsable according to the struct definition. | System    | High     |
| **MTS-FR-03-02**  | FR-03          | Test Blocking Read                          | 1. Stop the sampler. <br> 2. Attempt to read from `/dev/nxp_simtemp`. <br> 3. In another terminal, start the sampler.                          | The reading process should block until the sampler is started and a sample is produced, at which point the read completes successfully.   | System    | Medium   |
| **MTS-FR-03-03**  | FR-03          | Test Non-Blocking Read                      | 1. Open `/dev/nxp_simtemp` with `O_NONBLOCK`. <br> 2. Stop the sampler and ensure the buffer is empty. <br> 3. Attempt to read from the device. | The `read()` call should return immediately with an error code of `-EAGAIN`.                                                              | System    | Medium   |

### 5.3. FR-04/FR-06: Asynchronous Events & Thresholds

| Test Case ID      | Requirement ID | Test Title                                  | Test Description                                                                                                                            | Expected Results                                                                                                                            | Test Type | Priority |
| :---------------- | :------------- | :------------------------------------------ | :------------------------------------------------------------------------------------------------------------------------------------------ | :------------------------------------------------------------------------------------------------------------------------------------------ | :-------- | :------- |
| **MTS-FR-04-01**  | FR-04          | Verify `poll()` for Data Ready (`POLLIN`)   | 1. Start with an empty buffer. <br> 2. Call `poll()` on the device file descriptor, waiting for `POLLIN`. <br> 3. Wait for the timer to produce a sample. | `poll()` should return with the `POLLIN | POLLRDNORM` events set as soon as a sample is available.                                           | System    | High     |
| **MTS-FR-04-02**  | FR-04, FR-06   | Verify `poll()` for Threshold Alert (`POLLPRI`) | 1. Set `threshold_mC` to a high value (e.g., 50000). <br> 2. Call `poll()` waiting for `POLLPRI`. The call should timeout. <br> 3. Set `threshold_mC` to a low value that will be crossed. | `poll()` should return with the `POLLPRI` event set when the simulated temperature crosses the threshold.                                   | System    | High     |
| **MTS-FR-04-03**  | FR-04, FR-06   | Verify `POLLPRI` Alert is Sticky            | 1. Trigger a `POLLPRI` event as in the test above. <br> 2. Call `poll()` for `POLLPRI` again without changing the threshold. <br> 3. Read the sample that caused the alert. <br> 4. Call `poll()` for `POLLPRI` again. | The second `poll()` call should succeed (event is sticky). The third `poll()` call should timeout (event is cleared on read).              | System    | Medium   |

### 5.4. FR-08/FR-09: Configuration & Stats Interface (SysFS/DT)

| Test Case ID      | Requirement ID | Test Title                                  | Test Description                                                                                                                            | Expected Results                                                                                                                            | Test Type | Priority |
| :---------------- | :------------- | :------------------------------------------ | :------------------------------------------------------------------------------------------------------------------------------------------ | :------------------------------------------------------------------------------------------------------------------------------------------ | :-------- | :------- |
| **MTS-FR-08-01**  | FR-08          | Verify SysFS Attribute Creation             | 1. Load the module. <br> 2. Check for the existence of the SysFS attributes under `/sys/class/misc/nxp_simtemp/`.                           | The files `sampling_ms`, `threshold_mC`, `mode`, and `stats` must exist and have correct permissions.                                     | System    | High     |
| **MTS-FR-08-02**  | FR-01, FR-08   | Verify `sampling_ms` Attribute              | 1. `cat` the `sampling_ms` attribute. <br> 2. `echo 100 > sampling_ms`. <br> 3. `cat` the attribute again.                                     | The initial value should be the default. The new value (100) should be successfully written and read back.                                  | System    | High     |
| **MTS-FR-08-03**  | FR-06, FR-08   | Verify `threshold_mC` Attribute             | 1. `cat` the `threshold_mC` attribute. <br> 2. `echo 35000 > threshold_mC`. <br> 3. `cat` the attribute again.                                  | The initial value should be the default. The new value (35000) should be successfully written and read back.                                | System    | High     |
| **MTS-FR-08-04**  | FR-01, FR-08   | Test Invalid `sampling_ms` Input            | 1. Attempt to `echo 4 > sampling_ms`. <br> 2. Attempt to `echo 5001 > sampling_ms`. <br> 3. Attempt to `echo "abc" > sampling_ms`.           | All writes should fail with an `-EINVAL` error. The current sampling period should remain unchanged.                                        | System    | Medium   |
| **MTS-FR-08-05**  | FR-02, FR-08   | Verify `mode` Attribute                     | 1. `cat` the `mode` attribute. <br> 2. `echo noisy > mode`. <br> 3. `cat` the attribute again. <br> 4. `echo invalid > mode`.                | The initial value should be `normal`. The new value (`noisy`) should be written and read back. The invalid write should fail with `-EINVAL`. | System    | High     |
| **MTS-FR-08-06**  | FR-08          | Verify Driver Binding via Device Tree       | 1. Ensure the `nxp,simtemp` device tree overlay is loaded. <br> 2. Load the `nxp_simtemp.ko` module.                                          | The driver's `probe` function should be called, and the driver should bind successfully to the device defined in the DT. Check `dmesg`.      | System    | High     |
| **MTS-FR-08-07**  | FR-08          | Verify DT `sampling-ms` Property            | 1. Set `sampling-ms = <20>;` in the device tree overlay. <br> 2. Load the driver. <br> 3. Read the `sampling_ms` value from SysFS.         | The `sampling_ms` attribute should reflect the value from the device tree (20 ms).                                                          | System    | Medium   |
| **MTS-FR-08-08**  | FR-08          | Verify DT `threshold-mC` Property           | 1. Set `threshold-mC = <42000>;` in the device tree overlay. <br> 2. Load the driver. <br> 3. Read the `threshold_mC` value from SysFS.     | The `threshold_mC` attribute should reflect the value from the device tree (42000 mC).                                                    | System    | Medium   |
| **MTS-FR-09-01**  | FR-09          | Verify `stats` Attribute                    | 1. Load driver, read `stats`. <br> 2. Generate samples, trigger an alert, cause an overflow, and perform a short read. <br> 3. Read `stats` again. | Initial stats should be zero. Counters for `samples`, `alerts`, `overruns`, `shortreads` must increment correctly. Attribute must be read-only. | System    | High     |

### 5.5. FR-07: Synchronization & Buffering

| Test Case ID      | Requirement ID | Test Title                                  | Test Description                                                                                                                            | Expected Results                                                                                                                            | Test Type | Priority |
| :---------------- | :------------- | :------------------------------------------ | :------------------------------------------------------------------------------------------------------------------------------------------ | :------------------------------------------------------------------------------------------------------------------------------------------ | :-------- | :------- |
| **MTS-FR-07-01**  | FR-07          | Verify Buffer Overrun Policy                | 1. Set a fast sampling rate (e.g., 5ms). <br> 2. Do not read from the device, allowing the buffer to fill up and overflow. <br> 3. Read one sample. | The driver must not crash. The first sample read *after* the overflow should have the `OVERFLOW` flag set. The `overruns` counter in `stats` must be non-zero. | System    | High     |
| **MTS-FR-07-02**  | FR-07          | Concurrent Read/Write Stress Test           | 1. Start a process that reads from the device in a tight loop. <br> 2. While reading, continuously change `sampling_ms`, `threshold_mC`, and `mode` values via SysFS from another process. | The driver must remain stable without deadlocks, crashes, or data corruption.                                                               | System    | High     |

### 5.6. Module Lifecycle & Licensing

| Test Case ID      | Requirement ID | Test Title                                  | Test Description                                                                                                                            | Expected Results                                                                                                                            | Test Type | Priority |
| :---------------- | :------------- | :------------------------------------------ | :------------------------------------------------------------------------------------------------------------------------------------------ | :------------------------------------------------------------------------------------------------------------------------------------------ | :-------- | :------- |
| **MTS-LC-01**     | N/A            | Test Driver Unload                          | 1. Load the driver and use it (read, poll, etc.). <br> 2. Unload the driver using `rmmod nxp_simtemp`.                                        | The module should unload cleanly without kernel panics or memory leaks. All resources (device node, SysFS files, timer) must be released. | System    | High     |
| **MTS-LIC-01**    | N/A            | Verify SPDX License Identifiers             | 1. Scan all `.c` and `.h` source files.                                                                                                     | Every source file must begin with the comment `// SPDX-License-Identifier: GPL-2.0-only` or similar.                                        | Static    | High     |
| **MTS-LIC-02**    | N/A            | Verify `MODULE_LICENSE` Tag                 | 1. Check the module's info after loading.                                                                                                   | The output of `modinfo nxp_simtemp.ko` must show `license: GPL` or `GPLv2`.                                                                 | System    | High     |

### 5.7. UAPI and IOCTLs

| Test Case ID      | Requirement ID | Test Title                                  | Test Description                                                                                                                            | Expected Results                                                                                                                            | Test Type | Priority |
| :---------------- | :------------- | :------------------------------------------ | :------------------------------------------------------------------------------------------------------------------------------------------ | :------------------------------------------------------------------------------------------------------------------------------------------ | :-------- | :------- |
| **MTS-UAPI-01**   | IF-IOCTL-01    | Test `SIMTEMP_IOC_START`/`STOP`             | 1. Call `SIMTEMP_IOC_START`. Verify samples are generated. <br> 2. Call `SIMTEMP_IOC_STOP`. Verify samples cease.                               | The sampler should start and stop correctly. The IOCTLs should return 0 on success.                                                         | System    | High     |
| **MTS-UAPI-02**   | IF-IOCTL-01    | Test `SIMTEMP_IOC_SET_PERIOD`               | 1. Stop sampler. <br> 2. Call `SIMTEMP_IOC_SET_PERIOD` with `100`. <br> 3. Check via `SIMTEMP_IOC_GET_PERIOD` or SysFS that the value was set. | The IOCTL should return 0 on success. The new period should be reflected. The call should fail with `-EBUSY` if the sampler is running.      | System    | Medium   |
| **MTS-UAPI-03**   | IF-IOCTL-01    | Test `SIMTEMP_IOC_SET_CFG` (Atomic)         | 1. Stop sampler. <br> 2. Prepare a `struct simtemp_cfg` with new period, threshold, and mode. <br> 3. Call `SIMTEMP_IOC_SET_CFG`. <br> 4. Verify all values with GET ioctls or SysFS. | All configuration parameters should be updated in a single, atomic operation. The IOCTL should return 0 on success. (Future work, based on MSD) | System    | Medium   |

---

## 6. Document History

| Version   | Date       | Description                                                        | Author     |
| --------- | ---------- | ------------------------------------------------------------------ | ---------- |
| 0.3       | 2025-10-15 | Refined test cases, fixed IDs, and improved alignment with MSD v0.3. | F. Cabañas |
| 0.2       | 2025-10-14 | Aligned test cases with MSD v0.3 (nxp_simtemp, stats, modes)       | F. Cabañas |
| 0.1       | 2025-10-05 | Initial draft based on `MSD_SimTemp.md` v0.1                       | F. Cabañas |