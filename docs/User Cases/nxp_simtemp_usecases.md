# Use Cases — nxp_simtemp

This catalog summarizes the interaction scenarios for the `nxp_simtemp` driver, its supporting Device Tree, and the user-space utility included in the repository. Each use case draws on reviews of the source code (`core/`, `include/`) and the requirements documentation (`docs/`) to maintain traceability with the current implementation.

## Primary Actors
- **Platform Integrator:** Defines the Device Tree overlay and provides default values for the simulated sensor.
- **Kernel Platform Bus:** Detects the `platform_device`, runs `probe`, and handles resource teardown.
- **System Operator:** Adjusts parameters and controls the sampler through Sysfs.
- **User Application (CLI or custom):** Consumes samples, issues IOCTLs, and processes alerts.

## Summary Catalog

| ID | Name | Primary actor | Objective | Key references |
|----|--------|-----------------|----------|-------------------|
| UC-L1 | Provision default values | Platform Integrator | Set period, threshold, and initial mode through the Device Tree. | `core/simtemp_dt.c:24` |
| UC-L2 | Register and expose the driver | Kernel Platform Bus | Initialize structures, the misc device, and Sysfs attributes. | `core/simtemp_core.c:580` |
| UC-L3 | Control sampling state | System Operator / User Application | Start or stop the `hrtimer` that governs sample generation. | `core/simtemp_core.c:104`, `core/simtemp_core.c:515` |
| UC-L4 | Adjust simulation parameters | System Operator / User Application | Configure period, mode, threshold, and operation mode. | `core/simtemp_core.c:139`, `core/simtemp_core.c:191`, `core/simtemp_core.c:229`, `core/simtemp_core.c:269` |
| UC-L5 | Consume sample stream | User Application | Read binary data and convert it to temperature/flags. | `core/simtemp_core.c:406`, `include/uapi/simtemp_uapi.h:11` |
| UC-L6 | Detect and handle alerts | User Application | Receive notifications (`poll`) and react to threshold crossings. | `core/simtemp_core.c:486`, `core/simtemp_core.c:560` |
| UC-L7 | One-shot capture | System Operator / User Application | Perform a single acquisition flagged with `SIMTEMP_FLAG_ONESHOT_DONE`. | `core/simtemp_core.c:365` |
| UC-L8 | Query operational statistics | System Operator | Monitor counters for samples, overruns, and pending alerts. | `core/simtemp_core.c:295` |

## Detailed Use Cases

### UC-L1 — Provision default values
**Primary actor:** Platform Integrator  
**Stakeholders:** System Operator (receives a ready-to-use driver), User Application (avoids manual configuration).  
**Preconditions:** The `nxp-simtemp-overlay.dts` overlay is available.  
**Trigger:** The driver must be deployed with project-appropriate parameters.  
**Main flow:**
1. The integrator defines the `sampling-ms`, `threshold-mC`, and optionally `operation-mode` properties in the overlay.
2. The system compiles the overlay (`dtc`) and installs it in the boot path.
3. During boot, the kernel applies the overlay and creates the `platform_device` compatible with `"nxp,simtemp"`.
4. During `probe`, the driver reads those properties (`core/simtemp_dt.c:24`) and copies them into its internal context.
**Alternate flows:** If any property is missing, the driver uses default values (`sampling_ms = 500 ms`, `threshold_mC = 45000`).  
**Postconditions:** The driver starts with coherent initial parameters without manual intervention.

### UC-L2 — Register and expose the driver
**Primary actor:** Kernel Platform Bus  
**Stakeholders:** All driver users, as they rely on the initialized resources.  
**Preconditions:** The `platform_device` was created and matches the compatibility table.  
**Trigger:** The kernel invokes `nxp_simtemp_probe()` after the Device Tree match.  
**Main flow:**
1. The driver allocates memory for `struct nxp_simtemp_dev` and configures the ring buffer (`simtemp_ringbuf.c`).
2. It initializes the `hrtimer` with the current period and clears counters and flags.
3. It registers a `miscdevice` to expose `/dev/nxp_simtemp`.
4. It creates the Sysfs attributes (`state`, `sampling_ms`, `mode`, `threshold_mC`, `stats`) under `nxp_simtemp`.
5. It exposes the IOCTL interface (`nxp_simtemp_ioctl`) by registering the file operations (`core/simtemp_core.c:580`).
**Alternate flows:** If any step fails, the driver performs cleanup and returns an error code to the kernel.  
**Postconditions:** The device appears under `/dev/` and the Sysfs namespace is ready to consume.

### UC-L3 — Control sampling state
**Actors:** System Operator, User Application  
**Preconditions:** The driver is registered (`UC-L2`).  
**Trigger:** A request is made to start or stop acquisition.  
**Main flow:**
1. The actor writes `RUN`/`STOP` to `state` or issues `SIMTEMP_IOC_START/STOP`.
2. The driver validates the current state to avoid redundant transitions (`-EBUSY`).  
3. `RUN` arms the `hrtimer`, which schedules the periodic sampling routine.
4. `STOP` cancels the `hrtimer` and pauses the production of new samples.
**Postconditions:** `dev->state` is updated and wait queues notify blocked readers when applicable.

### UC-L4 — Adjust simulation parameters
**Actors:** System Operator, User Application  
**Preconditions:** The sampler is stopped (`UC-L3`).  
**Trigger:** The period, simulation mode, threshold, or operation mode must be modified.  
**Main flow:**
1. The actor writes to the corresponding Sysfs attribute or uses the associated IOCTL.
2. The driver converts and validates the data (e.g., period within `[5, 5000]` ms, modes `normal/noisy/ramp`).  
3. If the value is valid and the sampler is stopped, it updates the `nxp_simtemp_dev` structure.  
4. It optionally restarts the sampler to apply the changes.  
**Alternate flows:** Writing while the sampler is running returns `-EBUSY` (`core/simtemp_core.c:160`, `core/simtemp_core.c:532`).  
**Postconditions:** The next sampler execution reflects the updated parameters.

### UC-L5 — Consume sample stream
**Primary actor:** User Application  
**Preconditions:** The `/dev/nxp_simtemp` device exists and the sampler is running.  
**Trigger:** The application requires data for visualization or telemetry.  
**Main flow:**
1. The application opens the device with read permissions (`open(O_RDWR)` in `apitest/apitest.c:27`).  
2. It calls `read()` to obtain multiple `struct simtemp_sample_v1` instances.  
3. It interprets `temp_mC`, `timestamp_ns`, and `flags` according to the ABI (`include/uapi/simtemp_uapi.h:11`).  
4. It processes or displays the data and repeats while the sampler remains active.
**Alternate flows:** The user can rely on `poll()` to wait for available data and avoid long blocks.  
**Postconditions:** Samples are consumed and the read pointer advances within the ring buffer.

### UC-L6 — Detect and handle alerts
**Primary actor:** User Application  
**Preconditions:** A threshold is configured below the value that will trigger the signal.  
**Trigger:** The application wants to react to a temperature threshold crossing.  
**Main flow:**
1. It configures `threshold_mC` and optionally `mode = ramp` to force a rising value.  
2. The application sets up `poll()`/`epoll()` on the descriptor (`core/simtemp_core.c:486`).  
3. When the threshold is crossed, the driver marks the alert flag and wakes up with `POLLPRI`.  
4. The application processes the alert, reads the sample, and, if needed, resets the threshold or clears the condition.  
**Alternate flows:** When only new data is available without an alert, `poll()` returns `POLLIN`; both events may coexist.  
**Postconditions:** The application records the event and, if appropriate, notifies other components.

### UC-L7 — One-shot capture
**Actors:** System Operator, User Application  
**Preconditions:** The sampler is stopped and `operation_mode = one-shot` is configured.  
**Trigger:** A single representative sample is required.  
**Main flow:**
1. The actor selects the one-shot mode via Sysfs or IOCTL (`SIMTEMP_IOC_SET_MODE`).  
2. They execute `RUN` or `SIMTEMP_IOC_START`.  
3. The `hrtimer` generates a single sample and stops the sampler (`core/simtemp_core.c:365`).  
4. The final sample is tagged with `SIMTEMP_FLAG_ONESHOT_DONE` to signal that no further data will arrive.  
**Postconditions:** The driver returns to the stopped state and the application knows it must rearm to obtain another sample.

### UC-L8 — Query operational statistics
**Primary actor:** System Operator  
**Preconditions:** The driver was initialized (`UC-L2`).  
**Trigger:** Health validation of the sampling process or anomaly investigation is required.  
**Main flow:**
1. The operator reads `stats` in Sysfs (`core/simtemp_core.c:295`).  
2. The driver returns a textual summary with total samples, overruns, alerts, and pending flags.  
3. The operator uses the information to diagnose configuration issues or detect data loss.  
**Postconditions:** Telemetry is available to adjust parameters (for example, increase the period if overruns occur).

## Operational Considerations
- Critical configuration changes require the sampler to be stopped to avoid inconsistent states.
- The ring buffer uses an overwrite policy; applications must read frequently enough to avoid data loss.
- The ecosystem offers two control channels (Sysfs and IOCTL); standardizing on IOCTL enables atomic automation.
- The tests described in `docs/MTS_SimTemp.md` provide complementary scenarios to validate each use case.
