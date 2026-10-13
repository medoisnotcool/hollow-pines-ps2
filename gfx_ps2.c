/* PS2 graphics backend: gsKit, 640x448 interlaced (field) NTSC, 24-bit colour, 16-bit Z. */
#include <kernel.h>
#include <gsKit.h>
#include <dmaKit.h>
#include "gfx.h"

/* Some older gsKit releases spell the gouraud 3D triangle call "goraud".
 * If you get an "implicit declaration of gsKit_prim_triangle_gouraud_3d" error, build with
 *   make EE_CFLAGS+=-DGSKIT_OLD_GORAUD */
#ifdef GSKIT_OLD_GORAUD
#define gsKit_prim_triangle_gouraud_3d gsKit_prim_triangle_goraud_3d
#endif

static GSGLOBAL *gs;
#define COL(rgb, a) GS_SETREG_RGBAQ(((rgb) >> 16) & 0xFF, ((rgb) >> 8) & 0xFF, (rgb) & 0xFF, (a), 0)

void gfx_init(void) {
    /* big one-shot render queue: a frame is a few thousand triangles */
    gs = gsKit_init_global_custom(2 * 1024 * 1024, 64 * 1024);
    gs->Mode = GS_MODE_NTSC;
    gs->Interlace = GS_INTERLACED;
    gs->Field = GS_FIELD;
    gs->Width = SCR_W;
    gs->Height = SCR_H;
    gs->PSM = GS_PSM_CT24;
    gs->PSMZ = GS_PSMZ_16S;
    gs->ZBuffering = GS_SETTING_ON;
    gs->DoubleBuffering = GS_SETTING_ON;
    gs->PrimAlphaEnable = GS_SETTING_ON;
    gs->PrimAAEnable = GS_SETTING_OFF;

    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);

    gsKit_init_screen(gs);
    gsKit_mode_switch(gs, GS_ONESHOT);
    gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);   /* (Cs-Cd)*As + Cd */
    gsKit_set_test(gs, GS_ZTEST_ON);
}

void gfx_begin(unsigned rgb) { gsKit_clear(gs, COL(rgb, 0x80)); }

void gfx_tri(const GVert *a, const GVert *b, const GVert *c) {
    gsKit_prim_triangle_gouraud_3d(gs, a->x, a->y, a->z, b->x, b->y, b->z, c->x, c->y, c->z,
                                   COL(a->rgb, 0x80), COL(b->rgb, 0x80), COL(c->rgb, 0x80));
}

void gfx_rect(float x0, float y0, float x1, float y1, int z, unsigned rgb, int alpha) {
    gsKit_prim_sprite(gs, x0, y0, x1, y1, z, COL(rgb, alpha));
}

void gfx_end(void) {
    gsKit_queue_exec(gs);
    gsKit_sync_flip(gs);
}
