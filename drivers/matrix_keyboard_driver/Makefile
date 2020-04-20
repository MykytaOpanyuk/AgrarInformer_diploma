ifneq ($(KERNELRELEASE),)

# kbuild part of makefile

CFLAGS_matrix_keypad.o := -DDEBUG
obj-m := matrix_keypad_v1.o matrix_keypad_v2.o

else

# normal makefile

KDIR ?= /lib/modules/$(shell uname -r)/build

module:
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILING) -C $(KDIR) M=$(PWD) C=1 modules

clean:
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILING) -C $(KDIR) M=$(PWD) C=1 clean

.PHONY: module clean

endif

