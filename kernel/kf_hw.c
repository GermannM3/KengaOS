/* kf_hw.c — hardware info (CPUID) + real-time clock (CMOS RTC). Safe port-I/O. */
#include "kf_rt.h"

static void cpuid(uint32_t leaf, uint32_t* a, uint32_t* b, uint32_t* c, uint32_t* d) {
    __asm__ __volatile__("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf));
}

static uint8_t inb(uint16_t p) { uint8_t v; __asm__ __volatile__("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static void    outb(uint16_t p, uint8_t v) { __asm__ __volatile__("outb %0,%1" : : "a"(v), "Nd"(p)); }

static uint8_t cmos_read(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static int bcd(uint8_t v) { return ((v >> 4) * 10) + (v & 0xF); }

/* CPU vendor (leaf 0 -> 12 chars: ebx, edx, ecx). */
int64_t k_hw_cpu_vendor(char* out, int max) {
    uint32_t a, b, c, d;
    cpuid(0, &a, &b, &c, &d);
    char* p = out;
    int n = 0;
    const char* s = (const char*)&b;
    for (int i = 0; i < 4 && n < max - 1; i++) out[n++] = s[i];
    s = (const char*)&d;
    for (int i = 0; i < 4 && n < max - 1; i++) out[n++] = s[i];
    s = (const char*)&c;
    for (int i = 0; i < 4 && n < max - 1; i++) out[n++] = s[i];
    out[n] = 0;
    (void)p;
    return n;
}

/* CPU brand as a stable string (hardware bridge for Kenga GUI). */
static char g_cpu_brand_str[64];
const char* k_hw_cpu_brand_str(void) {
    k_hw_cpu_brand(g_cpu_brand_str, sizeof g_cpu_brand_str);
    return g_cpu_brand_str;
}

/* CPU brand string (leaf 0x80000002..04). */
int64_t k_hw_cpu_brand(char* out, int max) {
    int n = 0;
    for (uint32_t leaf = 0x80000002; leaf <= 0x80000004 && n < max - 1; leaf++) {
        uint32_t a, b, c, d;
        cpuid(leaf, &a, &b, &c, &d);
        char tmp[16];
        for (int i = 0; i < 4; i++) tmp[i] = ((char*)&a)[i];
        for (int i = 0; i < 4; i++) tmp[4+i] = ((char*)&b)[i];
        for (int i = 0; i < 4; i++) tmp[8+i] = ((char*)&c)[i];
        for (int i = 0; i < 4; i++) tmp[12+i] = ((char*)&d)[i];
        for (int i = 0; i < 16 && n < max - 1; i++) out[n++] = tmp[i];
    }
    /* trim trailing spaces */
    while (n > 0 && out[n-1] == ' ') n--;
    out[n] = 0;
    return n;
}

/* Key CPUID feature bits (leaf 1 EDX) as a flags word. */
int64_t k_hw_cpu_flags(void) {
    uint32_t a, b, c, d;
    cpuid(1, &a, &b, &c, &d);
    return (int64_t)d;
}

/* Real time from CMOS RTC: "YYYY-MM-DD HH:MM:SS" (BCD). */
int64_t k_hw_rtc_str(char* out, int max) {
    uint8_t sec = cmos_read(0x00);
    uint8_t min = cmos_read(0x02);
    uint8_t hr  = cmos_read(0x04);
    uint8_t day = cmos_read(0x07);
    uint8_t mon = cmos_read(0x08);
    uint8_t yr  = cmos_read(0x09);
    uint8_t cen = cmos_read(0x32);
    int year = bcd(cen) * 100 + bcd(yr);
    int n = 0;
    const char* digits = "0123456789";
    int vals[6] = { year, bcd(mon), bcd(day), bcd(hr), bcd(min), bcd(sec) };
    const char* sep = "-- ::";   /* after year-month, month-day, day-hour, hour-min, min-sec */
    for (int i = 0; i < 6; i++) {
        if (i > 0 && n < max - 1) out[n++] = sep[i-1];
        if (i == 0) { /* 4-digit year */
            if (n < max - 1) out[n++] = digits[(vals[i]/1000)%10];
            if (n < max - 1) out[n++] = digits[(vals[i]/100)%10];
        }
        if (n < max - 1) out[n++] = digits[(vals[i]/10)%10];
        if (n < max - 1) out[n++] = digits[vals[i]%10];
    }
    out[n] = 0;
    (void)digits; (void)sep;
    return n;
}

/* Current wall-clock time as a stable "HH:MM:SS" string (hardware bridge
   for the Kenga desktop top bar). */
static char g_rtc_time_str[16];
const char* k_hw_rtc_time_str(void) {
    uint8_t sec = cmos_read(0x00);
    uint8_t min = cmos_read(0x02);
    uint8_t hr  = cmos_read(0x04);
    const char* dg = "0123456789";
    g_rtc_time_str[0] = dg[bcd(hr)/10]; g_rtc_time_str[1] = dg[bcd(hr)%10];
    g_rtc_time_str[2] = ':';
    g_rtc_time_str[3] = dg[bcd(min)/10]; g_rtc_time_str[4] = dg[bcd(min)%10];
    g_rtc_time_str[5] = ':';
    g_rtc_time_str[6] = dg[bcd(sec)/10]; g_rtc_time_str[7] = dg[bcd(sec)%10];
    g_rtc_time_str[8] = 0;
    return g_rtc_time_str;
}
