/* usb_a64.c — xHCI host controller + QEMU usb-tablet (HID boot, absolute) для
   QEMU virt (aarch64). Аналог kf_usb.c (UHCI на x86), те же FFI-семантики:
   k_usb_init / k_usb_poll / k_usb_tab_x|y|btn.

   Минимально достаточный xHCI: ECAM PCI, HCRST, command/event ring (поллинг,
   без MSI-X), Enable Slot -> Address Device -> GET_DESCRIPTOR (config) ->
   CONFIG_EP для interrupt IN, SET_PROTOCOL(boot). Отчёт планшета:
   [buttons, x_lo, x_hi, y_lo, y_hi] 0..0x7FFF — как на x86.

   ponytail: один контроллер, одно устройство, один endpoint; без scratchpad,
   без потоков, без MSI. Для реального железа (этап 3+) — dma-coherency,
   scratchpad из HCSPARAMS2, hot-plug через Port Status Change.
*/
#include "kf_rt.h"

void k_arch_uart_puts(const char* s);

/* --- PCI ECAM (QEMU virt: 0x3f000000, внутри identity-map окна;
       0x10000000 — это окно PCI MMIO, куда кладут BAR'ы) --- */
#define ECAM_BASE 0x3f000000ull

static uint32_t pci_cfg32(int bus, int dev, int fn, int off) {
    return mmio_read32(ECAM_BASE + (((uint32_t)bus << 20) | ((uint32_t)dev << 15)
                       | ((uint32_t)fn << 12) | (uint32_t)(off & 0xFFC)));
}
static void pci_cfg32w(int bus, int dev, int fn, int off, uint32_t v) {
    mmio_write32(ECAM_BASE + (((uint32_t)bus << 20) | ((uint32_t)dev << 15)
                 | ((uint32_t)fn << 12) | (uint32_t)(off & 0xFFC)), v);
}

/* --- xHCI register offsets --- */
#define X_CAPLENGTH  0x00
#define X_HCSPARAMS1 0x04
#define X_HCSPARAMS2 0x08
#define X_HCCPARAMS1 0x0C
#define X_DBOFF      0x14
#define X_RTSOFF     0x18

#define OP_USBCMD   0x00
#define OP_USBSTS   0x04
#define OP_DNCTRL   0x14
#define OP_CRCR     0x18
#define OP_DCBAAP   0x30
#define OP_CONFIG   0x38
#define OP_PORTSC   0x400

#define RT_IMAN     0x20
#define RT_ERSTSZ   0x28
#define RT_ERSTBA   0x30
#define RT_ERDP     0x38

/* --- TRB --- */
#define TRB_LINK        6
#define TRB_SETUP       2
#define TRB_DATA        3
#define TRB_STATUS      4
#define TRB_NORMAL      1
#define TRB_ENABLE_SLOT 9
#define TRB_ADDRESS_DEV 11
#define TRB_CONFIG_EP   12
#define TRB_EVALUATE_CTX 13
#define TRB_TRANSFER_EV 32
#define TRB_CMD_CMPL    33
#define TRB_PORT_EV     34

#define TRB_TYPE_SHIFT 10
#define TRB_CYCLE      1u
#define TRB_IOC        (1u << 5)
#define TRB_IDT        (1u << 6)
#define TRB_TC         (1u << 1)

#define ER_SUCCESS 1
#define DSB() __asm__ __volatile__("dsb sy" ::: "memory")

/* --- EP types (xHCI) --- */
#define EP_TYPE_CTRL   4
#define EP_TYPE_INT_IN 6

/* --- состояние --- */
typedef struct { uint32_t p[2]; uint32_t status; uint32_t ctrl; } trb_t;

static volatile uint32_t* op;   /* operational regs base (VA=identity PA) */
static volatile uint32_t* rts;  /* runtime regs base */
static volatile uint32_t* db;   /* doorbells */
static volatile uint32_t* portsc0;

static trb_t*   cmd_ring;   static uint64_t cmd_ring_pa;
static int      cmd_i, cmd_cycle;
static trb_t*   ev_ring;    static uint64_t ev_ring_pa;
static int      ev_i;       static uint8_t ev_cycle;
static uint64_t erst_pa;
static uint64_t dcbaa_pa, devctx_pa, inctx_pa;
static volatile uint32_t* devctx_va;

static trb_t*   ep0_ring;   static uint64_t ep0_ring_pa;
static int      ep0_i, ep0_cycle;
static trb_t*   in_ring;    static uint64_t in_ring_pa;
static int      in_i, in_cycle;

static uint8_t* rep_buf;    static uint64_t rep_buf_pa;   /* отчёты планшета */
#define REP_N 4
#define REP_SZ 8
static uint64_t rep_trb_pa[REP_N];

static uint8_t  usb_slot = 0;
static uint8_t  usb_ok = 0, tab_present = 0;
static int      tab_x = 400, tab_y = 300, tab_btn = 0;
static uint32_t tab_w = 800, tab_h = 600;

static void ulog(const char* s) { k_arch_uart_puts(s); }

static void ulog_hx(uint64_t v) {
    const char* h = "0123456789abcdef";
    char out[19]; int n = 0;
    out[n++] = '0'; out[n++] = 'x';
    int started = 0;
    for (int i = 60; i >= 0; i -= 4) {
        int d = (int)((v >> i) & 0xF);
        if (d || started || i == 0) { out[n++] = h[d]; started = 1; }
    }
    out[n] = 0;
    ulog(out);
}

/* --- кольца --- */
static void trb_set(trb_t* t, uint64_t param, uint32_t status, uint32_t type, int cycle) {
    t->p[0] = (uint32_t)param;
    t->p[1] = (uint32_t)(param >> 32);
    t->status = status;
    t->ctrl = (type << TRB_TYPE_SHIFT) | (cycle ? TRB_CYCLE : 0);
}

/* push в кольцо с LINK на конце; возвращает PA записанного TRB.
   ctrl_extra — доп. биты control-dword (напр. slot id << 24 для слот-команд). */
static uint64_t ring_push(trb_t* ring, uint64_t ring_pa, int* i, int* cycle,
                          uint64_t param, uint32_t status, uint32_t type,
                          uint32_t ctrl_extra) {
    trb_t* t = ring + *i;
    trb_set(t, param, status, type, *cycle);
    t->ctrl |= ctrl_extra;
    uint64_t pa = ring_pa + (uint64_t)*i * 16;
    (*i)++;
    if (*i == 63) {   /* предпоследний слот занят — LINK на 63-м */
        trb_set(ring + 63, ring_pa, 0, TRB_LINK, *cycle);
        ring[63].ctrl |= TRB_TC;
        *i = 0;
        *cycle ^= 1;
    }
    return pa;
}

/* --- поллинг event ring; обрабатывает события, заполняет last_* --- */
static uint64_t last_event_param;
static uint32_t last_event_code;
static uint32_t last_event_ctrl;   /* slot id командного completion — ctrl[31:24] */
static uint8_t  got_transfer, got_command;

static void events_poll(void) {
    for (int n = 0; n < 32; n++) {
        trb_t* e = ev_ring + ev_i;
        if ((e->ctrl & TRB_CYCLE) != ev_cycle) return;
        last_event_param = (uint64_t)e->p[0] | ((uint64_t)e->p[1] << 32);
        uint32_t type = (e->ctrl >> TRB_TYPE_SHIFT) & 0x3F;
        last_event_code = (e->status >> 24) & 0xFF;
        if (type == TRB_CMD_CMPL) got_command = 1;
        if (type == TRB_TRANSFER_EV) got_transfer = 1;
        ev_i++;
        if (ev_i == 64) { ev_i = 0; ev_cycle ^= 1; }
    }
}

/* сдвинуть ERDP после поллинга */
static void erdp_sync(void) {
    volatile uint32_t* erdp = rts + (RT_ERDP / 4);
    uint64_t pa = ev_ring_pa + (uint64_t)ev_i * 16;
    erdp[0] = (uint32_t)pa | (1u << 3);   /* EHB */
    erdp[1] = (uint32_t)(pa >> 32);
}

static int wait_command(void) {
    int processed = 0;
    for (int spin = 0; spin < 20000000; spin++) {
        processed = 0;
        for (int n = 0; n < 32; n++) {
            trb_t* e = ev_ring + ev_i;
            if ((e->ctrl & TRB_CYCLE) != ev_cycle) break;
            last_event_param = (uint64_t)e->p[0] | ((uint64_t)e->p[1] << 32);
            uint32_t type = (e->ctrl >> TRB_TYPE_SHIFT) & 0x3F;
            last_event_code = (e->status >> 24) & 0xFF;
            last_event_ctrl = e->ctrl;
            if (type == TRB_CMD_CMPL) got_command = 1;
            if (type == TRB_TRANSFER_EV) got_transfer = 1;
            ev_i++;
            if (ev_i == 64) { ev_i = 0; ev_cycle ^= 1; }
            processed = 1;
        }
        if (processed) erdp_sync();
        if (got_command) {
            got_command = 0;
            return (int)last_event_code;
        }
    }
    return -1;
}

/* --- контрольная передача на EP0 через transfer ring --- */
static void ep0_doorbell(void) {
    DSB();
    db[usb_slot] = 1;   /* dci 1 = EP0 */
}

static int ctrl_xfer(uint8_t bmReqType, uint8_t bReq, uint16_t wV, uint16_t wI,
                     void* buf, uint16_t len, int dir_in) {
    uint8_t s[8];
    s[0] = bmReqType; s[1] = bReq;
    s[2] = (uint8_t)wV;  s[3] = (uint8_t)(wV >> 8);
    s[4] = (uint8_t)wI;  s[5] = (uint8_t)(wI >> 8);
    s[6] = (uint8_t)len; s[7] = (uint8_t)(len >> 8);
    uint64_t s_pa = (uint64_t)k_mem_virt_to_phys((int64_t)(uintptr_t)s);
    (void)s_pa;

    uint64_t setup_pa = ring_push(ep0_ring, ep0_ring_pa, &ep0_i, &ep0_cycle,
                                  (uint64_t)s[0] | ((uint64_t)s[1] << 8)
                                  | ((uint64_t)s[2] << 16) | ((uint64_t)s[3] << 24)
                                  | ((uint64_t)s[4] << 32) | ((uint64_t)s[5] << 40)
                                  | ((uint64_t)s[6] << 48) | ((uint64_t)s[7] << 56),
                                  8, TRB_SETUP, 0);
    ep0_ring[ep0_i ? ep0_i - 1 : 62].ctrl |= TRB_IDT;
    (void)setup_pa;

    uint64_t expect = 0;
    if (buf && len) {
        /* DMA-безопасный буфер всегда rep_buf (стек/статика не hhdm-линейны) */
        if (!dir_in) { uint8_t* d = (uint8_t*)buf; for (uint16_t i = 0; i < len; i++) rep_buf[i] = d[i]; }
        uint32_t dflags = (dir_in ? (1u << 16) : 0) | (1u << 5) | (1u << 2);   /* DIR + IOC + ISP */
        ring_push(ep0_ring, ep0_ring_pa, &ep0_i, &ep0_cycle, rep_buf_pa, len, TRB_DATA, dflags);
    }
    expect = ring_push(ep0_ring, ep0_ring_pa, &ep0_i, &ep0_cycle, 0, 0, TRB_STATUS, 0);
    ep0_ring[ep0_i ? ep0_i - 1 : 62].ctrl |= TRB_IOC | (dir_in ? 0 : (1u << 16));  /* статус: OUT для IN-передачи */

    ep0_doorbell();

    /* ждём transfer event на STATUS-TRB */
    for (int spin = 0; spin < 3000000; spin++) {
        int processed = 0;
        for (int n = 0; n < 32; n++) {
            trb_t* e = ev_ring + ev_i;
            if ((e->ctrl & TRB_CYCLE) != ev_cycle) break;
            last_event_param = (uint64_t)e->p[0] | ((uint64_t)e->p[1] << 32);
            uint32_t type = (e->ctrl >> TRB_TYPE_SHIFT) & 0x3F;
            last_event_code = (e->status >> 24) & 0xFF;
            if (type == TRB_TRANSFER_EV) {
                got_transfer = 1;
                ev_i++;
                if (ev_i == 64) { ev_i = 0; ev_cycle ^= 1; }
                processed = 1;
                if (last_event_param == expect) {
                    erdp_sync();
                    int code = (int)last_event_code;
                    if (code == ER_SUCCESS && buf && len && dir_in) {
                        for (uint16_t i = 0; i < len; i++) ((uint8_t*)buf)[i] = rep_buf[i];
                    }
                    return code;
                }
            } else {
                ev_i++;
                if (ev_i == 64) { ev_i = 0; ev_cycle ^= 1; }
                processed = 1;
            }
        }
        if (processed) erdp_sync();
    }
    ulog("usb: ev2=");
    for (int k = 0; k < 8; k++) {
        ulog_hx((uint64_t)k);
        ulog(":");
        ulog_hx(((uint64_t)ev_ring[k].p[1] << 32) | ev_ring[k].p[0]);
        ulog("/");
        ulog_hx(ev_ring[k].ctrl);
        ulog(" ");
    }
    ulog("evi=");
    ulog_hx((uint64_t)ev_i);
    ulog(" evc=");
    ulog_hx((uint64_t)ev_cycle);
    ulog("|n");
    ulog("usb: ep0ctx state=");
    ulog_hx(devctx_va[8]);
    ulog(" dq=");
    ulog_hx(((uint64_t)devctx_va[13] << 32) | devctx_va[12]);
    ulog("|n");
    ulog("usb: ctrl timeout expect=");
    ulog_hx(expect);
    ulog(" lastp=");
    ulog_hx(last_event_param);
    ulog(" lastc=");
    ulog_hx((uint64_t)last_event_code);
    ulog(" sts=");
    ulog_hx(op[OP_USBSTS / 4]);
    ulog("\n");
    return -1;
}

/* --- init --- */
int64_t k_fb_width(void);
int64_t k_fb_height(void);

int64_t k_usb_init(void) {
    tab_w = (uint32_t)k_fb_width();
    tab_h = (uint32_t)k_fb_height();
    if (!tab_w) tab_w = 800;
    if (!tab_h) tab_h = 600;

    /* 1. PCI: найти xHCI (class 0x0C/0x03/0x30) на шине 0 */
    int fdev = -1;
    for (int dev = 0; dev < 32; dev++) {
        uint32_t id = pci_cfg32(0, dev, 0, 0);
        if ((id & 0xFFFFu) == 0xFFFFu) continue;
        uint32_t cls = pci_cfg32(0, dev, 0, 8);
        if ((cls >> 24) == 0x0C && ((cls >> 16) & 0xFF) == 0x03 && ((cls >> 8) & 0xFF) == 0x30) {
            fdev = dev; break;
        }
    }
    if (fdev < 0) {
        ulog("usb: no xhci; pci:");
        for (int dev = 0; dev < 32; dev++) {
            uint32_t id = pci_cfg32(0, dev, 0, 0);
            if ((id & 0xFFFFu) == 0xFFFFu) continue;
            ulog(" ["); ulog_hx((uint64_t)dev); ulog("="); ulog_hx(id); ulog("]");
        }
        ulog("\n");
        return 0;
    }

    /* bus master + memory space */
    uint32_t cmd = pci_cfg32(0, fdev, 0, 4);
    pci_cfg32w(0, fdev, 0, 4, cmd | 0x6);

    uint32_t bar0 = pci_cfg32(0, fdev, 0, 0x10);
    uint32_t bar0h = pci_cfg32(0, fdev, 0, 0x14);
    uint64_t bar = (uint64_t)bar0 | ((uint64_t)bar0h << 32);
    bar &= ~0xFULL;
    ulog("usb: xhci bar="); ulog_hx(bar); ulog("\n");

    volatile uint32_t* cap = (volatile uint32_t*)(uintptr_t)bar;
    uint8_t caplen = *(volatile uint8_t*)((uintptr_t)bar + X_CAPLENGTH);
    op  = (volatile uint32_t*)(uintptr_t)(bar + caplen);
    uint32_t rtsoff = cap[X_RTSOFF / 4] & ~0xFu;
    rts = (volatile uint32_t*)(uintptr_t)(bar + rtsoff);
    uint32_t dboff = cap[X_DBOFF / 4] & ~0x3u;
    db  = (volatile uint32_t*)(uintptr_t)(bar + dboff);
    portsc0 = op + OP_PORTSC / 4;

    uint32_t hcs1 = cap[X_HCSPARAMS1 / 4];
    uint32_t hcs2 = cap[X_HCSPARAMS2 / 4];
    uint32_t hcc1 = cap[X_HCCPARAMS1 / 4];
    uint8_t  max_slots = hcs1 & 0xFF;
    int      csz = (int)((hcc1 >> 2) & 1);

    /* 2. HCRST */
    op[OP_USBCMD / 4] |= (1u << 1);
    /* 2. стоп -> HCRST -> ждём CNR==0 (EDK2 оставляет HC запущенным:
       HCRST при RUN=1 игнорируется, CNR не падает) */
    op[OP_USBCMD / 4] &= ~1u;                  /* RUN=0 */
    for (int spin = 0; spin < 100000000 && !(op[OP_USBSTS / 4] & 1u); spin++) { }
    op[OP_USBCMD / 4] = (1u << 1);             /* HCRST */
    for (int spin = 0; spin < 100000000 && (op[OP_USBCMD / 4] & (1u << 1)); spin++) { }
    for (int spin = 0; spin < 100000000 && (op[OP_USBSTS / 4] & (1u << 12)); spin++) { } /* CNR */
    ulog("usb: after reset usbsts=");
    ulog_hx(op[OP_USBSTS / 4]);
    ulog("\n");

    /* 3. страницы от кадрового аллокатора */
    int64_t dcbaa_v = k_mem_palloc();
    int64_t devctx_v = k_mem_palloc();
    int64_t inctx_v = k_mem_palloc();
    int64_t cmd_v = k_mem_palloc();
    int64_t ev_v = k_mem_palloc();
    int64_t ep0_v = k_mem_palloc();
    int64_t in_v = k_mem_palloc();
    int64_t scr_v = k_mem_palloc();
    int64_t rep_v = k_mem_palloc();
    if (!dcbaa_v || !devctx_v || !inctx_v || !cmd_v || !ev_v || !ep0_v || !in_v || !scr_v || !rep_v) {
        ulog("usb: alloc fail\n"); return 0;
    }
    dcbaa_pa = (uint64_t)k_mem_virt_to_phys(dcbaa_v);
    devctx_pa = (uint64_t)k_mem_virt_to_phys(devctx_v);
    devctx_va = (volatile uint32_t*)(uintptr_t)devctx_v;
    inctx_pa = (uint64_t)k_mem_virt_to_phys(inctx_v);
    cmd_ring_pa = (uint64_t)k_mem_virt_to_phys(cmd_v);
    ev_ring_pa = (uint64_t)k_mem_virt_to_phys(ev_v);
    ep0_ring_pa = (uint64_t)k_mem_virt_to_phys(ep0_v);
    in_ring_pa = (uint64_t)k_mem_virt_to_phys(in_v);
    rep_buf_pa = (uint64_t)k_mem_virt_to_phys(rep_v);
    cmd_ring = (trb_t*)(uintptr_t)cmd_v;
    ev_ring = (trb_t*)(uintptr_t)ev_v;
    ep0_ring = (trb_t*)(uintptr_t)ep0_v;
    in_ring = (trb_t*)(uintptr_t)in_v;
    rep_buf = (uint8_t*)(uintptr_t)rep_v;
    for (uint32_t i = 0; i < 4096 / 16; i++) { trb_set(cmd_ring + i, 0, 0, 0, 0); trb_set(ev_ring + i, 0, 0, 0, 0); }

    /* кольца transfer'ов тоже чистим: мусорные TRB с рандомным cycle-битом
       ломают xhci_ring_chain_length (цепочка уезжает в мусор) */
    for (uint32_t i = 0; i < 4096 / 16; i++) { trb_set(ep0_ring + i, 0, 0, 0, 0); trb_set(in_ring + i, 0, 0, 0, 0); }

    cmd_i = 0; cmd_cycle = 1;
    ep0_i = 0; ep0_cycle = 1;
    in_i = 0; in_cycle = 1;
    ev_i = 0; ev_cycle = 1;

    /* LINK-TRB в конце каждого кольца */
    trb_set(cmd_ring + 63, cmd_ring_pa, 0, TRB_LINK, 1);
    cmd_ring[63].ctrl |= TRB_TC;
    trb_set(ep0_ring + 63, ep0_ring_pa, 0, TRB_LINK, 1);
    ep0_ring[63].ctrl |= TRB_TC;
    trb_set(in_ring + 63, in_ring_pa, 0, TRB_LINK, 1);
    in_ring[63].ctrl |= TRB_TC;

    ulog("usb: sts@dcbaa="); ulog_hx(op[OP_USBSTS / 4]); ulog("|n");
    /* 4. DCBAA: слот 1 -> контекст устройства */
    volatile uint64_t* dcbaa = (volatile uint64_t*)(uintptr_t)dcbaa_v;
    for (int i = 0; i < 32; i++) dcbaa[i] = 0;
    dcbaa[1] = devctx_pa;
    uint64_t dcbaa0 = dcbaa[0];

    /* 5. command ring + DCBAAP до старта */
    op[OP_DNCTRL / 4] = 2;
    op[OP_CRCR / 4] = (uint32_t)cmd_ring_pa | 1;
    op[OP_CRCR / 4 + 1] = (uint32_t)(cmd_ring_pa >> 32);
    op[OP_DCBAAP / 4] = (uint32_t)dcbaa_pa;
    op[OP_DCBAAP / 4 + 1] = (uint32_t)(dcbaa_pa >> 32);

    ulog("usb: sts@erst="); ulog_hx(op[OP_USBSTS / 4]); ulog("|n");
    /* 6. event ring (ERST-entry кладём в ту же страницу: события 0x400Б + ERST)
       Runtime-набор: IMAN @0x20, IMOD @0x24, ERSTSZ @0x28, ERSTBA @0x30,
       ERDP @0x38 (64-битные — парами dword). */
    volatile uint64_t* erst_v = (volatile uint64_t*)((uintptr_t)ev_v + 0x400);
    erst_v[0] = ev_ring_pa;
    erst_v[1] = 64;
    erst_pa = ev_ring_pa + 0x400;
    volatile uint32_t* ir = rts + (RT_IMAN / 4);
    ir[0] = 0;   /* IMAN.IE = 0 (поллинг) */
    DSB();
    ir[2] = 1;   /* ERSTSZ = 1 (@0x28) */
    DSB();
    ir[4] = (uint32_t)erst_pa;   /* ERSTBA @0x30, пишется последним */
    ir[5] = (uint32_t)(erst_pa >> 32);

    /* ERDP */
    volatile uint32_t* erdp = rts + (RT_ERDP / 4);
    erdp[0] = (uint32_t)ev_ring_pa | (1u << 3);
    erdp[1] = (uint32_t)(ev_ring_pa >> 32);

    /* 7. старт */
    ulog("usb: sts@config="); ulog_hx(op[OP_USBSTS / 4]); ulog("|n");
    op[OP_CONFIG / 4] = max_slots;
    ulog("usb: sts@run="); ulog_hx(op[OP_USBSTS / 4]); ulog("|n");
    op[OP_USBCMD / 4] |= 1;   /* RUN */

    /* 8. порт: power + reset, ждём CCS */
    uint32_t nports = (hcs1 >> 24) & 0xFF;
    ulog("usb: nports="); ulog_hx((uint64_t)nports);
    for (uint32_t p = 0; p < nports; p++) { ulog(" raw"); ulog_hx((uint64_t)p); ulog("="); ulog_hx(portsc0[p * 4]); }
    ulog("\n");
    int port = -1;
    /* QEMU-устройства прицеплены на старте: ищем порт с CCS=1, ресет не
       нужен (ресет занятого порта — кirk QEMU со стагнирующим CNR). */
    for (uint32_t p = 0; p < nports && port < 0; p++) {
        volatile uint32_t* ps = portsc0 + p * 4;   /* PORTSC stride 0x10 байт */
        uint32_t before = ps[0];
        *ps = before | (1u << 9);   /* PP */
        for (volatile int i = 0; i < 20000; i++) { }
        if (ps[0] & 1u) { port = (int)p; }
    }
    if (port < 0) { ulog("usb: no port\n"); return 0; }
    uint32_t speed = (portsc0[port * 4] >> 10) & 0xF;
    ulog("usb: port="); ulog_hx((uint64_t)port); ulog(" speed="); ulog_hx(speed); ulog("\n");

    /* 9. Enable Slot */
    ulog("usb: diag hcs2="); ulog_hx(hcs2);
    ulog(" crcr="); ulog_hx(((uint64_t)op[OP_CRCR / 4 + 1] << 32) | op[OP_CRCR / 4]);
    ulog(" dcbaa="); ulog_hx(((uint64_t)op[OP_DCBAAP / 4 + 1] << 32) | op[OP_DCBAAP / 4]);
    ulog(" erstsz="); ulog_hx(rts[10]);
    ulog(" erstba="); ulog_hx(((uint64_t)rts[13] << 32) | rts[12]);
    ulog(" erdp="); ulog_hx(((uint64_t)rts[15] << 32) | rts[14]);
    ulog(" d0="); ulog_hx(dcbaa0);
    ulog("|n");
    ulog("usb: pre-cmd usbcmd=");
    ulog_hx(op[OP_USBCMD / 4]);
    ulog(" usbsts=");
    ulog_hx(op[OP_USBSTS / 4]);
    ulog("\n");
    got_command = 0;
    ring_push(cmd_ring, cmd_ring_pa, &cmd_i, &cmd_cycle, 0, 0, TRB_ENABLE_SLOT, 0);
    ulog("usb: cmd trb0 ctrl=");
    ulog_hx(cmd_ring[0].ctrl);
    DSB(); db[0] = 0;
    int code = wait_command();
    if (code != ER_SUCCESS) {
        ulog("usb: enable slot fail code=");
        ulog_hx((uint64_t)code);
        ulog(" ev0c=");
        ulog_hx(ev_ring[0].ctrl);
        ulog(" sts=");
        ulog_hx(op[OP_USBSTS / 4]);
        ulog("\n");
        return 0;
    }
    /* Slot ID командного completion — ctrl[31:24], не param */
    usb_slot = (uint8_t)(last_event_ctrl >> 24);
    if (!usb_slot) usb_slot = 1;
    ulog("usb: enable ok slot=");
    ulog_hx((uint64_t)usb_slot);

    /* 10. Input context: slot + EP0 (Address Device) */
    uint32_t csz_d = csz ? 16 : 8;   /* dword'ов на контекст */
    volatile uint32_t* inctx = (volatile uint32_t*)(uintptr_t)inctx_v;
    for (int i = 0; i < 128; i++) inctx[i] = 0;
    inctx[0] = 0;                                            /* drop-flags = 0 */
    inctx[1] = 3;                                            /* add: slot + EP0 (dw1!) */
    volatile uint32_t* slot = inctx + csz_d;
    slot[1] = (uint32_t)(port + 1) << 16;                    /* root hub port (QEMU: >>16) */
    slot[2] = (1u << 27) | (speed << 20);                    /* ctx entries=1, speed */
    volatile uint32_t* ep0 = inctx + csz_d * 2;
    ep0[2] = (3u << 27) | (EP_TYPE_CTRL << 3);
    ep0[3] = (speed == 3 ? 64u : 8u) << 16;                  /* MPS: HS=64, FS/LS=8 */
    ep0[4] = (uint32_t)ep0_ring_pa | 1;
    ep0[5] = (uint32_t)(ep0_ring_pa >> 32);
    ep0[6] = 8u << 16;                                       /* avg TRB len */

    got_command = 0;
    ring_push(cmd_ring, cmd_ring_pa, &cmd_i, &cmd_cycle, inctx_pa, 0, TRB_ADDRESS_DEV, (uint32_t)usb_slot << 24);
    DSB(); db[0] = 0;
    code = wait_command();
    if (code != ER_SUCCESS) {
        ulog("usb: address fail code=");
        ulog_hx((uint64_t)code);
        ulog(" add=");
        ulog_hx(inctx[1]);
        ulog(" slot1=");
        ulog_hx(slot[1]);
        ulog(" csz=");
        ulog_hx((uint64_t)csz);
        ulog(" hcc1=");
        ulog_hx(hcc1);
        ulog(" icc0=");
        ulog_hx(inctx[0]);
        ulog(" slot2=");
        ulog_hx(slot[2]);
        ulog(" ep0mps=");
        ulog_hx(ep0[3]);
        ulog(" ep0ring=");
        ulog_hx(ep0[4]);
        ulog("\n");
        return 0;
    }
    ulog("usb: addressed slot="); ulog_hx(usb_slot); ulog("\n");

    /* 11. дескрипторы */
    uint8_t ddev[18];
    for (int a = 0; a < 3; a++) { code = ctrl_xfer(0x80, 0x06, 0x0100, 0, ddev, 18, 1); if (code == 0) break; }
    if (code != 0) { ulog("usb: getdev fail\n"); return 0; }
    uint16_t mps0 = ddev[7];

    uint8_t dcfg[9];
    ctrl_xfer(0x80, 0x06, 0x0200, 0, dcfg, 9, 1);
    uint16_t total = (uint16_t)(dcfg[2] | (dcfg[3] << 8));
    if (total > 128) total = 128;
    static uint8_t cfgbuf[128];
    ctrl_xfer(0x80, 0x06, 0x0200, 0, cfgbuf, total, 1);

    /* walk: HID boot tablet */
    int ep_in = 0, ep_mps = 8, ep_int = 10;
    uint8_t if_cls = 0, if_sub = 0, if_proto = 0;
    for (int off = 0; off + 1 < total;) {
        uint8_t len = cfgbuf[off], type = cfgbuf[off + 1];
        if (len < 2) break;
        if (type == 4) {
            if_cls = cfgbuf[off + 5]; if_sub = cfgbuf[off + 6]; if_proto = cfgbuf[off + 7];
        }
        if (type == 5 && if_cls == 3) {
            uint8_t addr = cfgbuf[off + 2];
            uint8_t attr = cfgbuf[off + 3];
            if ((addr & 0x80) && (attr & 3) == 3) {
                ep_in = addr & 0x0F;
                ep_mps = (int)(cfgbuf[off + 4] | (cfgbuf[off + 5] << 8));
                ep_int = cfgbuf[off + 6];
            }
        }
        off += len;
    }
    ulog("usb: hid cls/sub/proto="); ulog_hx((if_cls << 16) | (if_sub << 8) | if_proto);
    ulog(" ep="); ulog_hx((uint64_t)ep_in); ulog("\n");
    if (if_cls != 3 || if_sub != 1 || if_proto != 2 || !ep_in) {
        ulog("usb: not a tablet\n"); return 0;
    }
    tab_present = 1;

    /* SET_CONFIGURATION(1) + HID SET_PROTOCOL(boot) + SET_IDLE */
    ctrl_xfer(0x00, 0x09, 0x0001, 0, 0, 0, 0);
    ctrl_xfer(0x21, 0x0B, 0x0000, 0, 0, 0, 0);
    ctrl_xfer(0x21, 0x0A, 0x0000, 0, 0, 0, 0);

    /* 12. EP0 MPS уточнить (bMaxPacketSize0) */
    inctx[0] = 0;
    inctx[1] = 3;
    slot[2] = (2u << 27) | (speed << 20);        /* ctx entries=2 */
    volatile uint32_t* ep0u = inctx + csz_d * 2;
    ep0u[3] = (uint32_t)mps0 << 16;
    got_command = 0;
    ring_push(cmd_ring, cmd_ring_pa, &cmd_i, &cmd_cycle, inctx_pa, 0, TRB_EVALUATE_CTX, (uint32_t)usb_slot << 24);
    DSB(); db[0] = 0;
    wait_command();

    /* 13. interrupt IN ring + CONFIG_EP (dci 3) */
    for (int i = 0; i < REP_N; i++) {
        rep_trb_pa[i] = ring_push(in_ring, in_ring_pa, &in_i, &in_cycle,
                                  rep_buf_pa + (uint64_t)i * REP_SZ, REP_SZ, TRB_NORMAL, 0);
        in_ring[in_i ? in_i - 1 : 62].ctrl |= TRB_IOC;
    }
    inctx[0] = 0;
    inctx[1] = 1 | (1u << 3);                    /* add: slot + ep dci3 (dw1!) */
    slot[2] = (3u << 27) | (speed << 20);
    volatile uint32_t* epin = inctx + csz_d * 4; /* dci3 -> смещение (1+3)*csz */
    epin[1] = (uint32_t)ep_int << 16;
    epin[2] = (3u << 27) | (EP_TYPE_INT_IN << 3);
    epin[3] = (uint32_t)ep_mps << 16;
    epin[4] = (uint32_t)in_ring_pa | 1;
    epin[5] = (uint32_t)(in_ring_pa >> 32);
    epin[6] = 8u << 16;
    got_command = 0;
    ring_push(cmd_ring, cmd_ring_pa, &cmd_i, &cmd_cycle, inctx_pa, 0, TRB_CONFIG_EP, (uint32_t)usb_slot << 24);
    DSB(); db[0] = 0;
    code = wait_command();
    if (code != ER_SUCCESS) { ulog("usb: config ep fail\n"); return 0; }

    /* doorbell EP1 IN */
    DSB();
    db[usb_slot] = 3;

    usb_ok = 1;
    ulog("usb: tablet ready\n");
    return 1;
}

/* перезаполнить один IN-TRB после разбора отчёта */
static void refill(int idx) {
    trb_t* t = in_ring + idx;
    trb_set(t, rep_buf_pa + (uint64_t)idx * REP_SZ, REP_SZ, TRB_NORMAL, in_cycle);
    t->ctrl |= TRB_IOC;
    in_i = idx + 1;
    if (in_i == 63) { in_i = 0; in_cycle ^= 1; }
}

int64_t k_usb_poll(void) {
    if (!usb_ok || !tab_present) return 0;
    int processed = 0;
    for (int n = 0; n < 16; n++) {
        trb_t* e = ev_ring + ev_i;
        if ((e->ctrl & TRB_CYCLE) != ev_cycle) break;
        uint64_t param = (uint64_t)e->p[0] | ((uint64_t)e->p[1] << 32);
        uint32_t type = (e->ctrl >> TRB_TYPE_SHIFT) & 0x3F;
        uint32_t code = (e->status >> 24) & 0xFF;
        if (type == TRB_TRANSFER_EV) {
            got_transfer = 0;
            if (code == ER_SUCCESS) {
                for (int i = 0; i < REP_N; i++) {
                    if (param == rep_trb_pa[i]) {
                        uint8_t* r = rep_buf + (uint64_t)i * REP_SZ;
                        tab_btn = r[0] & 0x07;
                        uint32_t x = (uint32_t)r[1] | ((uint32_t)r[2] << 8);
                        uint32_t y = (uint32_t)r[3] | ((uint32_t)r[4] << 8);
                        tab_x = (int)(x * tab_w / 0x8000);
                        tab_y = (int)(y * tab_h / 0x8000);
                        refill(i);
                        processed = 1;
                        break;
                    }
                }
            } else {
                /* номер TRB неизвестен — переармим все по кругу */
                processed = 0;
            }
        }
        ev_i++;
        if (ev_i == 64) { ev_i = 0; ev_cycle ^= 1; }
    }
    return processed;
}

int64_t k_usb_ready(void) { return usb_ok; }
int64_t k_usb_tab_present(void) { return tab_present; }
int64_t k_usb_tab_x(void) { return tab_x; }
int64_t k_usb_tab_y(void) { return tab_y; }
int64_t k_usb_tab_btn(void) { return tab_btn; }
