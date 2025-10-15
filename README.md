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
*    **Load dtoverlay**
        ```bash
        sudo cp simtemp.dtbo /boot/overlays/
        sudo dtoverlay nxp-simtemp
        ```
    
*   **Load the driver into the kernel:**
    ```bash
    sudo insmod nxp_simtemp.ko
    ```

*   **Verify that the device was created:**
    ```bash
    ls -l /dev/nxp_simtemp
    ```

*   **Unload the driver from the kernel:**
    ```bash
    sudo rmmod nxp_simtemp
    ```

### 2. Device Tree Overlay (Crucial para la Carga)

Este es un `platform_driver`, lo que significa que necesita que un `platform_device` sea declarado en el Device Tree para que la función `probe` sea llamada.

1.  **Crea el fichero de overlay `nxp-simtemp-overlay.dts`:**
    ```dts
    /dts-v1/;
    /plugin/;

    / {
        compatible = "brcm,bcm2835"; // Específico para Raspberry Pi

        fragment@0 {
            target-path = "/";
            __overlay__ {
                simtemp: simtemp@0 {
                    compatible = "nxp,simtemp";
                    status = "okay";
                };
            };
        };
    };
    ```

2.  **Compila e instala el overlay:**
    ```bash
    dtc -@ -I dts -O dtb -o nxp-simtemp.dtbo nxp-simtemp-overlay.dts
    sudo cp nxp-simtemp.dtbo /boot/overlays/
    sudo dtoverlay nxp-simtemp
    ```

### 2. Sysfs Interface (Configuration)

The driver exposes its parameters via files in `/sys/class/misc/nxp_simtemp/`. Use `cat` to read and `echo ... | sudo tee ...` to write.

*   **Check current state (0=STOP, 1=RUN):**
    ```bash
    cat /sys/class/misc/nxp_simtemp/state
    ```
*   **Set Operation Mode ("continuous" or "one-shot"):**
    ```bash
    echo "one-shot" | sudo tee /sys/class/misc/nxp_simtemp/operation_mode
    ```
    o, para volver al modo por defecto:
    ```bash
    echo "continuous" | sudo tee /sys/class/misc/nxp_simtemp/operation_mode
   ```

*   **Start the sampler:**
    ```bash
    echo RUN | sudo tee /sys/class/misc/nxp_simtemp/state
    ```
    **Stop the sampler:**
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
    ```

## Project Documentation

This project includes detailed design and requirements documentation:
-   `docs/MSD_SimTemp.md`: Module Software Design
-   `docs/Requirements/Requirements.md`: Functional Requirements
-   `docs/MTS_SimTemp.md`: Module Test Specification
