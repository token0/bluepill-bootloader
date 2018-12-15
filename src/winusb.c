/*
 * Copyright (c) 2016, Devan Lai
 *
 * Permission to use, copy, modify, and/or distribute this software
 * for any purpose with or without fee is hereby granted, provided
 * that the above copyright notice and this permission notice
 * appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <libopencm3/usb/usbd.h>
#include <logger.h>
#include "usb_conf.h"
#include "winusb.h"

#define CONTROL_CALLBACK_TYPE USB_REQ_TYPE_VENDOR
#define CONTROL_CALLBACK_MASK USB_REQ_TYPE_TYPE
#define DESCRIPTOR_CALLBACK_TYPE USB_REQ_TYPE_DEVICE
#define DESCRIPTOR_CALLBACK_MASK USB_REQ_TYPE_RECIPIENT

#define MIN(a, b) ({ typeof(a) _a = (a); typeof(b) _b = (b); _a < _b ? _a : _b; })

static int usb_descriptor_type(uint16_t wValue) {
	return wValue >> 8;
}

static int usb_descriptor_index(uint16_t wValue) {
	return wValue & 0xFF;
}

static struct winusb_compatible_id_descriptor winusb_wcid = {
	.header = {
		.dwLength = sizeof(struct winusb_compatible_id_descriptor_header) +
					1 * sizeof(struct winusb_compatible_id_function_section),
		.bcdVersion = WINUSB_BCD_VERSION,
		.wIndex = WINUSB_REQ_GET_COMPATIBLE_ID_FEATURE_DESCRIPTOR,
		.bNumSections = 1,
		.reserved = { 0, 0, 0, 0, 0, 0, 0 },
	},
	.functions = {
		{
			// note - bInterfaceNumber is rewritten in winusb_setup with the correct interface number
			.bInterfaceNumber = 0,
			.reserved0 = { 1 },
			.compatibleId = "WINUSB",
			.subCompatibleId = "",
			.reserved1 = { 0, 0, 0, 0, 0, 0}
		},
	}
};

static const struct usb_string_descriptor winusb_string_descriptor = {
	.bLength = 0x12,
	.bDescriptorType = USB_DT_STRING,
	.wData = WINUSB_EXTRA_STRING
};

static const struct usb_string_descriptor empty_string_descriptor = {
	//  Empty string
	.bLength = 0x02,  //  Number of chars * 2 (unicode) + 2 (length, desc)
	.bDescriptorType = USB_DT_STRING,
	.wData = { }
};

static const struct winusb_extended_properties_descriptor guid = {
	.header = {
		.dwLength = sizeof(struct winusb_extended_properties_descriptor_header)
					+ 1 * sizeof (struct winusb_extended_properties_feature_descriptor),
		.bcdVersion = WINUSB_BCD_VERSION,
		.wIndex = WINUSB_REQ_GET_EXTENDED_PROPERTIES_OS_FEATURE_DESCRIPTOR,
		.wNumFeatures = 1,
	},
	.features = {
		{
			.dwLength = sizeof(struct winusb_extended_properties_feature_descriptor),
			.dwPropertyDataType = WINUSB_EXTENDED_PROPERTIES_MULTISZ_DATA_TYPE,
			.wNameLength = WINUSB_EXTENDED_PROPERTIES_GUID_NAME_SIZE_C,
			.name = WINUSB_EXTENDED_PROPERTIES_GUID_NAME,
			.dwPropertyDataLength = WINUSB_EXTENDED_PROPERTIES_GUID_DATA_SIZE_C,
			.propertyData = WINUSB_EXTENDED_PROPERTIES_GUID_DATA,
		},
	}
};

static int winusb_descriptor_request(usbd_device *usbd_dev,
					struct usb_setup_data *req,
					uint8_t **buf, uint16_t *len,
					usbd_control_complete_callback* complete) {
	(void)complete;
	(void)usbd_dev;
	if ((req->bmRequestType & USB_REQ_TYPE_TYPE) != USB_REQ_TYPE_STANDARD) {
		return USBD_REQ_NEXT_CALLBACK;
	}
	if (req->bRequest == USB_REQ_GET_DESCRIPTOR && usb_descriptor_type(req->wValue) == USB_DT_STRING) {
		if (usb_descriptor_index(req->wValue) == WINUSB_EXTRA_STRING_INDEX) {
			dump_usb_request("windes", req); ////
			*buf = (uint8_t*) &winusb_string_descriptor;
			*len = MIN(*len, winusb_string_descriptor.bLength);
			return USBD_REQ_HANDLED;
		}
		else if (usb_descriptor_index(req->wValue) == 0 && *len == 2) {
			//  Windows will request descriptor for Language ID at index 0 with length 2, 
			//  which causes libopencm3 to return a corrupted response.  We fix that here.
			dump_usb_request("windes", req); debug_print_int(*len); debug_println(""); ////
			*buf = (uint8_t*) &empty_string_descriptor;  //  Return the empty string.
			*len = MIN(*len, empty_string_descriptor.bLength);
			return USBD_REQ_HANDLED;
		}
	}
	return USBD_REQ_NEXT_CALLBACK;
}

static int winusb_control_vendor_request(usbd_device *usbd_dev,
					struct usb_setup_data *req,
					uint8_t **buf, uint16_t *len,
					usbd_control_complete_callback* complete) {
	(void)complete;
	(void)usbd_dev;
	if (req->bRequest != WINUSB_MS_VENDOR_CODE) { return USBD_REQ_NEXT_CALLBACK; }
	//  Skip requests meant for CDC.
	if (req->bmRequestType == 0xc0 || req->bmRequestType == 0xc1) { return USBD_REQ_NEXT_CALLBACK; }

	int status = USBD_REQ_NOTSUPP;
	if (((req->bmRequestType & USB_REQ_TYPE_RECIPIENT) == USB_REQ_TYPE_DEVICE) &&
		(req->wIndex == WINUSB_REQ_GET_COMPATIBLE_ID_FEATURE_DESCRIPTOR)) {
		dump_usb_request("winctl", req); ////

		*buf = (uint8_t*)(&winusb_wcid);
		*len = MIN(*len, winusb_wcid.header.dwLength);
		status = USBD_REQ_HANDLED;

	} else if (((req->bmRequestType & USB_REQ_TYPE_RECIPIENT) == USB_REQ_TYPE_INTERFACE) &&
		(req->wIndex == WINUSB_REQ_GET_EXTENDED_PROPERTIES_OS_FEATURE_DESCRIPTOR) &&
		(usb_descriptor_index(req->wValue) == winusb_wcid.functions[0].bInterfaceNumber)) {
		dump_usb_request("winctl", req); ////

		*buf = (uint8_t*)(&guid);
		*len = MIN(*len, guid.header.dwLength);
		status = USBD_REQ_HANDLED;

	} else {
		status = USBD_REQ_NEXT_CALLBACK;  //  Previously USBD_REQ_NOTSUPP
	}

	return status;
}

static void winusb_set_config(usbd_device* usbd_dev, uint16_t wValue) {
	//  debug_println("winusb_set_config"); // debug_flush(); ////
	(void)wValue;
	int status = aggregate_register_callback(
		usbd_dev,
		CONTROL_CALLBACK_TYPE,
		CONTROL_CALLBACK_MASK,
		winusb_control_vendor_request);
	if (status < 0) {
    	debug_println("*** winusb_set_config failed"); debug_flush(); ////
	}
}

void winusb_setup(usbd_device* usbd_dev, uint8_t interface) {
	// debug_println("winusb_setup"); // debug_flush(); ////
	winusb_wcid.functions[0].bInterfaceNumber = interface;

	int status = aggregate_register_config_callback(usbd_dev, winusb_set_config);

	/* Windows probes the compatible ID before setting the configuration,
	   so also register the callback now */

	int status1 = aggregate_register_callback(
		usbd_dev,
		CONTROL_CALLBACK_TYPE,
		CONTROL_CALLBACK_MASK,
		winusb_control_vendor_request);

	int status2 = aggregate_register_callback(
		usbd_dev,
		DESCRIPTOR_CALLBACK_TYPE,
		DESCRIPTOR_CALLBACK_MASK,
		winusb_descriptor_request);

	if (status < 0 || status1 < 0 || status2 < 0) { debug_println("*** winusb_setup failed"); debug_flush(); }
}
