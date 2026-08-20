/* kf_gui.c — KengaOS 0.4: graphical agent desktop (Graphics Server).
 *
 * Draws an agent-native desktop over the linear framebuffer: a top bar, a left
 * sidebar with the four apps (Agents / Model / Files / System), a content area
 * and a status bar. Input comes from the PS/2 mouse (kf_mouse.c) and keyboard
 * (kf_kbd.c). The console shell stays as a fallback; the desktop is primary.
 */
#include "kf_rt.h"

/* --- palette (KDE-ish dark) --- */
#define C_BG       0x12151a
#define C_PANEL    0x1c2129
#define C_PANEL2   0x232a34
#define C_ACCENT   0x3daee9
#define C_GREEN    0x2ee6a6
#define C_TEXT     0xe6edf3
#define C_DIM      0x8a94a6
#define C_HILITE   0x2a3340
#define C_RED      0xf85149

#define W 1280
#define H 800
#define TOPBAR 40
#define SIDEBAR 200
#define STATUSB 28

static int cur_app = 0;      /* 0 Agents, 1 Model, 2 Files, 3 System */
static int sel_pid = 0;      /* selected agent in Agents view */
static int mprev_buttons = 0;

static void draw_desktop(void);
static void draw_app(int x,int y,int w,int h);
static void draw_agents(int x,int y,int w,int h);
static void draw_model(int x,int y,int w,int h);
static void draw_files(int x,int y,int w,int h);
static void draw_system(int x,int y,int w,int h);
static void handle_click(int mx,int my);

/* Model view state */
static int model_a = 0, model_b = 1;
static int model_out = 0;
static char* model_out_s = "?";

/* --- drawing helpers (fast fills + text) --- */
static void rect(int x,int y,int w,int h,int c){ k_fb_fill_rect(x,y,w,h,c); }
static void txt(int x,int y,int fg,int bg,const char* s){ k_fb_text(x,y,fg,bg,s); }

/* a simple clickable button: returns 1 if (mx,my) inside */
static int btn(int x,int y,int w,int h,int mx,int my){ return mx>=x && mx<x+w && my>=y && my<y+h; }

/* redraw the whole desktop */
static void draw_desktop(void){
    rect(0,0,W,H,C_BG);
    /* chrome (top bar + sidebar + status bar) drawn by Kenga code (ui.kenga) */
    k_ui_chrome(W, (int64_t)k_proc_count(), k_time_uptime_ms()/1000,
                (int64_t)k_mouse_x(), (int64_t)k_mouse_y(), (int64_t)cur_app);
    /* content area */
    int cx = SIDEBAR, cy = TOPBAR, cw = W-SIDEBAR, ch = H-TOPBAR-STATUSB;
    rect(cx,cy,cw,ch,C_BG);
    draw_app(cx+16, cy+12, cw-32, ch-24);
}

/* --- cursor: saved snapshot so moves don't need a full redraw --- */
#define CUR_W 7
#define CUR_H 10
static uint32_t cur_save[CUR_W*CUR_H];
static int cur_sx = -1, cur_sy = -1;

static void cursor_save(int mx, int my) {
    for (int j = 0; j < CUR_H; j++)
        for (int i = 0; i < CUR_W; i++)
            cur_save[j*CUR_W+i] = (uint32_t)k_fb_getpixel(mx + i, my + j);
}

static void cursor_restore(void) {
    if (cur_sx < 0) return;
    for (int j = 0; j < CUR_H; j++)
        for (int i = 0; i < CUR_W; i++)
            k_fb_putpixel(cur_sx + i, cur_sy + j, cur_save[j*CUR_W+i]);
}

static void cursor_draw(int mx, int my) {
    cursor_save(mx, my);
    for (int j = 0; j < CUR_H; j++)
        for (int i = 0; i < CUR_W; i++)
            if (i == 0 || j == CUR_H-1 || i == j) k_fb_putpixel(mx + i, my + j, 0xffffff);
    cur_sx = mx; cur_sy = my;
}

/* render the selected app's content */
static void draw_app(int x,int y,int w,int h){
    switch(cur_app){
    case 0: draw_agents(x,y,w,h); break;
    case 1: draw_model(x,y,w,h); break;
    case 2: draw_files(x,y,w,h); break;
    case 3: draw_system(x,y,w,h); break;
    }
}

static void draw_agents(int x,int y,int w,int h){
    txt(x,y, C_TEXT,C_BG, "Agents (process tree)");
    int n = (int)k_proc_count();
    int row = y + 28;
    for(int i=0;i<n && i<20;i++){
        char line[64]; int c=0;
        const char* nm = k_proc_name_at(i);
        int on = (k_proc_pid_at(i)==sel_pid);
        /* draw a selectable row */
        rect(x,row,w,20, on?C_HILITE:C_BG);
        /* pid */
        c=0; const char* d=dec(k_proc_pid_at(i)); for(;*d&&c<60;c++) line[c]=*d; line[c]=0;
        txt(x+6,row+4, C_ACCENT, on?C_HILITE:C_BG, line);
        /* name */
        txt(x+40,row+4, C_TEXT, on?C_HILITE:C_BG, nm);
        /* parent */
        char pa[16]; int c2=0; d=dec(k_proc_parent_at(i)); for(;*d&&c2<15;c2++) pa[c2]=*d; pa[c2]=0;
        txt(x+120,row+4, C_DIM, on?C_HILITE:C_BG, "p:");
        txt(x+134,row+4, C_DIM, on?C_HILITE:C_BG, pa);
        row += 20;
    }
    /* details of selected */
    if(sel_pid>0){
        int i=0; for(;i<n;i++) if(k_proc_pid_at(i)==sel_pid) break;
        int dy = y + 28 + 20*((n>20)?20:n) + 8;
        rect(x,dy,w,h-(dy-y), C_PANEL2);
        txt(x+12,dy+8, C_GREEN, C_PANEL2, "Selected agent");
        char d1[32];
        int c=0; const char* s="PID  "; for(;*s&&c<31;c++) d1[c]=*s; s=dec(sel_pid); for(;*s&&c<31;c++) d1[c]=*s; d1[c]=0;
        txt(x+12,dy+28, C_TEXT, C_PANEL2, d1);
        if(i<n){
            char d2[32]; c=0; s="Name "; for(;*s&&c<31;c++) d2[c]=*s; s=k_proc_name_at(i); for(;*s&&c<31;c++) d2[c]=*s; d2[c]=0;
            txt(x+12,dy+48, C_TEXT, C_PANEL2, d2);
            char d3[32]; c=0; s="Caps "; for(;*s&&c<31;c++) d3[c]=*s; s=dec(k_proc_caps(sel_pid)); for(;*s&&c<31;c++) d3[c]=*s; d3[c]=0;
            txt(x+12,dy+68, C_TEXT, C_PANEL2, d3);
        }
    }
}

static void draw_model(int x,int y,int w,int h){
    txt(x,y, C_TEXT,C_BG, "Kenga Model (MLP, XOR)");
    /* input labels + boxes */
    char a[8]; int i=0; const char* d=dec(model_a); for(;*d&&i<7;i++) a[i]=*d; a[i]=0;
    char b[8]; i=0; d=dec(model_b); for(;*d&&i<7;i++) b[i]=*d; b[i]=0;
    txt(x,y+30, C_DIM, C_BG, "in1"); rect(x+40,y+26,60,24,C_PANEL2); txt(x+48,y+32, C_TEXT,C_PANEL2, a);
    txt(x+130,y+30, C_DIM, C_BG, "in2"); rect(x+170,y+26,60,24,C_PANEL2); txt(x+178,y+32, C_TEXT,C_PANEL2, b);
    /* inference button */
    rect(x,y+60,110,30,C_ACCENT); txt(x+16,y+68, C_BG,C_ACCENT, "Inference");
    /* toggle buttons for inputs (0/1) */
    rect(x+40,y+100,40,24,C_HILITE); txt(x+52,y+106, C_TEXT,C_HILITE, "0");
    rect(x+90,y+100,40,24,C_HILITE); txt(x+102,y+106, C_TEXT,C_HILITE, "1");
    txt(x+170,y+106, C_DIM,C_BG, "in1");
    rect(x+40,y+130,40,24,C_HILITE); txt(x+52,y+136, C_TEXT,C_HILITE, "0");
    rect(x+90,y+130,40,24,C_HILITE); txt(x+102,y+136, C_TEXT,C_HILITE, "1");
    txt(x+170,y+136, C_DIM,C_BG, "in2");
    /* output */
    txt(x,y+180, C_GREEN, C_BG, "output:");
    if(model_out_s){ txt(x+80,y+180, C_TEXT, C_BG, model_out_s); }
}

static void draw_files(int x,int y,int w,int h){
    txt(x,y, C_TEXT,C_BG, "Files (VFS)");
    int n=(int)k_vfs_count();
    for(int i=0;i<n && i<20;i++){
        txt(x+8, y+24+i*20, C_TEXT,C_BG, k_vfs_name(i));
        txt(x+120, y+24+i*20, C_DIM,C_BG, "cat to view");
    }
}

static void draw_system(int x,int y,int w,int h){
    txt(x,y, C_TEXT,C_BG, "System");
    char l[64]; int c=0;
    const char* s="CPU  "; for(;*s&&c<63;c++) l[c]=*s;
    char b[128]; k_hw_cpu_brand(b,sizeof b); for(int k=0;b[k]&&c<63;c++) l[c]=b[k]; l[c]=0;
    txt(x+8,y+24, C_TEXT,C_BG, l);
    c=0; s="RAM frames "; for(;*s&&c<63;c++) l[c]=*s; s=dec(k_mem_pages_free()); for(;*s&&c<63;c++) l[c]=*s; l[c]=0;
    txt(x+8,y+44, C_TEXT,C_BG, l);
    c=0; s="Uptime "; for(;*s&&c<63;c++) l[c]=*s; s=dec(k_time_uptime_ms()/1000); for(;*s&&c<63;c++) l[c]=*s; l[c]=0;
    txt(x+8,y+64, C_TEXT,C_BG, l);
    c=0; s="Kernel KengaOS 0.4 x86_64"; for(;*s&&c<63;c++) l[c]=*s; l[c]=0;
    txt(x+8,y+84, C_DIM,C_BG, l);
}

/* handle a mouse click at (mx,my) */
static void handle_click(int mx,int my){
    /* sidebar app switch */
    if(mx<SIDEBAR && my>TOPBAR && my<H-STATUSB){
        int idx=(my-TOPBAR-12)/44;
        if(idx>=0 && idx<4) cur_app=idx;
        return;
    }
    int x=SIDEBAR+16, y=TOPBAR+12;
    switch(cur_app){
    case 0: {
        /* agents rows */
        int n=(int)k_proc_count(); int row=y+28;
        for(int i=0;i<n && i<20;i++){
            if(mx>=x+6 && mx<x+180 && my>=row && my<row+20){ sel_pid=(int)k_proc_pid_at(i); return; }
            row+=20;
        }
        break; }
    case 1: {
        /* model inputs toggles */
        if(mx>=x+40 && mx<x+80 && my>=y+100 && my<y+124) model_a=0;
        if(mx>=x+90 && mx<x+130 && my>=y+100 && my<y+124) model_a=1;
        if(mx>=x+40 && mx<x+80 && my>=y+130 && my<y+154) model_b=0;
        if(mx>=x+90 && mx<x+130 && my>=y+130 && my<y+154) model_b=1;
        /* inference button */
        if(mx>=x && mx<x+110 && my>=y+60 && my<y+90){
            model_out=(int)k_model_infer(model_a, model_b);
            model_out_s = model_out?dec(1):dec(0);
        }
        break; }
    case 2: {
        /* files: clicking a file cats it to the console (append) - just ignore for now */
        break; }
    }
}

int64_t k_gui_init(void) {
    k_kbd_init();              /* PS/2 keyboard IRQ1 (vector 33) */
    k_fb_con_init();           /* clear screen */
    k_proc_init();             /* spawn logger + agent + model agents */
    k_mouse_init();            /* PS/2 mouse IRQ12 */
    uint8_t imr0;
    __asm__ __volatile__("inb %1, %0" : "=a"(imr0) : "Nd"((uint16_t)0x21));
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)(imr0 & ~0x03)), "Nd"((uint16_t)0x21)); /* ensure IRQ0+IRQ1 unmasked */
    __asm__ __volatile__("sti");               /* enable interrupts (timer/kbd/mouse) */
    /* boot / init screen: wait for ENTER to open the desktop */
    rect(0,0,W,H,C_BG);
    txt(440,300, C_GREEN,C_BG, "KENGAOS");
    txt(400,340, C_DIM,C_BG, "initializing agents...");
    txt(400,364, C_GREEN,C_BG, "kernel  memory  IPC  capabilities  model  graphics");
    txt(500,420, C_DIM,C_BG, "[ ENTER ]  ->  Desktop");
    int64_t boot_at = k_time_uptime_ms();
    uint64_t spins = 0;
    for(;;){
        if(k_kbd_pending()){ int c=(int)k_kbd_read(); if(c=='\n') break; }
        if(k_time_uptime_ms() - boot_at > 2000) break;   /* auto to desktop */
        if(++spins == 5000000ULL){ __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)'U'), "Nd"((uint16_t)0x3F8)); }
        if(++spins > 20000000ULL) break;                 /* safety fallback */
        __asm__ __volatile__("hlt");                     /* sleep until an IRQ */
    }
    /* desktop: draw static layout + first content + cursor */
    draw_desktop();
    cursor_draw((int)k_mouse_x(), (int)k_mouse_y());
    int last_mx = (int)k_mouse_x(), last_my = (int)k_mouse_y();
    int last_app = cur_app, last_btn = 0;
    for(;;){
        int bx = (int)k_mouse_buttons();
        int mx = (int)k_mouse_x(), my = (int)k_mouse_y();
        int clicked = (bx && !mprev_buttons);
        if(clicked || cur_app != last_app){
            handle_click(mx, my);
            cursor_restore();
            draw_desktop();              /* content / app switch */
            cursor_draw(mx, my);
            last_app = cur_app;
        } else if(mx != last_mx || my != last_my){
            cursor_restore();            /* just move the cursor */
            cursor_draw(mx, my);
        }
        mprev_buttons = bx;
        last_mx = mx; last_my = my; last_btn = bx;
        __asm__ __volatile__("hlt");
    }
    return 1;
}
