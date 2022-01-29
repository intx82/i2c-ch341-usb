/*
 * Driver for the CH341 USB to I2C adapter
 *
 * Copyright (c) 2017 Ruban Danil (intx82@gmail.com)
 *
 * Derived from
 *
 *  i2c-ch341-usb.c Copyright (c) 2017 Gunar Schorcht
 *  i2c-ch341-usb.c Copyright (c) 2016 Tse Lun Bien
 *  i2c-ch341.c     Copyright (c) 2014 Marco Gittler
 *  i2c-tiny-usb.c  Copyright (c) 2006-2007 Till Harbaum (Till@Harbaum.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2.
 */

// uncomment following line to activate kernel debug handling
// #define DEBUG
#define DEBUG_PRINTK

#ifdef DEBUG_PRINTK
#define PRINTK(fmt, ...) printk("%s: " fmt "\n", __func__, ##__VA_ARGS__)
#else
#define PRINTK(fmt, ...)
#endif

#define CH341_IF_ADDR (&(ch341_dev->usb_if->dev))
#define DEV_ERR(d, f, ...) dev_err(d, "%s: " f "\n", __FUNCTION__, ##__VA_ARGS__)
#define DEV_DBG(d, f, ...) dev_dbg(d, "%s: " f "\n", __FUNCTION__, ##__VA_ARGS__)
#define DEV_INFO(d, f, ...) dev_info(d, "%s: " f "\n", __FUNCTION__, ##__VA_ARGS__)

// check for condition and return with or without err code if it fails
#define CHECK_PARAM_RET(cond, err)                                                                                     \
	if (!(cond))                                                                                                   \
		return err;
#define CHECK_PARAM(cond)                                                                                              \
	if (!(cond))                                                                                                   \
		return;

#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 0)
#error The driver requires at least kernel version 3.4
#else

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/i2c.h>

#define CH341_USB_MAX_BULK_SIZE 32 // CH341A wMaxPacketSize for ep_02 and ep_82
#define CH341_USB_CHUNK_SIZE 24 // Maximum transfered size in one operation

#define CH341_I2C_LOW_SPEED 0 // low speed - 20kHz
#define CH341_I2C_STANDARD_SPEED 1 // standard speed - 100kHz
#define CH341_I2C_FAST_SPEED 2 // fast speed - 400kHz
#define CH341_I2C_HIGH_SPEED 3 // high speed - 750kHz

#define CH341_CMD_I2C_STREAM 0xAA // I2C stream command
#define CH341_CMD_UIO_STREAM 0xAB // UIO stream command

#define CH341_CMD_I2C_STM_STA 0x74 // I2C set start condition
#define CH341_CMD_I2C_STM_STO 0x75 // I2C set stop condition
#define CH341_CMD_I2C_STM_OUT 0x80 // I2C output data
#define CH341_CMD_I2C_STM_IN 0xC0 // I2C input data
#define CH341_CMD_I2C_STM_SET 0x60 // I2C set configuration
#define CH341_CMD_I2C_STM_END 0x00 // I2C end command

#define CH341_OK 0

// device specific structure
struct ch341_device {
	// USB device description
	struct usb_device *usb_dev; // usb device
	struct usb_interface *usb_if; // usb interface

	struct usb_endpoint_descriptor *ep_in; // usb endpoint bulk in
	struct usb_endpoint_descriptor *ep_out; // usb endpoint bulk out

	uint8_t in_buf[CH341_USB_MAX_BULK_SIZE]; // usb input buffer
	uint8_t out_buf[CH341_USB_MAX_BULK_SIZE]; // usb outpu buffer

	// I2C device description
	struct i2c_adapter i2c_dev; // i2c related things
};

// ----- variables configurable during runtime ---------------------------

static uint speed = CH341_I2C_STANDARD_SPEED; // module parameter speed, default standard speed
static uint speed_last = CH341_I2C_FAST_SPEED + 1; // last used speed, default invalid

// ----- function prototypes ---------------------------------------------

static int ch341_usb_transfer(struct ch341_device *dev, int out_len, int in_len);

// ----- i2c layer begin -------------------------------------------------

static struct mutex ch341_lock;

static int ch341_i2c_set_speed(struct ch341_device *ch341_dev)
{
	static char *ch341_i2c_speed_desc[] = { "20 kbps", "100 kbps", "400 kbps", "750 kbps" };
	int result;

	CHECK_PARAM_RET(speed != speed_last, CH341_OK)

	if (speed < CH341_I2C_LOW_SPEED || speed > CH341_I2C_HIGH_SPEED) {
		DEV_ERR(CH341_IF_ADDR, "parameter speed can only have values from 0 to 3");
		speed = speed_last;
		return -EINVAL;
	}

	DEV_INFO(CH341_IF_ADDR, "Change i2c bus speed to %s", ch341_i2c_speed_desc[speed]);
	mutex_lock(&ch341_lock);

	ch341_dev->out_buf[0] = CH341_CMD_I2C_STREAM;
	ch341_dev->out_buf[1] = CH341_CMD_I2C_STM_SET | speed;
	ch341_dev->out_buf[2] = CH341_CMD_I2C_STM_END;
	result = ch341_usb_transfer(ch341_dev, 3, 0);

	mutex_unlock(&ch341_lock);

	if (result < 0) {
		DEV_ERR(CH341_IF_ADDR, "failure setting speed %d\n", result);
		return result;
	}

	speed_last = speed;

	return result;
}

static int ch341_i2c_read(struct ch341_device *ch341_dev, struct i2c_msg *msg)
{
	int result = 0;
	uint8_t *ob = ch341_dev->out_buf;
	uint8_t *ib = ch341_dev->in_buf;

	size_t ptr = 0;
	size_t msg_len = msg->len;
	DEV_INFO(CH341_IF_ADDR, "msg: [Read len: %d]", (int)msg_len);

	while (ptr < msg_len && ptr < 65535) {
		size_t chunk = (msg_len - ptr) > CH341_USB_CHUNK_SIZE ? CH341_USB_CHUNK_SIZE : (msg_len - ptr);
		size_t k = 0;

		ob[k++] = CH341_CMD_I2C_STREAM;
		ob[k++] = CH341_CMD_I2C_STM_STA; // START condition
		ob[k++] = CH341_CMD_I2C_STM_OUT | 0x1; // write len (only address byte)
		ob[k++] = (msg->addr << 1) | 0x1; // address byte with read flag

		if (chunk > 0) {
			for (size_t j = chunk - 1; j > 0; j--) {
				ob[k++] = CH341_CMD_I2C_STM_IN | 1;
			}

			ob[k++] = CH341_CMD_I2C_STM_IN;
		}

		ob[k++] = CH341_CMD_I2C_STM_STO;
		ob[k++] = CH341_CMD_I2C_STM_END;

		// wirte address byte and read data
		result = ch341_usb_transfer(ch341_dev, k, chunk);

		DEV_DBG(CH341_IF_ADDR, "msg: [Read ret: %d, out: %d, in: %d]", result, (int)k, (int)chunk);
		// if data were read
		if (result > 0) {
			if (msg->flags & I2C_M_RECV_LEN) {
				msg->buf[0] = result; // length byte
				memcpy(msg->buf + 1 + ptr, ib, chunk);
			} else {
				memcpy(msg->buf + ptr, ib, chunk);
			}
		} else {
			DEV_ERR(CH341_IF_ADDR, "msg: [Read err: %d]", result);
			if (result < 0) {
				break;
			}
		}

		if (chunk == 0) {
			break;
		}

		ptr += chunk;
	}

	return result;
}

static int ch341_i2c_write(struct ch341_device *ch341_dev, struct i2c_msg *msg)
{
	int result = 0;
	size_t ptr = 0;
	uint8_t *ob = ch341_dev->out_buf;
	size_t msg_len = msg->len;
	DEV_INFO(CH341_IF_ADDR, "msg: [Write len: %d]", (int)msg_len);

	while (ptr < msg_len && ptr < 65535) {
		size_t chunk = (msg_len - ptr) > CH341_USB_CHUNK_SIZE ? CH341_USB_CHUNK_SIZE : (msg_len - ptr);
		int k = 0;
		ob[k++] = CH341_CMD_I2C_STREAM;
		ob[k++] = CH341_CMD_I2C_STM_STA; // START condition
		ob[k++] = CH341_CMD_I2C_STM_OUT | (chunk + 1);
		ob[k++] = msg->addr << 1; // address byte

		memcpy(&ob[k], msg->buf, chunk);
		k += chunk;

		ob[k++] = CH341_CMD_I2C_STM_STO;
		ob[k++] = CH341_CMD_I2C_STM_END;

		// write address byte and data
		result = ch341_usb_transfer(ch341_dev, k, 0);
		if (result < 0) {
			DEV_ERR(CH341_IF_ADDR, "msg: [Write err: %d]", result);
			break;
		}

		ptr += chunk;
	}

	return result;
}

static int ch341_i2c_transfer(struct i2c_adapter *adpt, struct i2c_msg *msgs, int num)
{
	struct ch341_device *ch341_dev;
	int result = 0;

	CHECK_PARAM_RET(adpt, EIO);
	CHECK_PARAM_RET(msgs, EIO);
	CHECK_PARAM_RET(num > 0, EIO);

	ch341_dev = (struct ch341_device *)adpt->algo_data;

	CHECK_PARAM_RET(ch341_dev, EIO);

	mutex_lock(&ch341_lock);

	for (int i = 0; i < num; i++) {
		if (msgs[i].flags & I2C_M_TEN) {
			DEV_ERR(CH341_IF_ADDR, "10 bit i2c addresses not supported");
			result = -EINVAL;
			break;
		}

		if (msgs[i].flags & I2C_M_RD) {
			result = ch341_i2c_read(ch341_dev, &msgs[i]);
		} else {
			result = ch341_i2c_write(ch341_dev, &msgs[i]);
		}
	}

	mutex_unlock(&ch341_lock);

	if (result < 0) {
		return result;
	}

	return num;
}

static u32 ch341_i2c_func(struct i2c_adapter *dev)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm ch341_i2c_algorithm = {
	.master_xfer = ch341_i2c_transfer,
	.functionality = ch341_i2c_func,
};

static int ch341_i2c_probe(struct ch341_device *ch341_dev)
{
	int result;

	CHECK_PARAM_RET(ch341_dev, -EINVAL);

	DEV_DBG(CH341_IF_ADDR, "start");

	// setup i2c adapter description
	ch341_dev->i2c_dev.owner = THIS_MODULE;
	ch341_dev->i2c_dev.class = 0; // I2C_CLASS_HWMON;
	ch341_dev->i2c_dev.algo = &ch341_i2c_algorithm;
	ch341_dev->i2c_dev.algo_data = ch341_dev;
	snprintf(ch341_dev->i2c_dev.name, sizeof(ch341_dev->i2c_dev.name), "i2c-ch341-usb at bus %03d device %03d",
		 ch341_dev->usb_dev->bus->busnum, ch341_dev->usb_dev->devnum);

	ch341_dev->i2c_dev.dev.parent = &ch341_dev->usb_if->dev;

	// finally attach to i2c layer
	if ((result = i2c_add_adapter(&ch341_dev->i2c_dev)) < 0) {
		return result;
	}

	DEV_INFO(CH341_IF_ADDR, "created i2c device /dev/i2c-%d", ch341_dev->i2c_dev.nr);

	mutex_init (&ch341_lock);

	// set ch341 i2c speed
	speed_last = CH341_I2C_FAST_SPEED + 1;
	if ((result = ch341_i2c_set_speed(ch341_dev)) < 0) {
		return result;
	}

	DEV_DBG(CH341_IF_ADDR, "done");

	return CH341_OK;
}

static void ch341_i2c_remove(struct ch341_device *ch341_dev)
{
	CHECK_PARAM(ch341_dev);

	if (ch341_dev->i2c_dev.nr)
		i2c_del_adapter(&ch341_dev->i2c_dev);

	return;
}

// ----- i2c layer end ---------------------------------------------------

// ----- usb layer begin -------------------------------------------------

static const struct usb_device_id ch341_usb_table[] = { { USB_DEVICE(0x1a86, 0x5512) }, {} };

MODULE_DEVICE_TABLE(usb, ch341_usb_table);

static int ch341_usb_transfer(struct ch341_device *ch341_dev, int out_len, int in_len)
{
	int retval;
	int actual;

	// DEV_INFO(CH341_IF_ADDR, "bulk_out %d bytes, bulk_in %d bytes", out_len, (in_len == 0) ? 0 : CH341_USB_MAX_BULK_SIZE);

	retval = usb_bulk_msg(ch341_dev->usb_dev,
			      usb_sndbulkpipe(ch341_dev->usb_dev, usb_endpoint_num(ch341_dev->ep_out)),
			      ch341_dev->out_buf, out_len, &actual, 2000);
	if (retval < 0)
		return retval;

	if (in_len == 0)
		return actual;

	memset(ch341_dev->in_buf, 0, sizeof(ch341_dev->in_buf));
	retval = usb_bulk_msg(ch341_dev->usb_dev,
			      usb_rcvbulkpipe(ch341_dev->usb_dev, usb_endpoint_num(ch341_dev->ep_in)),
			      ch341_dev->in_buf, CH341_USB_MAX_BULK_SIZE, &actual, 2000);

	if (retval < 0)
		return retval;

	return actual;
}

static void ch341_usb_free_device(struct ch341_device *ch341_dev)
{
	CHECK_PARAM(ch341_dev)

	ch341_i2c_remove(ch341_dev);

	usb_set_intfdata(ch341_dev->usb_if, NULL);
	usb_put_dev(ch341_dev->usb_dev);

	kfree(ch341_dev);
}

static int ch341_usb_probe(struct usb_interface *usb_if, const struct usb_device_id *usb_id)
{
	struct usb_device *usb_dev = usb_get_dev(interface_to_usbdev(usb_if));
	struct usb_endpoint_descriptor *epd;
	struct usb_host_interface *settings;
	struct ch341_device *ch341_dev;
	int i;
	int error;

	DEV_DBG(&usb_if->dev, "connect device");

	// create and initialize a new device data structure
	if (!(ch341_dev = kzalloc(sizeof(struct ch341_device), GFP_KERNEL))) {
		DEV_ERR(&usb_if->dev, "could not allocate device memor");
		usb_put_dev(ch341_dev->usb_dev);
		return -ENOMEM;
	}

	// save USB device data
	ch341_dev->usb_dev = usb_dev;
	ch341_dev->usb_if = usb_if;

	// find endpoints
	settings = usb_if->cur_altsetting;
	DEV_DBG(CH341_IF_ADDR, "bNumEndpoints=%d", settings->desc.bNumEndpoints);

	for (i = 0; i < settings->desc.bNumEndpoints; i++) {
		epd = &settings->endpoint[i].desc;

		DEV_DBG(CH341_IF_ADDR, "  endpoint=%d type=%d dir=%d addr=%0x", i, usb_endpoint_type(epd),
			usb_endpoint_dir_in(epd), usb_endpoint_num(epd));

		if (usb_endpoint_is_bulk_in(epd)) {
			ch341_dev->ep_in = epd;
		} else if (usb_endpoint_is_bulk_out(epd)) {
			ch341_dev->ep_out = epd;
		}
	}

	// save the pointer to the new ch341_device in USB interface device data
	usb_set_intfdata(usb_if, ch341_dev);
	error = ch341_i2c_probe(ch341_dev);

	if (error != CH341_OK) // initialize I2C adapter
	{
		ch341_usb_free_device(ch341_dev);
		return error;
	}

	DEV_INFO(CH341_IF_ADDR, "connected");

	return CH341_OK;
}

static void ch341_usb_disconnect(struct usb_interface *usb_if)
{
	struct ch341_device *ch341_dev = usb_get_intfdata(usb_if);

	DEV_INFO(CH341_IF_ADDR, "disconnected");

	ch341_usb_free_device(ch341_dev);
}

static struct usb_driver ch341_usb_driver = { .name = "i2c-ch341-usb",
					      .id_table = ch341_usb_table,
					      .probe = ch341_usb_probe,
					      .disconnect = ch341_usb_disconnect };

module_usb_driver(ch341_usb_driver);

// ----- usb layer end ---------------------------------------------------

MODULE_ALIAS("i2c:ch341");
MODULE_AUTHOR("Gunar Schorcht <gunar@schorcht.net>");
MODULE_DESCRIPTION("i2c-ch341-usb driver v1.0.0");
MODULE_LICENSE("GPL");

module_param(speed, uint, 0644);
MODULE_PARM_DESC(speed, " I2C bus speed: 0 (20 kbps), 1 (100 kbps), 2 (400 kbps), 3 (750 kbps): ");

#endif // LINUX_VERSION_CODE < KERNEL_VERSION(3,4,0)
