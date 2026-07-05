obj-m += hello.o

KDIR ?= /home/mdabr/linux
HOST_KDIR ?= /lib/modules/$(shell uname -r)/build 

all:
	make -C $(KDIR) \
	ARCH=arm64 \
	CROSS_COMPILE=aarch64-linux-gnu- \
	M=$(PWD) modules

host:
	make -C $(HOST_KDIR) \
	M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean

help:
	make -C $(KDIR) \
	ARCH=arm64 \
	CROSS_COMPILE=aarch64-linux-gnu- \
	M=$(PWD) help
