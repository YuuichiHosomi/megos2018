// USB: Universal Serial Bus
// Copyright (c) 2019 MEG-OS project, All rights reserved.
// License: MIT
#include "moe.h"
#include "kernel.h"
#include <stdatomic.h>
#include "usb.h"
#include "hid.h"


#define DEBUG
#ifdef DEBUG
#define DEBUG_PRINT(...)    _zprintf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...)
#endif


typedef struct {
    uint8_t buffer[2048];

    usb_host_controller_t hci;
    // moe_semaphore_t *dev_sem;
    _Atomic intptr_t lock;
    uintptr_t base_buffer;
    _Atomic uint64_t trb_result;
    int slot_id;
    // _Atomic int trb_status;

    uint32_t dev_class;
    uint16_t vid, pid, usb_ver;

} usb_device;

typedef struct {
    usb_device *usb;
    int packet_size;
    int kbd_if, kbd_dci;
} hid_device;


#define USB_TIMEOUT 3000000
#define ERROR_TIMEOUT -1
usb_device **usb_devices;


uint32_t usb_interface_class(usb_interface_descriptor_t *desc) {
    return (desc->bInterfaceClass << 16) | (desc->bInterfaceSubClass << 8) | desc->bInterfaceProtocol;
}


int usb_get_descriptor(usb_device *self, uint8_t desc_type, uint8_t index, size_t length) {

    urb_setup_data_t setup_data;
    setup_data.setup.bmRequestType = 0x80;
    setup_data.setup.bRequest = URB_GET_DESCRIPTOR;
    setup_data.setup.wValue = (desc_type << 8) | index;
    setup_data.setup.wIndex = 0;
    setup_data.setup.wLength = length;

    return self->hci.control(&self->hci, 1, URB_TRT_CONTROL_IN, setup_data, self->base_buffer, USB_TIMEOUT);
}

int usb_hid_set_protocol(usb_device *self, int hid_if, int value) {

    urb_setup_data_t setup_data;
    setup_data.setup.bmRequestType = 0x21;
    setup_data.setup.bRequest = URB_HID_SET_PROTOCOL;
    setup_data.setup.wValue = value;
    setup_data.setup.wIndex = hid_if;
    setup_data.setup.wLength = 0;

    return self->hci.control(&self->hci, 1, URB_TRT_NO_DATA, setup_data, 0, USB_TIMEOUT);
}


int usb_set_configuration(usb_device *self, int value) {

    urb_setup_data_t setup_data;
    setup_data.setup.bmRequestType = 0x00;
    setup_data.setup.bRequest = URB_SET_CONFIGURATION;
    setup_data.setup.wValue = value;
    setup_data.setup.wIndex = 0;
    setup_data.setup.wLength = 0;

    return self->hci.control(&self->hci, 1, URB_TRT_NO_DATA, setup_data, 0, USB_TIMEOUT);
}


int usb_config_endpoint(usb_device *self, int epno, int attributes, int max_packet_size, int interval) {
    return self->hci.configure_ep(&self->hci, epno, attributes, max_packet_size, interval, USB_TIMEOUT);
}


int usb_data_transfer(usb_device *self, int epno, uint16_t length, int64_t timeout) {
    return self->hci.data_transfer(&self->hci, epno, self->base_buffer, length, timeout);
}


_Noreturn void usb_hid_thread(void *args) {
    hid_device *self = args;

    int status;

    moe_hid_kbd_report_t keyreport[2];
    moe_usleep(2000000);
    _zprintf("\n[Installed USB Keyboard %d %d %d]\n", self->usb->slot_id, self->kbd_if, self->kbd_dci);

    void *buffer = MOE_PA2VA(self->usb->base_buffer);
    memset(buffer, 0, 8);
    for (;;) {
        status = usb_data_transfer(self->usb, self->kbd_dci, self->packet_size, MOE_FOREVER);
        if (status >= 0) {
#ifdef DEBUG
            size_t len = status;
            printf("(Key ");
            uint8_t *cp = buffer;
            for (int i = 0; i < len; i++) {
                uint8_t c = cp[i];
                printf(" %02x", c);
            }
            printf(")");
#endif
            memcpy(keyreport, buffer, sizeof(moe_hid_kbd_report_t));
            hid_process_key_report(keyreport);
        } else {
            printf("#USB_ERR(%d)", status);
            moe_usleep(100000);
        }
    }

}

hid_device *configure_hid(usb_device *self, usb_interface_descriptor_t *hid_if, usb_endpoint_descriptor_t *endpoint) {
    int status;
    hid_device *hid = NULL;

    int packet = ((endpoint->wMaxPacketSize[1] << 8) | endpoint->wMaxPacketSize[0]);
    int epno = endpoint->bEndpointAddress & 15;
    int io = (endpoint->bEndpointAddress & 0x80) ? 1 : 0;

    switch (usb_interface_class(hid_if)) {
        case USB_CLS_HID_KBD:
        {
            DEBUG_PRINT("#%d ENDPOINT %d IO %d ATTR %02x MAX PACKET %d INTERVAL %d\n", self->hci.slot_id, epno, io, endpoint->bmAttributes, packet, endpoint->bInterval);

            hid = moe_alloc_object(sizeof(hid_device), 1);
            hid->usb = self;
            hid->kbd_if = hid_if->bInterfaceNumber;
            hid->kbd_dci = (epno << 1) | io;
            hid->packet_size = packet;

            status = usb_config_endpoint(self, endpoint->bEndpointAddress, endpoint->bmAttributes, packet, endpoint->bInterval);
            DEBUG_PRINT("#%d HID CONFIG ENDPOINT %d STATUS %d\n", self->slot_id, epno, status);
            status = usb_hid_set_protocol(self, hid->kbd_if, 0);
            DEBUG_PRINT("#%d HID SET PROTOCOL STATUS %d\n", self->slot_id, status);

            break;
        }

    }

    return hid;
}


int setup_usb_device(usb_device *self) {

    int slot_id = self->slot_id;
    int status;

    int max_packet_size = self->hci.get_max_packet_size(&self->hci);
    status = usb_get_descriptor(self, USB_DEVICE_DESCRIPTOR, 0, 8);
    usb_device_descriptor_t *temp = MOE_PA2VA(self->base_buffer);
    if (max_packet_size != temp->bMaxPacketSize0) {
        // DEBUG_PRINT("@[%d USB %d.%d PS %d]", slot_id, temp->bcdUSB[1], temp->bcdUSB[0] >> 4, temp->bMaxPacketSize0);
        if (temp->bcdUSB[1] < 3) {
            self->hci.set_max_packet_size(&self->hci, temp->bMaxPacketSize0, USB_TIMEOUT);
        }
    };

    status = usb_get_descriptor(self, USB_DEVICE_DESCRIPTOR, 0, sizeof(usb_device_descriptor_t));
    if (status < 0) return -1;
    usb_device_descriptor_t *dd = MOE_PA2VA(self->base_buffer);
    self->vid = (dd->idVendor[1] << 8) | (dd->idVendor[0]);
    self->pid = (dd->idProduct[1] << 8) | (dd->idProduct[0]);
    self->usb_ver = (dd->bcdUSB[1] << 8) | (dd->bcdUSB[0]);
    self->dev_class = (dd->bDeviceClass << 16) | (dd->bDeviceSubClass << 8) | dd->bDeviceProtocol;
    DEBUG_PRINT("\n#USB_DEVICE %d USB %d.%d LEN %d SZ %d VID %04x PID %04x CLASS %06x CONF %d\n", slot_id, self->usb_ver >> 8, (self->usb_ver >> 4) & 15, dd->bLength, dd->bMaxPacketSize0, self->vid, self->pid, self->dev_class, dd->bNumConfigurations);

    status = usb_get_descriptor(self, USB_CONFIGURATION_DESCRIPTOR, 0, 8);
    if (status < 0) return -1;


    usb_configuration_descriptor_t *config_temp = MOE_PA2VA(self->base_buffer);
    uint16_t sz = (config_temp->wTotalLength[1] << 8) | config_temp->wTotalLength[0];
    if (sz) {
        status = usb_get_descriptor(self, USB_CONFIGURATION_DESCRIPTOR, 0, sz);

        uint8_t buffer[512];
        memcpy(buffer, MOE_PA2VA(self->base_buffer), sz);
        uint8_t *p = buffer;

        usb_configuration_descriptor_t *c_cnf;
        usb_interface_descriptor_t *c_if;

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
                        c_cnf = config;
                    }
                    break;

                case USB_INTERFACE_DESCRIPTOR:
                    c_if = (usb_interface_descriptor_t *)(p + index);
                    DEBUG_PRINT("#%d IF %d ALT %d #EP %d CLASS %06x\n", slot_id, c_if->bInterfaceNumber, c_if->bAlternateSetting, c_if->bNumEndpoints, usb_interface_class(c_if));
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


void usb_new_device(usb_host_controller_t *hci, int slot_id) {
    uintptr_t ptr = moe_alloc_physical_page(sizeof(usb_device));
    usb_device *device = MOE_PA2VA(ptr);
    memset(device, 0, sizeof(usb_device));
    device->base_buffer = ptr;
    device->hci = *hci;
    device->hci.slot_id = slot_id;
    device->slot_id = slot_id;
    device->hci.semaphore = moe_sem_create(0);
    // usb_devices[slot_id] = device;

    setup_usb_device(device);
    // char s[256];
    // snprintf(s, 255, "Usb #%d", slot_id);
    // moe_create_thread(usb_device_fibre, priority_high, device, s);
}
