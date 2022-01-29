# CH341A USB to I2C Linux kernel driver

The driver can be used with **CH341A** USB to UART/I2C/SPI adapter boards to connect I2C devices to a Linux host.

The I2C interface driver was initially derived from [CH341 I2C driver from Tse Lun Bien](https://github.com/allanbian1017/i2c-ch341-usb.git)

## I2C interface limitations

By default, the driver uses the standard I2C bus speed of **100 kbps**. I2C bus speeds of 20 kbps, **400 kbps** and 750 kbps are also available. 

Currently only basic I2C read and write functions (**`I2C_FUNC_I2C`**) are supported natively. However, SMBus protocols are emulated (**`I2C_FUNC_SMBUS_EMUL`**) with the exception of SMBus Block Read (`I2C_FUNC_SMBUS_READ_BLOCK_DATA`) and SMBus Block Process Call (`I2C_FUNC_SMBUS_BLOCK_PROC_CALL`).

The CH341A only supports **7 bit addressing**.

Due of the limited CH341A USB endpoint buffer size of 32 byte that is used for I2C data as well as adapter in-band signaling, the driver supports only I2C messages with a **maximum data size of 26 bytes**.

## Installation of the driver

#### Prerequisites

To compile the driver, you must have installed current **kernel header files**. 

Even though it is not mandatory, it is highly recommended to use **DKMS** (dynamic kernel module support) for the installation of the driver. DKMS allows to manage kernel modules whose sources reside outside the kernel source tree. Such modules are then automatically rebuilt when a new kernel version is installed.

To use DKMS, it has to be installed before, e.g., with command
```
sudo apt-get install dkms
```
on Debian based systems.

#### Installaton

The driver can be compiled with following commands:

```
git clone https://github.com/intx82/i2c-ch341-usb.git
cd i2c-ch341-usb
make
sudo make install
```

If **DKMS** is installed (**recommended**), command `sudo make install` adds the driver to the DKMS tree so that the driver is recompiled automatically when a new kernel version is installed.

In case you have not installed DKMS, command `sudo make install` simply copies the driver after compilation to the kernel modules directory. However, the module will not be loadable anymore and have to be recompiled explicitly when kernel version changes.

If you do not want to install the driver in the kernel directory at all because you only want to load it manually when needed, simply omit the `sudo make install`.

#### Loading

Once the driver is installed, it should be loaded automatically when you connect a device with USB device id `1a86:5512`. If not try to figure out, whether the USB device is detected correctly using command

```
lsusb
```
and try to load it manually with command:
```
insmod i2c-ch341-usb.ko
```

#### Uninstallation

To uninstall the module simply use command
```
make uninstall
```
in the source directory.

#### Conflicts with CH341A USB to SPI Linux kernel driver

Since the CH341A also provides an SPI interface as USB device with same id, you have to unload the driver module with

```
modprobe -r i2c-ch341-usb
```

before you can load the driver module for the SPI interface.

### Configuration of I2C bus speed

The I2C bus speed can be configured using the module parameter `speed`. The following I2C bus speeds are supported.

| Parameter value | I2C bus speed |
| --------------- | ------------- |
| 0  | 20 kbps    |
| 1  | 100 kbps   |
| 2  | 400 kbps   |
| 0  | 750 kbps   |

By default the driver uses an I2C bus speed of 100 kbps (speed=0). It can be changed using the module parameter `speed` either when loading the module, e.g.,
```
sudo modprobe i2c-ch341-usb speed=2
```
or as real `root` during runtime using sysf, e.g.,
```
echo 2 > /sys/module/i2c_ch341_usb/parameters/speed 
```

## Usage from user space

### Using I2C slaves

Once the driver is loaded successfully, it provides a new I2C bus as device, e.g.,

```
/dev/i2c-2
```

according to the naming scheme `/dev/i2c-<bus>` where `<bus>` is the bus number selected automatically by the driver. Standard I/O functions such as `open`, `read`, `write`, `ioctl` and `close` can then be used to communicate with slaves which are connected to this I2C bus.

#### Open the I2C device

To open the I2C bus device for data transfer simply use function `open`, e.g.,
```.c
int i2c_bus = open ("/dev/i2c-2", O_RDWR));
```
Once the device is opened successfully, you can communicate with the slaves connected to the I2C bus.

Function `close` can be used to close the device anytime.

#### Data transfer with function `ioctl`

Before data are transfered using function `ioctl`, a data structure of type `struct i2c_rdwr_ioctl_data` has to be created. This can either contain only a single I2C message of type `struct i2c_msg` or an array of I2C messages of type `struct i2c_msg`, all of which are transfered together as a combined transaction. In latter case each I2C message begins with start condition, but only the last ends with stop condition to indicate the end of the combined transaction.

Each I2C message consists of 

- a slave address, 
- some flags combined into a single value, e.g., read/write flag, 
- a pointer to the buffer for data bytes written to or read from the slave, and
- the length of data in bytes written to or read from the slave.

The following example shows an array of messages with two command messages written to the slave and two data messages to read the results from the slave.
```.c
#define I2C_SLAVE_ADDR   0x18

uint8_t i2c_id_cmd  [] = { 0x0f };  // get ID command 
uint8_t i2c_rd_cmd  [] = { 0xa8 };  // read data command

uint8_t i2c_id_data [1];            // ID is one byte
uint8_t i2c_rd_data [6];            // data are 6 bytes

struct i2c_msg i2c_messages[] = 
{
    {
        .addr  = I2C_SLAVE_ADDR,
        .flags = 0,
        .buf   = i2c_id_cmd,
        .len   = sizeof(i2c_id_cmd)        
    },
    {
        .addr  = I2C_SLAVE_ADDR,
        .flags = I2C_M_RD,
        .buf   = i2c_id_data,
        .len   = sizeof(i2c_id_data)
    },
    {
        .addr  = I2C_SLAVE_ADDR,
        .flags = 0,
        .buf   = i2c_rd_cmd,
        .len   = sizeof(i2c_rd_cmd)
    },
    {
        .addr  = I2C_SLAVE_ADDR,
        .flags = I2C_M_RD,
        .buf   = i2c_rd_data,
        .len   = sizeof(i2c_rd_data)
    }
};
```
These messages can then be transfered to the slave by filling a data structure of type `struct i2c_rdwr_ioctl_data` with them and calling function `ioctl`, e.g., 
```.c
struct i2c_rdwr_ioctl_data ioctl_data = 
{
    .msgs  = i2c_messages,
    .nmsgs = 4
};

if (ioctl(i2c_bus, I2C_RDWR, &ioctl_data) < 0)
{
    perror("ioctl");
    return -1;
}
```

#### Using `read` and `write` for data transfer

Functions `read` and `write` can also be used for data transfer. However, since these functions do not allow to specify the slave address, it has to be set before using function `ioctl`.

```.c
if (ioctl(i2c_bus, I2C_SLAVE_FORCE, I2C_SLAVE_ADDR) < 0)
{
    perror("Could not set i2c slave addr");
    return -1;
}
```

This slave address is then used for all subsequent `read` and `write` function calls until it is changed again with function `ioctl`.

Supposing the data are preparated as in the example with `ioctl`, the transfer of them is quite simple.

```.c
if (write(i2c_bus, i2c_id_cmd, sizeof(i2c_id_cmd)) != sizeof(i2c_id_cmd))
{
    perror("Could not write id command to i2c slave");
    return -1;
}

if (read (i2c_bus, i2c_id_data, sizeof(i2c_id_data)) != sizeof(i2c_id_data))
{
    perror("Could not write read id from i2c slave");
    return -1;
}

if (write(i2c_bus, i2c_rd_cmd, sizeof(i2c_rd_cmd)) != sizeof(i2c_rd_cmd))
{
    perror("Could not write read data command to i2c slave");
    return -1;
}

if (read (i2c_bus, i2c_rd_data, sizeof(i2c_rd_data)) != sizeof(i2c_rd_data))
{
    perror("Could not write read data from i2c slave");
    return -1;
}
```
