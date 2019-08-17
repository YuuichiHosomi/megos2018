// xHCI: Extensible Host Controller Interface
// Copyright (c) 2019 MEG-OS project, All rights reserved.
// License: MIT
#include "moe.h"
#include "kernel.h"
#include <stdatomic.h>
#include "usb.h"


#define MAX_SLOTS           64
#define EVENT_QUEUE_SIZE    64
#define EVENT_MSI_VALUE     1

#define MAX_TR              256
#define MAX_TR_INDEX        16
#define SIZE_COMMAND_RING   16
#define SIZE_EVENT_RING     16
#define SIZE_ERST           1


/*********************************************************************/

#define PCI_CLS_XHCI    0x0C033000
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
};

enum {
    TRB_CC_SUCCESS = 1,
    TRB_CC_DATA_BUFFER_ERROR,
    TRB_CC_BABBLE_ERROR,
    TRB_CC_USB_TRANSACTION_ERROR,
    TRB_CC_TRB_ERROR,
    TRB_CC_STALL_ERROR,
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
    _Atomic uintptr_t lock;
    uint64_t tr_base;
    int slot_id, epno, index, pcs;
} ring_context;


typedef struct {
    uint64_t input_context;
} usb_device_context;


/*********************************************************************/


typedef struct xhci_t {
    usb_host_controller_t uhci;

    moe_queue_t *event_queue;

    ring_context tr_ctx[MAX_TR];

    MOE_PHYSICAL_ADDRESS base_address, base_opr, base_rts;

    uint8_t port2slot[256];
    _Atomic uint64_t wait_for_allocate_port;
    _Atomic uint32_t *DB;
    _Atomic uint64_t *DCBAA;
    usb_device_context *usb_devices;
    xhci_trb_t *CR;
    xhci_trb_t *ERS0;

    uint32_t pci_base;
    uint32_t max_dev_slot, max_port, event_cycle, context_size;
    int irq;
} xhci_t;

xhci_t xhci;

int xhci_init_dev(xhci_t *self);


void xhci_msi_handler(int irq) {
    moe_queue_write(xhci.event_queue, EVENT_MSI_VALUE);
}


static xhci_trb_t trb_create(int type) {
    xhci_trb_t result;
    memset(&result, 0, sizeof(xhci_trb_t));
    result.common.type = type;
    return result;
}


static void wait_cnr(xhci_t *self, int us) {
    while (READ_PHYSICAL_UINT32(self->base_opr + 4) & USB_STS_CNR) {
        io_pause();
    }
}


static uintptr_t get_portsc(xhci_t *self, int port_id) {
    moe_assert(port_id, "PORT ID IS NOT BE NULL");
    uintptr_t portsc = self->base_opr + 0x400 + (port_id - 1) * 16;
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


uint32_t xhci_reset_port(xhci_t *self, int port_id) {
    uintptr_t portsc = get_portsc(self, port_id);
    wait_cnr(self, 0);
    uint32_t status = READ_PHYSICAL_UINT32(portsc);
    uint32_t ccs_csc = USB_PORTSC_CCS | USB_PORTSC_CSC;
    if ((status & ccs_csc) == ccs_csc) {
        WRITE_PHYSICAL_UINT32(portsc, (status & 0x0e00c3e0) | USB_PORTSC_CSC | USB_PORTSC_PR);
        wait_cnr(self, 0);
        while (READ_PHYSICAL_UINT32(portsc) & USB_PORTSC_PR) {
            moe_usleep(10000);
        }
    }
    return READ_PHYSICAL_UINT32(portsc);
}


static void copy_trb(xhci_trb_t *buffer, xhci_trb_t *trb, int cycle) {
    buffer->u32[0] = trb->u32[0];
    buffer->u32[1] = trb->u32[1];
    buffer->u32[2] = trb->u32[2];
    buffer->u32[3] = (trb->u32[3] & 0xFFFFFFFE) | cycle;
}


void xhci_write_transfer(xhci_t *self, int slot_id, int epno, xhci_trb_t *trb, int doorbell) {
    ring_context *ctx = find_ep_ring(self, slot_id, epno);
    for (;;) {
        uintptr_t expected = 0;
        uintptr_t desired = moe_get_current_thread_id();
        if (atomic_compare_exchange_weak(&ctx->lock, &expected, desired)) {
            int pcs = ctx->pcs;
            uint64_t tr_base = ctx->tr_base;
            int index = ctx->index;
            xhci_trb_t *tr = MOE_PA2VA(tr_base);

            copy_trb(tr + index, trb, pcs);

            index++;
            if (index >= MAX_TR_INDEX - 1) {
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

            ctx->lock = 0;
            return;
        } else {
            moe_assert(expected != desired, "CONFLICT THREAD %d\n", desired);
            moe_assert(false, "RING CONFLICT %d %d\n", slot_id, epno);
            moe_usleep(1);
        }
    }
}


static void xhci_write_command(xhci_t *self, xhci_trb_t *trb) {
    xhci_write_transfer(self, 0, 0, trb, 1);
}


int xhci_init_dev(xhci_t *self) {

    uint8_t CAP_LENGTH = READ_PHYSICAL_UINT32(self->base_address) & 0xFF;
    self->base_opr = self->base_address + CAP_LENGTH;
    self->DB = MOE_PA2VA(self->base_address + (READ_PHYSICAL_UINT32(self->base_address + 0x14) & ~3));
    self->base_rts = self->base_address + (READ_PHYSICAL_UINT32(self->base_address + 0x18) & ~31);

    uint64_t USBCMD = self->base_opr;
    uint64_t USBSTS = self->base_opr + 4;

    uint32_t HCCPARAMS1 = READ_PHYSICAL_UINT32(self->base_address + 0x10);
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
    uint32_t sts = READ_PHYSICAL_UINT32(USBSTS);
    moe_assert(sts & USB_STS_HCH, "USBSTS.HCH");
    WRITE_PHYSICAL_UINT32(USBCMD, USB_CMD_HCRST);
    moe_usleep(10000);
    while ((READ_PHYSICAL_UINT32(USBCMD) & USB_CMD_HCRST) || (READ_PHYSICAL_UINT32(USBSTS) & USB_STS_CNR)) {
        moe_usleep(10000);
    }


    uint32_t HCSPARAMS1 = READ_PHYSICAL_UINT32(self->base_address + 4);
    int max_dev_slot = HCSPARAMS1 & 0xFF;
    self->max_port = (HCSPARAMS1 >> 24) & 255;

    // Device Context Base Address Array
    self->max_dev_slot = MIN(max_dev_slot, MAX_SLOTS);
    size_t size_dcbaa = (1 + self->max_dev_slot) * 8;
    self->usb_devices = moe_alloc_object(sizeof(usb_device_context), 1 + self->max_dev_slot);
    uintptr_t DCBAA_PA = moe_alloc_physical_page(size_dcbaa);
    self->DCBAA = MOE_PA2VA(DCBAA_PA);
    memset(self->DCBAA, 0, size_dcbaa);
    WRITE_PHYSICAL_UINT64(self->base_opr + 0x30, DCBAA_PA);
    WRITE_PHYSICAL_UINT32(self->base_opr + 0x38, self->max_dev_slot);

    // Command Ring
    uintptr_t CR_PA = alloc_ep_ring(self, 0, 0);
    WRITE_PHYSICAL_UINT64(self->base_opr + 0x18, CR_PA);

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
        WRITE_PHYSICAL_UINT32(USBCMD, READ_PHYSICAL_UINT32(USBCMD) | USB_CMD_INTE);

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
    WRITE_PHYSICAL_UINT32(USBCMD, READ_PHYSICAL_UINT32(USBCMD) | USB_CMD_RS);
    while (READ_PHYSICAL_UINT32(USBSTS) & USB_STS_HCH) {
        moe_usleep(10000);
    }

    for (int i = 1; i <= self->max_port; i++) {
        xhci_reset_port(self, i);
    }

    return 0;
}


void process_event(xhci_t *self) {
    wait_cnr(self, 0);
    uint64_t ERDP = READ_PHYSICAL_UINT64(self->base_rts + 0x38);
    xhci_trb_t *er = MOE_PA2VA(ERDP & ~0xF);
    xhci_trb_t *er_base = (xhci_trb_t*)((uintptr_t)er & ~0xFFF);
    uintptr_t index = er - er_base;
    int cycle = self->event_cycle;

    for (;;) {
        xhci_trb_t *trb = er_base + index;
        if (trb->common.C != self->event_cycle) break;
        int trb_type = trb->common.type;
        // printf("(%d %zd %d) ", cycle, index, trb_type);
        switch (trb_type) {
            // case TRB_PORT_STATUS_CHANGE_EVENT:
            //     on_port_status_change(self, trb);
            //     break;

            // case TRB_COMMAND_COMPLETION_EVENT:
            //     if (on_command_complete(self, trb)) {
            //         usb_signal_device(trb->cce.slot_id, trb->cce.parameter, trb->cce.completion);
            //     }
            //     break;

            // case TRB_TRANSFER_EVENT:
            //     if (on_transfer_event(self, trb)) {
            //         break;
            //     }
            //     break;

            default:
                printf("UNHANDLED TRB [%08x %08x %08x %08x]\n", trb->u32[0], trb->u32[1], trb->u32[2], trb->u32[3]);
                break;
        }
        index++;
        if (index >= SIZE_EVENT_RING) {
            index = 0;
            cycle ^= 1;
            self->event_cycle = cycle;
        }
    }
    ERDP = (ERDP & ~0xFF3) | (index * sizeof(xhci_trb_t)) | 8;
    WRITE_PHYSICAL_UINT64(self->base_rts + 0x38, ERDP);
}


// xHCI Main Thread
_Noreturn void xhci_event_thread(void *args) {
    xhci_t *self = args;

    xhci_init_dev(self);

    uint8_t SBRN = pci_read_config(self->pci_base + 0x60);
    uint16_t ver = READ_PHYSICAL_UINT32(self->base_address) >> 16;
    uint32_t HCSPARAMS1 = READ_PHYSICAL_UINT32(self->base_address + 4);
    int MAX_DEV_SLOT = HCSPARAMS1 & 255;
    int MAX_INT = (HCSPARAMS1 >> 8) & 0x7FF;
    uint32_t pagesize = READ_PHYSICAL_UINT32(self->base_opr + 0x08) & 0xFFFF;
    printf("xHCI v%d.%d.%d USB v%d.%d BASE %012llx IRQ %d SLOT %d/%d MAX_INT %d MAX_PORT %d CSZ %d PGSZ %04x\n"
        "# USB TEST MODE #\n",
        (ver >> 8), (ver >> 4) & 15, (ver & 15), (SBRN >> 4) & 15, SBRN & 15,
        self->base_address, self->irq,
        self->max_dev_slot, MAX_DEV_SLOT, MAX_INT, self->max_port, self->context_size, pagesize);

    if (1) {
        xhci_trb_t nop = trb_create(TRB_NOP_COMMAND);
        xhci_write_command(self, &nop);
    }


    for (;;) {
        moe_usleep(100000);
        intptr_t value;
        if (moe_queue_wait(self->event_queue, &value, 100000)) {
            process_event(self);
        }
    }
}


void xhci_init() {
    uint32_t base = pci_find_by_class(PCI_CLS_XHCI, PCI_CLS_INTERFACE);
    if (base) {
        uint64_t bar, bar_limit;
        int n = pci_parse_bar(base, 0, &bar, &bar_limit);
        moe_assert(n, "INVALID BAR");
        // moe_assert((bar & 7) == 4, "UNEXPECTED BAR");

        void *p = pg_map_mmio(bar & ~0xF, bar_limit);
        printf("XHCI: map mmio [%012llx %08llx] => [%p]\n", bar, bar_limit, p);

        xhci.event_queue = moe_queue_create(EVENT_QUEUE_SIZE);
        xhci.base_address = bar & ~0xF;
        xhci.pci_base = base;

        moe_create_thread(xhci_event_thread, priority_realtime, &xhci, "xhci");
    }
}
