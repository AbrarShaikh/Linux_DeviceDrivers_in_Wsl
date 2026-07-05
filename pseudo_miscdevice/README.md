# Pseudo Miscellaneous Driver (miscdevice)

This directory contains a Linux kernel loadable module implementing a pseudo character device using the lightweight miscellaneous device framework (`miscdevice`).

---

An out-of-tree Linux Loadable Kernel Module implementing a simple in-memory
pseudo character device using the lightweight **miscellaneous device framework**
(`miscdevice`). The driver backs a `/dev/pseudomisc` node with a fixed **4 KB**
kernel buffer and supports `read`, `write`, and `llseek`, with all buffer access
serialized by a mutex.

It is the low-boilerplate counterpart to the
[`pseudo_character_driver`](../pseudo_character_driver/) (`cdev`) driver — the
two expose identical read/write/seek behaviour but register very differently.
See [`cdev_vs_miscdevice_review.md`](../cdev_vs_miscdevice_review.md) for a
side-by-side comparison.

---

## Features

- Single-call registration via `misc_register` / `misc_deregister`.
- Automatic `/dev/pseudomisc` node creation — no `class_create` /
  `device_create` boilerplate.
- Dynamic minor number (`MISC_DYNAMIC_MINOR`) under the shared misc major (10).
- 4096-byte shared kernel buffer (`PSEUDO_BUFFER_SIZE`).
- `read` / `write` / `llseek` (`SEEK_SET`, `SEEK_CUR`, `SEEK_END`).
- Bounds-checked seeks and offset-based partial writes (`-ENOSPC` at buffer end).
- Concurrency safety via `mutex_lock_interruptible`.

### Why `miscdevice`?

| | `cdev` | `miscdevice` |
| --- | --- | --- |
| Major number | Dynamically allocated | Shared global major `10` |
| Node creation | Manual `class_create` + `device_create` | Automatic via `misc_register` |
| Boilerplate | High | Minimal (one struct + one call) |
| Best for | Multi-instance devices | Single-instance / control interfaces |

---

## 📂 Directory Contents

* **[pseudo_misc.c]**: The kernel driver source code implementing file operations registered under the misc subsystem.
* **[test_pseudo_misc.c]**: The user-space test harness verifying dynamic major/minor registration, file operations, and device access under `/dev/pseudomisc`.
* **[Makefile]**: Kernel build orchestration file.
* **[init.test]**: Reference initial startup script copy showing how to configure QEMU automated test runs.
* **[qemu_run.log]**: Captured boot execution log showing successful module load and test suite run.

---

## 🔍 Code Walkthrough & Architecture

The driver creates a virtual character device (`/dev/pseudomisc`) backed by a 4096-byte memory buffer in kernel space.

### 1. Key Components
* **Memory Buffer (`pseudo_buffer`)**: A 4096-byte static array acting as the device storage.
* **Data Tracker (`pseudo_data_size`)**: Tracks the logical end-of-file.
* **Synchronization (`pseudo_mutex`)**: Prevents race conditions during simultaneous accesses.

### 2. Registration Framework (`miscdevice`)
Unlike standard character drivers that require separate region allocation, character device structure creation, class creation, and node population, this driver registers using `misc_register`.
* A single `struct miscdevice` defines the dynamic minor number (`MISC_DYNAMIC_MINOR`), name (`"pseudomisc"`), and file operations (`&pseudo_fops`).
* The system automatically creates `/dev/pseudomisc` node on registration and cleans it up during deregistration (`misc_deregister`).

---

## 🧪 How It Is Tested

The user-space program [test_pseudo_misc.c](file:///home/mdabr/diy-lkm/pseudo_miscdevice/test_pseudo_misc.c) performs functional verification of the misc driver:

1. **Open device**: Opens `/dev/pseudomisc` with read/write access.
2. **Write Payload**: Writes a test message (`"Hello from miscdevice user-space!"`) and validates the write return size.
3. **Seek Start**: Repositions the file descriptor back to offset 0.
4. **Read & Match**: Reads the written string back and uses `strcmp` to assert content equality.
5. **Seek Boundaries**: Tests seeking to offset 100 to verify custom file position tracking.

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

1. **Compile the Kernel Module (`pseudo_misc.ko`)**:
   ```bash
   make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- KDIR="/home/mdabr/linux" -C "/home/mdabr/linux" M="$PWD" modules
   ```

2. **Compile the Test Program (`test_pseudo_misc`)**:
   ```bash
   aarch64-linux-gnu-gcc -static -o test_pseudo_misc test_pseudo_misc.c
   ```

---

## 🖥️ How to Run in QEMU

1. **Copy Binaries**: Stage the compiled kernel module and test executable into the root filesystem directory:
   ```bash
   cp pseudo_misc.ko test_pseudo_misc ~/initramfs/
   ```

2. **Configure Init Script**: Integrate automated loader instructions in `~/initramfs/init` before the terminal execution line:
   ```sh
   # Load driver
   insmod /pseudo_misc.ko
   
   # Run tests
   if [ -f /test_pseudo_misc ]; then
       chmod +x /test_pseudo_misc
       /test_pseudo_misc
   fi
   
   # Log kernel info and shut down QEMU
   dmesg | tail -n 25
   poweroff -f
   ```
   *(For full details, reference the local [init.test](file:///home/mdabr/diy-lkm/pseudo_miscdevice/init.test) script)*

3. **Repack & Boot**: Repack the rootfs and launch QEMU:
   ```bash
   # Repack
   cd ~/initramfs
   find . -print0 | cpio --null -ov --format=newc | gzip -9 > ~/initramfs.cpio.gz

   # Boot QEMU
   bash ~/qemu_launch.sh
   ```
