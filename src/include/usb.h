// Universal Serial Bus
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


typedef enum {
    USB_BASE_CLASS_COMPOSITE = 0x00,
    USB_BASE_CLASS_AUDIO = 0x01,
    USB_BASE_CLASS_COMM = 0x02,
    USB_BASE_CLASS_HID = 0x03,
    // USB_BASE_CLASS_PHYSICAL = 0x05,
    USB_BASE_CLASS_IMAGE = 0x06,
    USB_BASE_CLASS_PRINTER = 0x07,
    USB_BASE_CLASS_STORAGE = 0x08,
    USB_BASE_CLASS_HUB = 0x09,
    // USB_BASE_CLASS_CDC_DATA = 0x0A,
    // USB_BASE_CLASS_SMART_CARD = 0x0B,
    USB_BASE_CLASS_CONTENT_SECURITY = 0x0C,
    USB_BASE_CLASS_VIDEO = 0x0E,
    USB_BASE_CLASS_PERSONAL_HEALTHCARE = 0x0F,
    USB_BASE_CLASS_AUDIO_VIDEO = 0x10,
    USB_BASE_CLASS_BILLBOARD = 0x11,
    USB_BASE_CLASS_TYPE_C_BRIDGE = 0x12,
    USB_BASE_CLASS_DIAGNOSTIC = 0xDC,
    USB_BASE_CLASS_WIRELESS = 0xE0,
    USB_BASE_CLASS_MISCELLANEOUS = 0xEF,
    USB_BASE_CLASS_APPLICATION_SPECIFIC = 0xFE,
    USB_BASE_CLASS_VENDOR_SPECIFIC = 0xFF,
} usb_base_class;

typedef enum {
    USB_CLASS_MIDI_STREAMING = 0x010300,
    USB_CLASS_HID_GENERIC = 0x030000,
    USB_CLASS_HID_KBD = 0x030101,
    USB_CLASS_HID_MOS = 0x030102,
    USB_CLASS_STORAGE_BULK = 0x080650,
    USB_CLASS_FLOPPY = 0x080400,
    USB_CLASS_HUB_FS = 0x090000,
    USB_CLASS_HUB_HS_STT = 0x090001,
    USB_CLASS_HUB_HS_MTT = 0x090002,
    USB_CLASS_HUB_SS = 0x090003,
    USB_CLASS_BLUETOOTH = 0xE00101,
    USB_CLASS_XINPUT = 0xFF5D01,
    // USB_CLASS_XINPUT_HEADSET = 0xFF5D02,
    // USB_CLASS_XINPUT_IF2 = 0xFF5D03,
    // USB_CLASS_XINPUT_IF3 = 0xFF5D04,
} usb_class;


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
    USB_HUB_DESCRIPTOR = 0x29,
    USB_SS_HUB_DESCRIPTOR = 0x2A,

    URB_TRT_NO_DATA = 0,
    URB_TRT_CONTROL_OUT = 2,
    URB_TRT_CONTROL_IN = 3,

    HID_BOOT_PROTOCOL = 0,
    HID_REPORT_PROTOCOL = 1,
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
} usb_hid_class_descriptor_t;


typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bNbrPorts;
    uint8_t wHubCharacteristics[2];
    uint8_t bPwrOn2PwrGood;
    uint8_t bHubContrCurrent;
    uint8_t DeviceRemovable[2];
} usb_hub_descriptor_t;


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

// USB host controller to device interface
typedef struct usb_host_interface_t {
    void *host_context;
    void *device_context;
    moe_semaphore_t *semaphore;
    int slot_id;
    int speed;

    void (*dispose)(usb_host_interface_t *self);

    int (*configure_endpoint)(usb_host_interface_t *self, usb_endpoint_descriptor_t *endpoint);
    int (*reset_endpoint)(usb_host_interface_t *self, int epno);
    int (*set_max_packet_size)(usb_host_interface_t *self, int mask_packet_size);
    int (*get_max_packet_size)(usb_host_interface_t *self);

    int (*control)(usb_host_interface_t *self, int trt, urb_setup_data_t setup, uintptr_t buffer);

    int (*data_transfer)(usb_host_interface_t *self, int dci, uintptr_t buffer, uint16_t length);

} usb_host_interface_t;
