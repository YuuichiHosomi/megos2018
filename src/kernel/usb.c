// USB: Universal Serial Bus
// Copyright (c) 2019 MEG-OS project, All rights reserved.
// License: MIT

// #define DEBUG
#include "moe.h"
#include "kernel.h"
#include <stdatomic.h>
#include "usb.h"
#include "hid.h"


#define MAX_HUB_PORTS       16
#define MAX_IFS             16
#define MAX_ENDPOINTS       32
#define MAX_USB_BUFFER      4096
#define MAX_STRING_BUFFER   4096
#define ERROR_TIMEOUT       -1
#define MAX_USB_DEVICES     127

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
    union {
        uint32_t vid_pid;
        struct {
            uint16_t pid, vid;
        };
    };
    uint16_t usb_ver;
    const char *sProduct;

    struct interfaces {
        uint32_t if_class, endpoint_bitmap;
        const char *sInterface;
    } interfaces[MAX_IFS];

    struct endpoints {
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
    { USB_CLASS_XINPUT, "XInput Device"},
    { 0, NULL },
};


usb_device *usb_devices[MAX_USB_DEVICES];

void usb_dummy_class_driver(usb_device *self);
void hid_start_class_driver(usb_device *self, int ifno);
void usb_hub_class_driver(usb_device *self, int ifno);
void xinput_class_driver(usb_device *self, int ifno);

typedef struct {
    uint32_t class_code;
    void (*start_class_driver)(usb_device *self, int ifno);
} usb_class_driver_declaration;

typedef struct {
    uint32_t vid_pid;
    void (*start_device_driver)(usb_device *self);
} usb_device_driver_declaration;

#define VID_PID(v, p) ((v) << 16 | (p))

static usb_device_driver_declaration device_specific_driver_list[] = {
    // { VID_PID(0x067B, 0x2303), &usb_dummy_class_driver }, // PL2303
    { 0, NULL },
};

static usb_class_driver_declaration device_class_driver_list[] = {
    { USB_CLASS_HUB_FS, &usb_hub_class_driver },
    { USB_CLASS_HUB_HS_STT, &usb_hub_class_driver },
    { USB_CLASS_HUB_HS_MTT, &usb_hub_class_driver },
    { 0, NULL },
};

static usb_class_driver_declaration interface_class_driver_list[] = {
    { USB_CLASS_HID_GENERIC, &hid_start_class_driver },
    { USB_CLASS_HID_KBD, &hid_start_class_driver },
    { USB_CLASS_HID_MOS, &hid_start_class_driver },
    { USB_CLASS_XINPUT, &xinput_class_driver },
    { 0, NULL },
};


void usb_dealloc(void *context) {
    // usb_device *self = context;
    // TODO: release buffer
}

void usb_release(usb_device *self) {
    if (!self) return;
    moe_release(&self->shared, &usb_dealloc);
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
    return self->hci->control(self->hci, URB_TRT_CONTROL_IN, setup_data, self->base_buffer);
}

int usb_get_class_descriptor(usb_device *self, uint8_t request_type, uint8_t desc_type, uint8_t index, int ifno, size_t length) {
    urb_setup_data_t setup_data = setup_create(request_type, URB_GET_DESCRIPTOR, (desc_type << 8) | index, ifno, length);
    return self->hci->control(self->hci, URB_TRT_CONTROL_IN, setup_data, self->base_buffer);
}

int usb_set_configuration(usb_device *self, int value) {
    urb_setup_data_t setup_data = setup_create(0x00, URB_SET_CONFIGURATION, value, 0, 0);
    return self->hci->control(self->hci, URB_TRT_NO_DATA, setup_data, 0);
}


int usb_config_endpoint(usb_device *self, usb_endpoint_descriptor_t *endpoint) {
    return self->hci->configure_endpoint(self->hci, endpoint);
}


int usb_read_data(usb_device *self, int epno, uint16_t length) {
    int dci = self->endpoints[16 + (epno & 15)].dci;
    if (dci) {
        return self->hci->data_transfer(self->hci, dci, self->base_buffer, length);
    } else {
        return -114514;
    }
}

int usb_write_data(usb_device *self, int epno, uint16_t length) {
    int dci = self->endpoints[(epno & 15)].dci;
    if (dci) {
        return self->hci->data_transfer(self->hci, dci, self->base_buffer, length);
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


void uhci_dispose(usb_host_interface_t *hci) {
    usb_device *self = hci->device_context;
    usb_devices[self->hci->slot_id] = NULL;
    self->isAlive = false;
    moe_release(&self->shared, &usb_dealloc);
}


static int parse_endpoint_bitmap(uint32_t bitmap, int dir) {
    int mask = dir ? 0xFFFF0000 : 0x0000FFFF;
    return __builtin_ctz(bitmap & mask);
}


usb_function *usb_create_function(usb_device *device, int ifno) {
    usb_function *result = moe_alloc_object(sizeof(usb_function), 1);
    result->device = device;
    result->ifno = ifno;
    result->class_code = device->interfaces[ifno].if_class;
    return result;
}


int usb_new_device(usb_host_interface_t *hci) {

    usb_device *self = moe_alloc_object(sizeof(usb_device), 1);
    moe_shared_init(&self->shared, self);
    self->base_buffer = moe_alloc_io_buffer(MAX_USB_BUFFER);
    self->buffer = MOE_PA2VA(self->base_buffer);
    self->isAlive = true;
    self->strings = moe_alloc_object(MAX_STRING_BUFFER, 1);
    self->hci = hci;
    self->hci->device_context = self;
    self->hci->dispose = &uhci_dispose;
    usb_devices[self->hci->slot_id] = self;

    int slot_id = self->hci->slot_id;
    int status;

    if (self->hci->psiv == USB_PSIV_FS) {
        for (int i = 0; i < 5; i++) {
            moe_usleep(50000);
            status = usb_get_descriptor(self, USB_DEVICE_DESCRIPTOR, 0, 8);
            if (status > 0) break;
        }
        if (status < 0) {
            printf("[USB PS ERR %d %d]\n", slot_id, status);
            return -1;
        }
        usb_device_descriptor_t *temp = self->buffer;
        int max_packet_size = self->hci->get_max_packet_size(self->hci);
        if (max_packet_size != temp->bMaxPacketSize0) {
            self->hci->set_max_packet_size(self->hci, temp->bMaxPacketSize0);
        };
    }

    for (int i = 0; i < 5; i++) {
        moe_usleep(50000);
        status = usb_get_descriptor(self, USB_DEVICE_DESCRIPTOR, 0, sizeof(usb_device_descriptor_t));
        if (status > 0) break;
    }
    if (status < 0) {
        printf("[USB DEV_DESC ERR %d %d]", slot_id, status);
        return -1;
    }
    usb_device_descriptor_t *dd = self->buffer;
    self->dev_desc = *dd;
    self->vid_pid = VID_PID(usb_u16(dd->idVendor), usb_u16(dd->idProduct));
    self->dev_class = (dd->bDeviceClass << 16) | (dd->bDeviceSubClass << 8) | dd->bDeviceProtocol;
    if (self->dev_desc.iProduct) {
        self->sProduct = read_string(self, self->dev_desc.iProduct);
    }
#ifdef DEBUG
    int usb_ver = usb_u16(dd->bcdUSB);
    DEBUG_PRINT("\n#USB_DEVICE %d USB %d.%d LEN %d SZ %d VID %04x PID %04x CLASS %06x CONF %d\n", slot_id, usb_ver >> 8, (usb_ver >> 4) & 15, dd->bLength, dd->bMaxPacketSize0, self->vid, self->pid, self->dev_class, dd->bNumConfigurations);
#endif

    moe_usleep(10000);
    status = usb_get_descriptor(self, USB_CONFIGURATION_DESCRIPTOR, 0, sizeof(usb_configuration_descriptor_t));
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
                    self->interfaces[if_index].if_class = (c_if->bInterfaceClass << 16) | (c_if->bInterfaceSubClass << 8) | c_if->bInterfaceProtocol;
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
                    status = self->hci->configure_endpoint(self->hci, endpoint);
                    if (status > 0) {
                        self->endpoints[ep_idx].dci = status;
                        self->endpoints[ep_idx].interval = endpoint->bInterval;
                        self->endpoints[ep_idx].ps = usb_u16(endpoint->wMaxPacketSize);
                    }
                    break;
                }
                
                case USB_HID_CLASS_DESCRIPTOR:
                {
                    break;
                }

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

    int issued = false;
    {
        uint32_t vid_pid = self->vid_pid;
        for (int i = 0; device_specific_driver_list[i].vid_pid != 0; i++) {
            if (device_specific_driver_list[i].vid_pid == vid_pid) {
                device_specific_driver_list[i].start_device_driver(self);
                issued = true;
                break;
            }
        }
    }
    if (!issued) {
        uint32_t dev_class = self->dev_class;
        for (int i = 0; device_class_driver_list[i].class_code != 0; i++) {
            if (device_class_driver_list[i].class_code == dev_class) {
                device_class_driver_list[i].start_class_driver(self, -1);
                issued = true;
                break;
            }
        }
    }
    if (!issued) {
        int max_ifno = self->current_config->bNumInterface;
        for (int ifno= 0; ifno < max_ifno; ifno++) {
            uint32_t if_class = self->interfaces[ifno].if_class;
            for (int i = 0; interface_class_driver_list[i].class_code != 0; i++)  {
                if (interface_class_driver_list[i].class_code == if_class) {
                    interface_class_driver_list[i].start_class_driver(self, ifno);
                    issued = true;
                    break;
                }
            }
        }
    }

    return 0;
}

void usb_dummy_class_driver(usb_device *self) {

}


/*********************************************************************/
// USB HUB Class Driver

int usb_hub_get_descriptor(usb_device *self, uint8_t desc_type, uint8_t index, size_t length) {
    urb_setup_data_t setup_data = setup_create(0xA0, URB_GET_DESCRIPTOR, (desc_type << 8) | index, 0, length);
    return self->hci->control(self->hci, URB_TRT_CONTROL_IN, setup_data, self->base_buffer);
}

int usb_hub_set_feature(usb_device *self, uint16_t feature_sel, uint8_t port) {
    urb_setup_data_t setup_data = setup_create(0x23, URB_SET_FEATURE, feature_sel, port, 0);
    return self->hci->control(self->hci, URB_TRT_NO_DATA, setup_data, 0);
}

int usb_hub_clear_feature(usb_device *self, uint16_t feature_sel, uint8_t port) {
    urb_setup_data_t setup_data = setup_create(0x23, URB_CLEAR_FEATURE, feature_sel, port, 0);
    return self->hci->control(self->hci, URB_TRT_NO_DATA, setup_data, 0);
}

int usb_hub_get_port_status(usb_device *self, uint8_t port) {
    urb_setup_data_t setup_data = setup_create(0xA3, URB_GET_STATUS, 0, port, 4);
    return self->hci->control(self->hci, URB_TRT_CONTROL_IN, setup_data, self->base_buffer);
}

static int usb_hub_get_port_psiv(usb_hub_port_status_t port_status) {
    if (port_status.status.PORT_LOW_SPEED) {
        return USB_PSIV_LS;
    } else if (port_status.status.PORT_HIGH_SPEED) {
        return USB_PSIV_HS;
    } else {
        return USB_PSIV_FS;
    }
}

usb_device *usb_hub_configure_port(usb_function *self, int port_id, usb_hub_port_status_t port_status) {

    usb_device *result = NULL;
    int status;

    if (port_status.status.PORT_CONNECTION) {
        // printf("[HUB %d Attached %d]", slot_id, port_id);
        self->device->hci->enter_configuration(self->device->hci);
        usb_hub_set_feature(self->device, PORT_RESET, port_id);
        for (int i = 0; i < 3; i++) {
            moe_usleep(10000);
            status = usb_hub_get_port_status(self->device, port_id);
            port_status = *(usb_hub_port_status_t *)(self->device->buffer);
            if (port_status.changes.C_PORT_RESET) break;
        }
        usb_hub_clear_feature(self->device, C_PORT_RESET, port_id);
        if (port_status.status.PORT_ENABLE) {
            int speed = usb_hub_get_port_psiv(port_status);
            usb_host_interface_t *child_hci = self->device->hci->hub_attach_device(self->device->hci, port_id, speed);
            if (child_hci) {
                result = child_hci->device_context;
            }
        }
        self->device->hci->leave_configuration(self->device->hci);
    } else {
        // printf("[HUB %d Detached %d]", slot_id, port_id);
    }
    return result;
}

void usb_hub_thread(void *args) {
    usb_function *self = args;

    usb_hub_descriptor_t hub_desc;
    usb_device *children[MAX_HUB_PORTS] = {NULL};

    int ifno = 0;
    uint32_t bmEndpoint = self->device->interfaces[ifno].endpoint_bitmap;
    int ep_in = parse_endpoint_bitmap(bmEndpoint, 1);
    int ps = self->device->endpoints[ep_in].ps;
    int n_ports = 0;

    int status;
    status = usb_hub_get_descriptor(self->device, USB_HUB_DESCRIPTOR, 0, sizeof(usb_hub_descriptor_t));
    if (status > 0) {
        usb_hub_descriptor_t *p = (usb_hub_descriptor_t *)self->device->buffer;
        hub_desc = *p;
        self->device->hci->configure_hub(self->device->hci, &hub_desc, (self->device->dev_class == USB_CLASS_HUB_HS_MTT));
        n_ports = hub_desc.bNbrPorts;
        for (int i = 1; i <= n_ports; i++) {
            status = usb_hub_set_feature(self->device, PORT_POWER, i);
        }
        // for (int i = 1; i <= n_ports; i++) {
        //     status = usb_hub_clear_feature(self->device, C_PORT_CONNECTION, i);
        // }
        moe_usleep(100000);
    }

    while(self->device->isAlive) {
        int status = usb_read_data(self->device, ep_in, ps);
        (void)status;
        if (!self->device->isAlive) break;
        _Atomic uint16_t *p = self->device->buffer;
        uint16_t port_change_bitmap = *p;
        for (int i = 1; i <= n_ports; i++) {
            if (port_change_bitmap & (1 << i)) {
                int port_id = i;
                status = usb_hub_get_port_status(self->device, port_id);
                if (status == sizeof(usb_hub_port_status_t)) {
                    usb_hub_port_status_t port_status = *(usb_hub_port_status_t *)(self->device->buffer);
                    if (port_status.changes.C_PORT_CONNECTION) {
                        moe_usleep(100000);
                        usb_hub_clear_feature(self->device, C_PORT_CONNECTION, port_id);
                        usb_device *child = usb_hub_configure_port(self, port_id, port_status);
                        if (child) {
                            moe_retain(&child->shared);
                            children[port_id] = child;
                        } else {
                            usb_device *child = children[port_id];
                            if (child) {
                                child->hci->hub_detach_device(child->hci);
                                usb_release(child);
                                children[port_id] = NULL;
                            }
                        }
                    } else if (port_status.changes.C_PORT_RESET) {
                        usb_hub_clear_feature(self->device, C_PORT_RESET, port_id);
                    } else if (port_status.changes.u16) {
                        printf("[HUB_UNK %d %08x]", port_id, port_status.u32);
                    }

                } else {
                    int slot_id = self->device->hci->slot_id;
                    printf("[HUB_ERR %d %d %d]", slot_id, port_id, status);
                }
            }
        }
        moe_usleep(50000);
    }

    for (int i = 1; i <= n_ports; i++) {
        usb_device *child = children[i];
        if (child) {
            child->hci->hub_detach_device(child->hci);
            usb_release(child);
            children[i] = NULL;
        }
    }

    usb_release(self->device);
}

void usb_hub_class_driver(usb_device *self, int ifno) {
    if (!moe_retain(&self->shared)) return;
    usb_function *fnc = usb_create_function(self, 0);
    char buffer[32];
    snprintf(buffer, 32, "usb.hub#%d.V%04x:%04x.%06x", self->hci->slot_id, self->vid, self->pid, fnc->class_code);
    moe_create_thread(&usb_hub_thread, 0, fnc, buffer);
}


/*********************************************************************/
// USB HID Class Driver

int hid_set_protocol(usb_device *self, int ifno, int value) {
    urb_setup_data_t setup_data = setup_create(0x21, URB_HID_SET_PROTOCOL, value, ifno, 0);
    return self->hci->control(self->hci, URB_TRT_NO_DATA, setup_data, 0);
}

int hid_set_report(usb_device *self, int ifno, uint8_t report_type, uint8_t report_id, size_t length) {
    urb_setup_data_t setup_data = setup_create(0x21, URB_HID_SET_REPORT, (report_type << 8) | report_id, ifno, length);
    return self->hci->control(self->hci, URB_TRT_CONTROL_OUT, setup_data, self->base_buffer);
}

void hid_thread(void *args) {
    usb_function *self = (usb_function *)args;

    int ifno = self->ifno;
    uint32_t bmEndpoint = self->device->interfaces[ifno].endpoint_bitmap;
    int ep_in = parse_endpoint_bitmap(bmEndpoint, 1);
    int ps = self->device->endpoints[ep_in].ps;
    // int interval = self->device->endpoints[ep_in].interval * 1000;

    // printf("[HID %d %06x %d %d %d %d]\n", self->device->hci->slot_id, self->class_code, ifno, ep_in, ps, interval);

    switch (self->class_code) {
        case USB_CLASS_HID_KBD:
        {
            int status = hid_set_protocol(self->device, ifno, HID_BOOT_PROTOCOL);
            int f = 2;
            if (self->device->vid_pid == VID_PID(0x0603, 0x0002)) { // GPD WIN Keyboard
                f = 0; // Workaround: disable flash
            }
            for (int i = 0; i < f; i++) {
                // flash
                uint8_t *p = self->device->buffer;
                p[0] = 1 << i;
                hid_set_report(self->device, ifno, 2, 0, 0x01);
                moe_usleep(50000);
                p[0] = 0x00;
                hid_set_report(self->device, ifno, 2, 0, 0x01);
                moe_usleep(50000);
            }
            moe_hid_kbd_state_t kbd;
            while (self->device->isAlive) {
                status = usb_read_data(self->device, ep_in, ps);
                if (!self->device->isAlive) break;
                hid_raw_kbd_report_t *report = self->device->buffer;
                if (status == ps) {
                    kbd.current = *report;
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
            int status = hid_set_protocol(self->device, ifno, HID_BOOT_PROTOCOL);
            moe_hid_mos_state_t mos;
            while (self->device->isAlive) {
                status = usb_read_data(self->device, ep_in, ps);
                hid_raw_mos_report_t *report = self->device->buffer;
                if (!self->device->isAlive) break;
                if (status == ps) {
                    hid_process_mouse_report(hid_convert_mouse(&mos, report));
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
                int status = usb_read_data(self->device, ep_in, ps);
                (void)status;
                if (!self->device->isAlive) break;
                // uint64_t *p = self->device->buffer;
                // printf("[HID %d %04llx]", self->device->hci->slot_id, *p);
                moe_usleep(50000);
            }
        }
    }

    usb_release(self->device);
}

void hid_start_class_driver(usb_device *self, int ifno) {
    if (!moe_retain(&self->shared)) return;
    usb_function *fnc = usb_create_function(self, ifno);
    char buffer[32];
    snprintf(buffer, 32, "hid.usb#%d.V%04x:%04x.%d.%06x", self->hci->slot_id, self->vid, self->pid, ifno, fnc->class_code);
    moe_create_thread(&hid_thread, 0, fnc, buffer);
}


/*********************************************************************/
// XInput Device

// refs https://gitlab.com/xboxdrv/xboxdrv/blob/develop/PROTOCOL
typedef struct Xbox360Msg {
  // -------------------------
  unsigned int type       :8; // always 0
  unsigned int length     :8; // always 0x14 

  // data[2] ------------------
  unsigned int dpad_up     :1;
  unsigned int dpad_down   :1;
  unsigned int dpad_left   :1;
  unsigned int dpad_right  :1;

  unsigned int start       :1;
  unsigned int back        :1;

  unsigned int thumb_l     :1;
  unsigned int thumb_r     :1;

  // data[3] ------------------
  unsigned int lb          :1;
  unsigned int rb          :1;
  unsigned int guide       :1;
  unsigned int dummy1      :1; // always 0

  unsigned int a           :1; // green
  unsigned int b           :1; // red
  unsigned int x           :1; // blue
  unsigned int y           :1; // yellow

  // data[4] ------------------
  unsigned int lt          :8;
  unsigned int rt          :8;

  // data[6] ------------------
  int x1                   :16;
  int y1                   :16;

  // data[10] -----------------
  int x2                   :16;
  int y2                   :16;

  // data[14]; ----------------
  unsigned int dummy2      :32; // always 0
  unsigned int dummy3      :16; // always 0
} __attribute__((__packed__)) Xbox360Msg;


void xinput_thread(void *args) {
    usb_function *self = (usb_function *)args;
    int ifno = self->ifno;
    uint32_t bmEndpoint = self->device->interfaces[ifno].endpoint_bitmap;
    int ep_in = parse_endpoint_bitmap(bmEndpoint, 1);
    // int ep_out = parse_endpoint_bitmap(bmEndpoint, 0);
    int ps = self->device->endpoints[ep_in].ps;
    // printf("[XINPUT %d %06x %d %d]\n", self->device->hci->slot_id, self->class_code, ifno, ep_in);

    while (self->device->isAlive) {
        int status = usb_read_data(self->device, ep_in, ps);
        if (!self->device->isAlive) break;
        if (status > 0) {
            Xbox360Msg *msg = (Xbox360Msg *)self->device->buffer;
            switch (msg->type) {
                case 0x00:
                {
                    _Atomic uint64_t *p = (void *)((uintptr_t)self->device->buffer + 2);
                    if (*p) {
                        printf("[XI %d %d %d %d]", msg->a, msg->b, msg->x, msg->y);
                    }
                    break;
                }

                default:
                    break;
            }
        } else {
            printf("[XI ERR %d %d]", self->device->hci->slot_id, status);
            moe_usleep(50000);
        }
    }

    usb_release(self->device);
}

void xinput_class_driver(usb_device *self, int ifno) {
    if (!moe_retain(&self->shared)) return;
    usb_function *fnc = usb_create_function(self, ifno);
    char buffer[32];
    snprintf(buffer, 32, "xinput#%d.V%04x:%04x.%d.%06x", self->hci->slot_id, self->vid, self->pid, ifno, fnc->class_code);
    moe_create_thread(&xinput_thread, 0, fnc, buffer);
}


/*********************************************************************/


void lsusb_sub(uint32_t parent, int nest) {
    for (int i = 0; i < MAX_USB_DEVICES; i++) {
        usb_device *device = usb_devices[i];
        if (!device || device->hci->parent_slot_id != parent) continue;
        for (int j = 0; j < nest; j++) {
            printf("  ");
        }
        usb_configuration_descriptor_t *config = device->current_config;
        if (config) {
            const char *product_name = device->sProduct;
            if (!product_name) {
                if (device->dev_class > 0) {
                    product_name = usb_get_generic_name(device->dev_class);
                } else {
                    product_name = "Composite Device";
                }
            }
            printf("Device %d ID %04x:%04x Class %06x PSIV %d PS %d USB %x.%x IF %d %dmA %s\n",
                i, device->vid, device->pid, device->dev_class,
                device->hci->psiv, device->dev_desc.bMaxPacketSize0,
                device->dev_desc.bcdUSB[1], device->dev_desc.bcdUSB[0],
                config->bNumInterface, config->bMaxPower * 2, product_name
            );
            // for (int j = 0; j < device->current_config->bNumInterface; j++) {
            //     const size_t size_buffer = 256;
            //     char ep_strings[size_buffer];
            //     uint32_t if_class = device->interfaces[j].if_class;
            //     int ep_bmp = device->interfaces[j].endpoint_bitmap;
            //     intptr_t ep_index = 0;
            //     for (int i = 0; i < MAX_ENDPOINTS; i++) {
            //         if (ep_bmp & (1 << i)) {
            //             if (ep_index > 0) {
            //                 ep_strings[ep_index++] = ',';
            //             }
            //             int epno = i & 15;
            //             if (i > 16) {
            //                 ep_index += snprintf(ep_strings + ep_index, size_buffer - ep_index, "%di", epno);
            //             } else {
            //                 ep_index += snprintf(ep_strings + ep_index, size_buffer - ep_index, "%do", epno);
            //             }
            //         }
            //     }
            //     ep_strings[ep_index] = '\0';
            //     const char *if_string = device->interfaces[j].sInterface;
            //     if (!if_string) {
            //         if_string = usb_get_generic_name(device->interfaces[j].if_class);
            //     }
            //     printf(" IF#%d Class %06x ep (%s) %s\n", j, if_class, ep_strings, if_string);
            // }
            // for (int i = 0; i < MAX_ENDPOINTS; i++) {
            //     int dci = device->endpoints[i].dci;
            //     if (!dci) continue;
            //     int interval = device->endpoints[i].interval;
            //     int ps = device->endpoints[i].ps;
            //     int epno = i & 15;
            //     printf(" EP#%d%c dci %d size %d interval %d\n", epno, (i < 16) ? 'o' : 'i', dci, ps, interval);
            // }
        } else {
            printf("Device %d ID %04x:%04x Class %06x PSIV %d PS %d USB %x.%x [NOT CONFIGURED]\n",
                i, device->vid, device->pid, device->dev_class,
                device->hci->psiv, device->dev_desc.bMaxPacketSize0,
                device->dev_desc.bcdUSB[1], device->dev_desc.bcdUSB[0]
            );
            // printf("Device %d NOT CONFIGURED\n", i);
        }
        if (device->dev_desc.bDeviceClass == USB_BASE_CLASS_HUB) {
            lsusb_sub(i, nest + 1);
        }
    }

}


int cmd_lsusb(int argc, char **argv) {
    lsusb_sub(0, 0);
    return 0;
}
