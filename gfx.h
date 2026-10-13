/* Tiny graphics backend interface. Two implementations:
 *   gfx_ps2.c  - gsKit (real PS2 / PCSX2)
 *   gfx_host.c - software rasteriser used for desktop testing/screenshots
 * Depth: 0 (far) .. 65535 (near), larger wins (matches GS ZTST=GEQUAL). */
#ifndef GFX_H
#define GFX_H
#define SCR_W 640
#define SCR_H 448
typedef struct { float x, y; int z; unsigned rgb; } GVert;   /* rgb = 0xRRGGBB */
void gfx_init(void);
void gfx_begin(unsigned clear_rgb);   /* clears colour + depth */
void gfx_tri(const GVert *a, const GVert *b, const GVert *c);   /* gouraud, depth tested */
void gfx_rect(float x0, float y0, float x1, float y1, int z, unsigned rgb, int alpha); /* alpha 0..128 */
void gfx_end(void);                   /* flush + vsync/flip */
#endif
