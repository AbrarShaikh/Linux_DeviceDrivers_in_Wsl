# Pseudo Character Driver (cdev)

This directory contains a Linux kernel loadable module implementing a pseudo character device using the standard character device registration model (`cdev`).

---

An out-of-tree Linux Loadable Kernel Module implementing a simple in-memory
pseudo character device using the **standard character device API** (`cdev`).
The driver backs a `/dev/pseudodev` node with a fixed **4 KB** kernel buffer and
supports `read`, `write`, and `llseek`, with all buffer access serialized by a
mutex.

---

## Features

- Dynamic major number via `alloc_chrdev_region`.
- Char device registration via `cdev_init` / `cdev_add`.
- Automatic `/dev/pseudodev` node creation via `class_create` + `device_create`.
- 4096-byte shared kernel buffer (`PSEUDO_BUFFER_SIZE`).
- `read` / `write` / `llseek` (`SEEK_SET`, `SEEK_CUR`, `SEEK_END`).
- Bounds-checked seeks and offset-based partial writes (`-ENOSPC` at buffer end).
- Concurrency safety via `mutex_lock_interruptible`.

> **Note:** Targets modern kernels (6.4+) where `class_create()` takes a single
> `name` argument. This was verified on kernel **6.12.0**.

---

## 📂 Directory Contents

* **[pseudo_char.c]**: The kernel driver source code implementing character device file operations (`read`, `write`, `llseek`, `open`, `release`).
* **[test_pseudo.c]**: The user-space test harness verifying boundaries, sparse writing, seeking, and multi-byte copy accuracy.
* **[Makefile]**: Kernel build orchestration file.
* **[build.sh]**: A helper script to invoke compilation targeting the ARM64 platform.
* **[init.test]**: Reference initial startup script copy showing how to configure QEMU automated test runs.
* **[qemu_run.log]**: Captured boot execution log showing successful module load and test suite run.
* **[qemu_testing_guide.md]**: A detailed guide for compilation and emulation setup.

---

## 🔍 Code Walkthrough & Architecture

The driver creates a virtual character device (`/dev/pseudodev`) backed by a static memory buffer in kernel space.

### 1. Key Components
* **Memory Buffer (`pseudo_buffer`)**: A 4096-byte array (`PSEUDO_BUFFER_SIZE`) acting as the storage medium for the virtual device.
* **Data Tracker (`pseudo_data_size`)**: Keeps track of the maximum written position in the buffer, defining the logical end-of-file.
* **Synchronization (`pseudo_mutex`)**: A global mutex ensuring that concurrent file operations (reads, writes, seeks) from different processes do not race.

### 2. File Operations
* **`pseudo_read`**:
  * Clamps the read count to the remaining available data size.
  * Uses `copy_to_user` to safely transfer data to the user buffer.
  * Updates the file pointer position (`*ppos`).
* **`pseudo_write`**:
  * Prevents writing past the 4096-byte hardware limit (`-ENOSPC`).
  * Transfers user data to kernel space via `copy_from_user`.
  * Automatically grows `pseudo_data_size` if the write extends past the previous limit.
* **`pseudo_llseek`**:
  * Implements repositioning relative to the start (`SEEK_SET`), current position (`SEEK_CUR`), or end of written data (`SEEK_END`).
  * Enforces the `[0, 4096]` boundary, returning `-EINVAL` if violated.

### 3. Registration Boilerplate
* Uses `alloc_chrdev_region` to obtain a dynamic major number.
* Initializes and registers the device structure using `cdev_init` and `cdev_add`.
* Creates class and device structures (`class_create` & `device_create`) to trigger `udev`/`devtmpfs` to automatically populate `/dev/pseudodev`.

---

## 🧪 How It Is Tested

The user-space program [test_pseudo.c](file:///home/mdabr/diy-lkm/pseudo_character_driver/test_pseudo.c) performs 8 distinct functional checks to assert driver correctness:

1. **Test 1: Basic Write**: Writes a string to the device and asserts that the write size matches the string length.
2. **Test 2: Seek Start**: Seeks the file descriptor back to offset 0.
3. **Test 3: Read & Match**: Reads back the data and compares it using `strcmp` to verify data integrity.
4. **Test 4: Seek past EOF (Sparse)**: Seeks forward to offset 100 (which is beyond the initial 22-byte file size, but within the 4096-byte limit).
5. **Test 5: Sparse Write**: Writes a second string at offset 100.
6. **Test 6: SEEK_END**: Seeks to `SEEK_END` and asserts the file pointer is positioned exactly at the end of the second string (118 bytes).
7. **Test 7: Negative Seek**: Tries to seek to a negative index and asserts that the system call returns `-EINVAL`.
8. **Test 8: Out-of-Bounds Seek**: Tries to seek beyond 4096 bytes and asserts it correctly fails with `-EINVAL`.

---

## 🛠️ How to Compile

Because the testing environment runs on an emulated ARM64 guest system, you must cross-compile the targets.

### The Easy Way (Automated Build Script)
You can compile both the kernel module and the user-space test runner together using the provided build script:
```bash
./build.sh
```

### Manual Compilation
Alternatively, you can compile each component individually:

1. **Compile the Kernel Module (`pseudo_char.ko`)**:
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
   cp pseudo_char.ko test_pseudo ~/initramfs/
   ```

2. **Configure Init Script**: Integrate automated loader instructions in `~/initramfs/init` before the terminal execution line:
   ```sh
   # Load driver
   insmod /pseudo_char.ko
   
   # Run tests
   if [ -f /test_pseudo ]; then
       chmod +x /test_pseudo
       /test_pseudo
   fi
   
   # Log kernel info and shut down QEMU
   dmesg | tail -n 25
   poweroff -f
   ```
   *(For full details, reference the local [init.test](file:///home/mdabr/diy-lkm/pseudo_character_driver/init.test) script)*

3. **Repack & Boot**: Repack the rootfs and launch QEMU:
   ```bash
   # Repack
   cd ~/initramfs
   find . -print0 | cpio --null -ov --format=newc | gzip -9 > ~/initramfs.cpio.gz

   # Boot QEMU
   bash ~/qemu_launch.sh
   ```
4. **Example run** 
   ```sh
   ~ # lnsmod p
   proc/           pseudo_char.ko
   ~ # lnsmod pseudo_char.ko
   /bin/sh: lnsmod: not found
   ~ # insmod pseudo_char.ko
   [   34.017980] pseudo_char: loading out-of-tree module taints kernel.
   [   34.025724] pseudo_init: pseudodev: registered major=511 minor=0
   ~ # dmesg | tail -n 5
   [    0.939986]   with environment:
   [    0.939998]     HOME=/
   [    0.940007]     TERM=linux
   [   34.017980] pseudo_char: loading out-of-tree module taints kernel.
   [   34.025724] pseudo_init: pseudodev: registered major=511 minor=0
   ~ #
   ~ # ls /sys/class/pseudo_class/
   pseudodev
   ~ # ls /sys/class/pseudo_class/pseudodev/
   dev        power      subsystem  uevent
   ~ # cat /sys/class/pseudo_class/pseudodev/dev
   511:0
   ~ # cat /sys/class/pseudo_class/pseudodev/uevent
   MAJOR=511
   MINOR=0
   DEVNAME=pseudodev
   ~ #
   ~ # ls -l dev/pseudodev
   crw-------    1 0        0         511,   0 Jul  5 21:22 dev/pseudodev

   ```

---

## 📖 Why Are Character Devices Treated as Files in Linux?

Character devices (and block devices, pipes, sockets, etc.) are treated as files
because of a core Unix design philosophy: **"everything is a file."**

### The core idea: a uniform interface

Unix exposes almost every resource through the **same small set of system calls**:
`open()`, `read()`, `write()`, `close()`, `ioctl()`, `lseek()`. A character device
like `/dev/pseudodev`, `/dev/ttyS0` (serial port), `/dev/random`, or `/dev/null`
is just another thing you can open and read/write bytes from.

This means a program written to read from a file can read from a keyboard, a
serial port, or our pseudo device **without knowing the difference**:

```c
int fd = open("/dev/pseudodev", O_RDWR);  // our char device
read(fd, buf, n);                          // same call as reading a regular file
```

That uniformity is the whole payoff — tools compose. `cat`, redirection
(`>`, `<`), pipes, and shell scripting all work on devices because devices
*look* like files.

### Why "character" specifically

Devices come in two flavors, distinguished by *how* you access the data:

| Type | Access pattern | Examples |
|------|---------------|----------|
| **Character device** (cdev) | Stream of bytes, sequential, usually unbuffered | terminals, serial ports, `/dev/random`, mice, `/dev/pseudodev` |
| **Block device** | Fixed-size blocks, buffered, random access | disks, SSDs, USB drives |

A character device delivers/accepts data one byte at a time as a stream — which
maps naturally onto the `read`/`write` file interface.

### How it actually works under the hood

The "file" you see in `/dev` is a special inode, not real data on disk:

1. **The device node** (`/dev/pseudodev`) stores two numbers instead of file
   contents: a **major number** (which driver) and a **minor number** (which
   specific device). Notice the leading `c` and the `511, 0` in the listing above:

   ```
   crw-------  1 0  0  511, 0  Jul  5 21:22 dev/pseudodev
   ^                  ^^^^^^^
   c = char device    major=511, minor=0
   ```

2. **The kernel routes calls.** When you `open()` and `read()` that node, the VFS
   (Virtual File System) layer sees it's a char device, looks up the major number,
   and dispatches to the driver's registered `file_operations` (`.read`, `.write`,
   `.open`, `.llseek` — exactly the callbacks implemented in `pseudo_char.c`).

3. **The driver does the real work** — here, copying bytes to/from the 4 KB kernel
   buffer — but presents the file-shaped interface upward.

So the device *is not* a file; the kernel presents a **file-like handle** and the
VFS transparently forwards file operations to this driver instead of to a
filesystem.

### Summary

Character devices are treated as files because Unix deliberately unified all I/O
behind the file abstraction, giving you:

- **One API** for files, devices, pipes, and sockets.
- **Composability** — shell tools, redirection, and pipes work everywhere.
- **Driver flexibility** — hardware (or, here, an in-memory buffer) hides behind
  `read` / `write` / `llseek`.

The device node in `/dev` is a thin naming/routing stub (major + minor numbers);
the VFS layer makes the file illusion real by dispatching to the right driver.