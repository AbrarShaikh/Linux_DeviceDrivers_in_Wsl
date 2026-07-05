# Guide: Testing Kernel Modules inside QEMU (ARM64)

This guide documents the detailed step-by-step process of testing the custom kernel module ([pseudo_char.c](file:///home/mdabr/diy-lkm/pseudo_character_driver/pseudo_char.c)) and its user-space companion ([test_pseudo.c](file:///home/mdabr/diy-lkm/pseudo_character_driver/test_pseudo.c)) using an emulated ARM64 Linux system under QEMU.

---

## 🏗️ Step 1: Cross-Compilation

Because the QEMU emulation target is an **ARM64** virtual machine running on an x86_64 host system, both the kernel module and user-space test program must be cross-compiled.

### A. Compile the Kernel Module (`.ko`)
Run the cross-compiler on the driver sources. This requires pointing to the target Linux kernel build directory (`~/linux`):

```bash
cd /home/mdabr/diy-lkm/pseudo_character_driver
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- KDIR="/home/mdabr/linux" -C "/home/mdabr/linux" M="$PWD" modules
```
* **Output:** `pseudo_char.ko` (ELF 64-bit LSB relocatable, ARM aarch64)

### B. Compile the User Space Tester (`test_pseudo`)
Cross-compile the test runner statically. Static linking is preferred to avoid dependencies on shared libraries inside the minimal `initramfs`:

```bash
aarch64-linux-gnu-gcc -static -o test_pseudo test_pseudo.c
```
* **Output:** `test_pseudo` (ELF 64-bit LSB executable, ARM aarch64, statically linked)

---

## 📂 Step 2: Integrate Binaries into Initramfs

The target machine boots a ramdisk (`initramfs`). We must copy the compiled files into the decompressed root filesystem directory:

```bash
cp /home/mdabr/diy-lkm/pseudo_character_driver/pseudo_char.ko /home/mdabr/initramfs/
cp /home/mdabr/diy-lkm/pseudo_character_driver/test_pseudo /home/mdabr/initramfs/
```

---

## 📝 Step 3: Configure Automated Execution in Boot Script

To execute tests immediately on boot and automatically shut down QEMU (preventing it from hanging in the background), edit the initialization script [init](file:///home/mdabr/initramfs/init):

1. Open `/home/mdabr/initramfs/init`.
2. Locate the terminal setup line at the end (`exec setsid cttyhack /bin/sh`).
3. Replace/prepend it with the following test commands:

```sh
# 1. Load the compiled kernel module
insmod /pseudo_char.ko

# 2. Run the user-space test harness
if [ -f /test_pseudo ]; then
    chmod +x /test_pseudo
    /test_pseudo
fi

# 3. Print the trailing kernel messages
dmesg | tail -n 20

# 4. Trigger an immediate kernel powerdown
poweroff -f
```

---

## 📦 Step 4: Repack and Launch QEMU

With the binaries integrated and `/init` configured, package the ramdisk and boot the virtual machine.

### A. Repack the Root Filesystem
Generate the compressed CPIO archive from the `initramfs` workspace:

```bash
cd /home/mdabr/initramfs
find . -print0 | cpio --null -ov --format=newc | gzip -9 > ~/initramfs.cpio.gz
```

### B. Execute QEMU Target
Launch the emulator pointing to the compiled kernel image (`Image`) and the newly repacked `initramfs.cpio.gz`:

```bash
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

## 🧹 Step 5: Clean Up and Restore

To return the workspace to its default interactive shell state:

1. **Restore Init Script:** Replace `/home/mdabr/initramfs/init` with your backup (`init.orig` or manual edit) to remove automated test runs.
2. **Remove Test Binaries:**
   ```bash
   rm -f /home/mdabr/initramfs/pseudo_char.ko /home/mdabr/initramfs/test_pseudo
   ```
3. **Repack the baseline ramdisk** so the system boots normally next time.
