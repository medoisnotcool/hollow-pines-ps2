/* Software rasteriser backend (desktop testing only): z-buffered gouraud, alpha rects. */
#include "../src/gfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static unsigned char FB[SCR_W * SCR_H * 3]; static unsigned short ZB[SCR_W * SCR_H];
void gfx_init(void) {}
void gfx_begin(unsigned c) { for (int i = 0; i < SCR_W * SCR_H; i++) { FB[i*3] = c >> 16; FB[i*3+1] = c >> 8; FB[i*3+2] = c; ZB[i] = 0; } }
void gfx_end(void) {}
void gfx_rect(float x0, float y0, float x1, float y1, int z, unsigned rgb, int a) {
    int ix0 = (int)floorf(x0 + .5f), iy0 = (int)floorf(y0 + .5f), ix1 = (int)floorf(x1 + .5f), iy1 = (int)floorf(y1 + .5f);
    if (ix0 < 0) ix0 = 0; if (iy0 < 0) iy0 = 0; if (ix1 > SCR_W) ix1 = SCR_W; if (iy1 > SCR_H) iy1 = SCR_H;
    for (int y = iy0; y < iy1; y++) for (int x = ix0; x < ix1; x++) {
        int i = y * SCR_W + x; if (z < ZB[i]) continue; ZB[i] = z;
        for (int k = 0; k < 3; k++) { int s = (rgb >> (16 - 8 * k)) & 255; FB[i*3+k] = (unsigned char)((s * a + FB[i*3+k] * (128 - a)) >> 7); }
    }
}
void gfx_tri(const GVert *a, const GVert *b, const GVert *c) {
    float minx = fminf(a->x, fminf(b->x, c->x)), maxx = fmaxf(a->x, fmaxf(b->x, c->x)), miny = fminf(a->y, fminf(b->y, c->y)), maxy = fmaxf(a->y, fmaxf(b->y, c->y));
    int x0 = (int)fmaxf(0, floorf(minx)), x1 = (int)fminf(SCR_W - 1, ceilf(maxx)), y0 = (int)fmaxf(0, floorf(miny)), y1 = (int)fminf(SCR_H - 1, ceilf(maxy));
    float area = (b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x); if (fabsf(area) < 1e-6f) return;
    float inv = 1.f / area;
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
        float px = x + .5f, py = y + .5f;
        float w0 = ((b->x - px) * (c->y - py) - (b->y - py) * (c->x - px)) * inv, w1 = ((c->x - px) * (a->y - py) - (c->y - py) * (a->x - px)) * inv, w2 = 1 - w0 - w1;
        if (w0 < 0 || w1 < 0 || w2 < 0) continue;
        int z = (int)(w0 * a->z + w1 * b->z + w2 * c->z), i = y * SCR_W + x; if (z < ZB[i]) continue; ZB[i] = z;
        for (int k = 0; k < 3; k++) { int sh = 16 - 8 * k; float v = w0 * ((a->rgb >> sh) & 255) + w1 * ((b->rgb >> sh) & 255) + w2 * ((c->rgb >> sh) & 255); FB[i*3+k] = (unsigned char)(v > 255 ? 255 : v); }
    }
}
void gfx_host_save(const char *path) { FILE *f = fopen(path, "wb"); fprintf(f, "P6\n%d %d\n255\n", SCR_W, SCR_H); fwrite(FB, 1, sizeof FB, f); fclose(f); }
