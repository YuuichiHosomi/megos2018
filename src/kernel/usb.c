// USB: Universal Serial Bus
// Copyright (c) 2019 MEG-OS project, All rights reserved.
// License: MIT

// #define DEBUG
#include "moe.h"
#include "kernel.h"
#include <stdatomic.h>
#include "usb.h"
#include "hid.h"


#define MAX_USB_BUFFER 2048
typedef struct {
    uint8_t buffer[MAX_USB_BUFFER];

    usb_host_interface_t *hci;
    MOE_PHYSICAL_ADDRESS base_buffer;

    uint32_t dev_class;
    uint16_t vid, pid, usb_ver;

    usb_device_descriptor_t dev_desc;
    usb_configuration_descriptor_t current_config;
} usb_device;

typedef struct {
    usb_device *usb;
    int if_id, n_ep;
    uint32_t if_class;
} usb_interface;

typedef struct {
    usb_device *usb;
    int packet_size, interval;
    int kbd_if, kbd_dci, mos_if, mos_dci;
} hid_device;


#define USB_TIMEOUT 3000000
#define ERROR_TIMEOUT -1
usb_device **usb_devices;


uint32_t usb_interface_class(usb_interface_descriptor_t *desc) {
    return (desc->bInterfaceClass << 16) | (desc->bInterfaceSubClass << 8) | desc->bInterfaceProtocol;
}


static urb_setup_data_t setup_create(uint8_t type, uint8_t request, uint16_t value, uint16_t index, uint16_t length) {
    urb_setup_data_t result;
    result.setup.bmRequestType = type;
    result.setup.bRequest = request;
    result.setup.wValue = value;
    result.setup.wIndex = index;
    result.setup.wLength = length;
    return result;
}


int usb_get_descriptor(usb_device *self, uint8_t desc_type, uint8_t index, size_t length) {

    urb_setup_data_t setup_data = setup_create(0x80, URB_GET_DESCRIPTOR, (desc_type << 8) | index, 0, length);

    return self->hci->control(self->hci, 1, URB_TRT_CONTROL_IN, setup_data, self->base_buffer, USB_TIMEOUT);
}

int hid_set_protocol(usb_device *self, int ifno, int value) {

    urb_setup_data_t setup_data = setup_create(0x21, URB_HID_SET_PROTOCOL, value, ifno, 0);

    return self->hci->control(self->hci, 1, URB_TRT_NO_DATA, setup_data, 0, USB_TIMEOUT);
}


int usb_set_configuration(usb_device *self, int value) {

    urb_setup_data_t setup_data = setup_create(0x00, URB_SET_CONFIGURATION, value, 0, 0);

    return self->hci->control(self->hci, 1, URB_TRT_NO_DATA, setup_data, 0, USB_TIMEOUT);
}


int usb_config_endpoint(usb_device *self, usb_endpoint_descriptor_t *endpoint) {
    return self->hci->configure_ep(self->hci, endpoint, USB_TIMEOUT);
}


int usb_data_transfer(usb_device *self, int dci, uint16_t length, int64_t timeout) {
    return self->hci->data_transfer(self->hci, dci, self->base_buffer, length, timeout);
}


void usb_hid_thread(void *args) {
    hid_device *self = args;

    int status;

    void *buffer = MOE_PA2VA(self->usb->base_buffer);
    memset(buffer, 0, self->packet_size);
    if (self->kbd_dci) {
        moe_hid_kbd_state_t state;
        DEBUG_PRINT("\n[Installed USB Keyboard SLOT %d IF %d DCI %d]\n", self->usb->slot_id, self->kbd_if, self->kbd_dci);

        for (;;) {
            status = usb_data_transfer(self->usb, self->kbd_dci, self->packet_size, MOE_FOREVER);
            if (status >= 0) {
                _Atomic uint64_t *p = buffer;
                _Atomic uint64_t *q = (_Atomic uint64_t *)&state.current;
                if (*p != *q) {
                    memcpy(&state.current, buffer, sizeof(hid_raw_kbd_report_t));
                    DEBUG_PRINT("(Key %02x %02x)", state.current.modifier, state.current.keydata[0]);
                    hid_process_key_report(&state);
                }
            } else {
                _zprintf("#KBD_ERR(%d)", status);
                moe_usleep(100000);
            }
        }
    } else if (self->mos_dci) {
        moe_hid_mos_state_t state;
        hid_raw_mos_report_t report;
        DEBUG_PRINT("\n[Installed USB Mouse SLOT %d IF %d DCI %d]\n", self->usb->slot_id, self->mos_if, self->mos_dci);

        for (;;) {
            status = usb_data_transfer(self->usb, self->mos_dci, self->packet_size, MOE_FOREVER);
            if (status >= 0) {
                uint8_t *p = buffer;
                memcpy(&report, buffer, sizeof(hid_raw_mos_report_t));
                DEBUG_PRINT("(Mouse %x %02x %02x %02x)", p[0], p[1], p[2], p[3]);
                hid_process_mouse_report(hid_convert_mouse(&state, &report));
            } else {
                _zprintf("#MOS_ERR(%d)", status);
                moe_usleep(100000);
            }
        }
    }
}

hid_device *configure_hid(usb_device *self, usb_interface_descriptor_t *hid_if, usb_endpoint_descriptor_t *endpoint) {
    int status;
    hid_device *hid = NULL;

    int packet = ((endpoint->wMaxPacketSize[1] << 8) | endpoint->wMaxPacketSize[0]);
    int epno = endpoint->bEndpointAddress & 15;
    int io = (endpoint->bEndpointAddress & 0x80) ? 1 : 0;
    int dci = (epno << 1) | io;

    uint32_t if_class = usb_interface_class(hid_if);
    switch (if_class) {
        case USB_CLS_HID_KBD:
        case USB_CLS_HID_MOS:
        {
            DEBUG_PRINT("#%d ENDPOINT %d IO %d ATTR %02x MAX PACKET %d INTERVAL %d\n", self->hci.slot_id, epno, io, endpoint->bmAttributes, packet, endpoint->bInterval);

            int ifno = hid_if->bInterfaceNumber;
            hid = moe_alloc_object(sizeof(hid_device), 1);
            hid->usb = self;
            if (if_class == USB_CLS_HID_KBD) {
                hid->kbd_if = ifno;
                hid->kbd_dci = dci;
            } else if(if_class == USB_CLS_HID_MOS) {
                hid->mos_if = ifno;
                hid->mos_dci = dci;
            }
            hid->packet_size = packet;
            hid->interval = endpoint->bInterval * 1000;

            status = hid_set_protocol(self, ifno, 0);
            DEBUG_PRINT("#%d HID %06x SET PROTOCOL STATUS %d\n", self->slot_id, if_class, status);
            status = usb_config_endpoint(self, endpoint);
            DEBUG_PRINT("#%d CONFIG ENDPOINT %d STATUS %d\n", self->slot_id, epno, status);

            break;
        }

    }

    return hid;
}


static int setup_usb_device(usb_device *self) {

    int status;

    int max_packet_size = self->hci->get_max_packet_size(self->hci);
    status = usb_get_descriptor(self, USB_DEVICE_DESCRIPTOR, 0, 8);
    usb_device_descriptor_t *temp = MOE_PA2VA(self->base_buffer);
    if (max_packet_size != temp->bMaxPacketSize0) {
        // DEBUG_PRINT("@[%d USB %d.%d PS %d]", slot_id, temp->bcdUSB[1], temp->bcdUSB[0] >> 4, temp->bMaxPacketSize0);
        if (temp->bcdUSB[1] < 3) {
            self->hci->set_max_packet_size(self->hci, temp->bMaxPacketSize0, USB_TIMEOUT);
        }
    };

    status = usb_get_descriptor(self, USB_DEVICE_DESCRIPTOR, 0, sizeof(usb_device_descriptor_t));
    if (status < 0) return -1;
    usb_device_descriptor_t *dd = MOE_PA2VA(self->base_buffer);
    self->dev_desc = *dd;
    self->vid = (dd->idVendor[1] << 8) | (dd->idVendor[0]);
    self->pid = (dd->idProduct[1] << 8) | (dd->idProduct[0]);
    self->dev_class = (dd->bDeviceClass << 16) | (dd->bDeviceSubClass << 8) | dd->bDeviceProtocol;
#ifdef DEBUG
    int usb_ver = (dd->bcdUSB[1] << 8) | (dd->bcdUSB[0]);
    DEBUG_PRINT("\n#USB_DEVICE %d USB %d.%d LEN %d SZ %d VID %04x PID %04x CLASS %06x CONF %d\n", slot_id, usb_ver >> 8, (usb_ver >> 4) & 15, dd->bLength, dd->bMaxPacketSize0, self->vid, self->pid, self->dev_class, dd->bNumConfigurations);
#endif
    status = usb_get_descriptor(self, USB_CONFIGURATION_DESCRIPTOR, 0, 8);
    if (status < 0) return -1;

    usb_configuration_descriptor_t *config_temp = MOE_PA2VA(self->base_buffer);
    uint16_t sz = (config_temp->wTotalLength[1] << 8) | config_temp->wTotalLength[0];
    if (sz) {
        status = usb_get_descriptor(self, USB_CONFIGURATION_DESCRIPTOR, 0, sz);

        uint8_t buffer[MAX_USB_BUFFER];
        memcpy(buffer, MOE_PA2VA(self->base_buffer), sz);
        uint8_t *p = buffer;

        usb_configuration_descriptor_t *c_cnf = NULL;
        usb_interface_descriptor_t *c_if = NULL;

        size_t index = 0;
        while (index < sz) {
            uint8_t type = p[index + 1];
            switch (type) {
                case USB_CONFIGURATION_DESCRIPTOR:
                    usb_configuration_descriptor_t *config = (usb_configuration_descriptor_t *)(p + index);
                    DEBUG_PRINT("#%d CONFIG #IF %d CONFIG %d POWER %d mA\n", slot_id, config->bNumInterface, config->bConfigurationValue, config->bMaxPower * 2);
                    if (!c_cnf) {
                        status = usb_set_configuration(self, config->bConfigurationValue);
                        DEBUG_PRINT("#%d SET_CONFIG %d STATUS %d\n", slot_id, config->bConfigurationValue, status);
                        self->current_config = *config;
                        c_cnf = config;
                    }
                    break;

                case USB_INTERFACE_DESCRIPTOR:
                    c_if = (usb_interface_descriptor_t *)(p + index);
                    // DEBUG_PRINT("#%d IF %d ALT %d #EP %d CLASS %06x\n", slot_id, c_if->bInterfaceNumber, c_if->bAlternateSetting, c_if->bNumEndpoints, usb_interface_class(c_if));
                    break;

                case USB_ENDPOINT_DESCRIPTOR:
                    usb_endpoint_descriptor_t *endpoint = (usb_endpoint_descriptor_t *)(p + index);
                    if (c_if->bInterfaceClass == USB_CLS_HID) {
                        hid_device *hid = configure_hid(self, c_if, endpoint);
                        if (hid) {
                            moe_create_thread(&usb_hid_thread, priority_normal, hid, "usb.hid");
                        }
                    }
                    break;
            }
            uint8_t q = p[index];
            if (q) {
                index += q;
            } else {
                break;
            }
        }
    }

    return 0;
}


void usb_new_device(usb_host_interface_t *hci) {
    uintptr_t ptr = moe_alloc_physical_page(sizeof(usb_device));
    usb_device *device = MOE_PA2VA(ptr);
    memset(device, 0, sizeof(usb_device));
    device->base_buffer = ptr;
    device->hci = hci;
    // usb_devices[slot_id] = device;
    setup_usb_device(device);
}
