# Module Test Specification (MTS)
## Sampler Data Collector (simtemp)

**License:** GPLv2  
**Author:** Fidel Eduardo Cabañas Castillo  
**Version:** Draft v0.1  
**Date:** 2025-10-05

---

> **Summary:**  
> This document specifies the test cases for the "simtemp" Linux kernel module. The tests are designed to verify the functional requirements and interfaces defined in the Module Software Design (MSD) document (`MSD_SimTemp.md`).

---

## Table of Contents

1. [Introduction and Purpose](#1-introduction-and-purpose)
2. [Referenced Documents](#2-referenced-documents)
3. [Test Strategy](#3-test-strategy)
4. [Test Environment](#4-test-environment)
5. [Test Case Specification](#5-test-case-specification)
    5.1. [FR-01: Sampling Frequency](#51-fr-01-sampling-frequency)
    5.2. [FR-02: User Interface](#52-fr-02-user-interface)
    5.3. [FR-03: Asynchronous Events](#53-fr-03-asynchronous-events)
    5.4. [FR-04: Configuration Interface](#54-fr-04-configuration-interface)
    5.5. [FR-05: Synchronization & Buffering](#55-fr-05-synchronization--buffering)
    5.6. [FR-06: Platform Integration](#56-fr-06-platform-integration)
    5.7. [FR-07: Licensing](#57-fr-07-licensing)
    5.8. [UAPI and IOCTLs](#58-uapi-and-ioctls)
6. [Document History](#6-document-history)

---

## 1. Introduction and Purpose

This document provides a comprehensive set of test cases to validate the `simtemp` kernel module. Its purpose is to ensure that the module meets all specified requirements, functions correctly, and is robust. All test cases are traceable to a requirement or design element in the MSD.

## 2. Referenced Documents

| ID        | Document Name         |
| :-------- | :-------------------- |
| **MSD-01**| `MSD_SimTemp.md`      |

## 3. Test Strategy

The testing will be performed through a combination of automated scripts and manual procedures.
- **Unit/Integration Tests:** Where possible, kernel unit testing frameworks could be used to test internal APIs like the ring buffer. (Future work)
- **System/Functional Tests:** The primary focus will be on black-box and gray-box testing from user space using shell scripts and Python applications. These tests will interact with the driver via the `/dev/simtemp` character device and the SysFS interface.
- **Static Analysis:** Code will be checked with `sparse` and other static analysis tools.

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
| **MTS-FR-01-01**  | FR-01          | Verify Minimum Sampling Frequency (200 Hz)  | 1. Set `sampling_ms` to `5` via SysFS. <br> 2. Start the sampler. <br> 3. Read a large number of samples (e.g., 1000). <br> 4. Analyze the timestamps (`period_msec`) of the collected samples. | The delta between consecutive timestamps should be approximately 5 ms. The average frequency should be close to 200 Hz, within an acceptable tolerance (e.g., +/- 5%). | System    | High     |
| **MTS-FR-01-02**  | FR-01, FR-04   | Verify Dynamic Frequency Change             | 1. Set `sampling_ms` to `100`. <br> 2. Read a few samples and verify the timestamp delta is ~100 ms. <br> 3. Set `sampling_ms` to `20`. <br> 4. Read a few more samples. | The timestamp delta should now be ~20 ms. The driver must not crash or hang.                                                                | System    | High     |

### 5.2. FR-02: User Interface

| Test Case ID      | Requirement ID | Test Title                                  | Test Description                                                                                                                            | Expected Results                                                                                                                            | Test Type | Priority |
| :---------------- | :------------- | :------------------------------------------ | :------------------------------------------------------------------------------------------------------------------------------------------ | :------------------------------------------------------------------------------------------------------------------------------------------ | :-------- | :------- |
| **MTS-FR-02-01**  | FR-02          | Verify Device Node Creation                 | 1. Load the `simtemp` module. <br> 2. Check for the existence of the device node.                                                          | The character device `/dev/simtemp` is created with major/minor numbers assigned. Permissions should be appropriate (e.g., 0666).          | System    | High     |
| **MTS-FR-02-02**  | FR-02          | Verify Binary Read Format                   | 1. Read one sample from `/dev/simtemp`. <br> 2. Verify the size of the data read.                                                            | The number of bytes read must equal `sizeof(struct simtemp_sample_v1)`. The content should be parsable according to the struct definition. | System    | High     |
| **MTS-FR-02-03**  | FR-02          | Test Blocking Read                          | 1. Stop the sampler. <br> 2. Attempt to read from `/dev/simtemp`. <br> 3. In another terminal, start the sampler.                            | The reading process should block until the sampler is started and a sample is produced, at which point the read completes successfully.   | System    | Medium   |
| **MTS-FR-02-04**  | FR-02          | Test Non-Blocking Read                      | 1. Open `/dev/simtemp` with `O_NONBLOCK`. <br> 2. Stop the sampler and ensure the buffer is empty. <br> 3. Attempt to read from the device. | The `read()` call should return immediately with an error code of `-EAGAIN`.                                                              | System    | Medium   |

### 5.3. FR-03: Asynchronous Events

| Test Case ID      | Requirement ID | Test Title                                  | Test Description                                                                                                                            | Expected Results                                                                                                                            | Test Type | Priority |
| :---------------- | :------------- | :------------------------------------------ | :------------------------------------------------------------------------------------------------------------------------------------------ | :------------------------------------------------------------------------------------------------------------------------------------------ | :-------- | :------- |
| **MTS-FR-03-01**  | FR-03          | Verify `poll()` for Data Ready (`POLLIN`)   | 1. Start with an empty buffer. <br> 2. Call `poll()` on the device file descriptor, waiting for `POLLIN`. <br> 3. Wait for the timer to produce a sample. | `poll()` should return with the `POLLIN | POLLRDNORM` events set as soon as a sample is available.                                           | System    | High     |
| **MTS-FR-03-02**  | FR-03          | Verify `poll()` for Threshold Alert (`POLLPRI`) | 1. Set `threshold_mC` to a high value (e.g., 50000). <br> 2. Call `poll()` waiting for `POLLPRI`. The call should timeout. <br> 3. Set `threshold_mC` to a low value that will be crossed. | `poll()` should return with the `POLLPRI` event set when the simulated temperature crosses the threshold.                                   | System    | High     |
| **MTS-FR-03-03**  | FR-03          | Verify `POLLPRI` Alert is One-Shot          | 1. Trigger a `POLLPRI` event as in the test above. <br> 2. Call `poll()` for `POLLPRI` again without changing the threshold. <br> 3. Write the same value to `threshold_mC` to reset the alert. <br> 4. Wait for the temperature to cross again. | The second `poll()` call should timeout. After resetting, the third `poll()` call should succeed.                                         | System    | Medium   |

### 5.4. FR-04: Configuration Interface

| Test Case ID      | Requirement ID | Test Title                                  | Test Description                                                                                                                            | Expected Results                                                                                                                            | Test Type | Priority |
| :---------------- | :------------- | :------------------------------------------ | :------------------------------------------------------------------------------------------------------------------------------------------ | :------------------------------------------------------------------------------------------------------------------------------------------ | :-------- | :------- |
| **MTS-FR-04-01**  | FR-04          | Verify SysFS Attribute Creation             | 1. Load the module. <br> 2. Check for the existence of the SysFS attributes under `/sys/class/misc/simtemp/`.                               | The files `sampling_ms` and `threshold_mC` must exist and have correct permissions.                                                         | System    | High     |
| **MTS-FR-04-02**  | FR-04          | Verify `sampling_ms` Attribute              | 1. `cat` the `sampling_ms` attribute. <br> 2. `echo 100 > sampling_ms`. <br> 3. `cat` the attribute again.                                     | The initial value should be the default. The new value (100) should be successfully written and read back.                                  | System    | High     |
| **MTS-FR-04-03**  | FR-04          | Verify `threshold_mC` Attribute             | 1. `cat` the `threshold_mC` attribute. <br> 2. `echo 35000 > threshold_mC`. <br> 3. `cat` the attribute again.                                  | The initial value should be the default. The new value (35000) should be successfully written and read back.                                | System    | High     |
| **MTS-FR-04-04**  | FR-04          | Test Invalid `sampling_ms` Input            | 1. Attempt to `echo 0 > sampling_ms`. <br> 2. Attempt to `echo 10001 > sampling_ms`. <br> 3. Attempt to `echo "abc" > sampling_ms`.         | All writes should fail with an `-EINVAL` error (or similar). The current sampling period should remain unchanged.                           | System    | Medium   |

### 5.5. FR-05: Synchronization & Buffering

| Test Case ID      | Requirement ID | Test Title                                  | Test Description                                                                                                                            | Expected Results                                                                                                                            | Test Type | Priority |
| :---------------- | :------------- | :------------------------------------------ | :------------------------------------------------------------------------------------------------------------------------------------------ | :------------------------------------------------------------------------------------------------------------------------------------------ | :-------- | :------- |
| **MTS-FR-05-01**  | FR-05          | Verify Buffer Overrun Detection             | 1. Set a fast sampling rate. <br> 2. Do not read from the device, allowing the buffer to fill up completely. <br> 3. Wait for more samples to be generated. <br> 4. Check the `buffer_overflows` counter in debugfs. | The `buffer_overflows` counter should be greater than 0. The driver must not crash. The `status_flags` in subsequent samples may indicate an overflow. | System    | High     |
| **MTS-FR-05-02**  | FR-05          | Concurrent Read/Write Stress Test           | 1. Start a process that reads from the device in a tight loop. <br> 2. While reading, continuously change the `sampling_ms` and `threshold_mC` values via SysFS. | The driver must remain stable without deadlocks, crashes, or data corruption.                                                               | System    | High     |

### 5.6. FR-06: Platform Integration

| Test Case ID      | Requirement ID | Test Title                                  | Test Description                                                                                                                            | Expected Results                                                                                                                            | Test Type | Priority |
| :---------------- | :------------- | :------------------------------------------ | :------------------------------------------------------------------------------------------------------------------------------------------ | :------------------------------------------------------------------------------------------------------------------------------------------ | :-------- | :------- |
| **MTS-FR-06-01**  | FR-06          | Verify Driver Binding via Device Tree       | 1. Ensure the `simtemp` device tree overlay is loaded. <br> 2. Load the `simtemp.ko` module.                                                | The driver's `probe` function should be called, and the driver should bind successfully to the device defined in the DT. Check `dmesg`.      | System    | High     |
| **MTS-FR-06-02**  | FR-06          | Verify DT `sample-period-us` Property       | 1. Set `sample-period-us = <20000>;` in the device tree overlay. <br> 2. Load the driver. <br> 3. Read the `sampling_ms` value from SysFS. | The `sampling_ms` attribute should reflect the value from the device tree (20 ms).                                                          | System    | Medium   |
| **MTS-FR-06-03**  | FR-06          | Test Driver Unload                          | 1. Load the driver and use it (read, poll, etc.). <br> 2. Unload the driver using `rmmod simtemp`.                                          | The module should unload cleanly without kernel panics or memory leaks. All resources (device node, SysFS files, timer) must be released. | System    | High     |

### 5.7. FR-07: Licensing

| Test Case ID      | Requirement ID | Test Title                                  | Test Description                                                                                                                            | Expected Results                                                                                                                            | Test Type | Priority |
| :---------------- | :------------- | :------------------------------------------ | :------------------------------------------------------------------------------------------------------------------------------------------ | :------------------------------------------------------------------------------------------------------------------------------------------ | :-------- | :------- |
| **MTS-FR-07-01**  | FR-07          | Verify SPDX License Identifiers             | 1. Scan all `.c` and `.h` source files.                                                                                                     | Every source file must begin with the comment `// SPDX-License-Identifier: GPL-2.0`.                                                        | Static    | High     |
| **MTS-FR-07-02**  | FR-07          | Verify `MODULE_LICENSE` Tag                 | 1. Check the module's info after loading.                                                                                                   | The output of `modinfo simtemp.ko` must show `license: GPLv2`.                                                                              | System    | High     |

### 5.8. UAPI and IOCTLs

| Test Case ID      | Requirement ID | Test Title                                  | Test Description                                                                                                                            | Expected Results                                                                                                                            | Test Type | Priority |
| :---------------- | :------------- | :------------------------------------------ | :------------------------------------------------------------------------------------------------------------------------------------------ | :------------------------------------------------------------------------------------------------------------------------------------------ | :-------- | :------- |
| **MTS-UAPI-01**   | IF-UAPI-01     | Test `SIMTEMP_IOC_SET_STATE` (Start/Stop)   | 1. Call the IOCTL with argument `1` (START). Verify samples are generated. <br> 2. Call the IOCTL with argument `0` (STOP). Verify samples cease. | The sampler should start and stop correctly. The IOCTL should return 0 on success.                                                          | System    | High     |
| **MTS-UAPI-02**   | IF-UAPI-01     | Test `SIMTEMP_IOC_SET_STATE` (Invalid Arg)  | 1. Call the IOCTL with an invalid argument (e.g., `2`).                                                                                     | The IOCTL should return `-EINVAL`.                                                                                                          | System    | Medium   |
| **MTS-UAPI-03**   | IF-UAPI-02     | Test `SIMTEMP_IOC_GET_STATE`                | 1. Call `SET_STATE` to start, then call `GET_STATE`. <br> 2. Call `SET_STATE` to stop, then call `GET_STATE`.                               | `GET_STATE` should return `1` when the sampler is running and `0` when it is stopped.                                                       | System    | High     |

---

## 6. Document History

| Version   | Date       | Description                                                        | Author     |
| --------- | ---------- | ------------------------------------------------------------------ | ---------- |
| 0.1       | 2025-10-05 | Initial draft based on `MSD_SimTemp.md` v0.1                       | F. Cabañas |
