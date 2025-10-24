# LxDrTemp: Linux Driver Temperature Demo
**Author:** Fidel Cabañas (fideuardo@gmail.com)

## Overview

`nxp_simtemp` is a Linux kernel module that simulates a temperature sensor. It serves as a demonstration of modern driver development practices, including:

-   **Platform Driver Model:** Binds to a device defined in the Device Tree.
-   **Misc Device:** Exposes a simple character device interface (`/dev/nxp_simtemp`).
-   **HRTimer:** Used for high-resolution, periodic sampling.
-   **Sysfs:** Provides a runtime configuration interface for parameters like sampling period, simulation mode, and statistics.
-   **IOCTL:** Offers an alternative interface for application control.
-   **Poll/Epoll:** Supports asynchronous I/O for efficient data consumption.
# Features

*   **Two Operation Modes:**
    *   `continuous`: Generates samples periodically.
    *   `one-shot`: Generates a single sample and stops.
*   **Three Simulation Modes:**
    *   `noisy` : Introduce samples with noise.
    *   `ramp`  : Increase the temperature value over the time.
    *   `normal`: Sample without noise.
*   **Configurable Sampling Period:** The sampling rate can be adjusted at runtime via Sysfs.
*   **Device Tree Support:** Default parameters (period, mode) can be set via a Device Tree overlay.
*   **Efficient Data Buffering:** Uses a ring buffer to store samples, preventing data loss between the kernel and user space.

## Building the Driver

### Prerequisites

Ensure you have the necessary tools and kernel headers installed. On Debian-based systems (like Raspberry Pi OS), you can run:

```bash
sudo apt update
sudo apt install build-essential linux-headers-$(uname -r) libelf-dev dkms
```

### Build Commands

*   **Build the driver and test utility:**
    ```bash
    make
    ```

*   **Clean the build artifacts:**
    ```bash
    make clean
    ```

## How to Use the Driver

### 1. Loading and Unloading the Module
*   **Load the Device Tree overlay:**
    ```bash
    sudo cp simtemp.dtbo /boot/overlays/
    sudo dtoverlay simtemp
    ```

*   **Load the driver into the kernel:**
    ```bash
    sudo insmod simtemp.ko
    ```

*   **Verify that the device was created:**
    ```bash
    ls -l /dev/nxp_simtemp
    ```

*   **Unload the driver from the kernel:**
    ```bash
    sudo rmmod nxp_simtemp
    ```

### 2. Device Tree Overlay (Required for Probe)

This driver is a `platform_driver`, so a matching `platform_device` must exist in the Device Tree for the `probe` routine to run.

1.  **Create the overlay file `nxp-simtemp-overlay.dts`:**
    ```dts
    /dts-v1/;
    /plugin/;

    / {
        compatible = "brcm,bcm2835"; // Raspberry Pi specific

        fragment@0 {
            target-path = "/";
            __overlay__ {
                simtemp: simtemp {
                    compatible = "nxp,simtemp";
                    status = "okay";
                    sampling-ms = <500>;
                    threshold-mC = <45000>;
                };
            };
        };
    };
    ```

2.  **Compile and install the overlay:**
    ```bash
    dtc -@ -I dts -O dtb -o simtemp.dtbo nxp-simtemp-overlay.dts
    sudo cp simtemp.dtbo /boot/overlays/
    sudo dtoverlay simtemp
    ```

### ACPI/x86 Test Setup (No Device Tree)

On ACPI-based hosts there is no Device Tree overlay support. Load the helper module that registers a fake `platform_device` before inserting the driver:

```bash
make modules
sudo insmod simtemp_pdev_stub.ko   # registers nxp_simtemp platform_device
sudo insmod simtemp.ko             # driver now probes and creates /dev/nxp_simtemp
```

To unload:

```bash
sudo rmmod simtemp
sudo rmmod simtemp_pdev_stub
```

Without `simtemp_pdev_stub.ko` the driver will not bind on ACPI platforms and `/dev/nxp_simtemp` will not appear.

### 2. Sysfs Interface (Configuration)

The driver exposes its parameters via files in `/sys/class/misc/nxp_simtemp/`. Use `cat` to read and `echo ... | sudo tee ...` to write.

*   **Check current state (0=STOP, 1=RUN):**
    ```bash
    cat /sys/class/misc/nxp_simtemp/state
    ```
*   **Set the operation mode ("continuous" or "one-shot"):**
    ```bash
    echo "one-shot" | sudo tee /sys/class/misc/nxp_simtemp/operation_mode
    ```
    or return to the default mode:
    ```bash
    echo "continuous" | sudo tee /sys/class/misc/nxp_simtemp/operation_mode
    ```

*   **Start the sampler:**
    ```bash
    echo RUN | sudo tee /sys/class/misc/nxp_simtemp/state
    ```


*   **Stop the sampler:**
    ```bash
    echo STOP | sudo tee /sys/class/misc/nxp_simtemp/state
    ```


*   **Change sampling period to 200ms (must be stopped first):**
    ```bash
    echo STOP | sudo tee /sys/class/misc/nxp_simtemp/state
    echo 200 | sudo tee /sys/class/misc/nxp_simtemp/sampling_ms
    ```

*   **Change simulation mode to "noisy":**
    ```bash
    echo STOP | sudo tee /sys/class/misc/nxp_simtemp/state
    echo noisy | sudo tee /sys/class/misc/nxp_simtemp/mode
    ```

*   **Check statistics:**
    ```bash
    cat /sys/class/misc/nxp_simtemp/stats
    ```
    The output includes total samples, buffer overruns, number of threshold alerts,
    and whether an alert or overflow is currently pending (1 = yes, 0 = no).

*   **Configure the temperature threshold:**
    ```bash
    echo STOP | sudo tee /sys/class/misc/nxp_simtemp/state
    echo 28000 | sudo tee /sys/class/misc/nxp_simtemp/threshold_mC
    echo ramp | sudo tee /sys/class/misc/nxp_simtemp/mode   # generate a linear ramp
    echo RUN  | sudo tee /sys/class/misc/nxp_simtemp/state
    ```
    Read a few samples and inspect the `flags` field; when the temperature exceeds the threshold the alert bit becomes active:
    ```bash
    sudo dd if=/dev/nxp_simtemp bs=16 count=4 2>/dev/null | hexdump -e '1/8 "%016x " " " 1/4 "%08x " "\n"'
    ```
    To confirm the current threshold value:
    ```bash
    cat /sys/class/misc/nxp_simtemp/threshold_mC
    ```

### 3. Device Node (Reading Data)

The `/dev/nxp_simtemp` node is used to read the binary temperature samples.

*   **Read one sample (16 bytes) and save it to a file:**
    ```bash
    dd if=/dev/nxp_simtemp of=sample.bin bs=16 count=1
    ```

*   **Display the sample content in hexadecimal:**
    ```bash
    hexdump -C sample.bin
    ```

### 4. `apitest` Utility (IOCTL Commands)

The `apitest` utility provides a command-line wrapper for the `ioctl` interface. It is compiled automatically with `make`.

*   **Example Usage:**
    ```bash
    # Start the sampler
    ./apitest /dev/nxp_simtemp start

    # Read 5 temperature samples
    ./apitest /dev/nxp_simtemp read 5

    # Stop the sampler to change settings
    ./apitest /dev/nxp_simtemp stop

    # Change the period to 500ms
    ./apitest /dev/nxp_simtemp set_period 500

    # Inspect / adjust the temperature threshold
    ./apitest /dev/nxp_simtemp get_threshold
    ./apitest /dev/nxp_simtemp set_threshold 28000

    # Run the automated self-test (sets a low threshold and checks for alert)
    ./apitest /dev/nxp_simtemp --test
    ```

### 5. Automated Self-Test Script

For a fully automated cycle that installs the overlay, loads the module, runs the CLI self-test, prints stats, and removes everything afterwards, use:

```bash
cd kernel
./scripts/run_selftest.sh
```

This script requires sudo privileges. It rebuilds `simtemp.dtbo` and the `apitest` CLI if needed, refreshes `/boot/overlays/simtemp.dtbo`, applies the overlay on-demand (or detects if it is already enabled via `config.txt`), loads `simtemp.ko`, runs the CLI self-test, prints stats, and removes everything afterwards.

## Project Documentation

This project includes detailed design and requirements documentation:
-   `docs/MSD_SimTemp.md`: Module Software Design
-   `docs/Requirements/Requirements.md`: Functional Requirements
-   `docs/MTS_SimTemp.md`: Module Test Specification
