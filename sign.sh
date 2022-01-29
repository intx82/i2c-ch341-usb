#!/bin/bash

MOK_PATH=/var/lib/shim-signed/mok
if [ -e $MOK_PATH ]; then
    sudo modprobe -r i2c-ch341-usb
    echo "MOK password: "
    read -s KBUILD_SIGN_PIN
    export KBUILD_SIGN_PIN
    export KBUILD_DIR=/usr/src/$(basename linux-headers-$(uname -r | sed '0,/amd64/{s/amd64/common/}'))

    sudo --preserve-env=KBUILD_SIGN_PIN $KBUILD_DIR/scripts/sign-file sha256 $MOK_PATH/MOK.priv $MOK_PATH/MOK.der /lib/modules/$(uname -r)/updates/dkms/i2c-ch341-usb.ko
    sudo modprobe i2c-ch341-usb
    echo "Done! Module loaded"
else
    echo "MOK path not found (read: https://wiki.debian.org/SecureBoot)"
fi
