# Multi-Device Pseudo Character Driver (cdev)

This directory contains a Linux kernel loadable module implementing a multi-device pseudo character driver using the standard character device registration model (`cdev`).

---

An out-of-tree Linux Loadable Kernel Module implementing four independent, in-memory pseudo character devices using the **standard character device API** (`cdev`). A single driver manages all four devices under a single implementation of the standard file operations (`open`, `release`, `read`, `write`, `llseek`).

---

## Features

- **Single Driver, Multi-Device Mapping**: Registers one `struct cdev` managing 4 minor numbers (`[0-3]`) allocated dynamically using `alloc_chrdev_region`.
- **Dynamic Device Detection**: Inspects the inode minor number (`iminor(inode)`) inside the `open` callback to determine which device is being accessed and stores the device state in `file->private_data`.
- **Isolated Device Buffers**: Each device (minor 0 to 3) has its own independent 4 KB memory buffer (`PSEUDO_BUFFER_SIZE`) and logical data size tracking.
- **Granular Synchronization**: Employs a dedicated mutex per device structure, allowing concurrent operations on different devices without blocking.
- **Automatic Device Nodes**: Populates sysfs classes and triggers automated population of `/dev/pseudodev0`, `/dev/pseudodev1`, `/dev/pseudodev2`, and `/dev/pseudodev3`.
- **Full File Ops Support**: Supports standard `read`, `write`, and boundary-checked `llseek` (`SEEK_SET`, `SEEK_CUR`, `SEEK_END`).

> **Note:** Targets modern kernels (6.4+) where `class_create()` takes a single `name` argument. This was verified on kernel **6.12.0**.

---

## 📂 Directory Contents

* **[pseudo_char_multi.c](file:///home/mdabr/diy-lkm/pseudo_char_multiple/pseudo_char_multi.c)**: The kernel driver source code implementing file operations for all four devices.
* **[test_pseudo.c](file:///home/mdabr/diy-lkm/pseudo_char_multiple/test_pseudo.c)**: The user-space test harness verifying independence, seek behavior, and boundary checks.
* **[Makefile](file:///home/mdabr/diy-lkm/pseudo_char_multiple/Makefile)**: Kernel build orchestration file.
* **[build.sh](file:///home/mdabr/diy-lkm/pseudo_char_multiple/build.sh)**: A helper script to compile targets for the ARM64 platform.

---

## 🔍 Code Walkthrough & Architecture

The driver creates four virtual character devices (`/dev/pseudodev0` through `/dev/pseudodev3`).

### 1. Per-Device Structure
```c
struct pseudo_device_data {
    char buffer[PSEUDO_BUFFER_SIZE];  /* Storage for device data */
    size_t data_size;                 /* Current size of valid data written */
    struct mutex mutex;               /* Mutex to serialize access to this device */
    struct device *device;            /* Device pointer for sysfs representation */
    int minor;                        /* Minor number for this device */
};
```
An array `static struct pseudo_device_data devices[MAX_DEVICES]` contains the state for each of the four devices.

### 2. Device Detection via `file->private_data`
When a user-space process calls `open()` on one of the devices:
1. The virtual file system passes the `struct inode` representing the specific `/dev/pseudodevX` node.
2. `pseudo_open()` reads the minor number using `iminor(inode)` to determine the target device index.
3. The pointer `&devices[minor]` is saved to `file->private_data`.
4. Subsequent operations (`read`, `write`, `llseek`, `release`) simply retrieve the pointer:
   ```c
   struct pseudo_device_data *dev_data = file->private_data;
   ```
   This approach isolates the state of each device while sharing a single, clean function implementation.

---

## 🧪 How It Is Tested

The user-space tester [test_pseudo.c](file:///home/mdabr/diy-lkm/pseudo_char_multiple/test_pseudo.c) performs the following assertions:

1. **Device Initialization**: Opens `/dev/pseudodev0` through `/dev/pseudodev3`.
2. **Unique Writes**: Writes a unique message to each device buffer.
3. **Readback & Isolation**: Reads the content from each device and verifies that the read data matches the message written to it, proving that writing to one device does not affect or overwrite the buffers of other devices.
4. **Seek Independence**: Seeks to offset 10 on `/dev/pseudodev0` and asserts that the current offset of `/dev/pseudodev1` remains unaffected.
5. **SEEK_END / Seek Limits**: Verifies that seeking to `SEEK_END` maps directly to the device's written data size.
6. **Boundary Violations**: Asserts that invalid seeks (negative offsets or offsets beyond the 4096-byte hardware limit) correctly fail with `EINVAL`.

---

## 🛠️ How to Compile

Because the testing environment runs on an emulated ARM64 guest system, you must cross-compile the targets.

### The Easy Way (Automated Build Script)
Compile both the kernel module and the user-space test runner together using the provided build script:
```bash
./build.sh
```

### Manual Compilation
Alternatively, compile each component individually:

1. **Compile the Kernel Module (`pseudo_char_multi.ko`)**:
   ```bash
   make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- KDIR="/home/mdabr/linux" -C "/home/mdabr/linux" M="$PWD" modules
   ```

2. **Compile the Test Program (`test_pseudo`)**:
   ```bash
   aarch64-linux-gnu-gcc -static -o test_pseudo test_pseudo.c
   ```

---

## 🖥️ How to Run in QEMU

1. **Copy Binaries**: Stage the compiled kernel module and test executable into the root filesystem directory:
   ```bash
   cp pseudo_char_multi.ko test_pseudo ~/initramfs/
   ```

2. **Configure Init Script**: Integrate automated loader instructions in `~/initramfs/init` before the terminal execution line:
   ```sh
   # Load driver
   insmod /pseudo_char_multi.ko
   
   # Run tests
   if [ -f /test_pseudo ]; then
       chmod +x /test_pseudo
       /test_pseudo
   fi
   
   # Log kernel info and shut down QEMU
   dmesg | tail -n 25
   poweroff -f
   ```

3. **Repack & Boot**: Repack the rootfs and launch QEMU:
   ```bash
   # Repack
   cd ~/initramfs
   find . -print0 | cpio --null -ov --format=newc | gzip -9 > ~/initramfs.cpio.gz

   # Boot QEMU
   qemu-system-aarch64 \
       -cpu cortex-a57 \
       -machine virt \
       -smp 2 \
       -m 1024 \
       -kernel ~/linux/arch/arm64/boot/Image \
       -initrd ~/initramfs.cpio.gz \
       -nographic \
       -append "console=ttyAMA0 root=/dev/ram rdinit=/init"
   ```

---

## 📖 How Linux Routes Calls to Multiple Devices

Under Linux, character devices are routing abstractions represented by unique major and minor number pairs.

### The Role of Major and Minor Numbers
* **Major Number**: Identifies the driver itself. The Virtual File System (VFS) uses the major number to associate file operations (like `read`, `write`, `open`) with the correct driver structure (`pseudo_fops`).
* **Minor Number**: Identifies the specific device instance managed by that driver. The driver uses this number to index into its internal data array.

For example, our driver registers starting at a dynamic major number (e.g. 511) and maps 4 devices:
```text
/dev/pseudodev0 -> major = 511, minor = 0
/dev/pseudodev1 -> major = 511, minor = 1
/dev/pseudodev2 -> major = 511, minor = 2
/dev/pseudodev3 -> major = 511, minor = 3
```

When `/dev/pseudodev2` is opened, VFS determines that major 511 belongs to our driver and routes the request to `pseudo_open()`. Inside the callback, the `struct inode` is queried using `iminor(inode)`, revealing that minor `2` is being accessed. The driver saves `devices[2]` in `file->private_data`, directing all subsequent I/O requests for that file descriptor straight to device 2.
