# Pseudo Character Driver (cdev)

This directory contains a Linux kernel loadable module implementing a pseudo character device using the standard character device registration model (`cdev`).

---

## 📂 Directory Contents

* **[pseudo_char.c](file:///home/mdabr/diy-lkm/pseudo_character_driver/pseudo_char.c)**: The kernel driver source code implementing character device file operations (`read`, `write`, `llseek`, `open`, `release`).
* **[test_pseudo.c](file:///home/mdabr/diy-lkm/pseudo_character_driver/test_pseudo.c)**: The user-space test harness verifying boundaries, sparse writing, seeking, and multi-byte copy accuracy.
* **[Makefile](file:///home/mdabr/diy-lkm/pseudo_character_driver/Makefile)**: Kernel build orchestration file.
* **[build.sh](file:///home/mdabr/diy-lkm/pseudo_character_driver/build.sh)**: A helper script to invoke compilation targeting the ARM64 platform.
* **[init.test](file:///home/mdabr/diy-lkm/pseudo_character_driver/init.test)**: Reference initial startup script copy showing how to configure QEMU automated test runs.
* **[qemu_run.log](file:///home/mdabr/diy-lkm/pseudo_character_driver/qemu_run.log)**: Captured boot execution log showing successful module load and test suite run.
* **[qemu_testing_guide.md](file:///home/mdabr/diy-lkm/pseudo_character_driver/qemu_testing_guide.md)**: A detailed guide for compilation and emulation setup.

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

### 1. Compile the Kernel Module (`pseudo_char.ko`)
You can use the helper script:
```bash
bash build.sh
```
Or run the `make` build loop command manually pointing to the kernel source directory:
```bash
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- KDIR="/home/mdabr/linux" -C "/home/mdabr/linux" M="$PWD" modules
```

### 2. Compile the Test Program (`test_pseudo`)
Statically compile the test executable to avoid shared library linkage issues inside the minimal `initramfs`:
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
