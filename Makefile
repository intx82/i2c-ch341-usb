MODULE_NAME=i2c-ch341-usb
MODULE_VERSION=1.0.0

DKMS       := $(shell which dkms)
PWD        := $(shell pwd) 
KVERSION   := $(shell uname -r)
KERNEL_DIR  = /lib/modules/$(KVERSION)/build
MODULE_DIR  = /lib/modules/$(KVERSION)

ifneq ($(DKMS),)
MODULE_INSTALLED := $(shell dkms status $(MODULE_NAME))
else
MODULE_INSTALLED =
endif

MODULE_NAME  = i2c-ch341-usb
obj-m       := $(MODULE_NAME).o   
ccflags-y   := -std=gnu11 -Wno-declaration-after-statement

$(MODULE_NAME).ko: $(MODULE_NAME).c
	make -C $(KERNEL_DIR) M=$(PWD) modules

all:
	make -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	make -C $(KERNEL_DIR) M=$(PWD) clean

ifeq ($(DKMS),)  # if DKMS is not installed

install: $(MODULE_NAME).ko
	cp $(MODULE_NAME).ko $(MODULE_DIR)/kernel/drivers/i2c/busses
	depmod
	modprobe -r $(MODULE_NAME) || true
	modprobe $(MODULE_NAME)
	
uninstall:
	rm -f $(MODULE_DIR)/kernel/drivers/i2c/busses/$(MODULE_NAME).ko
	depmod

else  # if DKMS is installed

install: $(MODULE_NAME).ko
ifneq ($(MODULE_INSTALLED),)
	@echo Module $(MODULE_NAME) is installed ... uninstall it first
	@make uninstall
endif
	@dkms install .
	modprobe -r $(MODULE_NAME) || true
	modprobe $(MODULE_NAME)
	
uninstall:
ifneq ($(MODULE_INSTALLED),)
	dkms remove -m $(MODULE_NAME) -v $(MODULE_VERSION) --all
	rm -rf /usr/src/$(MODULE_NAME)-$(MODULE_VERSION)
endif

endif  # ifeq ($(DKMS),)

