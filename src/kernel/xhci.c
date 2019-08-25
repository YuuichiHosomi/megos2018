// xHCI: Extensible Host Controller Interface
// Copyright (c) 2019 MEG-OS project, All rights reserved.
// License: MIT
#include "moe.h"
#include "kernel.h"
#include <stdatomic.h>
#include "usb.h"


// #define DEBUG
#ifdef DEBUG
#define DEBUG_PRINT(...)    _zprintf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...)
#endif


void usb_new_device(usb_host_interface_t *hci, int slot_id);


#define MAX_SLOTS           64
#define EVENT_MSI_VALUE     1

#define MAX_TR              256
#define MAX_TR_INDEX        64
#define SIZE_COMMAND_RING   64
#define SIZE_EVENT_RING     64
#define SIZE_ERST           1
#define MAX_PORT_CHANGE     64

#define MAX_URB             1024
#define URB_DISPOSE_TIMEOUT 100000

#define PORTSC_MAGIC_WORD   0x0e00c3e0


/*********************************************************************/

#define PCI_CLS_XHCI        0x0C033000
#define PCI_CLS_INTERFACE   0xFFFFFF00
#define PCI_CAP_MSI         0x05

/*********************************************************************/

#define USB_HCCP_CSZ    0x00000004

#define USB_CMD_RS      0x00000001
#define USB_CMD_HCRST   0x00000002
#define USB_CMD_INTE    0x00000004

#define USB_STS_HCH     0x00000001
#define USB_STS_CNR     0x00000800

#define USB_IMAN_IP     0x0001
#define USB_IMAN_IE     0x0002

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
    uint64_t tr_base;
    unsigned slot_id, epno, index, pcs;
} ring_context;


typedef struct {
    uint64_t input_context;
} usb_device_context;


/*********************************************************************/


typedef enum {
    urb_state_any = -1,
    urb_state_none = 0,
    urb_state_acquired,
    urb_state_scheduled,
    urb_state_completed,
    urb_state_failed,
} urb_state_t;

typedef struct {
    _Atomic urb_state_t state;
    _Atomic (xhci_trb_t *) scheduled_trb;
    moe_semaphore_t *semaphore;
    moe_measure_t reuse_delay;
    xhci_trb_t request;
    xhci_trb_t response;
} usb_request_block_t;


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


typedef struct xhci_t {
    usb_host_interface_t uhci;

    moe_semaphore_t *sem_event;
    moe_semaphore_t *sem_urb;
    moe_semaphore_t *sem_request;

    ring_context tr_ctx[MAX_TR];

    MOE_PHYSICAL_ADDRESS base_address, base_rts, base_portsc;
    xhci_cap_t *cap;
    xhci_opr_t *opr;
    size_t min_pagesize;

    uint8_t port2slot[256];
    _Atomic uint64_t wait_for_allocate_port;
    _Atomic uint32_t *DB;
    _Atomic uint64_t *DCBAA;
    usb_device_context *usb_devices;
    xhci_trb_t *CR;
    xhci_trb_t *ERS0;

    usb_request_block_t *urbs;
    _Atomic uint64_t port_change_request;
    moe_queue_t *port_change_queue;

    uint32_t pci_base, bar_limit;
    uint32_t max_dev_slot, max_port, event_cycle, context_size;
    int irq;
} xhci_t;

xhci_t xhci;

_Noreturn void xhci_request_thread(void *args);
_Noreturn void xhci_event_thread(void *args);
int xhci_init_dev(xhci_t *self);


void xhci_msi_handler(int irq) {
    moe_sem_signal(xhci.sem_event);
}


static xhci_trb_t trb_create(int type) {
    xhci_trb_t result = {{{0}}};
    result.common.type = type;
    return result;
}


static void wait_cnr(xhci_t *self, int us) {
    while (self->opr->usbsts & USB_STS_CNR) {
        cpu_relax();
    }
}


static uintptr_t get_portsc(xhci_t *self, int port_id) {
    moe_assert(port_id, "PORT ID IS NOT BE NULL");
    uintptr_t portsc = self->base_portsc + (port_id - 1) * 16;
    return portsc;
}


static ring_context *find_ep_ring(xhci_t *self, int slot_id, int epno) {
    for (int i = 0; i < MAX_TR; i++) {
        ring_context *ctx = &self->tr_ctx[i];
        if (ctx->tr_base != 0 && ctx->slot_id == slot_id && ctx->epno == epno) {
            return ctx;
        }
    }
    return NULL;
}

static uintptr_t alloc_ep_ring(xhci_t *self, int slot_id, int epno) {
    const size_t size = (MAX_TR_INDEX + 1) * sizeof(xhci_trb_t);
    ring_context* ctx = find_ep_ring(self, slot_id, epno);
    if (ctx) {
        ctx->pcs = 1;
        memset(MOE_PA2VA(ctx->tr_base), 0, size);
        return ctx->tr_base | ctx->pcs;
    }
    for (int i = 0; i < MAX_TR; i++) {
        ring_context *ctx = &self->tr_ctx[i];
        if (ctx->tr_base == 0) {
            int pcs = 1;
            uint64_t base = moe_alloc_physical_page(size);
            memset(MOE_PA2VA(base), 0, size);
            ctx->tr_base = base;
            ctx->slot_id = slot_id;
            ctx->epno = epno;
            ctx->index = 0;
            ctx->pcs = 1;
            return base | pcs;
        }
    }
    return 0;
}


static void copy_trb(xhci_trb_t *buffer, const xhci_trb_t *trb, int cycle) {
    _Atomic uint32_t *p = (void *)buffer;
    uint32_t *q = (void *)trb;
    atomic_store(&p[0], q[0]);
    atomic_store(&p[1], q[1]);
    atomic_store(&p[2], q[2]);
    atomic_store(&p[3], (q[3] & 0xFFFFFFFE) | cycle);
}


xhci_trb_t *xhci_write_transfer(xhci_t *self, usb_request_block_t *urb, int slot_id, int epno, xhci_trb_t *trb, int doorbell) {
    ring_context *ctx = find_ep_ring(self, slot_id, epno);
    int pcs = ctx->pcs;
    uint64_t tr_base = ctx->tr_base;
    uintptr_t index = ctx->index;
    xhci_trb_t *tr = MOE_PA2VA(tr_base);

    xhci_trb_t *result = tr + index;
    if (urb) {
        urb->state = urb_state_scheduled;
        urb->scheduled_trb = result;
        urb->response = trb_create(0);
        urb->request = *trb;
    }
    copy_trb(result, trb, pcs);

    index++;
    if (index == MAX_TR_INDEX - 1) {
        xhci_trb_t link = trb_create(TRB_LINK);
        link.link.ptr = tr_base;
        link.link.toggle_cycle = 1;
        copy_trb(tr + index, &link, pcs);

        index = 0;
        pcs ^= 1;
    }
    ctx->pcs = pcs;
    ctx->index = index;

    if (doorbell) {
        wait_cnr(self, 0);
        self->DB[slot_id] = epno;
    }

    return result;
}


usb_request_block_t *urb_allocate(xhci_t *self, moe_semaphore_t *semaphore) {
    for (int i = 0; i < MAX_URB; i++) {
        usb_request_block_t *urb = &self->urbs[i];
        if (moe_measure_until(urb->reuse_delay)) continue;
        urb_state_t expected = atomic_load(&urb->state);
        if (expected != urb_state_none) continue;
        if (atomic_compare_exchange_weak(&urb->state, &expected, urb_state_acquired)) {
            urb->semaphore = semaphore;
            return urb;
        }
    }
    return NULL;
}

void urb_dispose(usb_request_block_t *urb) {
    urb->reuse_delay = moe_create_measure(URB_DISPOSE_TIMEOUT);
    urb->state = urb_state_none;
}

usb_request_block_t *xhci_find_urb_by_trb(xhci_t *self, xhci_trb_t *trb, urb_state_t state) {
    for (int i = 0; i < MAX_URB; i++) {
        usb_request_block_t *urb = &self->urbs[i];
        if (urb->state == urb_state_none) continue;
        if (urb->scheduled_trb == trb) {
            if (state == urb_state_any || urb->state == state) {
                return urb;
            }
        }
    }
    return NULL;
}


int schedule_transfer(xhci_t *self, moe_semaphore_t *semaphore, int slot_id, int epno, xhci_trb_t *trb, xhci_trb_t *response, int doorbell, int64_t timeout) {
    if (timeout == 0) {
        timeout = 1000000;
    } else if (timeout < 0) {
        timeout = MOE_FOREVER;
    }
    usb_request_block_t *urb = urb_allocate(self, semaphore);
    if (urb) {
        // DEBUG_PRINT("<TRB %d %d %d>", slot_id, epno, trb->common.type);
        xhci_write_transfer(self, urb, slot_id, epno, trb, doorbell);
    }
    if (!moe_sem_wait(semaphore, timeout)) {
        *response = urb->response;
        urb_dispose(urb);
        return 0;
    } else {
        int result = -1;
        switch (atomic_load(&urb->state)) {
            case urb_state_completed:
            case urb_state_failed:
                *response = urb->response;
                result = 0;
                break;
            default:
                break;
        }
        urb_dispose(urb);
        return result;
    }
}


int execute_command(xhci_t *self, moe_semaphore_t *semaphore, xhci_trb_t *trb, xhci_trb_t *response, int64_t timeout) {
    static moe_spinlock_t lock;
    moe_spinlock_acquire(&lock);
    int result = schedule_transfer(self, semaphore, 0, 0, trb, response, 1, timeout);
    moe_spinlock_release(&lock);
    return result;
}


uint64_t configure_endpoint(xhci_t *self, int slot_id, uint32_t dci, uint32_t ep_type, uint32_t max_packet_size, uint32_t interval, int copy_dc) {

    uint64_t input_context = self->usb_devices[slot_id].input_context;
    xhci_slot_ctx_data_t *slot = MOE_PA2VA(input_context + self->context_size);
    xhci_input_control_ctx_t *icc = MOE_PA2VA(input_context);
    memset(icc, 0, self->context_size);
    icc->add = 1 | (1 << dci);
    icc->drop = 0;

    if (copy_dc) {
        uint64_t device_context = self->DCBAA[slot_id];
        void *q = MOE_PA2VA(device_context);
        memcpy(slot, q, self->context_size);
    }

    slot->context_entries = MAX(dci, slot->context_entries);

    xhci_endpoint_ctx_data_t *ep = MOE_PA2VA(input_context + self->context_size * (dci + 1));
    uint64_t TR = alloc_ep_ring(self, slot_id, dci);
    ep->ep_type = ep_type;
    if (max_packet_size) {
        int ps = max_packet_size & 0x7FF;
        ep->max_packet_size = ps;
        ep->max_burst_size = (max_packet_size & 0x1800) >> 11;
        // ep->average_trb_len = ps;
        // ep->max_esit_payload_lo = ps;
    } else {
        switch(slot->speed) {
            case 4: // SS
                ep->max_packet_size = 512;
                break;
            case 3: // HS
                ep->max_packet_size = 64;
                break;
            default:
                ep->max_packet_size = 8;
                break;
        }
        ep->average_trb_len = 8;
    }
    ep->interval = interval;
    ep->error_count = 3;
    ep->TRDP = TR;

    return input_context;
}


static void _set_usbcmd(xhci_t *self, uint32_t value) {
    _Atomic uint32_t *usbcmd = &self->opr->usbcmd;
    *usbcmd = *usbcmd | value;
}


uint32_t xhci_reset_port(xhci_t *self, int port_id) {
    _Atomic uint32_t *portsc = MOE_PA2VA(get_portsc(self, port_id));
    wait_cnr(self, 0);
    uint32_t status = atomic_load(portsc);
    uint32_t ccs_csc = USB_PORTSC_CCS | USB_PORTSC_CSC;
    if ((status & ccs_csc) == ccs_csc) {
        atomic_store(portsc, (status & PORTSC_MAGIC_WORD) | USB_PORTSC_CSC | USB_PORTSC_PR);
        wait_cnr(self, 0);
        while (atomic_load(portsc) & USB_PORTSC_PR) {
            cpu_relax();
        }
    }
    return atomic_load(portsc);
}


int uhi_configure_ep(usb_host_interface_t *uhc, usb_endpoint_descriptor_t *endpoint, int64_t timeout) {
    xhci_trb_t response = trb_create(0);

    xhci_t *self = uhc->context;
    int slot_id = uhc->slot_id;
    uint32_t max_packet_size = ((endpoint->wMaxPacketSize[1] << 8) | endpoint->wMaxPacketSize[0]);
    int epno = endpoint->bEndpointAddress;
    uint32_t dci = ((epno & 15) << 1) | ((epno & 0x80) ? 1 : 0);
    uint32_t ep_type = (endpoint->bmAttributes & 3) | ((epno & 0x80) ? 4 : 0);
    uint32_t interval = endpoint->bInterval;
    uint64_t input_context = configure_endpoint(self, slot_id, dci, ep_type, max_packet_size, interval, 1);

    xhci_trb_t trb = trb_create(TRB_CONFIGURE_ENDPOINT_COMMAND);
    trb.address_device_command.ptr = input_context;
    trb.address_device_command.slot_id = slot_id;
    execute_command(self, uhc->semaphore, &trb, &response, timeout);

    switch (response.cce.completion) {
        case TRB_CC_NULL:
            return -1;
        case TRB_CC_SUCCESS:
            return dci;
        default:
            return -response.transfer_event.completion;
    }
}

int uhi_reset_ep(usb_host_interface_t *uhc, int epno, int64_t timeout) {
    xhci_trb_t response = trb_create(0);

    xhci_t *self = uhc->context;
    int slot_id = uhc->slot_id;

    xhci_trb_t trb = trb_create(TRB_RESET_ENDPOINT_COMMAND);
    trb.transfer_event.slot_id = slot_id;
    trb.transfer_event.endpoint_id = epno;
    execute_command(self, uhc->semaphore, &trb, &response, timeout);

    return response.cce.completion;
}

int uhi_get_max_packet_size(usb_host_interface_t *uhc) {
    xhci_t *self = uhc->context;
    int slot_id = uhc->slot_id;

    uint64_t device_context = self->DCBAA[slot_id];
    xhci_endpoint_ctx_data_t *ep = MOE_PA2VA(device_context + self->context_size);

    return ep->max_packet_size;
}

int uhi_set_max_packet_size(usb_host_interface_t *uhc, int max_packet_size, int64_t timeout) {
    xhci_trb_t response = trb_create(0);
    xhci_t *self = uhc->context;
    int slot_id = uhc->slot_id;

    uint64_t input_context = self->usb_devices[slot_id].input_context;
    xhci_slot_ctx_data_t *slot = MOE_PA2VA(input_context + self->context_size);
    xhci_input_control_ctx_t *icc = MOE_PA2VA(input_context);
    memset(icc, 0, self->context_size);
    icc->add = 3;
    // icc->drop = 0;

    uint64_t device_context = self->DCBAA[slot_id];
    void *q = MOE_PA2VA(device_context);
    memcpy(slot, q, self->context_size * 2);

    xhci_endpoint_ctx_data_t *ep = MOE_PA2VA(input_context + self->context_size * 2);
    ep->max_packet_size = max_packet_size;

    xhci_trb_t trb = trb_create(TRB_EVALUATE_CONTEXT_COMMAND);
    trb.transfer_event.ptr = input_context;
    trb.transfer_event.slot_id = slot_id;
    trb.transfer_event.endpoint_id = 1;
    execute_command(self, uhc->semaphore, &trb, &response, timeout);

    return response.cce.completion;
}

int uhi_control(usb_host_interface_t *uhc, int dci, int trt, urb_setup_data_t setup_data, uintptr_t buffer, int64_t timeout) {
    xhci_trb_t response = trb_create(0);
    xhci_t *self = uhc->context;
    int slot_id = uhc->slot_id;

    int has_data = (buffer != 0) && (setup_data.setup.wLength != 0);

    xhci_trb_t setup = trb_create(TRB_SETUP);
    setup.u32[0] = setup_data.u32[0];
    setup.u32[1] = setup_data.u32[1];
    setup.setup.trb_xfer_len = 8;
    setup.setup.TRT = trt;
    setup.setup.IDT = 1;
    xhci_write_transfer(self, NULL, slot_id, dci, &setup, 0);

    if (has_data) {
        xhci_trb_t data = trb_create(TRB_DATA);
        data.normal.ptr = buffer;
        data.normal.trb_xfer_len = setup_data.setup.wLength;
        data.normal.td_size = 0;
        data.normal.DIR = (trt == URB_TRT_CONTROL_IN);
        // data.normal.IOC = 1;
        // data.normal.ISP = 1;
        xhci_write_transfer(self, NULL, slot_id, dci, &data, 0);
    }

    xhci_trb_t status = trb_create(TRB_STATUS);
    status.normal.DIR = (trt != URB_TRT_CONTROL_IN);
    status.normal.IOC = 1;
    schedule_transfer(self, uhc->semaphore, slot_id, dci, &status, &response, 1, timeout);

    // return response.cce.completion;
    switch (response.transfer_event.completion) {
        case TRB_CC_NULL:
            return -1;
        case TRB_CC_SUCCESS:
            return setup_data.setup.wLength - response.transfer_event.length;
        default:
            return -response.transfer_event.completion;
    }
}

int uhi_data_transfer(usb_host_interface_t *uhc, int dci, uintptr_t buffer, uint16_t length, int64_t timeout) {
    xhci_trb_t response = trb_create(0);
    xhci_t *self = uhc->context;
    int slot_id = uhc->slot_id;

    xhci_trb_t trb = trb_create(TRB_NORMAL);
    trb.normal.ptr = buffer;
    trb.normal.trb_xfer_len = length;
    trb.normal.IOC = 1;
    trb.normal.ISP = 1;
    schedule_transfer(self, uhc->semaphore, slot_id, dci, &trb, &response, 1, timeout);

    switch (response.transfer_event.completion) {
        case TRB_CC_NULL:
            return -1;
        case TRB_CC_SUCCESS:
        case TRB_CC_SHORT_PACKET:
            return length - response.transfer_event.length;
        default:
            return -response.transfer_event.completion;
    }
}


int xhci_init_dev(xhci_t *self) {

    self->uhci.context = self;
    self->uhci.configure_ep = uhi_configure_ep;
    self->uhci.reset_ep = uhi_reset_ep;
    self->uhci.get_max_packet_size = uhi_get_max_packet_size;
    self->uhci.set_max_packet_size = uhi_set_max_packet_size;
    self->uhci.control = uhi_control;
    self->uhci.data_transfer = uhi_data_transfer;

    self->cap = MOE_PA2VA(self->base_address);
    uint8_t CAP_LENGTH = self->cap->caplength & 0xFF;
    MOE_PHYSICAL_ADDRESS base_opr = self->base_address + CAP_LENGTH;
    self->opr = MOE_PA2VA(base_opr);
    self->base_portsc = base_opr + 0x400;
    self->DB = MOE_PA2VA(self->base_address + (self->cap->dboff & ~3));
    self->base_rts = self->base_address + (self->cap->rtsoff & ~31);

    uint32_t pagesize_bitmap = self->opr->pagesize & 0xFFFF;
    self->min_pagesize = 1 << (12 + __builtin_ctz(pagesize_bitmap));

    uint32_t HCCPARAMS1 = self->cap->hccparams1;
    self->context_size = (HCCPARAMS1 & USB_HCCP_CSZ) ? 64 : 32;
    uint32_t xecp_ptr = (HCCPARAMS1 >> 16) << 2;
    uint64_t xecp_base = self->base_address + xecp_ptr;
    while (xecp_ptr) {
        uint32_t xecp = READ_PHYSICAL_UINT32(xecp_base);
        switch (xecp & 0xFF) {
            case 0x01: // USB legacy support
                WRITE_PHYSICAL_UINT32(xecp_base, xecp | USBLEGSUP_OS_OWNED);
                while (READ_PHYSICAL_UINT32(xecp_base) & USBLEGSUP_BIOS_OWNED) {
                    moe_usleep(10000);
                }
                uint32_t data = READ_PHYSICAL_UINT32(xecp_base + 4);
                data = (data & 0x000E1FEE) | 0xE0000000;
                WRITE_PHYSICAL_UINT32(xecp_base + 4, data);
                break;
        }
        xecp_ptr = ((xecp >> 8) & 255) << 2;
        xecp_base += xecp_ptr;
    }

    // reset xHCI
    uint32_t sts = self->opr->usbsts;
    moe_assert(sts & USB_STS_HCH, "USBSTS.HCH");
    self->opr->usbcmd = USB_CMD_HCRST;
    moe_usleep(10000);
    while ((self->opr->usbcmd & USB_CMD_HCRST) || (self->opr->usbsts & USB_STS_CNR)) {
        moe_usleep(10000);
    }

    uint32_t HCSPARAMS1 = self->cap->hcsparams1;
    int max_dev_slot = HCSPARAMS1 & 0xFF;
    self->max_port = (HCSPARAMS1 >> 24) & 255;

    // Scratch Pad
    uint32_t HCSPARAMS2 = self->cap->hcsparams2;
    uint32_t scrpad_size = ((HCSPARAMS2 >> 27) & 0x1F) | (((HCSPARAMS2 >> 21) & 0x1F) << 5);
    uintptr_t SCRPAD_PA = 0;
    if (scrpad_size) {
        size_t scrpad_raw_size = scrpad_size * self->min_pagesize;
        SCRPAD_PA = moe_alloc_physical_page(scrpad_raw_size);
        memset(MOE_PA2VA(SCRPAD_PA), 0, scrpad_raw_size);
    }

    // Device Context Base Address Array
    self->max_dev_slot = MIN(max_dev_slot, MAX_SLOTS);
    size_t size_dcbaa = (1 + self->max_dev_slot) * 8;
    self->usb_devices = moe_alloc_object(sizeof(usb_device_context), 1 + self->max_dev_slot);
    uintptr_t DCBAA_PA = moe_alloc_physical_page(size_dcbaa);
    self->DCBAA = MOE_PA2VA(DCBAA_PA);
    memset(self->DCBAA, 0, size_dcbaa);
    self->DCBAA[0] = SCRPAD_PA;
    self->opr->dcbaap = DCBAA_PA;
    self->opr->config = self->max_dev_slot;

    // Command Ring
    uintptr_t CR_PA = alloc_ep_ring(self, 0, 0);
    self->opr->crcr = CR_PA;

    // Event Ring Segment Table
    self->event_cycle = 1;
    uintptr_t ERS_PA = moe_alloc_physical_page(4096);
    self->ERS0 = MOE_PA2VA(ERS_PA);
    memset(self->ERS0, 0, 4096);
    uintptr_t ERST_PA = moe_alloc_physical_page(4096);
    xhci_erste_t *erst = MOE_PA2VA(ERST_PA);
    xhci_erste_t erst0 = { ERS_PA, SIZE_EVENT_RING };
    erst[0] = erst0;
    WRITE_PHYSICAL_UINT32(self->base_rts + 0x28, SIZE_ERST);
    WRITE_PHYSICAL_UINT64(self->base_rts + 0x38, ERS_PA);
    WRITE_PHYSICAL_UINT64(self->base_rts + 0x30, ERST_PA);

    // interrupt
    uint32_t msi_cap = pci_find_capability(self->pci_base, PCI_CAP_MSI);
    if (msi_cap) {
        WRITE_PHYSICAL_UINT32(self->base_rts + 0x24, 4000);
        WRITE_PHYSICAL_UINT32(self->base_rts + 0x20, USB_IMAN_IP | USB_IMAN_IE);
        _set_usbcmd(self, USB_CMD_INTE);

        uint32_t pci_msi = self->pci_base + msi_cap;
        self->irq = moe_install_msi(xhci_msi_handler);
        uint64_t msi_addr;
        uint32_t msi_data;
        moe_make_msi_data(self->irq, 0xC, &msi_addr, &msi_data);
        pci_write_config(pci_msi + 4, msi_addr);
        pci_write_config(pci_msi + 8, msi_addr >> 32);
        pci_write_config(pci_msi + 12, msi_data);

        uint32_t data = pci_read_config(pci_msi);
        data &= 0xFF8FFFFF;
        data |= 0x00010000;
        pci_write_config(pci_msi, data);
    }

    // start xHCI
    wait_cnr(self, 0);
    _set_usbcmd(self, USB_CMD_RS);
    while (self->opr->usbsts & USB_STS_HCH) {
        moe_usleep(1000);
    }

    // Reset all ports (qemu)
    for (int i = 1; i <= self->max_port; i++) {
        xhci_reset_port(self, i);
    }

    return 0;
}


void process_event(xhci_t *self) {

    int cycle = self->event_cycle;
    for (;;) {
        _Atomic MOE_PHYSICAL_ADDRESS *ERDP = MOE_PA2VA(self->base_rts + 0x38);
        MOE_PHYSICAL_ADDRESS er = atomic_load(ERDP);
        xhci_trb_t *trb = MOE_PA2VA(er & ~15);
        if (trb->common.C != self->event_cycle) break;
        int trb_type = trb->common.type;
        // DEBUG_PRINT("\n<TRB %08x %d>", (uint32_t)er, trb_type);
        switch (trb_type) {
            case TRB_PORT_STATUS_CHANGE_EVENT:
            {
                int port_id = trb->psc.port_id;
                _Atomic uint32_t *portsc = MOE_PA2VA(get_portsc(self, port_id));
                DEBUG_PRINT("PSC(%d %d %08x)", port_id, trb->psc.completion, atomic_load(portsc));

                moe_queue_write(self->port_change_queue, port_id);
                moe_sem_signal(self->sem_request);
            }
                break;

            case TRB_COMMAND_COMPLETION_EVENT:
            {
                uint8_t cc = trb->cce.completion;
                xhci_trb_t *command = MOE_PA2VA(trb->cce.ptr);
                DEBUG_PRINT("CCE(%d %d %d)", trb->cce.slot_id, command->common.type, cc);

                usb_request_block_t *urb = xhci_find_urb_by_trb(self, command, urb_state_scheduled);
                if (urb) {
                    urb_state_t desired = (cc == TRB_CC_SUCCESS) ? urb_state_completed : urb_state_failed;
                    urb->response = *trb;
                    urb->state = desired;
                    moe_sem_signal(urb->semaphore);
                }
            }
                break;

            case TRB_TRANSFER_EVENT:
            {
                int cc = trb->transfer_event.completion;
                xhci_trb_t *command = MOE_PA2VA(trb->transfer_event.ptr);
                // DEBUG_PRINT("XFE(%d %d %d %d %d)", trb->transfer_event.slot_id, trb->transfer_event.endpoint_id, command->common.type, cc, trb->transfer_event.length);

                usb_request_block_t *urb = xhci_find_urb_by_trb(self, command, urb_state_scheduled);
                if (urb) {
                    urb_state_t desired = (cc == TRB_CC_SUCCESS) ? urb_state_completed : urb_state_failed;
                    urb->response = *trb;
                    urb->state = desired;
                    moe_sem_signal(urb->semaphore);
                }
            }
                break;

            default:
                DEBUG_PRINT("UNHANDLED TRB (%2d) [%08x %08x %08x %08x]\n", trb->common.type, trb->u32[0], trb->u32[1], trb->u32[2], trb->u32[3]);
                break;
        }
        MOE_PHYSICAL_ADDRESS er_base = er & ~0xFFF;
        int index = (er - er_base) / sizeof(xhci_trb_t);
        index++;
        if (index == SIZE_EVENT_RING) {
            index = 0;
            cycle ^= 1;
            self->event_cycle = cycle;
        }
        atomic_store(ERDP, (er & ~0xFF0) | (index * sizeof(xhci_trb_t)) | 8);
    }
}


// xHCI Event Thread
_Noreturn void xhci_event_thread(void *args) {
    xhci_t *self = args;

    xhci_init_dev(self);

#ifdef DEBUG
    uint8_t SBRN = pci_read_config(self->pci_base + 0x60);
    uint16_t ver = self->cap->caplength >> 16;
    uint32_t HCSPARAMS1 = self->cap->hcsparams1;
    // uint32_t HCSPARAMS2 = self->cap->hcsparams2;
    int MAX_DEV_SLOT = HCSPARAMS1 & 255;
    int MAX_INT = (HCSPARAMS1 >> 8) & 0x7FF;
    // uint32_t pagesize = self->opr->pagesize & 0xFFFF;
    // uint32_t scrpad = HCSPARAMS2 >> 21;
    DEBUG_PRINT("xHC v%d.%d.%d USB v%d.%d BASE %012llx IRQ# %d SLOT %d/%d INT %d PORT %d\n"
        "# USB DEBUG MODE\n",
        (ver >> 8), (ver >> 4) & 15, (ver & 15), (SBRN >> 4) & 15, SBRN & 15,
        self->base_address, self->irq,
        self->max_dev_slot, MAX_DEV_SLOT, MAX_INT, self->max_port);
#endif

    moe_create_thread(&xhci_request_thread, priority_normal, &xhci, "xhci.request");

    for (;;) {
        if (!moe_sem_wait(self->sem_event, MOE_FOREVER)) {
            process_event(self);
        }
    }
}


static int port_initialize(xhci_t *self, int port_id) {
    const int64_t timeout = 3000000;
    const int64_t csc_timeout = 100000;

    _Atomic uint32_t *portsc = MOE_PA2VA(get_portsc(self, port_id));
    wait_cnr(self, 0);
    uint32_t port_status = atomic_load(portsc);
    if (port_status & USB_PORTSC_CSC) {
        int attached = port_status & USB_PORTSC_CCS;
        // DEBUG_PRINT("CSC(%d %d %08x)", port_id, attached, port_status);
        if (attached) {
            atomic_store(portsc, (port_status & PORTSC_MAGIC_WORD) | USB_PORTSC_CSC | USB_PORTSC_PR);
            moe_measure_t deadline = moe_create_measure(csc_timeout);
            while (moe_measure_until(deadline) && (atomic_load(portsc) & USB_PORTSC_PED) == 0) {
                cpu_relax();
            }
            port_status = atomic_load(portsc);
            if (port_status & USB_PORTSC_PRC) {
                atomic_store(portsc, (port_status & PORTSC_MAGIC_WORD) | USB_PORTSC_PRC);
            }
            if (
                (port_status & USB_PORTSC_PR) != 0
                || (port_status & USB_PORTSC_PED) == 0
            ) {
                // port reset failed
                DEBUG_PRINT("[PORT RESET TIMED_OUT %d %08x]", port_id, port_status);
                return 0;
            }

            // moe_usleep(20000);
            xhci_trb_t cmd = trb_create(TRB_ENABLE_SLOT_COMMAND);
            xhci_trb_t result;
            int status = execute_command(self, self->sem_urb, &cmd, &result, timeout);
            if (status) {
                DEBUG_PRINT("\n[ENABLE SLOT on PORT %d TIMED_OUT]", port_id);
                return 0;
            }
            int slot_id = result.cce.slot_id;
            self->port2slot[port_id] = slot_id;
            // DEBUG_PRINT("\n[ENABLE SLOT %d PORT %d CC %d]", slot_id, port_id, result.cce.completion);

            usb_device_context *usb_device = &self->usb_devices[slot_id];

            size_t size_device_context = self->context_size * 32;
            uint64_t device_context = moe_alloc_physical_page(size_device_context);
            memset(MOE_PA2VA(device_context), 0, size_device_context);
            self->DCBAA[slot_id] = device_context;

            size_t size_input_context = self->context_size * 33;
            uint64_t input_context = moe_alloc_physical_page(size_input_context);
            xhci_input_control_ctx_t *icc = MOE_PA2VA(input_context);
            memset(icc, 0, size_input_context);
            usb_device->input_context = input_context;

            xhci_slot_ctx_data_t *slot = MOE_PA2VA(input_context + self->context_size);
            slot->root_hub_port_no = port_id;
            slot->speed = atomic_load(portsc) >> 10;
            slot->context_entries = 1;

            configure_endpoint(self, slot_id, 1, 4, 0, 0, 0);

            // moe_usleep(20000);
            xhci_trb_t adc = trb_create(TRB_ADDRESS_DEVICE_COMMAND);
            adc.address_device_command.ptr = input_context;
            adc.address_device_command.slot_id = slot_id;
            status = execute_command(self, self->sem_urb, &adc, &result, timeout);
            if (status) {
                DEBUG_PRINT("\n[ADDRESS DEVICE SLOT %d PORT %d TIMED_OUT]", slot_id, port_id);
                return 0;
            } else {
                // DEBUG_PRINT("\n[ADDRESS DEVICE SLOT %d PORT %d CC %d]", slot_id, port_id, result.cce.completion);
            }

            return slot_id;

        } else {
            if (self->port2slot[port_id]) {
                atomic_store(portsc, (port_status & PORTSC_MAGIC_WORD) | USB_PORTSC_CSC);
                xhci_trb_t cmd = trb_create(TRB_DISABLE_SLOT_COMMAND);
                xhci_trb_t result;
                int slot_id = self->port2slot[port_id];
                cmd.enable_slot_command.slot_id = slot_id;
                int status = execute_command(self, self->sem_urb, &cmd, &result, timeout);
                self->port2slot[port_id] = 0;
                self->DCBAA[slot_id] = 0;
                if (status) {
                    DEBUG_PRINT("\n[DISABLE SLOT PORT %d TIMED_OUT]", port_id);
                } else {
                    DEBUG_PRINT("\n[DISABLE SLOT %d PORT %d CC %d]", cmd.enable_slot_command.slot_id, port_id, result.cce.completion);
                }
            }
            // uint32_t port_status = atomic_load(portsc);
            // DEBUG_PRINT("DISC(%d %08x)", port_id, port_status);
        }
    }
    return 0;
}


// xHCI Request Processing Thread
_Noreturn void xhci_request_thread(void *args) {
    xhci_t *self = args;

    const int64_t timeout = 1000000;
    for (;;) {
        moe_sem_wait(self->sem_request, timeout);

        int port_id;
        do {
            port_id = moe_queue_read(self->port_change_queue, 0);
            if (port_id > 0) {
                int slot_id = port_initialize(self, port_id);
                if (slot_id > 0) {
                    usb_new_device(&self->uhci, slot_id);
                }
            }
        } while (port_id > 0);

    }
}


void xhci_init() {
    uint32_t base = pci_find_by_class(PCI_CLS_XHCI, PCI_CLS_INTERFACE);
    if (base) {
        uint64_t bar, bar_limit;
        int n = pci_parse_bar(base, 0, &bar, &bar_limit);
        // moe_assert(n, "INVALID BAR");
        // moe_assert((bar & 7) == 4, "UNEXPECTED BAR");
        if (!n) return;

        pg_map_mmio(bar & ~0xF, bar_limit);
        xhci.base_address = bar & ~0xF;
        xhci.bar_limit = bar_limit;
        xhci.pci_base = base;

        xhci.sem_event = moe_sem_create(0);
        xhci.sem_request = moe_sem_create(0);
        xhci.sem_urb = moe_sem_create(0);
        xhci.port_change_queue = moe_queue_create(MAX_PORT_CHANGE);
        xhci.urbs = moe_alloc_object(sizeof(usb_request_block_t), MAX_URB);

        moe_create_thread(&xhci_event_thread, priority_realtime, &xhci, "xhci.event");

#ifdef DEBUG
    for (;;) io_hlt();
#endif
    }
}
