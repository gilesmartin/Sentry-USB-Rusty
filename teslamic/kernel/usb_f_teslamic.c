// SPDX-License-Identifier: GPL-2.0+
/*
 * usb_f_teslamic.c - experimental configfs USB function matching a TeslaMic
 * descriptor dump and sourcing USB audio from a device-side ALSA playback PCM.
 *
 * This is an out-of-tree prototype derived from Linux gadget function APIs and
 * the in-tree f_uac1/u_audio model. It intentionally does not claim vehicle
 * compatibility; it only attempts to reproduce the descriptors documented in
 * ../TeslAux/real_mic_dump.md when linked before any other function.
 */

#include <linux/configfs.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb/audio.h>
#include <linux/usb/ch9.h>
#include <linux/usb/composite.h>
#include <linux/hid.h>
#include <linux/unaligned.h>

/* Private in-tree gadget-function helper. The Makefile adds this include dir. */
#include "u_audio.h"
#include "uac_common.h"

#define TM_PCM_RATE_48K        48000
#define TM_PCM_RATE_44K1       44100
#define TM_CHANNEL_MASK        0x3
#define TM_CHANNELS            2
#define TM_SAMPLE_SIZE         2
#define TM_AUDIO_PACKET_BYTES  192
#define TM_REQ_NUMBER          4

#define HID_REQ_GET_REPORT     0x01
#define HID_REQ_GET_IDLE       0x02
#define HID_REQ_SET_REPORT     0x09
#define HID_REQ_SET_IDLE       0x0a
#ifndef HID_INPUT_REPORT
#define HID_INPUT_REPORT       0x01
#endif
#ifndef HID_OUTPUT_REPORT
#define HID_OUTPUT_REPORT      0x02
#endif
#ifndef HID_FEATURE_REPORT
#define HID_FEATURE_REPORT     0x03
#endif

#define TM_IF_AC               0
#define TM_IF_AS               1
#define TM_IF_KBD              2
#define TM_IF_VENDOR           3
#define TM_FU_ID               5
#define TM_OT_ID               7

struct f_teslamic_opts {
	struct usb_function_instance func_inst;
	struct mutex lock;
	int refcnt;
};

struct f_teslamic {
	struct g_audio audio;
	struct usb_ep *kbd_ep;
	struct usb_request *kbd_req;
	struct usb_ctrlrequest setup_cr;
	u8 ac_intf, as_intf, kbd_intf, vendor_intf;
	u8 as_alt, kbd_idle;
	bool as_ep_enabled, kbd_ep_enabled;
	u32 p_srate;
	s16 volume;
	u8 mute;
};

static inline struct f_teslamic *func_to_tm(struct usb_function *f)
{
	return container_of(func_to_g_audio(f), struct f_teslamic, audio);
}

static inline struct f_teslamic_opts *to_tm_opts(struct config_item *item)
{
	return container_of(to_config_group(item), struct f_teslamic_opts,
			    func_inst.group);
}

/* Exact class-specific descriptor payloads from real_mic_dump.md. */
static u8 tm_ac_header[] = { 0x09, 0x24, 0x01, 0x00, 0x01, 0x2f, 0x00, 0x01, 0x01 };
static const u8 tm_ac_input_terminal[] = { 0x0c, 0x24, 0x02, 0x04, 0x01, 0x02, 0x00, 0x02, 0x03, 0x00, 0x00, 0x00 };
static const u8 tm_ac_feature_unit[] = { 0x0a, 0x24, 0x06, 0x05, 0x04, 0x01, 0x01, 0x02, 0x02, 0x00 };
static const u8 tm_ac_selector_unit[] = { 0x07, 0x24, 0x05, 0x06, 0x01, 0x05, 0x00 };
static const u8 tm_ac_output_terminal[] = { 0x09, 0x24, 0x03, 0x07, 0x01, 0x01, 0x00, 0x06, 0x00 };
static const u8 tm_as_general[] = { 0x07, 0x24, 0x01, 0x07, 0x01, 0x01, 0x00 };
static const u8 tm_as_format_type_i[] = { 0x0e, 0x24, 0x02, 0x01, 0x02, 0x02, 0x10, 0x02, 0x44, 0xac, 0x00, 0x80, 0xbb, 0x00 };
static const u8 tm_as_iso_endpoint[] = { 0x07, 0x25, 0x01, 0x01, 0x00, 0x00, 0x00 };

static const u8 tm_hid_keyboard_report[] = {
	0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x05, 0x07,
	0x19, 0xe0, 0x29, 0xe7, 0x15, 0x00, 0x25, 0x01,
	0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01,
	0x75, 0x08, 0x81, 0x01, 0x95, 0x05, 0x75, 0x01,
	0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
	0x95, 0x01, 0x75, 0x03, 0x91, 0x01, 0x95, 0x06,
	0x75, 0x08, 0x15, 0x00, 0x26, 0xa4, 0x00, 0x05,
	0x07, 0x19, 0x00, 0x2a, 0xa4, 0x00, 0x81, 0x00,
	0xc0,
};

static const u8 tm_hid_vendor_report[] = {
	0x06, 0x00, 0xff, 0x0a, 0xaa, 0x55, 0xa1, 0x01,
	0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x96,
	0x00, 0x01, 0x09, 0x01, 0x81, 0x02, 0x96, 0x00,
	0x01, 0x09, 0x01, 0x91, 0x02, 0x95, 0x08, 0x09,
	0x01, 0xb1, 0x02, 0xc0,
};

static const u8 tm_if3_feature_report[] = { 0x00, 0x01, 0x00, 0x03, 0x03, 0x00, 0x08, 0x00 };

/* HID descriptors as byte arrays for static exactness tests. */
static const u8 tm_hid_keyboard_desc[] = { 0x09, 0x21, 0x01, 0x02, 0x00, 0x01, 0x22, 0x41, 0x00 };
static const u8 tm_hid_vendor_desc[] = { 0x09, 0x21, 0x01, 0x02, 0x00, 0x01, 0x22, 0x24, 0x00 };

static struct usb_interface_descriptor tm_ac_interface_desc = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bAlternateSetting = 0,
	.bNumEndpoints = 0,
	.bInterfaceClass = USB_CLASS_AUDIO,
	.bInterfaceSubClass = USB_SUBCLASS_AUDIOCONTROL,
	.bInterfaceProtocol = 0,
};

static struct usb_interface_descriptor tm_as_alt0_desc = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bAlternateSetting = 0,
	.bNumEndpoints = 0,
	.bInterfaceClass = USB_CLASS_AUDIO,
	.bInterfaceSubClass = USB_SUBCLASS_AUDIOSTREAMING,
	.bInterfaceProtocol = 0,
};

static struct usb_interface_descriptor tm_as_alt1_desc = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bAlternateSetting = 1,
	.bNumEndpoints = 1,
	.bInterfaceClass = USB_CLASS_AUDIO,
	.bInterfaceSubClass = USB_SUBCLASS_AUDIOSTREAMING,
	.bInterfaceProtocol = 0,
};

static const u8 tm_as_in_ep_desc[] __maybe_unused = { 0x09, 0x05, 0x84, 0x09, 0xc0, 0x00, 0x01, 0x00, 0x00 };
static struct usb_endpoint_descriptor tm_as_in_ep = {
	.bLength = USB_DT_ENDPOINT_AUDIO_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_IN | 4,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_ADAPTIVE,
	.wMaxPacketSize = cpu_to_le16(TM_AUDIO_PACKET_BYTES),
	.bInterval = 1,
	.bRefresh = 0,
	.bSynchAddress = 0,
};

/* High-speed bInterval uses 2^(n-1) microframes; 4 therefore remains 1 ms. */
static struct usb_endpoint_descriptor tm_as_in_ep_hs = {
	.bLength = USB_DT_ENDPOINT_AUDIO_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_IN | 4,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_ADAPTIVE,
	.wMaxPacketSize = cpu_to_le16(TM_AUDIO_PACKET_BYTES),
	.bInterval = 4,
	.bRefresh = 0,
	.bSynchAddress = 0,
};

static struct usb_interface_descriptor tm_kbd_interface_desc = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bAlternateSetting = 0,
	.bNumEndpoints = 1,
	.bInterfaceClass = USB_CLASS_HID,
	.bInterfaceSubClass = 0,
	.bInterfaceProtocol = 0,
};

static const u8 tm_hid_keyboard_ep_desc[] __maybe_unused = { 0x07, 0x05, 0x81, 0x03, 0x40, 0x00, 0x01 };
static struct usb_endpoint_descriptor tm_kbd_ep = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_IN | 1,
	.bmAttributes = USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize = cpu_to_le16(64),
	.bInterval = 1,
};

static struct usb_endpoint_descriptor tm_kbd_ep_hs = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_IN | 1,
	.bmAttributes = USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize = cpu_to_le16(64),
	.bInterval = 4,
};

static struct usb_interface_descriptor tm_vendor_interface_desc = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bAlternateSetting = 0,
	.bNumEndpoints = 0,
	.bInterfaceClass = USB_CLASS_HID,
	.bInterfaceSubClass = 0,
	.bInterfaceProtocol = 0,
};

#define TM_HDR(x) ((struct usb_descriptor_header *)(x))
static struct usb_descriptor_header *tm_fs_descs[] = {
	TM_HDR(&tm_ac_interface_desc),
	TM_HDR(tm_ac_header),
	TM_HDR(tm_ac_input_terminal),
	TM_HDR(tm_ac_feature_unit),
	TM_HDR(tm_ac_selector_unit),
	TM_HDR(tm_ac_output_terminal),
	TM_HDR(&tm_as_alt0_desc),
	TM_HDR(&tm_as_alt1_desc),
	TM_HDR(tm_as_general),
	TM_HDR(tm_as_format_type_i),
	TM_HDR(&tm_as_in_ep),
	TM_HDR(tm_as_iso_endpoint),
	TM_HDR(&tm_kbd_interface_desc),
	TM_HDR(tm_hid_keyboard_desc),
	TM_HDR(&tm_kbd_ep),
	TM_HDR(&tm_vendor_interface_desc),
	TM_HDR(tm_hid_vendor_desc),
	NULL,
};

static struct usb_descriptor_header *tm_hs_descs[] = {
	TM_HDR(&tm_ac_interface_desc),
	TM_HDR(tm_ac_header),
	TM_HDR(tm_ac_input_terminal),
	TM_HDR(tm_ac_feature_unit),
	TM_HDR(tm_ac_selector_unit),
	TM_HDR(tm_ac_output_terminal),
	TM_HDR(&tm_as_alt0_desc),
	TM_HDR(&tm_as_alt1_desc),
	TM_HDR(tm_as_general),
	TM_HDR(tm_as_format_type_i),
	TM_HDR(&tm_as_in_ep_hs),
	TM_HDR(tm_as_iso_endpoint),
	TM_HDR(&tm_kbd_interface_desc),
	TM_HDR(tm_hid_keyboard_desc),
	TM_HDR(&tm_kbd_ep_hs),
	TM_HDR(&tm_vendor_interface_desc),
	TM_HDR(tm_hid_vendor_desc),
	NULL,
};

static struct usb_string tm_strings[] = {
	[0].s = "TeslaMic function",
	{ }
};

static struct usb_gadget_strings tm_stringtab = {
	.language = 0x0409,
	.strings = tm_strings,
};

static struct usb_gadget_strings *tm_strings_array[] = { &tm_stringtab, NULL };

static void tm_copy_to_ep0(struct usb_function *f, const void *src, unsigned int len,
			   const struct usb_ctrlrequest *ctrl, int *value)
{
	struct usb_composite_dev *cdev = f->config->cdev;
	struct usb_request *req = cdev->req;
	u16 w_length = le16_to_cpu(ctrl->wLength);

	*value = min_t(unsigned int, len, w_length);
	memcpy(req->buf, src, *value);
	req->complete = NULL;
	req->context = NULL;
	req->zero = 0;
	req->length = *value;
	*value = usb_ep_queue(cdev->gadget->ep0, req, GFP_ATOMIC);
}

static void tm_ep0_nop_complete(struct usb_ep *ep, struct usb_request *req)
{
	(void)ep;
	(void)req;
}

static int tm_queue_ep0_out(struct usb_function *f, unsigned int length,
			    unsigned int max_length,
			    void (*complete)(struct usb_ep *, struct usb_request *))
{
	struct usb_composite_dev *cdev = f->config->cdev;
	struct usb_request *req = cdev->req;

	if (length > max_length)
		return -EMSGSIZE;
	cdev->gadget->ep0->driver_data = f;
	req->context = f;
	req->complete = complete;
	req->zero = 0;
	req->length = length;
	return usb_ep_queue(cdev->gadget->ep0, req, GFP_ATOMIC);
}

static void tm_ep0_set_srate_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct usb_function *f = ep->driver_data;
	struct g_audio *audio = func_to_g_audio(f);
	struct f_teslamic *tm = func_to_tm(f);
	u8 *buf = req->buf;
	u32 rate;

	if (req->status || req->actual < 3)
		return;
	rate = buf[0] | (buf[1] << 8) | (buf[2] << 16);
	if (rate == TM_PCM_RATE_44K1 || rate == TM_PCM_RATE_48K) {
		tm->p_srate = rate;
		u_audio_set_playback_srate(audio, rate);
	}
}

static int tm_audio_class_setup(struct usb_function *f, const struct usb_ctrlrequest *ctrl)
{
	struct usb_composite_dev *cdev = f->config->cdev;
	struct usb_request *req = cdev->req;
	struct f_teslamic *tm = func_to_tm(f);
	u16 w_index = le16_to_cpu(ctrl->wIndex);
	u16 w_value = le16_to_cpu(ctrl->wValue);
	u16 w_length = le16_to_cpu(ctrl->wLength);
	u8 recipient = ctrl->bRequestType & USB_RECIP_MASK;
	u8 type_dir = ctrl->bRequestType & (USB_DIR_IN | USB_TYPE_MASK | USB_RECIP_MASK);
	u8 cs = w_value >> 8;
	u8 entity = w_index >> 8;
	u8 iface = w_index & 0xff;
	u8 *buf = req->buf;
	__le16 v;
	unsigned int max_length;
	int value = -EOPNOTSUPP;

	if (recipient == USB_RECIP_ENDPOINT && cs == UAC_EP_CS_ATTR_SAMPLE_RATE) {
		switch (type_dir) {
		case USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_ENDPOINT:
			if (ctrl->bRequest == UAC_GET_CUR) {
				buf[0] = tm->p_srate & 0xff;
				buf[1] = (tm->p_srate >> 8) & 0xff;
				buf[2] = (tm->p_srate >> 16) & 0xff;
				value = min_t(u16, w_length, 3);
			} else if (ctrl->bRequest == UAC_GET_MIN) {
				buf[0] = TM_PCM_RATE_44K1 & 0xff; buf[1] = (TM_PCM_RATE_44K1 >> 8) & 0xff; buf[2] = (TM_PCM_RATE_44K1 >> 16) & 0xff;
				value = min_t(u16, w_length, 3);
			} else if (ctrl->bRequest == UAC_GET_MAX) {
				buf[0] = TM_PCM_RATE_48K & 0xff; buf[1] = (TM_PCM_RATE_48K >> 8) & 0xff; buf[2] = (TM_PCM_RATE_48K >> 16) & 0xff;
				value = min_t(u16, w_length, 3);
			} else if (ctrl->bRequest == UAC_GET_RES) {
				memset(buf, 0, 3); value = min_t(u16, w_length, 3);
			}
			break;
		case USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_ENDPOINT:
			if (ctrl->bRequest == UAC_SET_CUR) {
				return tm_queue_ep0_out(f, w_length, 3,
							tm_ep0_set_srate_complete);
			}
			break;
		}
	} else if (recipient == USB_RECIP_INTERFACE && iface == tm->ac_intf && entity == TM_FU_ID) {
		if (type_dir == (USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE)) {
			switch (ctrl->bRequest) {
			case UAC_GET_CUR:
				if (cs == UAC_FU_MUTE) { buf[0] = tm->mute; value = min_t(u16, w_length, 1); }
				else if (cs == UAC_FU_VOLUME) { v = cpu_to_le16(tm->volume); memcpy(buf, &v, 2); value = min_t(u16, w_length, 2); }
				break;
			case UAC_GET_MIN:
				v = cpu_to_le16((s16)-25600); memcpy(buf, &v, 2); value = min_t(u16, w_length, 2); break;
			case UAC_GET_MAX:
				v = cpu_to_le16((s16)0); memcpy(buf, &v, 2); value = min_t(u16, w_length, 2); break;
			case UAC_GET_RES:
				v = cpu_to_le16((s16)256); memcpy(buf, &v, 2); value = min_t(u16, w_length, 2); break;
			}
		} else if (type_dir == (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) &&
			   ctrl->bRequest == UAC_SET_CUR) {
			/* Accept mute/volume writes; they are not required for the audio path. */
			if (cs == UAC_FU_MUTE)
				max_length = 1;
			else if (cs == UAC_FU_VOLUME)
				max_length = 2;
			else
				return -EOPNOTSUPP;
			return tm_queue_ep0_out(f, w_length, max_length,
						tm_ep0_nop_complete);
		}
	}

	if (value >= 0) {
		req->complete = NULL;
		req->context = NULL;
		req->zero = 0;
		req->length = value;
		value = usb_ep_queue(cdev->gadget->ep0, req, GFP_ATOMIC);
	}
	return value;
}

static int tm_hid_setup(struct usb_function *f, const struct usb_ctrlrequest *ctrl)
{
	struct f_teslamic *tm = func_to_tm(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	struct usb_request *req = cdev->req;
	u16 w_index = le16_to_cpu(ctrl->wIndex);
	u16 w_value = le16_to_cpu(ctrl->wValue);
	u16 w_length = le16_to_cpu(ctrl->wLength);
	u8 iface = w_index & 0xff;
	u8 dtype = w_value >> 8;
	u8 rtype = w_value >> 8;
	unsigned int max_length;
	int value = -EOPNOTSUPP;

	if (iface != tm->kbd_intf && iface != tm->vendor_intf)
		return -EOPNOTSUPP;

	if ((ctrl->bRequestType & (USB_DIR_IN | USB_TYPE_MASK | USB_RECIP_MASK)) ==
	    (USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_INTERFACE) && ctrl->bRequest == USB_REQ_GET_DESCRIPTOR) {
		if (iface == tm->kbd_intf && dtype == HID_DT_REPORT)
			tm_copy_to_ep0(f, tm_hid_keyboard_report, sizeof(tm_hid_keyboard_report), ctrl, &value);
		else if (iface == tm->kbd_intf && dtype == HID_DT_HID)
			tm_copy_to_ep0(f, tm_hid_keyboard_desc, sizeof(tm_hid_keyboard_desc), ctrl, &value);
		else if (iface == tm->vendor_intf && dtype == HID_DT_REPORT)
			tm_copy_to_ep0(f, tm_hid_vendor_report, sizeof(tm_hid_vendor_report), ctrl, &value);
		else if (iface == tm->vendor_intf && dtype == HID_DT_HID)
			tm_copy_to_ep0(f, tm_hid_vendor_desc, sizeof(tm_hid_vendor_desc), ctrl, &value);
		return value;
	}

	if ((ctrl->bRequestType & (USB_DIR_IN | USB_TYPE_MASK | USB_RECIP_MASK)) ==
	    (USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE)) {
		switch (ctrl->bRequest) {
		case HID_REQ_GET_REPORT:
			if (iface == tm->vendor_intf && rtype == HID_FEATURE_REPORT) {
				tm_copy_to_ep0(f, tm_if3_feature_report, sizeof(tm_if3_feature_report), ctrl, &value);
			} else if (iface == tm->vendor_intf && rtype == HID_INPUT_REPORT) {
				memset(req->buf, 0, min_t(u16, w_length, 256));
				req->complete = NULL;
				req->context = NULL;
				req->length = min_t(u16, w_length, 256);
				req->zero = 0;
				value = usb_ep_queue(cdev->gadget->ep0, req, GFP_ATOMIC);
			} else if (iface == tm->kbd_intf && rtype == HID_INPUT_REPORT) {
				/* real dump observed a one-byte no-key input report: 00 */
				((u8 *)req->buf)[0] = 0;
				req->complete = NULL;
				req->context = NULL;
				req->length = min_t(u16, w_length, 1);
				req->zero = 0;
				value = usb_ep_queue(cdev->gadget->ep0, req, GFP_ATOMIC);
			}
			return value;
		case HID_REQ_GET_IDLE:
			((u8 *)req->buf)[0] = tm->kbd_idle;
			req->complete = NULL;
			req->context = NULL;
			req->length = min_t(u16, w_length, 1);
			req->zero = 0;
			return usb_ep_queue(cdev->gadget->ep0, req, GFP_ATOMIC);
		}
	}

	if ((ctrl->bRequestType & (USB_DIR_IN | USB_TYPE_MASK | USB_RECIP_MASK)) ==
	    (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE)) {
		if (ctrl->bRequest == HID_REQ_SET_REPORT) {
			if (iface == tm->kbd_intf && rtype == HID_OUTPUT_REPORT)
				max_length = 1;
			else if (iface == tm->vendor_intf && rtype == HID_OUTPUT_REPORT)
				max_length = 256;
			else if (iface == tm->vendor_intf && rtype == HID_FEATURE_REPORT)
				max_length = sizeof(tm_if3_feature_report);
			else
				return -EOPNOTSUPP;
			return tm_queue_ep0_out(f, w_length, max_length,
						tm_ep0_nop_complete);
		}
		if (ctrl->bRequest == HID_REQ_SET_IDLE) {
			tm->kbd_idle = w_value >> 8;
			return tm_queue_ep0_out(f, 0, 0, tm_ep0_nop_complete);
		}
	}

	return -EOPNOTSUPP;
}

static int tm_setup(struct usb_function *f, const struct usb_ctrlrequest *ctrl)
{
	int ret;

	ret = tm_hid_setup(f, ctrl);
	if (ret != -EOPNOTSUPP)
		return ret;
	return tm_audio_class_setup(f, ctrl);
}

static int tm_set_alt(struct usb_function *f, unsigned intf, unsigned alt)
{
	struct f_teslamic *tm = func_to_tm(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	struct usb_gadget *gadget = cdev->gadget;
	int ret;

	if (intf == tm->ac_intf)
		return alt ? -EINVAL : 0;

	if (intf == tm->as_intf) {
		if (alt > 1)
			return -EINVAL;
		if (alt) {
			if (tm->as_ep_enabled)
				return 0;
			ret = config_ep_by_speed(gadget, f, tm->audio.in_ep);
			if (ret) {
				dev_err(&gadget->dev,
					"teslamic: config_ep_by_speed(as) failed: %d\n", ret);
				return ret;
			}
			ret = usb_ep_enable(tm->audio.in_ep);
			if (ret) {
				dev_err(&gadget->dev,
					"teslamic: usb_ep_enable(as) failed: %d\n", ret);
				return ret;
			}
			ret = u_audio_start_playback(&tm->audio);
			if (ret) {
				dev_err(&gadget->dev,
					"teslamic: u_audio_start_playback failed: %d\n", ret);
				usb_ep_disable(tm->audio.in_ep);
				return ret;
			}
			tm->as_ep_enabled = true;
			tm->as_alt = 1;
			return 0;
		}
		if (tm->as_ep_enabled) {
			u_audio_stop_playback(&tm->audio);
			usb_ep_disable(tm->audio.in_ep);
			tm->as_ep_enabled = false;
		}
		tm->as_alt = 0;
		return 0;
	}

	if (intf == tm->kbd_intf) {
		if (alt)
			return -EINVAL;
		if (tm->kbd_ep_enabled) {
			usb_ep_disable(tm->kbd_ep);
			tm->kbd_ep_enabled = false;
		}
		ret = config_ep_by_speed(gadget, f, tm->kbd_ep);
		if (ret) {
			dev_err(&gadget->dev,
				"teslamic: config_ep_by_speed(kbd) failed: %d (ep=%s addr=0x%02x desc_addr=0x%02x)\n",
				ret, tm->kbd_ep->name, tm->kbd_ep->address,
				tm->kbd_ep->desc ? tm->kbd_ep->desc->bEndpointAddress : 0xff);
			return ret;
		}
		ret = usb_ep_enable(tm->kbd_ep);
		if (ret) {
			dev_err(&gadget->dev,
				"teslamic: usb_ep_enable(kbd) failed: %d\n", ret);
			return ret;
		}
		tm->kbd_ep_enabled = true;
		return 0;
	}
	if (intf == tm->vendor_intf)
		return alt ? -EINVAL : 0;
	return -EINVAL;
}

static int tm_get_alt(struct usb_function *f, unsigned intf)
{
	struct f_teslamic *tm = func_to_tm(f);

	if (intf == tm->as_intf)
		return tm->as_alt;
	if (intf == tm->ac_intf || intf == tm->kbd_intf || intf == tm->vendor_intf)
		return 0;
	return -EINVAL;
}

static void tm_disable(struct usb_function *f)
{
	struct f_teslamic *tm = func_to_tm(f);

	tm->as_alt = 0;
	if (tm->as_ep_enabled) {
		u_audio_stop_playback(&tm->audio);
		if (tm->audio.in_ep)
			usb_ep_disable(tm->audio.in_ep);
		tm->as_ep_enabled = false;
	}
	if (tm->kbd_ep_enabled) {
		if (tm->kbd_ep)
			usb_ep_disable(tm->kbd_ep);
		tm->kbd_ep_enabled = false;
	}
}

static void tm_suspend(struct usb_function *f)
{
	u_audio_suspend(func_to_g_audio(f));
}

static int tm_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct f_teslamic *tm = func_to_tm(f);
	struct g_audio *audio = &tm->audio;
	struct usb_ep *ep;
	struct usb_string *us;
	int status;

	us = usb_gstrings_attach(cdev, tm_strings_array, ARRAY_SIZE(tm_strings));
	if (IS_ERR(us))
		return PTR_ERR(us);
	tm_ac_interface_desc.iInterface = us[0].id;

	status = usb_interface_id(c, f);
	if (status < 0)
		return status;
	tm->ac_intf = status;
	tm_ac_interface_desc.bInterfaceNumber = status;

	status = usb_interface_id(c, f);
	if (status < 0)
		return status;
	tm->as_intf = status;
	tm_as_alt0_desc.bInterfaceNumber = status;
	tm_as_alt1_desc.bInterfaceNumber = status;
	((u8 *)tm_ac_header)[8] = status;

	status = usb_interface_id(c, f);
	if (status < 0)
		return status;
	tm->kbd_intf = status;
	tm_kbd_interface_desc.bInterfaceNumber = status;

	status = usb_interface_id(c, f);
	if (status < 0)
		return status;
	tm->vendor_intf = status;
	tm_vendor_interface_desc.bInterfaceNumber = status;

	if (tm->ac_intf != 0 || tm->as_intf != 1 || tm->kbd_intf != 2 || tm->vendor_intf != 3)
		dev_warn(&cdev->gadget->dev,
			 "teslamic linked at IF%u-%u, not IF0-IF3; link it before other functions for exact numbering\n",
			 tm->ac_intf, tm->vendor_intf);

	audio->gadget = cdev->gadget;
	ep = usb_ep_autoconfig(cdev->gadget, &tm_as_in_ep);
	if (!ep)
		return -ENODEV;
	audio->in_ep = ep;
	audio->in_ep->desc = &tm_as_in_ep;
	/* Keep the HS variant's address in sync with what autoconfig resolved. */
	tm_as_in_ep_hs.bEndpointAddress = tm_as_in_ep.bEndpointAddress;

	ep = usb_ep_autoconfig(cdev->gadget, &tm_kbd_ep);
	if (!ep)
		return -ENODEV;
	tm->kbd_ep = ep;
	tm->kbd_ep->desc = &tm_kbd_ep;
	/* Keep the HS variant's address in sync with what autoconfig resolved. */
	tm_kbd_ep_hs.bEndpointAddress = tm_kbd_ep.bEndpointAddress;

	status = usb_assign_descriptors(f, tm_fs_descs, tm_hs_descs, NULL, NULL);
	if (status)
		return status;

	audio->in_ep_maxpsize = le16_to_cpu(tm_as_in_ep.wMaxPacketSize);
	audio->params.p_chmask = TM_CHANNEL_MASK;
	audio->params.p_srates[0] = TM_PCM_RATE_48K;
	audio->params.p_srates[1] = TM_PCM_RATE_44K1;
	audio->params.p_ssize = TM_SAMPLE_SIZE;
	audio->params.req_number = TM_REQ_NUMBER;
	audio->params.p_fu.id = TM_FU_ID;
	audio->params.p_fu.mute_present = true;
	audio->params.p_fu.volume_present = true;
	audio->params.p_fu.volume_min = -25600;
	audio->params.p_fu.volume_max = 0;
	audio->params.p_fu.volume_res = 256;
	tm->p_srate = TM_PCM_RATE_48K;
	tm->volume = 0;
	tm->mute = 0;

	status = g_audio_setup(audio, "TeslaMic_PCM", "TeslaMic_Gadget");
	if (status) {
		usb_free_all_descriptors(f);
		return status;
	}
	return 0;
}

static void tm_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct g_audio *audio = func_to_g_audio(f);

	g_audio_cleanup(audio);
	usb_free_all_descriptors(f);
	audio->gadget = NULL;
}

static void tm_free_func(struct usb_function *f)
{
	struct f_teslamic *tm = func_to_tm(f);
	struct f_teslamic_opts *opts = container_of(f->fi, struct f_teslamic_opts, func_inst);

	kfree(tm);
	mutex_lock(&opts->lock);
	opts->refcnt--;
	mutex_unlock(&opts->lock);
}

static struct usb_function *tm_alloc_func(struct usb_function_instance *fi)
{
	struct f_teslamic *tm;
	struct f_teslamic_opts *opts = container_of(fi, struct f_teslamic_opts, func_inst);

	tm = kzalloc(sizeof(*tm), GFP_KERNEL);
	if (!tm)
		return ERR_PTR(-ENOMEM);

	mutex_lock(&opts->lock);
	opts->refcnt++;
	mutex_unlock(&opts->lock);

	tm->audio.func.name = "teslamic";
	tm->audio.func.bind = tm_bind;
	tm->audio.func.unbind = tm_unbind;
	tm->audio.func.set_alt = tm_set_alt;
	tm->audio.func.get_alt = tm_get_alt;
	tm->audio.func.setup = tm_setup;
	tm->audio.func.disable = tm_disable;
	tm->audio.func.suspend = tm_suspend;
	tm->audio.func.free_func = tm_free_func;

	return &tm->audio.func;
}

static void tm_attr_release(struct config_item *item)
{
	struct f_teslamic_opts *opts = to_tm_opts(item);

	usb_put_function_instance(&opts->func_inst);
}

static struct configfs_item_operations tm_item_ops = {
	.release = tm_attr_release,
};

static const struct config_item_type tm_func_type = {
	.ct_item_ops = &tm_item_ops,
	.ct_owner = THIS_MODULE,
};

static void tm_free_inst(struct usb_function_instance *fi)
{
	struct f_teslamic_opts *opts = container_of(fi, struct f_teslamic_opts, func_inst);

	kfree(opts);
}

static struct usb_function_instance *tm_alloc_inst(void)
{
	struct f_teslamic_opts *opts;

	opts = kzalloc(sizeof(*opts), GFP_KERNEL);
	if (!opts)
		return ERR_PTR(-ENOMEM);
	mutex_init(&opts->lock);
	opts->func_inst.free_func_inst = tm_free_inst;
	config_group_init_type_name(&opts->func_inst.group, "", &tm_func_type);
	return &opts->func_inst;
}

DECLARE_USB_FUNCTION_INIT(teslamic, tm_alloc_inst, tm_alloc_func);
MODULE_DESCRIPTION("Experimental TeslaMic-like UAC1/HID configfs gadget function");
MODULE_AUTHOR("Hermes Agent");
MODULE_LICENSE("GPL");
