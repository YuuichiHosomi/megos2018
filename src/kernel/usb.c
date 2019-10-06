// USB: Universal Serial Bus
// Copyright (c) 2019 MEG-OS project, All rights reserved.
// License: MIT

// #define DEBUG
#include "moe.h"
#include "kernel.h"
#include <stdatomic.h>
#include "usb.h"
#include "hid.h"


#define MAX_IFS             16
#define MAX_ENDPOINTS       32
#define MAX_USB_BUFFER      4096
#define MAX_STRING_BUFFER   4096
typedef struct {
    moe_shared_t shared;

    usb_host_interface_t *hci;

    MOE_PHYSICAL_ADDRESS base_buffer;
    void *buffer;

    void *config;
    usb_configuration_descriptor_t *current_config;

    char *strings;
    uintptr_t string_index;

    _Atomic int isAlive;

    uint32_t dev_class;
    uint16_t vid, pid, usb_ver;
    const char *sProduct;

    struct {
        uint32_t if_class, endpoint_bitmap;
        const char *sInterface;
    } interfaces[MAX_IFS];

    struct {
        uint8_t dci, interval;
        uint16_t ps;
    } endpoints[MAX_ENDPOINTS];

    usb_device_descriptor_t dev_desc;
} usb_device;


typedef struct {
    usb_device *device;
    uint32_t class_code;
    int ifno;
} usb_function;


struct {
    uint8_t base_class;
    const char *class_string;
} device_base_class_strings[] = {
    { USB_BASE_CLASS_COMPOSITE, "Composite Device" },
    { USB_BASE_CLASS_AUDIO, "Audio Device" },
    { USB_BASE_CLASS_COMM, "Communication Device" },
    { USB_BASE_CLASS_HID, "Human Interface Device" },
    { USB_BASE_CLASS_PRINTER, "Printer" },
    { USB_BASE_CLASS_STORAGE, "Storage Device" },
    { USB_BASE_CLASS_HUB, "USB Hub" },
    { USB_BASE_CLASS_VIDEO, "Video Device" },
    { USB_BASE_CLASS_AUDIO_VIDEO, "Audio/Video Device" },
    { USB_BASE_CLASS_BILLBOARD, "Billboard Device" },
    { USB_BASE_CLASS_TYPE_C_BRIDGE, "Type-C Bridge" },
    { USB_BASE_CLASS_DIAGNOSTIC, "Diagnostic Device" },
    { USB_BASE_CLASS_WIRELESS, "Wireless Device" },
    { USB_BASE_CLASS_APPLICATION_SPECIFIC, "Application Specific" },
    { USB_BASE_CLASS_VENDOR_SPECIFIC, "Vendor Specific" },
    { 0, NULL },
};

struct {
    uint32_t usb_class;
    const char *class_string;
} device_class_strings[] = {
    { USB_CLASS_MIDI_STREAMING, "USB MIDI Streaming" },
    { USB_CLASS_HID_KBD, "HID Keyboard" },
    { USB_CLASS_HID_MOS, "HID Mouse" },
    { USB_CLASS_STORAGE_BULK, "Mass Storage Device" },
    { USB_CLASS_FLOPPY, "Floppy Device"},
    { USB_CLASS_HUB_HS_STT, "Hi speed Hub"},
    { USB_CLASS_HUB_HS_MTT, "Hi speed Hub with multi TT"},
    { USB_CLASS_HUB_SS, "Super speed Hub"},
    { USB_CLASS_BLUETOOTH, "Bluetooth Interface"},
    { 0, NULL },
};


#define USB_TIMEOUT 3000000
#define ERROR_TIMEOUT -1
#define MAX_USB_DEVICES 127
usb_device *usb_devices[MAX_USB_DEVICES];


void usb_dealloc(void *context) {
    usb_device *self = context;
    // TODO: release buffer
}

void usb_release(usb_device *self) {
    moe_release(&self->shared, &usb_dealloc);
}

static inline uint32_t usb_interface_class(usb_interface_descriptor_t *desc) {
    return (desc->bInterfaceClass << 16) | (desc->bInterfaceSubClass << 8) | desc->bInterfaceProtocol;
}

static inline uint16_t usb_u16(uint8_t *p) {
    return p[0] + p[1] * 256;
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
    return self->hci->control(self->hci, URB_TRT_CONTROL_IN, setup_data, self->base_buffer, USB_TIMEOUT);
}

int usb_get_class_descriptor(usb_device *self, uint8_t request_type, uint8_t desc_type, uint8_t index, int ifno, size_t length) {
    urb_setup_data_t setup_data = setup_create(request_type, URB_GET_DESCRIPTOR, (desc_type << 8) | index, ifno, length);
    return self->hci->control(self->hci, URB_TRT_CONTROL_IN, setup_data, self->base_buffer, USB_TIMEOUT);
}

int hid_set_protocol(usb_device *self, int ifno, int value) {
    urb_setup_data_t setup_data = setup_create(0x21, URB_HID_SET_PROTOCOL, value, ifno, 0);
    return self->hci->control(self->hci, URB_TRT_NO_DATA, setup_data, 0, USB_TIMEOUT);
}


int usb_set_configuration(usb_device *self, int value) {
    urb_setup_data_t setup_data = setup_create(0x00, URB_SET_CONFIGURATION, value, 0, 0);
    return self->hci->control(self->hci, URB_TRT_NO_DATA, setup_data, 0, USB_TIMEOUT);
}


int usb_get_hub_descriptor(usb_device *self, uint8_t desc_type, uint8_t index, size_t length) {
    urb_setup_data_t setup_data = setup_create(0xA0, URB_GET_DESCRIPTOR, (desc_type << 8) | index, 0, length);
    return self->hci->control(self->hci, URB_TRT_CONTROL_IN, setup_data, self->base_buffer, USB_TIMEOUT);
}


int usb_config_endpoint(usb_device *self, usb_endpoint_descriptor_t *endpoint) {
    return self->hci->configure_endpoint(self->hci, endpoint, USB_TIMEOUT);
}


int usb_read_data(usb_device *self, int epno, uint16_t length, int64_t timeout) {
    int dci = self->endpoints[16 + (epno & 15)].dci;
    if (dci) {
        return self->hci->data_transfer(self->hci, dci, self->base_buffer, length, timeout);
    } else {
        return -114514;
    }
}


static const char *read_string(usb_device *self, int index) {
    int status;
    usb_string_descriptor_t *desc = (usb_string_descriptor_t *)self->buffer;
    if (index == 0) return NULL;
    status = usb_get_descriptor(self, USB_STRING_DESCRIPTOR, index, 8);
    if (status < 0) return NULL;
    if (desc->bLength > 8) {
        moe_usleep(10000);
        status = usb_get_descriptor(self, USB_STRING_DESCRIPTOR, index, desc->bLength);
        if (status < 0) return NULL;
    }
    int string_index = self->string_index;
    int string_length = snprintf(self->strings + string_index, MAX_STRING_BUFFER - self->string_index - 1, "%S", desc->bString);
    self->string_index = string_index + string_length + 1;
    self->strings[self->string_index] = '\0';
    return (const char *)(self->strings + string_index);
}


const char *usb_get_generic_name(uint32_t usb_class) {
    for (int i = 0; device_class_strings[i].class_string; i++) {
        if (device_class_strings[i].usb_class == usb_class) {
            return device_class_strings[i].class_string;
        }
    }
    int base_class = usb_class >> 16;
    for (int i = 0; device_base_class_strings[i].class_string; i++) {
        if (device_base_class_strings[i].base_class == base_class) {
            return device_base_class_strings[i].class_string;
        }
    }
    return "Unknown Device";
}


void uhci_dealloc(usb_host_interface_t *hci) {
    usb_device *self = hci->device_context;
    usb_devices[self->hci->slot_id] = NULL;
    self->isAlive = false;
    moe_release(&self->shared, &usb_dealloc);
}


static int parse_endpoint_bitmap(uint32_t bitmap, int dir) {
    int mask = dir ? 0xFFFF0000 : 0x0000FFFF;
    return __builtin_ctz(bitmap & mask);
}


void hid_thread(void *args) {

    usb_function *self = args;
    if (!moe_retain(&self->device->shared)) return;

    int ifno = self->ifno;
    uint32_t bmEndpoint = self->device->interfaces[ifno].endpoint_bitmap;
    int epIn = parse_endpoint_bitmap(bmEndpoint, 1);
    int ps = self->device->endpoints[epIn].ps;
    int interval = self->device->endpoints[epIn].interval * 1000;

    // printf("[HID %d %06x %d %d %d %d]\n", self->device->hci->slot_id, self->class_code, ifno, epIn, ps, interval);

    switch (self->class_code) {
        case USB_CLASS_HID_KBD:
        {
            moe_hid_kbd_state_t kbd;
            while(self->device->isAlive) {
                if (interval) moe_usleep(interval);
                int status = usb_read_data(self->device, epIn, ps, MOE_FOREVER);
                if (!self->device->isAlive) break;
                if (status > 0) {
                    _Atomic uint64_t *p = (void *)&kbd.current;
                    _Atomic uint64_t *q = self->device->buffer;
                    *p = *q;
                    hid_process_key_report(&kbd);
                } else {
                    printf("[KBD ERR %d %d]", self->device->hci->slot_id, status);
                    moe_usleep(50000);
                }
            }
            break;
        }

        case USB_CLASS_HID_MOS:
        {
            moe_hid_mos_state_t mos;
            while(self->device->isAlive) {
                if (interval) moe_usleep(interval);
                int status = usb_read_data(self->device, epIn, ps, MOE_FOREVER);
                if (!self->device->isAlive) break;
                if (status > 0) {
                    hid_raw_mos_report_t *p = self->device->buffer;
                    hid_raw_mos_report_t report = *p;
                    hid_process_mouse_report(hid_convert_mouse(&mos, &report));
                } else {
                    printf("[MOS ERR %d %d]", self->device->hci->slot_id, status);
                    moe_usleep(50000);
                }
            }
            break;
        }

        default:
        {
            while(self->device->isAlive) {
                if (interval) moe_usleep(interval);
                int status = usb_read_data(self->device, epIn, ps, MOE_FOREVER);
                if (!self->device->isAlive) break;
            }
        }
    }

    usb_release(self->device);
}


void hid_start_class_driver(usb_device *self, int ifno) {
    usb_function *fnc = moe_alloc_object(sizeof(usb_function), 1);
    fnc->device = self;
    fnc->ifno = ifno;
    fnc->class_code = self->interfaces[ifno].if_class;
    char buffer[32];
    snprintf(buffer, 32, "hid.usb#%d.V%04x:%04x.%d.%06x", self->hci->slot_id, self->vid, self->pid, ifno, fnc->class_code);
    moe_create_thread(&hid_thread, 0, fnc, buffer);
}


static struct {
    uint32_t class_code;
    void (*usb_start_class_driver)(usb_device *self, int ifno);
} class_driver_list[] = {
    { USB_CLASS_HID_GENERIC, &hid_start_class_driver },
    { USB_CLASS_HID_KBD, &hid_start_class_driver },
    { USB_CLASS_HID_MOS, &hid_start_class_driver },
    { 0, NULL },
};


int usb_new_device(usb_host_interface_t *hci) {

    usb_device *self = moe_alloc_object(sizeof(usb_device), 1);
    moe_shared_init(&self->shared, self);
    self->base_buffer = moe_alloc_io_buffer(MAX_USB_BUFFER);
    self->buffer = MOE_PA2VA(self->base_buffer);
    self->isAlive = true;
    self->strings = moe_alloc_object(sizeof(MAX_STRING_BUFFER), 1);
    self->hci = hci;
    self->hci->device_context = self;
    self->hci->dealloc = &uhci_dealloc;
    usb_devices[self->hci->slot_id] = self;

    int slot_id = self->hci->slot_id;
    int status;

    for (int i = 0; i < 20; i++) {
        moe_usleep(50000);
        status = usb_get_descriptor(self, USB_DEVICE_DESCRIPTOR, 0, 8);
        if (status > 0) break;
    }
    if (status < 0) {
        printf("[USB PS ERR %d %d]\n", slot_id, status);
    }

    int max_packet_size = self->hci->get_max_packet_size(self->hci);
    usb_device_descriptor_t *temp = self->buffer;
    if (max_packet_size != temp->bMaxPacketSize0) {
        // printf("@[%d USB %d.%d PS %d]", slot_id, temp->bcdUSB[1], temp->bcdUSB[0] >> 4, temp->bMaxPacketSize0);
        if (temp->bcdUSB[1] < 3) {
            self->hci->set_max_packet_size(self->hci, temp->bMaxPacketSize0, USB_TIMEOUT);
        } else {
            self->hci->set_max_packet_size(self->hci, 1 << temp->bMaxPacketSize0, USB_TIMEOUT);
        }
        moe_usleep(50000);
    };

    moe_usleep(10000);
    status = usb_get_descriptor(self, USB_DEVICE_DESCRIPTOR, 0, sizeof(usb_device_descriptor_t));
    if (status < 0) {
        printf("[USB DEV_DESC ERR %d %d]", slot_id, status);
        return -1;
    }
    usb_device_descriptor_t *dd = self->buffer;
    self->dev_desc = *dd;
    self->vid = usb_u16(dd->idVendor);
    self->pid = usb_u16(dd->idProduct);
    self->dev_class = (dd->bDeviceClass << 16) | (dd->bDeviceSubClass << 8) | dd->bDeviceProtocol;
    if (self->dev_desc.iProduct) {
        self->sProduct = read_string(self, self->dev_desc.iProduct);
    }
#ifdef DEBUG
    int usb_ver = usb_u16(dd->bcdUSB);
    DEBUG_PRINT("\n#USB_DEVICE %d USB %d.%d LEN %d SZ %d VID %04x PID %04x CLASS %06x CONF %d\n", slot_id, usb_ver >> 8, (usb_ver >> 4) & 15, dd->bLength, dd->bMaxPacketSize0, self->vid, self->pid, self->dev_class, dd->bNumConfigurations);
#endif
    moe_usleep(10000);
    status = usb_get_descriptor(self, USB_CONFIGURATION_DESCRIPTOR, 0, 8);
    if (status < 0) {
        printf("[USB CONFIG_DESC ERR %d %d]", slot_id, status);
        return -1;
    }

    usb_configuration_descriptor_t *config_temp = self->buffer;
    uint16_t sz = usb_u16(config_temp->wTotalLength);
    if (sz) {
        for (int i = 0; i < 50; i++) {
            moe_usleep(10000);
            status = usb_get_descriptor(self, USB_CONFIGURATION_DESCRIPTOR, 0, sz);
            if (status >= 0) break;
        }

        uint8_t *p = moe_alloc_object(sz, 1);
        memcpy(p, self->buffer, sz);
        self->config = p;

        usb_interface_descriptor_t *c_if = NULL;

        size_t index = 0;
        while (index < sz) {
            uint8_t type = p[index + 1];
            switch (type) {
                case USB_CONFIGURATION_DESCRIPTOR:
                    usb_configuration_descriptor_t *config = (usb_configuration_descriptor_t *)(p + index);
                    DEBUG_PRINT("#%d CONFIG #IF %d CONFIG %d POWER %d mA\n", slot_id, config->bNumInterface, config->bConfigurationValue, config->bMaxPower * 2);
                    if (!self->current_config) {
                        status = usb_set_configuration(self, config->bConfigurationValue);
                        DEBUG_PRINT("#%d SET_CONFIG %d STATUS %d\n", slot_id, config->bConfigurationValue, status);
                        self->current_config = config;
                    }
                    break;

                case USB_INTERFACE_DESCRIPTOR:
                {
                    c_if = (usb_interface_descriptor_t *)(p + index);
                    int if_index = c_if->bInterfaceNumber;
                    self->interfaces[if_index].if_class = usb_interface_class(c_if);
                    self->interfaces[if_index].sInterface = read_string(self, c_if->iInterface);
                }
                    break;

                case USB_ENDPOINT_DESCRIPTOR:
                {
                    usb_endpoint_descriptor_t *endpoint = (usb_endpoint_descriptor_t *)(p + index);
                    int if_index = c_if->bInterfaceNumber;
                    int ep_adr = endpoint->bEndpointAddress;
                    int ep_idx = (ep_adr & 15) | ((ep_adr & 0x80) ? 0x10 : 0);
                    self->interfaces[if_index].endpoint_bitmap |= (1 << ep_idx);
                    moe_usleep(50000);
                    status = self->hci->configure_endpoint(self->hci, endpoint, USB_TIMEOUT);
                    if (status > 0) {
                        self->endpoints[ep_idx].dci = status;
                        self->endpoints[ep_idx].interval = endpoint->bInterval;
                        self->endpoints[ep_idx].ps = usb_u16(endpoint->wMaxPacketSize);
                    }
                    break;
                }
                
                case USB_HID_CLASS_DESCRIPTOR:
                    break;

                default:
                {
#ifdef DEBUG
                    int limit = p[index];
                    printf("#%d IF %d descriptor %02x\n", slot_id, config->bNumInterface, type);
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

    int max_ifno = self->current_config->bNumInterface;
    for (int ifno= 0; ifno < max_ifno; ifno++) {
        uint32_t if_class = self->interfaces[ifno].if_class;
        for (int i = 0; class_driver_list[i].class_code != 0; i++)  {
            if (class_driver_list[i].class_code == if_class) {
                class_driver_list[i].usb_start_class_driver(self, ifno);
                break;
            }
        }
    }

    return 0;
}



int cmd_lsusb(int argc, char **argv) {
    for (int i = 0; i < MAX_USB_DEVICES; i++) {
        usb_device *device = usb_devices[i];
        if (!device) continue;
        usb_configuration_descriptor_t *config = device->current_config;
        if (config) {
            const char *product_name = device->sProduct;
            if (!product_name) {
                product_name = usb_get_generic_name(device->dev_class);
            }
            printf("Device %d ID %04x:%04x Class %06x USB %x.%x IF %d %dmA %s\n",
                i, device->vid, device->pid, device->dev_class,
                device->dev_desc.bcdUSB[1], device->dev_desc.bcdUSB[0],
                config->bNumInterface, config->bMaxPower * 2, product_name
            );
            for (int j = 0; j < device->current_config->bNumInterface; j++) {
                const size_t size_buffer = 256;
                char ep_strings[size_buffer];
                uint32_t if_class = device->interfaces[j].if_class;
                int ep_bmp = device->interfaces[j].endpoint_bitmap;
                intptr_t ep_index = 0;
                for (int i = 0; i < MAX_ENDPOINTS; i++) {
                    if (ep_bmp & (1 << i)) {
                        if (ep_index > 0) {
                            ep_strings[ep_index++] = ',';
                        }
                        int epno = i & 15;
                        if (i > 16) {
                            ep_index += snprintf(ep_strings + ep_index, size_buffer - ep_index, "%di", epno);
                        } else {
                            ep_index += snprintf(ep_strings + ep_index, size_buffer - ep_index, "%do", epno);
                        }
                    }
                }
                ep_strings[ep_index] = '\0';
                const char *if_string = device->interfaces[j].sInterface;
                if (!if_string) {
                    if_string = usb_get_generic_name(device->interfaces[j].if_class);
                }
                printf(" IF#%d Class %06x ep (%s) %s\n", j, if_class, ep_strings, if_string);
            }
            for (int i = 0; i < MAX_ENDPOINTS; i++) {
                int dci = device->endpoints[i].dci;
                if (!dci) continue;
                int interval = device->endpoints[i].interval;
                int ps = device->endpoints[i].ps;
                int epno = i & 15;
                printf(" EP#%d dci %d size %d interval %d\n", epno, dci, ps, interval);
            }
        } else {
            printf("Device %d NOT CONFIGURED\n", i);
        }
    }
    return 0;
}
