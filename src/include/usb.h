// Universal Serial Bus
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


#define USB_CLS_HID             0x03
#define USB_CLS_STORAGE         0x08
#define USB_CLS_HUB             0x09

#define USB_CLS_HID_GENERIC     0x030000
#define USB_CLS_HID_KBD         0x030101
#define USB_CLS_HID_MOS         0x030102
#define USB_CLS_STORAGE_BULK    0x080650
#define USB_CLS_FLOPPY          0x080400


enum {
    URB_GET_DESCRIPTOR = 6,
    URB_SET_CONFIGURATION = 9,
    URB_HID_GET_REPORT = 1,
    URB_HID_SET_REPORT = 9,
    URB_HID_SET_PROTOCOL = 11,

    USB_DEVICE_DESCRIPTOR = 1,
    USB_CONFIGURATION_DESCRIPTOR,
    USB_STRING_DESCRIPTOR,
    USB_INTERFACE_DESCRIPTOR,
    USB_ENDPOINT_DESCRIPTOR,
    USB_DEVICE_QUALIFIER,
    USB_HID_CLASS_DESCRIPTOR = 0x21,
    USB_HID_REPORT_DESCRIPTOR,

    URB_TRT_NO_DATA = 0,
    URB_TRT_CONTROL_OUT = 2,
    URB_TRT_CONTROL_IN = 3,
};


typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bcdUSB[2];
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize0;
    uint8_t idVendor[2];
    uint8_t idProduct[2];
    uint8_t bcdDevice[2];
    uint8_t iManufacturer;
    uint8_t iProduct;
    uint8_t iSerialNumber;
    uint8_t bNumConfigurations;
} usb_device_descriptor_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t wTotalLength[2];
    uint8_t bNumInterface;
    uint8_t bConfigurationValue;
    uint8_t iConfiguration;
    uint8_t bmAttributes;
    uint8_t bMaxPower;
} usb_configuration_descriptor_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bInterfaceNumber;
    uint8_t bAlternateSetting;
    uint8_t bNumEndpoints;
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t iInterface;
} usb_interface_descriptor_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bEndpointAddress;
    uint8_t bmAttributes;
    uint8_t wMaxPacketSize[2];
    uint8_t bInterval;
} usb_endpoint_descriptor_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bcdUSB[2];
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize0;
    uint8_t bNumConfigurations;
    uint8_t bReserved;
} usb_device_qualifier_descriptor_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    union {
        uint16_t wLANGID;
        uint16_t bString[1];
    };
} usb_string_descriptor_t;

typedef struct {
    uint8_t bDescriptorType;
    uint8_t wDescriptorLength[2];
} usb_hid_report_descriptor_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bcdHID[2];
    uint8_t bCountryCode;
    uint8_t bNumDescriptors;
    usb_hid_report_descriptor_t reports[1];
} usb_hid_descriptor_t;


typedef union {
    uint32_t u32[2];
    struct {
        uint32_t bmRequestType:8;
        uint32_t bRequest:8;
        uint32_t wValue:16;
        uint32_t wIndex:16;
        uint32_t wLength:16;
    } setup;
} urb_setup_data_t;


typedef struct usb_host_interface_t usb_host_interface_t;

typedef int (*UHI_CONFIGURE_EP)
(usb_host_interface_t *uhc, usb_endpoint_descriptor_t *endpoint, int64_t timeout);

typedef int (*UHI_RESET_EP)
(usb_host_interface_t *self, int epno, int64_t timeout);

typedef int (*UHI_SET_MAX_PACKET_SIZE)
(usb_host_interface_t *self, int mask_packet_size, int64_t timeout);

typedef int (*UHI_GET_MAX_PACKET_SIZE)
(usb_host_interface_t *self);

typedef int (*UHI_CONTROL)
(usb_host_interface_t *self, int dci, int trt, urb_setup_data_t setup, uintptr_t buffer, int64_t timeout);

typedef int (*UHI_DATA_TRANSFER)
(usb_host_interface_t *self, int dci, uintptr_t buffer, uint16_t length, int64_t timeout);

// USB virtual host controller interface
typedef struct usb_host_interface_t {
    void *context;
    moe_semaphore_t *semaphore;
    int slot_id;
    UHI_CONFIGURE_EP configure_ep;
    UHI_RESET_EP reset_ep;
    UHI_GET_MAX_PACKET_SIZE get_max_packet_size;
    UHI_SET_MAX_PACKET_SIZE set_max_packet_size;
    UHI_CONTROL control;
    UHI_DATA_TRANSFER data_transfer;
} usb_host_interface_t;
