#!/bin/bash
set -e

KDIR="/home/mdabr/linux"

if ! command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    echo "ERROR: aarch64-linux-gnu-gcc not found."
    echo "Install the toolchain with: sudo apt update && sudo apt install gcc-aarch64-linux-gnu"
    exit 1
fi

if [ ! -d "$KDIR" ]; then
    echo "ERROR: Kernel source directory not found: $KDIR"
    exit 1
fi

echo "Building multi-device pseudo character driver for ARM64..."
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- KDIR="$KDIR" -C "$KDIR" M="$PWD" modules

echo "Building user-space test runner for ARM64..."
aarch64-linux-gnu-gcc -static -o test_pseudo test_pseudo.c

echo "Build complete: pseudo_char_multi.ko and test_pseudo"
file pseudo_char_multi.ko test_pseudo
