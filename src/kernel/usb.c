// USB: Universal Serial Bus
// Copyright (c) 2019 MEG-OS project, All rights reserved.
// License: MIT

// #define DEBUG
#include "moe.h"
#include "kernel.h"
#include <stdatomic.h>
#include "usb.h"
#include "hid.h"


#define MAX_IFS 16
#define MAX_USB_BUFFER 2048
typedef struct {
    uint8_t buffer[MAX_USB_BUFFER];
    uint8_t strings[MAX_USB_BUFFER];

    usb_host_interface_t *hci;
    MOE_PHYSICAL_ADDRESS base_buffer;

    _Atomic int isAlive;
    uint32_t dev_class;
    uint16_t vid, pid, usb_ver;
    uint16_t string_index, iManufacture, iProduct, iSerialNumber;
    wchar_t *sManufacture, *sProduct, *sSerialNumber;

    struct {
        uint32_t if_class;
        wchar_t *sInterface;
    } ifs[MAX_IFS];

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
    uint32_t if_class;
    int packet_size, interval;
    int hid_if, hid_dci;
} hid_device;


#define USB_TIMEOUT 3000000
#define ERROR_TIMEOUT -1
#define MAX_USB_DEVICES 127
usb_device *usb_devices[MAX_USB_DEVICES];


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

int usb_get_class_descriptor(usb_device *self, uint8_t request_type, uint8_t desc_type, uint8_t index, int ifno, size_t length) {
    urb_setup_data_t setup_data = setup_create(request_type, URB_GET_DESCRIPTOR, (desc_type << 8) | index, ifno, length);
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

    if (!self->hid_dci) return;

    int status;

    void *buffer = MOE_PA2VA(self->usb->base_buffer);
    switch (self->if_class) {
        case USB_CLS_HID_KBD:
        {
            moe_hid_kbd_state_t state;
            DEBUG_PRINT("\n[Installed USB Keyboard SLOT %d IF %d DCI %d]\n", self->usb->hci->slot_id, self->hid_if, self->hid_dci);

            while (self->usb->isAlive) {
                status = usb_data_transfer(self->usb, self->hid_dci, self->packet_size, MOE_FOREVER);
                if (status >= 0) {
                    _Atomic uint64_t *p = buffer;
                    _Atomic uint64_t *q = (_Atomic uint64_t *)&state.current;
                    if (*p != *q) {
                        memcpy(&state.current, buffer, sizeof(hid_raw_kbd_report_t));
                        DEBUG_PRINT("(Key %02x %02x)", state.current.modifier, state.current.keydata[0]);
                        hid_process_key_report(&state);
                    }
                } else {
                    printf("#KBD_ERR(%d)", status);
                    moe_usleep(100000);
                }
            }
            break;
        }
        
        case USB_CLS_HID_MOS:
        {
            moe_hid_mos_state_t state;
            hid_raw_mos_report_t report;
            DEBUG_PRINT("\n[Installed USB Mouse SLOT %d IF %d DCI %d]\n", self->usb->hci->slot_id, self->hid_if, self->hid_dci);

            while (self->usb->isAlive) {
                status = usb_data_transfer(self->usb, self->hid_dci, self->packet_size, MOE_FOREVER);
                if (status >= 0) {
                    uint8_t *p = buffer;
                    memcpy(&report, buffer, sizeof(hid_raw_mos_report_t));
                    DEBUG_PRINT("(Mouse %x %02x %02x %02x)", p[0], p[1], p[2], p[3]);
                    hid_process_mouse_report(hid_convert_mouse(&state, &report));
                } else {
                    printf("#MOS_ERR(%d)", status);
                    moe_usleep(100000);
                }
            }
            break;
        }

        default:
        {
            DEBUG_PRINT("\n[USB HID SLOT %d IF %d EP %d CLASS %06x]", self->usb->hci->slot_id, self->hid_if, self->hid_dci, self->if_class);

            while (self->usb->isAlive) {
                status = usb_data_transfer(self->usb, self->hid_dci, self->packet_size, MOE_FOREVER);
                if (status >= 0) {
                    uint8_t *p = buffer;
                    DEBUG_PRINT("[HID ");
                    for (int i = 0; i < status; i++) {
                        DEBUG_PRINT(" %02x", p[i]);
                    }
                    DEBUG_PRINT("]");
                } else {
                    printf("#HID_ERR(%d)", status);
                    moe_usleep(100000);
                }
            }
        }
    }
}

hid_device *configure_hid(usb_device *self, usb_interface_descriptor_t *hid_if, usb_endpoint_descriptor_t *endpoint) {
    int status;

    uint32_t if_class = usb_interface_class(hid_if);
    int ifno = hid_if->bInterfaceNumber;
    int packet = ((endpoint->wMaxPacketSize[1] << 8) | endpoint->wMaxPacketSize[0]);
    int epno = endpoint->bEndpointAddress & 15;
    int io = (endpoint->bEndpointAddress & 0x80) ? 1 : 0;
    DEBUG_PRINT("#%d ENDPOINT %d IO %d ATTR %02x MAX PACKET %d INTERVAL %d\n", self->hci->slot_id, epno, io, endpoint->bmAttributes, packet, endpoint->bInterval);

    hid_device *hid = moe_alloc_object(sizeof(hid_device), 1);
    hid->usb = self;
    hid->hid_if = ifno;
    hid->packet_size = packet;
    hid->interval = endpoint->bInterval * 1000;
    hid->if_class = if_class;

    switch (if_class) {
        case USB_CLS_HID_KBD:
        case USB_CLS_HID_MOS:
        {
            status = hid_set_protocol(self, ifno, 0);
            DEBUG_PRINT("#%d HID %06x SET PROTOCOL STATUS %d\n", self->hci->slot_id, if_class, status);
            status = usb_config_endpoint(self, endpoint);
            DEBUG_PRINT("#%d CONFIG ENDPOINT %d STATUS %d\n", self->hci->slot_id, epno, status);
            hid->hid_dci = status;
            break;
        }

        default:
            // status = hid_set_protocol(self, ifno, 1);
            // DEBUG_PRINT("#%d HID %06x SET PROTOCOL STATUS %d\n", self->hci->slot_id, if_class, status);
            status = usb_config_endpoint(self, endpoint);
            DEBUG_PRINT("#%d CONFIG ENDPOINT %d STATUS %d\n", self->hci->slot_id, epno, status);
            hid->hid_dci = status;

    }

    return hid;
}

static int read_string(usb_device *self, int index) {
    int status;
    if (index == 0) return 0;
    status = usb_get_descriptor(self, USB_STRING_DESCRIPTOR, index, 8);
    if (status < 0) return status;
    usb_string_descriptor_t *desc = (usb_string_descriptor_t *)&self->buffer;
    if (desc->bLength > 8) {
        status = usb_get_descriptor(self, USB_STRING_DESCRIPTOR, index, desc->bLength);
        if (status < 0) return status;
    }
    int bLength = desc->bLength;
    int string_index = self->string_index;
    memcpy(self->strings + string_index, desc, bLength);
    self->string_index = string_index + bLength + 2;
    return string_index + 2;
}


void uhci_dealloc(usb_host_interface_t *hci) {
    usb_device *self = hci->device_context;
    self->isAlive = false;
    usb_devices[self->hci->slot_id] = NULL;
    // TODO:
}


int usb_new_device(usb_host_interface_t *hci) {
    uintptr_t ptr = moe_alloc_physical_page(sizeof(usb_device));
    usb_device *self = MOE_PA2VA(ptr);
    memset(self, 0, sizeof(usb_device));
    self->isAlive = true;
    self->base_buffer = ptr;
    self->hci = hci;
    self->hci->device_context = self;
    self->hci->dealloc = &uhci_dealloc;
    usb_devices[self->hci->slot_id] = self;

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
    if (self->dev_desc.iManufacturer) {
        self->iManufacture = read_string(self, self->dev_desc.iManufacturer);
        self->sManufacture = (wchar_t *)(self->strings + self->iManufacture);
    }
    if (self->dev_desc.iProduct) {
        self->iProduct = read_string(self, self->dev_desc.iProduct);
        self->sProduct = (wchar_t *)(self->strings + self->iProduct);
    }
    if (self->dev_desc.iSerialNumber) {
        self->iSerialNumber = read_string(self, self->dev_desc.iSerialNumber);
        self->sSerialNumber = (wchar_t *)(self->strings + self->iSerialNumber);
    }
#ifdef DEBUG
    int usb_ver = (dd->bcdUSB[1] << 8) | (dd->bcdUSB[0]);
    DEBUG_PRINT("\n#USB_DEVICE %d USB %d.%d LEN %d SZ %d VID %04x PID %04x CLASS %06x CONF %d\n", self->hci->slot_id, usb_ver >> 8, (usb_ver >> 4) & 15, dd->bLength, dd->bMaxPacketSize0, self->vid, self->pid, self->dev_class, dd->bNumConfigurations);
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
                    DEBUG_PRINT("#%d CONFIG #IF %d CONFIG %d POWER %d mA\n", self->hci->slot_id, config->bNumInterface, config->bConfigurationValue, config->bMaxPower * 2);
                    if (!c_cnf) {
                        status = usb_set_configuration(self, config->bConfigurationValue);
                        DEBUG_PRINT("#%d SET_CONFIG %d STATUS %d\n", self->hci->slot_id, config->bConfigurationValue, status);
                        self->current_config = *config;
                        c_cnf = config;
                    }
                    break;

                case USB_INTERFACE_DESCRIPTOR:
                {
                    c_if = (usb_interface_descriptor_t *)(p + index);
                    int if_index = c_if->bInterfaceNumber;
                    self->ifs[if_index].if_class = usb_interface_class(c_if);
                    int sindex = read_string(self, c_if->iInterface);
                    if (sindex > 0) {
                        self->ifs[if_index].sInterface = (wchar_t *)(self->strings + sindex);
                    }
                }
                    if (self->dev_class == 0) {
                        DEBUG_PRINT("#%d IF %d ALT %d #EP %d CLASS %06x\n", self->hci->slot_id, c_if->bInterfaceNumber, c_if->bAlternateSetting, c_if->bNumEndpoints, usb_interface_class(c_if));
                    }
                    break;

                case USB_ENDPOINT_DESCRIPTOR:
                    usb_endpoint_descriptor_t *endpoint = (usb_endpoint_descriptor_t *)(p + index);
                    if (c_if->bInterfaceClass == USB_CLS_HID) {
                        hid_device *hid = configure_hid(self, c_if, endpoint);
                        if (hid) {
                            char name[32];
                            snprintf(name, 32, "usb.hid#%d.%d.%02x.%06x", self->hci->slot_id, c_if->bInterfaceNumber, endpoint->bEndpointAddress, hid->if_class);
                            moe_create_thread(&usb_hid_thread, priority_normal, hid, name);
                        }
                    }
                    break;
                
                case USB_HID_CLASS_DESCRIPTOR:
                {
                    // usb_hid_class_descriptor_t *hid = (usb_hid_class_descriptor_t *)(p + index);
                    // int size = hid->reports[0].wDescriptorLength[0] + hid->reports[0].wDescriptorLength[1] * 256;
                    // int status = usb_get_class_descriptor(self, 0x81, USB_HID_REPORT_DESCRIPTOR, 0, c_if->bInterfaceNumber, size);
                    // printf("#%d GET_REPORT_DESCRIPTOR %d\n", self->hci->slot_id, status);
                    // uint8_t *c = self->buffer;
                    // for (int i = 0; i < status; i++) {
                    //     printf(" %02x", c[i]);
                    // }
                    // printf("\n");
                }
                    break;

                default:
                {
#ifdef DEBUG
                    int limit = p[index];
                    printf("#%d IF %d descriptor %02x\n", self->hci->slot_id, config->bNumInterface, type);
                    for (int i = 0; i < limit; i++) {
                        printf(" %02x", p[index + i]);
                    }
                    printf("\n");
#endif
                }
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


int cmd_lsusb(int argc, char **argv) {
    for (size_t i = 0; i < MAX_USB_DEVICES; i++) {
        usb_device *device = usb_devices[i];
        if (!device) continue;
        usb_configuration_descriptor_t *config = &device->current_config;
        printf("Device %d ID %04x:%04x Class %06x IF#%d Attr %02x %dmA %S\n",
            i, device->vid, device->pid, device->dev_class,
            config->bNumInterface, config->bmAttributes, config->bMaxPower * 2, device->sProduct
        );
        for (int j = 0; j < device->current_config.bNumInterface; j++) {
            uint32_t if_class = device->ifs[j].if_class;
            wchar_t *if_string = device->ifs[j].sInterface;
            printf(" IF#%d Class %06x %S\n", j, if_class, if_string);
        }
    }
    return 0;
}
