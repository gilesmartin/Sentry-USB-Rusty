# SPDX-License-Identifier: GPL-2.0+
# Out-of-tree build for usb_f_teslamic.ko.
# Uses vendored copies of the GPL u_audio interface headers from Linux v6.18.

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

obj-m += usb_f_teslamic.o
ccflags-y += -I$(src)

.PHONY: all modules clean test static-test
all: modules
modules:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -rf __pycache__ tests/__pycache__ .pytest_cache
static-test test:
	python3 tests/test_descriptors.py
