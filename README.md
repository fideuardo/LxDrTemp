# LxDrTemp
Linux Driver Temperature Demo

## Development Environment

### Prerequisites

- Docker (Pending)

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









