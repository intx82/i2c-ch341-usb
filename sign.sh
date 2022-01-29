#!/bin/bash
read KBUILD_SIGN_PIN
export KBUILD_SIGN_PIN
export KBUILD_DIR=/usr/src/linux-headers-5.10.0-10-common
sudo --preserve-env=KBUILD_SIGN_PIN $KBUILD_DIR/scripts/sign-file sha256 /var/lib/shim-signed/mok/MOK.priv /var/lib/shim-signed/mok/MOK.der /lib/modules/5.10.0-10-amd64/updates/dkms/i2c-ch341-usb.ko
