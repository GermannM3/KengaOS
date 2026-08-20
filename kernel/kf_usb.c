/* kf_usb.c — minimal UHCI host controller driver + HID tablet (QEMU usb-tablet).
 *
 * QEMU's PS/2 mouse is relative-only, which makes the gtk window grab the
 * cursor. QEMU's usb-tablet reports ABSOLUTE positions, so the pointer follows
 * the host cursor with no grab at all. This file brings up the ICH9 UHCI on
 * q35, enumerates a HID boot-protocol tablet, and polls its interrupt endpoint.
 *
 * Category A (hardware bridge): PCI config + UHCI schedule + USB control
 * transfers are the low-level layer; the desktop stays in Kenga.
 */
#include "kf_rt.h"

/* --- UHCI structures (32 bytes, 16-aligned) --- */
typedef struct {
    uint32_t link;      /* +0: next TD phys | 1 = terminate */
    uint32_t cs;        /* +4: control/status (bit23 active, bit24 IOC, bits16-22 status) */
    uint32_t token;     /* +8: PID | dev<<8 | ep<<15 | toggle<<19 | (maxlen-1)<<21 */
    uint32_t buffer;    /* +12 */
    uint32_t res[4];    /* +16..+28 */
} uhci_td __attribute__((aligned(16)));

typedef struct {
    uint32_t link;      /* +0: next QH phys | 2, or 1 = terminate */
    uint32_t element;   /* +4: first TD phys, or 1 = empty */
    uint32_t res[6];
} uhci_qh __attribute__((aligned(16)));

/* --- registers --- */
#define USBCMD   0x00
#define USBSTS   0x02
#define USBINTR  0x04
#define FRNUM    0x06
#define FLBASE   0x08
#define PORTSC1  0x10
#define PORTSC2  0x12

/* --- PIDs --- */
#define PID_SETUP 0x2D
#define PID_IN    0x69
#define PID_OUT   0xE1

/* TD control bits */
#define TD_ACTIVE (1u << 23)
#define TD_IOC    (1u << 24)

static uint16_t io_base = 0;
static uint8_t  usb_ok = 0;
static uint8_t  tab_present = 0;

static uint8_t inb16(uint16_t p) { uint8_t v; __asm__ __volatile__("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static void    outb16(uint16_t p, uint8_t v) { __asm__ __volatile__("outb %0,%1" : : "a"(v), "Nd"(p)); }

static void usb_log(const char* s) { for (; *s; s++) outb16(0x3F8, (uint8_t)*s); }

static uhci_qh*  ctrl_qh;
static uhci_qh*  int_qh;
static uhci_td*  td_pool;
static uint32_t* frame_list;
static uint32_t  ctrl_qh_phys, int_qh_phys, td_phys, fl_phys;
static int       int_toggle = 0;
static int       tab_x = 640, tab_y = 400, tab_btn = 0;

/* Transfer buffers must be hhdm-mapped (kernel stack/BSS are NOT mapped at
   phys = virt - hhdm), so the DMA addresses computed by k_mem_virt_to_phys
   are correct. One dedicated page holds setup + data buffers. */
static uint8_t* xfer_bufs;        /* page from the frame allocator */
static uint8_t* g_setup;          /* 8 bytes */
static uint8_t* g_data;           /* 128 bytes */

static uint16_t inw16(uint16_t p) { uint16_t v; __asm__ __volatile__("inw %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static void    outw16(uint16_t p, uint16_t v) { __asm__ __volatile__("outw %0,%1" : : "a"(v), "Nd"(p)); }
static uint32_t inl16(uint16_t p) { uint32_t v; __asm__ __volatile__("inl %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static void    outl16(uint16_t p, uint32_t v) { __asm__ __volatile__("outl %0,%1" : : "a"(v), "Nd"(p)); }

static void udelay(int us) {
    volatile uint32_t n = (uint32_t)us * 500;
    while (n) n--;
}

/* --- PCI config access --- */
static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg) {
    uint32_t addr = 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11)
                  | ((uint32_t)fn << 8) | (reg & 0xFC);
    outl16(0xCF8, addr);
    return inl16(0xCFC);
}

/* Find the first UHCI controller (class 0x0C, subclass 0x03, progIF 0x00). */
static void pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg, uint32_t v) {
    uint32_t addr = 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11)
                  | ((uint32_t)fn << 8) | (reg & 0xFC);
    outl16(0xCF8, addr);
    outl16(0xCFC, v);
}

static int find_uhci(void) {
    for (uint8_t dev = 0; dev < 32; dev++) {
        uint32_t id = pci_read32(0, dev, 0, 0);
        if ((id & 0xFFFF) == 0xFFFF) continue;
        uint32_t cls = pci_read32(0, dev, 0, 8);
        uint8_t cc = (cls >> 24) & 0xFF, sc = (cls >> 16) & 0xFF, pi = (cls >> 8) & 0xFF;
        if (cc == 0x0C && sc == 0x03 && pi == 0x00) {
            /* ICH9 UHCI (8086:2934) exposes its I/O window in BAR4. */
            uint32_t bar0 = pci_read32(0, dev, 0, 0x20);
            io_base = (uint16_t)(bar0 & 0xFFFE);
            usb_log("usb: pci dev=");
            usb_log((char[]){ (char)('0' + (dev / 10)), (char)('0' + (dev % 10)), 0 });
            usb_log(" bar4=");
            for (int sh = 28; sh >= 0; sh -= 4) {
                uint8_t d = (uint8_t)((bar0 >> sh) & 0xF);
                usb_log((char[]){ (char)(d < 10 ? '0' + d : 'a' + d - 10), 0 });
            }
            usb_log("\n");
            /* take ownership: clear SMI bits, bus master on */
            uint16_t cmd = (uint16_t)(pci_read32(0, dev, 0, 4));
            pci_write32(0, dev, 0, 4, (uint32_t)(cmd | 0x0004));
            return 1;
        }
    }
    return 0;
}

/* Fill a TD. Returns 1. */
static void td_fill(uhci_td* td, uint32_t next_phys, uint8_t pid, uint8_t dev_addr,
                    uint8_t ep, uint8_t toggle, uint32_t maxlen, void* buf, int ioc) {
    td->link = next_phys ? next_phys : 1;
    td->cs = TD_ACTIVE | (ioc ? TD_IOC : 0) | (3u << 27);   /* 3 error retries */
    uint32_t ml = maxlen ? (maxlen - 1) : 0x7FF;
    td->token = (uint32_t)pid | ((uint32_t)dev_addr << 8) | ((uint32_t)ep << 15)
              | ((uint32_t)toggle << 19) | (ml << 21);
    td->buffer = (uint32_t)(uintptr_t)k_mem_virt_to_phys((int64_t)(uintptr_t)buf);
    for (int i = 0; i < 4; i++) td->res[i] = 0;
}

/* Run a synchronous TD chain; returns the worst TD status code (0 = ok).
   Waits for the LAST TD in the chain (the controller executes the chain
   sequentially, so earlier TDs may still be ACTIVE when the first finishes). */
static int run_td_chain(uhci_qh* qh, uhci_td* first) {
    /* find the terminating TD (link == 1) */
    uhci_td* last = first;
    int hops = 0;
    while (last->link != 1 && hops < 8) {
        last = (uhci_td*)((uintptr_t)td_pool + (last->link - td_phys));
        hops++;
    }
    qh->element = td_phys + (uint32_t)((uintptr_t)first - (uintptr_t)td_pool);
    /* timeouts must NOT depend on k_time_uptime_ms(): USB init runs before
       sti, so the PIT timer is frozen. Busy-wait with a counter instead.
       The TD status must be read via volatile (QEMU writes it via DMA). */
    volatile uint32_t* lastcs = &last->cs;
    volatile uint32_t spins = 0;
    while (*lastcs & TD_ACTIVE) {
        if (++spins > 50000000u) { qh->element = 1; return -1; }   /* timeout */
    }
    qh->element = 1;
    /* walk the chain, return worst status (bits 16-22; bit 23 = active now cleared) */
    uhci_td* t = first;
    int worst = 0;
    hops = 0;
    while (t && hops < 8) {
        volatile uint32_t cs_v = t->cs;
        uint32_t st = (cs_v >> 16) & 0x7F;
        if (st) worst = (int)st;
        uint32_t link = t->link;
        if (link == 1) break;
        t = (uhci_td*)((uintptr_t)td_pool + (link - td_phys));
        hops++;
    }
    return worst;
}

/* Control transfer (SETUP/DATA/STATUS). buf may be 0 for no data stage.
   Returns 0 on success. Buffers live in hhdm-mapped memory (see xfer_bufs),
   so k_mem_virt_to_phys gives valid DMA addresses. */
static int usb_ctrl(uint8_t dev_addr, uint8_t bmReqType, uint8_t bReq,
                    uint16_t wValue, uint16_t wIndex, void* data, uint32_t len, int dir_in) {
    g_setup[0] = bmReqType; g_setup[1] = bReq;
    g_setup[2] = (uint8_t)(wValue & 0xFF); g_setup[3] = (uint8_t)(wValue >> 8);
    g_setup[4] = (uint8_t)(wIndex & 0xFF); g_setup[5] = (uint8_t)(wIndex >> 8);
    g_setup[6] = (uint8_t)(len & 0xFF); g_setup[7] = (uint8_t)(len >> 8);
    if (data && len) {
        if (dir_in) { for (uint32_t i = 0; i < len; i++) g_data[i] = 0; }
        else        { for (uint32_t i = 0; i < len; i++) g_data[i] = ((uint8_t*)data)[i]; }
    }

    uhci_td* t0 = td_pool + 0;   /* SETUP */
    uhci_td* t1 = td_pool + 1;   /* DATA (optional) */
    uhci_td* t2 = td_pool + 2;   /* STATUS */

    td_fill(t0, td_phys + 32, PID_SETUP, dev_addr, 0, 0, 8, g_setup, 0);
    if (len) {
        td_fill(t1, td_phys + 64, dir_in ? PID_IN : PID_OUT, dev_addr, 0, 1, len, g_data, 0);
        td_fill(t2, 0, dir_in ? PID_OUT : PID_IN, dev_addr, 0, 1, 0, 0, 1);
    } else {
        /* no data stage (e.g. SET_ADDRESS): status stage is always IN */
        td_fill(t1, 0, PID_IN, dev_addr, 0, 1, 0, 0, 1);
    }
    int st = run_td_chain(ctrl_qh, t0);
    if (st < 0) return -1;
    if (st == 0) {
        if (data && len && dir_in) { for (uint32_t i = 0; i < len; i++) ((uint8_t*)data)[i] = g_data[i]; }
        return 0;
    }
    /* NAK/errors: retry once */
    udelay(1000);
    st = run_td_chain(ctrl_qh, t0);
    if (st == 0) {
        if (data && len && dir_in) { for (uint32_t i = 0; i < len; i++) ((uint8_t*)data)[i] = g_data[i]; }
    }
    return st;
}

/* Poll the interrupt endpoint (tablet report). */
static void usb_poll_tab(void) {
    for (int i = 0; i < 8; i++) g_data[i] = 0;
    uhci_td* t = td_pool + 3;
    td_fill(t, 0, PID_IN, 1, 1, int_toggle, 8, g_data, 1);
    int st = run_td_chain(int_qh, t);
    if (st == 0) {
        int_toggle ^= 1;
        /* QEMU usb-tablet report: [buttons, x_lo, x_hi, y_lo, y_hi] */
        tab_btn = g_data[0] & 0x07;
        uint32_t x = (uint32_t)g_data[1] | ((uint32_t)g_data[2] << 8);
        uint32_t y = (uint32_t)g_data[3] | ((uint32_t)g_data[4] << 8);
        tab_x = (int)(x * 1280 / 0x8000);
        tab_y = (int)(y * 800 / 0x8000);
        if (tab_x < 0) tab_x = 0; if (tab_x > 1279) tab_x = 1279;
        if (tab_y < 0) tab_y = 0; if (tab_y > 799) tab_y = 799;
    }
}

int64_t k_usb_init(void) {
    if (!find_uhci()) { usb_log("usb: no uhci\n"); return 0; }
    usb_log("usb: uhci at ");
    usb_log("0x");
    for (int sh = 12; sh >= 0; sh -= 4) {
        uint8_t d = (uint8_t)((io_base >> sh) & 0xF);
        usb_log((char[]){ (char)(d < 10 ? '0' + d : 'a' + d - 10), 0 });
    }
    usb_log("\n");
    /* reset controller */
    outw16(io_base + USBCMD, 0x0004);   /* GRESET */
    udelay(50000);
    outw16(io_base + USBCMD, 0x0000);
    udelay(10000);

    /* allocate structures (phys addresses via frame allocator) */
    int64_t fl_v = k_mem_palloc();
    int64_t qh_v = k_mem_palloc();
    int64_t td_v = k_mem_palloc();
    int64_t xf_v = k_mem_palloc();
    if (!fl_v || !qh_v || !td_v || !xf_v) { usb_log("usb: alloc fail\n"); return 0; }
    frame_list = (uint32_t*)(uintptr_t)fl_v;
    ctrl_qh = (uhci_qh*)(uintptr_t)qh_v;
    int_qh  = (uhci_qh*)((uintptr_t)qh_v + 32);
    td_pool = (uhci_td*)(uintptr_t)td_v;
    xfer_bufs = (uint8_t*)(uintptr_t)xf_v;
    g_setup = xfer_bufs;
    g_data  = xfer_bufs + 16;
    fl_phys  = (uint32_t)k_mem_virt_to_phys(fl_v);
    ctrl_qh_phys = (uint32_t)k_mem_virt_to_phys(qh_v);
    int_qh_phys  = ctrl_qh_phys + 32;
    td_phys = (uint32_t)k_mem_virt_to_phys(td_v);

    /* frame list: every entry -> ctrl QH */
    for (int i = 0; i < 1024; i++) frame_list[i] = ctrl_qh_phys | 2;
    ctrl_qh->link = int_qh_phys | 2;
    ctrl_qh->element = 1;
    int_qh->link = 1;
    int_qh->element = 1;

    outw16(io_base + USBINTR, 0);
    outw16(io_base + FRNUM, 0);
    outl16(io_base + FLBASE, fl_phys);

    /* port reset BEFORE starting the schedule (RS) */
    for (int port = 0; port < 2; port++) {
        uint16_t ps = inw16(io_base + (port ? PORTSC2 : PORTSC1));
        usb_log(port ? "usb: port2=" : "usb: port1=");
        for (int sh = 12; sh >= 0; sh -= 4) {
            uint8_t d = (uint8_t)((ps >> sh) & 0xF);
            usb_log((char[]){ (char)(d < 10 ? '0' + d : 'a' + d - 10), 0 });
        }
        usb_log("\n");
        if (!(ps & 0x0001)) continue;                  /* nothing connected */
        outw16(io_base + (port ? PORTSC2 : PORTSC1), ps | 0x0200);   /* reset */
        udelay(60000);
        outw16(io_base + (port ? PORTSC2 : PORTSC1), ps & ~0x0200u); /* clear reset */
        udelay(20000);
        /* enable the port (write 1 to bit 2) */
        ps = inw16(io_base + (port ? PORTSC2 : PORTSC1));
        outw16(io_base + (port ? PORTSC2 : PORTSC1), ps | 0x0004);
        udelay(20000);
        uint16_t p3 = inw16(io_base + (port ? PORTSC2 : PORTSC1));
        usb_log("usb: after=");
        for (int sh = 12; sh >= 0; sh -= 4) {
            uint8_t d = (uint8_t)((p3 >> sh) & 0xF);
            usb_log((char[]){ (char)(d < 10 ? '0' + d : 'a' + d - 10), 0 });
        }
        usb_log("\n");
    }

    outw16(io_base + USBCMD, 0x0001);   /* RS: run */
    /* clear any pending status */
    outw16(io_base + USBSTS, 0x3F);
    udelay(10000);                      /* let the first SOF pass */
    uint16_t st0 = inw16(io_base + USBSTS);
    usb_log("usb: usbsts=");
    for (int sh = 12; sh >= 0; sh -= 4) {
        uint8_t d = (uint8_t)((st0 >> sh) & 0xF);
        usb_log((char[]){ (char)(d < 10 ? '0' + d : 'a' + d - 10), 0 });
    }
    usb_log("\n");

    /* enumerate */
    uint8_t ddesc[8];
    int st = -1;
    for (int attempt = 0; attempt < 4 && st != 0; attempt++) {
        st = usb_ctrl(0, 0x80, 0x06, 0x0100, 0, ddesc, 8, 1);
        if (st != 0) udelay(20000);
    }
    usb_log("usb: getdesc st=");
    usb_log((char[]){ (char)('0' + (st < 0 ? 0 : (st > 9 ? 9 : st))), 0 });
    usb_log("\n");
    if (st != 0) return 0;
    uint8_t maxp = ddesc[7];
    if (maxp == 0) return 0;
    for (int a = 0; a < 4; a++) {
        if (usb_ctrl(0, 0x00, 0x05, 1, 0, 0, 0, 0) == 0) break;   /* SET_ADDRESS(1) */
        udelay(20000);
    }
    udelay(50000);
    uint8_t fdesc[18];
    int st18 = -1;
    for (int a = 0; a < 4 && st18 != 0; a++) {
        st18 = usb_ctrl(1, 0x80, 0x06, 0x0100, 0, fdesc, 18, 1);
        if (st18 != 0) {
            usb_log("usb: retry ");
            usb_log((char[]){ (char)('0' + a), 0 });
            usb_log(" st=");
            for (int sh = 20; sh >= 0; sh -= 4) {
                uint8_t d = (uint8_t)((st18 >> sh) & 0xF);
                usb_log((char[]){ (char)(d < 10 ? '0' + d : 'a' + d - 10), 0 });
            }
            usb_log("\n");
            udelay(20000);
        }
    }
    if (st18 != 0) { usb_log("usb: getdesc18 fail\n"); return 0; }
    for (int a = 0; a < 4; a++) {
        if (usb_ctrl(1, 0x00, 0x09, 1, 0, 0, 0, 0) == 0) break;   /* SET_CONFIG(1) */
        udelay(20000);
    }
    udelay(20000);

    usb_log("usb: tablet ready\n");
    tab_present = 1;
    usb_ok = 1;
    return 1;
}

int64_t k_usb_ready(void) { return usb_ok; }
int64_t k_usb_tab_present(void) { return tab_present; }

/* Poll + return state (called every desktop tick). */
int64_t k_usb_poll(void) {
    if (!usb_ok) return 0;
    usb_poll_tab();
    return 1;
}
int64_t k_usb_tab_x(void) { return tab_x; }
int64_t k_usb_tab_y(void) { return tab_y; }
int64_t k_usb_tab_btn(void) { return tab_btn; }
