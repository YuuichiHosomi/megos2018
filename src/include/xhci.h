// xHCI: Extensible Host Controller Interface

#include <stdint.h>

/*********************************************************************/

#define USB_HCCP_CSZ    0x00000004

#define USB_CMD_RS      0x00000001
#define USB_CMD_HCRST   0x00000002
#define USB_CMD_INTE    0x00000004

#define USB_STS_HCH     0x00000001
#define USB_STS_CNR     0x00000800

#define USB_IMAN_IP     0x0001
#define USB_IMAN_IE     0x0002

#define PORTSC_MAGIC_WORD   0x0e00c3e0
#define USB_PORTSC_CCS  0x00000001
#define USB_PORTSC_PED  0x00000002
#define USB_PORTSC_PR   0x00000010
#define USB_PORTSC_CSC  0x00020000
#define USB_PORTSC_PEC  0x00040000
#define USB_PORTSC_PRC  0x00200000

#define USBLEGSUP_BIOS_OWNED    0x00010000
#define USBLEGSUP_OS_OWNED      0x01000000

/*********************************************************************/

typedef struct {
    uint64_t base;
    uint16_t size;
    uint16_t reserved[3];
} xhci_erste_t;

typedef struct {
    uint32_t route_string:20;
    uint32_t speed:4;
    uint32_t :1;
    uint32_t MTT:1;
    uint32_t hub:1;
    uint32_t context_entries:5;
    uint32_t max_exit_latency:16;
    uint32_t root_hub_port_no:8;
    uint32_t number_of_ports:8;
    uint32_t TT_hub_slot_id:8;
    uint32_t TT_port_no:8;
    uint32_t TTT:2;
    uint32_t :4;
    uint32_t INT:10;
    uint32_t USB_dev_addr:8;
    uint32_t :19;
    uint32_t slot_state:5;
    uint32_t _RESERVED_1[4];
} xhci_slot_ctx_data_t;


typedef struct {
    uint32_t ep_state:3;
    uint32_t :5;
    uint32_t mult:2;
    uint32_t max_p_streams:5;
    uint32_t lsa:1;
    uint32_t interval:8;
    uint32_t max_esit_payload_hi:8;
    uint32_t :1;
    uint32_t error_count:2;
    uint32_t ep_type:3;
    uint32_t :1;
    uint32_t hid:1;
    uint32_t max_burst_size:8;
    uint32_t max_packet_size:16;
    uint64_t TRDP;
    uint32_t average_trb_len:16;
    uint32_t max_esit_payload_lo:16;
    uint32_t _RESERVED_1[3];
} xhci_endpoint_ctx_data_t;


typedef struct {
    uint64_t dcs:1;
    uint64_t type:3;
    uint64_t TRDP:60;
    uint32_t stopped_EDTLA:24;
    uint32_t :8;
    uint32_t _RESERVED_1;
} xhci_stream_ctx_data_t;


typedef struct {
    uint32_t drop;
    uint32_t add;
    uint32_t _RESERVED_1[5];
    uint32_t config_val:8;
    uint32_t if_no:8;
    uint32_t alternate:8;
    uint32_t :8;
} xhci_input_control_ctx_t;


// Transfer Request Block
typedef struct {
    uint32_t u32[3];
    uint32_t C:1;
    uint32_t :9;
    uint32_t type:6;
    uint32_t :16;
} xhci_trb_common_t;

enum {
    TRB_RESERVED = 0,
    TRB_NORMAL,
    TRB_SETUP,
    TRB_DATA,
    TRB_STATUS,
    TRB_ISOCH,
    TRB_LINK,
    TRB_EVENT_DATA,
    TRB_NOP,
    TRB_ENABLE_SLOT_COMMAND,
    TRB_DISABLE_SLOT_COMMAND,
    TRB_ADDRESS_DEVICE_COMMAND,
    TRB_CONFIGURE_ENDPOINT_COMMAND,
    TRB_EVALUATE_CONTEXT_COMMAND,
    TRB_RESET_ENDPOINT_COMMAND,

    TRB_NOP_COMMAND = 23,

    TRB_TRANSFER_EVENT = 32,
    TRB_COMMAND_COMPLETION_EVENT = 33,
    TRB_PORT_STATUS_CHANGE_EVENT = 34,
    TRB_HOST_CONTROLLER_EVENT = 37,

    TRB_CC_NULL = 0,
    TRB_CC_SUCCESS = 1,
    TRB_CC_DATA_BUFFER_ERROR,
    TRB_CC_BABBLE_ERROR,
    TRB_CC_USB_TRANSACTION_ERROR,
    TRB_CC_TRB_ERROR,
    TRB_CC_STALL_ERROR,
    TRB_CC_SHORT_PACKET = 13,
};

typedef struct {
    uint64_t ptr;
    uint32_t trb_xfer_len:17;
    uint32_t td_size:5;
    uint32_t interrupter_target:10;
    uint32_t C:1;
    uint32_t ENT:1;
    uint32_t ISP:1;
    uint32_t NS:1;
    uint32_t chain:1;
    uint32_t IOC:1;
    uint32_t IDT:1;
    uint32_t :2;
    uint32_t BEI:1;
    uint32_t type:6;
    uint32_t DIR:1;
    uint32_t :15;
} xhci_trb_normal_t;

typedef struct {
    uint32_t bmRequestType:8;
    uint32_t bRequest:8;
    uint32_t wValue:16;
    uint32_t wIndex:16;
    uint32_t wLength:16;
    uint32_t trb_xfer_len:17;
    uint32_t :5;
    uint32_t interrupter_target:10;
    uint32_t C:1;
    uint32_t :4;
    uint32_t IOC:1;
    uint32_t IDT:1;
    uint32_t :3;
    uint32_t type:6;
    uint32_t TRT:2;
    uint32_t :14;
} xhci_trb_setup_stage_t;

typedef struct {
    uint64_t ptr;
    uint32_t :24;
    uint32_t INT:8;
    uint32_t C:1;
    uint32_t toggle_cycle:1;
    uint32_t :2;
    uint32_t chain:1;
    uint32_t IOC:1;
    uint32_t :2;
    uint32_t type:6;
    uint32_t :16;
} xhci_trb_link_t;

typedef struct {
    uint32_t _RESERVED_1[3];
    uint32_t C:1;
    uint32_t :9;
    uint32_t type:6;
    uint32_t slot_type:5;
    uint32_t :3;
    uint32_t slot_id:8;
} xhci_trb_esc_t;

typedef struct {
    uint64_t ptr;
    uint32_t _RESERVED_1;
    uint32_t C:1;
    uint32_t :8;
    uint32_t bsr:1;
    uint32_t type:6;
    uint32_t :8;
    uint32_t slot_id:8;
} xhci_trb_adc_t;

typedef struct {
    uint64_t ptr;
    uint32_t parameter:24;
    uint32_t completion:8;
    uint32_t C:1;
    uint32_t :9;
    uint32_t type:6;
    uint32_t VFID:8;
    uint32_t slot_id:8;
} xhci_trb_cce_t;

typedef struct {
    uint32_t :24;
    uint32_t port_id:8;
    uint32_t :32;
    uint32_t :24;
    uint32_t completion:8;
    uint32_t C:1;
    uint32_t :9;
    uint32_t type:6;
    uint32_t :16;
} xhci_trb_psc_t;

typedef struct {
    uint64_t ptr;
    uint32_t length:24;
    uint32_t completion:8;
    uint32_t C:1;
    uint32_t :1;
    uint32_t ED:1;
    uint32_t :7;
    uint32_t type:6;
    uint32_t endpoint_id:5;
    uint32_t :3;
    uint32_t slot_id:8;
} xhci_trb_te_t;

typedef union {
    xhci_trb_common_t common;
    xhci_trb_normal_t normal;
    xhci_trb_setup_stage_t setup;
    xhci_trb_link_t link;
    xhci_trb_esc_t enable_slot_command;
    xhci_trb_adc_t address_device_command;
    xhci_trb_te_t transfer_event;
    xhci_trb_cce_t cce;
    xhci_trb_psc_t psc;
    uint32_t u32[4];
} xhci_trb_t;


/*********************************************************************/


typedef struct {
    _Atomic uint32_t caplength;
    _Atomic uint32_t hcsparams1;
    _Atomic uint32_t hcsparams2;
    _Atomic uint32_t hcsparams3;
    _Atomic uint32_t hccparams1;
    _Atomic uint32_t dboff;
    _Atomic uint32_t rtsoff;
    _Atomic uint32_t hccparams2;
} xhci_cap_t;

typedef struct {
    _Atomic uint32_t usbcmd;
    _Atomic uint32_t usbsts;
    _Atomic uint32_t const pagesize;
    uint32_t _rsrv1[2];
    _Atomic uint32_t dnctrl;
    _Atomic uint64_t crcr;
    uint32_t _rsrv2[4];
    _Atomic uint64_t dcbaap;
    _Atomic uint32_t config;
} xhci_opr_t;

typedef struct {
    _Atomic uint32_t iman, imod, erstsz;
    uint32_t _rsrv;
    _Atomic uint64_t erstba, erdp;
} xhci_rts_irs;

typedef struct {
    _Atomic uint32_t mfindex;
    uint32_t _rsrc1[7];
    xhci_rts_irs irs[1];
} xhci_rts_t;

/*********************************************************************/
