# Linux Device Deriver Development setup in wsl for ARM machines

```bash
cat << 'EOF' > README.md
# Developing ARM64 Linux Kernel Modules (KLMs) on x86_64 WSL via QEMU

This repository provides an end-to-end guide and workflow template to cross-compile
an ARM64 Linux Kernel, construct a lightweight, statically linked BusyBox `initramfs` root filesystem,
and develop/test dynamic Kernel Loadable Modules (KLMs)—**entirely on an x86_64 Windows host machine using WSL2
and QEMU system emulation.** No physical ARM64 target hardware is required.

---

## Workspace Architecture

```text
Windows (x86_64 Host Machine)
│
└── WSL2 (Ubuntu / Debian Environment)
      │
      ├── Cross-Compiler Toolchain (aarch64-linux-gnu-gcc)
      ├── Downstream Kernel Trees (ARM64 Linux Kernel Source)
      ├── Custom Out-of-Tree Drivers (hello.ko Compilation Workspace)
      │
      └── QEMU System Emulator (qemu-system-aarch64)
              │
              ├── Emulates Virtual ARM64 Environment (Cortex-A57 Virt Platform)
              ├── Boots Cross-Compiled ARM64 Kernel Image
              └── Dynamically Mounts Statically Packed Initramfs RootFS

```

---

## Repository Directory Layout

To maintain structural sanity throughout compilation loops, organize your primary project workspace as follows:

```text
project/
├── linux/                  # Linux kernel repository / uncompressed source tree
│   ├── arch/arm64/boot/    # Contains the generated 'Image' binary post-build
│   ├── vmlinux             # Uncompressed kernel image used for debugging symbols
│   └── ...
├── hello/                  # Custom LKM/KLM driver out-of-tree source space
│   ├── hello.c             # C source logic containing init/exit hooks
│   ├── Makefile            # Module build directives linking back to kernel headers
│   └── hello.ko            # Resulting cross-compiled ARM64 driver runtime object
└── initramfs/              # Staging area for constructing the temporary RootFS
    ├── init                # Early userspace shell initialization process script
    ├── hello.ko            # Placed binary driver ready for in-system execution test
    └── ...                 # Statically mapped BusyBox binaries (/bin, /sbin, etc.)

```

---

## Step-by-Step Implementation Guide

### Step 1: Install System Cross-Development Dependencies

Update your local WSL tracking indices and pull down the cross-compilers, static packaging tools, utilities, and QEMU hardware platform emulators:

```bash
sudo apt update && sudo apt-get update

sudo apt install -y \
    git bc bison flex \
    build-essential \
    libssl-dev \
    libelf-dev \
    dwarves \
    qemu-system-arm \
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu \
    cpio \
    wget \
    unzip

```

Verify your environment installations:

```bash
aarch64-linux-gnu-gcc --version
qemu-system-aarch64 --version

```

---

### Step 2: Download the Linux Kernel Source Code

You can fetch the official source tree either by cloning the stable mirror tracking logs or by fetching a tarball archive release directly from kernel.org.

#### Option A: Lightweight Git Clone (Recommended)

```bash
git clone --depth=1 [https://github.com/torvalds/linux.git](https://github.com/torvalds/linux.git)
cd linux
git fetch --tags
# Check out a target stable branch (e.g., version milestone 6.12)
git checkout tags/v6.12 -b linuxv6.12
git describe --tags

```

#### Option B: Direct XZ Tarball Extraction

```bash
wget [https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.12.tar.xz](https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.12.tar.xz)
tar xf linux-6.12.tar.xz
mv linux-6.12 linux
cd linux

```

---

### Step 3: Configure the Target ARM64 Kernel

Before starting compilation loops, instruct the kernel configuration interface to build layout paths explicitly targeted for **ARM64 architectures** by leveraging standard QEMU virtual configurations.

```bash
export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-

# Generate default template settings for the ARM64 virtual platform
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig

```

---

### Step 4: Refine Kernel Feature Controls & Module Support

To dynamically push driver payloads in and out of terminal runtime loops, ensure dynamic configuration hooks are explicitly verified.

```bash
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- menuconfig

```

Inside the interactive terminal graphics layout menu, navigate to and verify the following adjustments:

* Go to **General setup** $\rightarrow$ Ensure **`[*] Enable loadable module support`** is turned on.
* Under that sub-menu, explicitly verify **`[*] Module unloading`** is checked.
* Optionally, toggle **`[*] Module versioning support`** if managing multiple driver layouts.

*Save and exit out of the configuration wizard layer to update `.config`.*

---

### Step 5: Build the Base ARM64 Kernel Image

Kick off the parallel compilation workload execution. This outputs the core raw bootable runtime image file.

```bash
make -j$(nproc) ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-

```

* **Output Artifacts:**
* High-level raw boot kernel binary: `arch/arm64/boot/Image`
* System symbol image with full debug hooks: `vmlinux`



---

### Step 6: Prepare Internal Cross-Compilation Kernel Headers

Before jumping out-of-tree to write your custom driver code, run the preparation step. This optimizes script tooling links inside your kernel target layout to quickly compile separate external drivers without rebuilding the whole kernel later.

```bash
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- modules_prepare

```

---

### Step 7: Author the Out-of-Tree "Hello World" Driver Workspace

Move out of the core kernel tree to set up your isolated driver workspace directory layout.

```bash
cd ..
mkdir -p hello && cd hello

```

#### 1. Create `hello.c`

```c
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Abrar");
MODULE_DESCRIPTION("Hello ARM64 module");
MODULE_VERSION("1.0");

static int __init hello_init(void)
{
    pr_info("Hello from ARM64 module!\n");
    return 0;
}

static void __exit hello_exit(void)
{
    pr_info("Goodbye ARM64 module!\n");
}

module_init(hello_init);
module_exit(hello_exit);

```

#### 2. Create the Project `Makefile`

```makefile
obj-m += hello.o

# Points relatively to the prepared kernel directory tree built in earlier stages
KDIR ?= ../linux

all:
	make -C $(KDIR) \
	ARCH=arm64 \
	CROSS_COMPILE=aarch64-linux-gnu- \
	M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean

```

---

### Step 8: Build and Verify the Binary Driver Module Object

Compile the individual driver runtime payload:

```bash
make

```

#### Verification Diagnostics

Confirm that the driver was compiled correctly for the **ARM64 target architecture**, rather than matching your native x86 host machine:

```bash
file hello.ko
# Output should clearly state: ELF 64-bit LSB relocatable, ARM aarch64

modinfo hello.ko
# Validates parameters, license strings, and targets author meta signatures ("Abrar")

```

---

### Step 9: Construct the Statically Linked BusyBox Initramfs

To execute the module inside QEMU without a massive virtual disk footprint, build a lightweight, RAM-backed root filesystem from scratch using BusyBox.

#### 1. Download and Apply Compilation Error Fixes

```bash
cd ..
git clone --depth 1 [https://github.com/mirror/busybox.git](https://github.com/mirror/busybox.git)
cd busybox
make defconfig

# Fix 1: Disable architecture-incompatible Hardware SHA acceleration errors
sed -i 's/CONFIG_SHA1_HWACCEL=y/# CONFIG_SHA1_HWACCEL is not set/' .config
sed -i 's/CONFIG_SHA256_HWACCEL=y/# CONFIG_SHA256_HWACCEL is not set/' .config

# Fix 2: Disable deprecated Traffic Control (TC) structure mismatches 
sed -i 's/CONFIG_TC=y/# CONFIG_TC is not set/' .config

# Compile and statically map user spaces binaries
make -j$(nproc) ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- install

```

#### 2. Set Up Filesystem Tree Framework

Create a clean directory layout to act as your target workspace folder structure:

```bash
mkdir -p ~/initramfs/{bin,sbin,etc,proc,sys,usr/bin,usr/sbin,dev}
cp -av ~/busybox/_install/* ~/initramfs/

```

#### 3. Create Root Core Initialization Script

Create the initial user process script that runs immediately upon boot execution. This mounts vital virtual devices and attaches a proper terminal shell context:

```bash
nano ~/initramfs/init

```

Paste the following layout block into the editor:

```sh
#!/bin/sh
# 1. Mount essential pseudo-filesystems
mount -t proc none /proc
mount -t sysfs none /sys

# 2. Mount devtmpfs so the kernel automatically populates device nodes (like ttyAMA0)
mount -t devtmpfs devtmpfs /dev

echo -e "\n=========================================="
echo -e "   Successfully Booted QEMU ARM64 Target   "
echo -e "==========================================\n"

# 3. Use setsid and cttyhack to give the shell a proper controlling terminal
exec setsid cttyhack /bin/sh

```

Make the script executable, stage your target driver, and pack the directory structure into a compressed cpio archive:

```bash
chmod +x ~/initramfs/init

# Stage your compiled hello driver module inside the initramfs workspace
cp ~/hello/hello.ko ~/initramfs/

# Package everything into the compressed target file system archive
cd ~/initramfs
find . -print0 | cpio --null -ov --format=newc | gzip -9 > ~/initramfs.cpio.gz

```

---

### Step 10: Boot the Emulated Target System via QEMU

Run the following execution sequence to boot your virtual system right inside your standard non-graphical terminal session via raw console wrappers:

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

### Step 11: Execute & Verify Dynamic KLM Operations

Once execution sequences complete their standard boot logging lines, you will automatically pass straight into your functional interactive BusyBox guest prompt.

Run these runtime tests to verify your dynamic driver hooks:

```sh
# 1. Dynamically insert the binary driver module object into active kernel spaces
insmod hello.ko

# 2. Extract internal kernel ring logs to verify the initialization console output
dmesg | tail -n 5
# Expect log message output: Hello from ARM64 module!

# 3. Unload the module dynamically from the active memory workspace
rmmod hello

# 4. Check dmesg again to confirm exit hook confirmation printout routines
dmesg | tail -n 5
# Expect log message output: Goodbye ARM64 module!

```

---

## Terminating QEMU System Emulation Sessions

Because QEMU is run with the `-nographic` console parameter flag attached directly to your live shell terminal session, typical commands like `exit` or `logout` will only exit the shell inside the guest system, not the emulator itself.

To safely kill the persistent underlying QEMU emulator instance and return to your native WSL prompt at any point, use the standard system key sequence:

* Tap **`Ctrl + A`** concurrently, release them, then immediately press **`X`**.
EOF

