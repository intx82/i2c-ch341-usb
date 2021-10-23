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

$(MODULE_NAME).ko: $(MODULE_NAME).c
	make -C $(KERNEL_DIR) M=$(PWD) modules

all:
	make -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	make -C $(KERNEL_DIR) M=$(PWD) clean
	rm -f examples/gpio_input examples/gpio_output

install: pre-install do-install post-install

uninstall: pre-uninstall do-uninstall post-uninstall

ifeq ($(DKMS),)  # if DKMS is not installed

do-install: $(MODULE_NAME).ko
	cp $(MODULE_NAME).ko $(MODULE_DIR)/kernel/drivers/i2c/busses
	depmod
	
do-uninstall: pre-uninstall
	rm -f $(MODULE_DIR)/kernel/drivers/i2c/busses/$(MODULE_NAME).ko
	depmod

else  # if DKMS is installed

do-install: $(MODULE_NAME).ko
ifneq ($(MODULE_INSTALLED),)
	@echo Module $(MODULE_NAME) is installed ... uninstall it first
	@make uninstall
endif
	@dkms install .
	
do-uninstall:
ifneq ($(MODULE_INSTALLED),)
	dkms remove -m $(MODULE_NAME) -v $(MODULE_VERSION) --all
	rm -rf /usr/src/$(MODULE_NAME)-$(MODULE_VERSION)
endif

pre-install:

post-install:
	modprobe -r ch341
	modprobe i2c-ch341-usb
	echo "blacklist ch341\nalias ch341 off" > /etc/modprobe.d/blacklist-ch341.conf && \
	echo "blacklist spi-ch341-usb\nalias spi-ch341-usb off" >> /etc/modprobe.d/blacklist-ch341.conf

pre-uninstall:
	modprobe -r i2c-ch341-usb

post-uninstall:
	rm -f /etc/modprobe.d/blacklist-ch341.conf

endif  # ifeq ($(DKMS),)

