# SPDX-License-Identifier: GPL-2.0
obj-m := cubesat_watchdog/ \
         specialized_interfaces/cubesat_i2c/ \
         specialized_interfaces/cubesat_iwire/ \
         specialized_interfaces/cubesat_spi/ \
         specialized_interfaces/cubesat_uart/

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean

install:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules_install