/* Bridge the Aurora design primitives to the active framebuffer target. */
#include <stdint.h>

uint32_t* fb = 0;
int fb_w = 0;
int fb_h = 0;
int fb_pitch = 0;

void k_design_fb_sync(uintptr_t pixels, int w, int h, int pitch) {
    fb = (uint32_t*)pixels;
    fb_w = w;
    fb_h = h;
    fb_pitch = pitch;
}

void fb_puts(int x, int y, const char* s, uint32_t fg) {
    (void)x; (void)y; (void)s; (void)fg;
}
