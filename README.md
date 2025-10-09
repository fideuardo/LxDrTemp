# LxDrTemp: Linux Driver Temperature Demo
Author: Fidel Cabañas (fideuardo@gmail.com)

## Development Environment

### Windows (WSL)

* For install WSL refet to: [Install WSL](https://learn.microsoft.com/es-es/windows/wsl/install)

* Install WSL2-Linux-Kernel

    Install the kernel headers in the path: */lib/modules/$(shell uname -r)/*



### Linux (ubuntu 24.04)

* install kernel header
```bash
sudo apt install linux-headers-$(uname -r)
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









