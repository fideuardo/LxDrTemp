# LxDrTemp: Linux Driver Temperature Demo
Author: Fidel Cabañas (fideuardo@gmail.com)

## Overview

`simtemp` is a Linux kernel module that simulates a temperature sensor. It serves as a demonstration of modern Linux driver development practices, including:

*   **Platform Driver Model:** Binds to a device defined in the Device Tree.
*   **Misc Device:** Exposes a simple character device interface (`/dev/simtemp`).
*   **HRTimer:** Used for high-resolution, periodic sampling.
*   **Sysfs:** Provides a runtime configuration interface for parameters like sampling period and operation mode.
*   **IOCTL:** Offers an alternative interface for application control.
*   **Poll/Epoll:** Supports asynchronous I/O for efficient data consumption.

## Features

*   **Two Operation Modes:**
    *   `continuous`: Generates samples periodically.
    *   `one-shot`: Generates a single sample and stops.
*   **Configurable Sampling Period:** The sampling rate can be adjusted at runtime via Sysfs.
*   **Device Tree Support:** Default parameters (period, mode) can be set via a Device Tree overlay.
*   **Efficient Data Buffering:** Uses a ring buffer to store samples, preventing data loss between the kernel and user space.

## Building the Driver

### Prerequisites

Before building, ensure you have the necessary tools and kernel headers installed.

**On Debian/Ubuntu/Raspberry Pi OS:**
```bash
sudo apt update
sudo apt install build-essential linux-headers-$(uname -r) device-tree-compiler
```
* Install buid essential

```bash
sudo apt install build-essential
```
* Install addtional packages
```bash
sudo apt install dkms libelf-dev
```
### Raspberry
Pending
### Installation

## Test

* **Build the driver source code:**
```bash
make
```

* **Clean the build artifacts:**
```bash
make clean
```

* **Load the driver into the kernel:**
```bash
sudo insmod simtemp.ko
```

* **Unload the driver from the kernel:**
```bash
sudo rmmod simtemp
```

* **Check that the device was created and show its permissions:**
```bash
ls -l /dev/simtemp
```

* **Read one block of data from the device and save it to `sample.bin`:**
```bash
dd if=/dev/simtemp of=sample.bin bs=32 count=1
```

* **Display the content of the read data in hexadecimal format:**
```bash
hexdump -C sample.bin
```

* **Demonstrate the "one-shot" behavior. This command won't read anything new on a second run:**
```bash
cat /dev/simtemp > /dev/null 
```
