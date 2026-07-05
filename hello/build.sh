#!/bin/bash
# Helper script to build the LKM for ARM64 on x86 host

# Ensure compiler is installed
if ! command -v aarch64-linux-gnu-gcc &> /dev/null; then
    echo "ERROR: aarch64-linux-gnu-gcc not found."
    echo "Please install the toolchain using:"
    echo "  sudo apt update && sudo apt install gcc-aarch64-linux-gnu"
    exit 1
fi

# Define path to the kernel source tree
KDIR="/home/mdabr/linux"

echo "Building LKM for ARM64..."
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- KDIR="$KDIR" -C "$KDIR" M="$PWD" modules

if [ $? -eq 0 ]; then
    echo "Build successful! Created hello.ko (ARM64 format)."
    file hello.ko
else
    echo "Build failed!"
    exit 1
fi
