/* Hollow Pines - PlayStation 2 entry point: IOP/pad setup, DualShock mapping, main loop. No audio. */
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <iopcontrol.h>
#include <libpad.h>
#include <time.h>
#include <string.h>
#include "game.h"
#include "gfx.h"

static char padBuf[256] __attribute__((aligned(64)));

static void wait_pad_ready(void) {
    int st;
    do { st = padGetState(0, 0); } while (st != PAD_STATE_STABLE && st != PAD_STATE_FINDCTP1);
}

static void pad_setup(void) {
    SifLoadModule("rom0:SIO2MAN", 0, NULL);
    SifLoadModule("rom0:PADMAN", 0, NULL);
    padInit(0);
    padPortOpen(0, 0, padBuf);
    wait_pad_ready();
    padSetMainMode(0, 0, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK);   /* analog sticks on */
    wait_pad_ready();
}

static float stick(unsigned char raw) {      /* 0..255 -> -1..1 with a radial-friendly dead zone */
    float v = ((float)raw - 128.f) / 127.f;
    float a = v < 0 ? -v : v;
    if (a < .22f) return 0;
    a = (a - .22f) / .78f; if (a > 1) a = 1;
    return v < 0 ? -a : a;
}

static void pad_read(Input *in, unsigned *prev) {
    struct padButtonStatus b;
    unsigned h = 0;
    memset(in, 0, sizeof *in);
    int st = padGetState(0, 0);
    if ((st == PAD_STATE_STABLE || st == PAD_STATE_FINDCTP1) && padRead(0, 0, &b) != 0) {
        unsigned p = 0xffff ^ b.btns;                      /* libpad reports active-low */
        if (p & PAD_UP) h |= B_UP;           if (p & PAD_DOWN) h |= B_DOWN;
        if (p & PAD_LEFT) h |= B_LEFT;       if (p & PAD_RIGHT) h |= B_RIGHT;
        if (p & PAD_CROSS) h |= B_CROSS;     if (p & PAD_CIRCLE) h |= B_CIRCLE;
        if (p & PAD_SQUARE) h |= B_SQUARE;   if (p & PAD_TRIANGLE) h |= B_TRIANGLE;
        if (p & PAD_L1) h |= B_L1;           if (p & PAD_R1) h |= B_R1;
        if (p & PAD_L2) h |= B_L2;           if (p & PAD_R2) h |= B_R2;
        if (p & PAD_START) h |= B_START;     if (p & PAD_SELECT) h |= B_SELECT;
        if (p & PAD_L3) h |= B_L3;           if (p & PAD_R3) h |= B_R3;
        if ((b.mode >> 4) == 0x07) {                       /* DualShock in analog mode */
            in->lx = stick(b.ljoy_h);  in->ly = -stick(b.ljoy_v);
            in->rx = stick(b.rjoy_h);  in->ry = -stick(b.rjoy_v);
        }
    }
    in->btn = h; in->pressed = h & ~*prev; *prev = h;
}

int main(void) {
    SifInitRpc(0);
    while (!SifIopReset("", 0)) {}
    while (!SifIopSync()) {}
    SifInitRpc(0);

    pad_setup();
    gfx_init();
    game_init();
    render_init();

    unsigned prev = 0;
    clock_t last = clock();
    for (;;) {
        Input in;
        pad_read(&in, &prev);
        clock_t now = clock();
        float dt = (float)(now - last) / (float)CLOCKS_PER_SEC; last = now;
        if (dt < 1.f / 120.f) dt = 1.f / 120.f;
        if (dt > .05f) dt = .05f;
        game_update(&in, dt);
        render_frame();            /* ends with vsync + flip */
    }
    return 0;
}
