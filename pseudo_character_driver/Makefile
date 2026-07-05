obj-m += pseudo_char.o

KDIR ?= /home/mdabr/linux
HOST_KDIR ?= /lib/modules/$(shell uname -r)/build

.PHONY: all host clean help

help:
	@echo "Available targets:"
	@echo "  make all       - Build ARM64 module using local kernel source"
	@echo "  make host      - Build kernel module using host kernel headers"
	@echo "  make clean     - Clean build artifacts"
	@echo "  make help      - Show this help message"

all:
	make -C $(KDIR) ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- M=$(PWD) modules

host:
	@if [ ! -d "$(HOST_KDIR)" ]; then \
		echo "Error: host kernel headers not found at $(HOST_KDIR)"; \
		echo "Install headers or use 'make all' on WSL2."; \
		exit 1; \
	fi
	make -C $(HOST_KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean
