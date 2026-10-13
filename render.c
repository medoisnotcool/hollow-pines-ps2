/* Hollow Pines - renderer. Does its own 3D transform/clip/fog on the CPU and feeds
 * screen-space gouraud triangles to the gfx backend. */
#include "game.h"
#include "gfx.h"
#include <math.h>
#include <string.h>

typedef struct { float x, y, z; } V3;
typedef struct { float x, y, z, r, g, b; } CV;      /* view-space vertex + colour (0..255) */

#define PI_F 3.14159265f
#define ZN 0.25f
#define ZF 160.f
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

/* ---------- camera ---------- */
static float C_x, C_y, C_z, AX[3], AY[3], AZ[3], FOCAL, TANX;
static int FOGON, NIGHT, TORCH; static float FOGC[3], FOGN, FOGF, LM[3];
static int tri_count;
int render_stat_tris(void) { return tri_count; }

static void cam_setup(float x, float y, float z, float yaw, float pitch, float fov) {
    float sy = sinf(yaw), cy = cosf(yaw), sp = sinf(pitch), cp = cosf(pitch);
    C_x = x; C_y = y; C_z = z;
    AX[0] = cy; AX[1] = 0; AX[2] = -sy;
    AY[0] = sp * sy; AY[1] = cp; AY[2] = sp * cy;
    AZ[0] = -cp * sy; AZ[1] = sp; AZ[2] = -cp * cy;
    float t = tanf(fov * .5f * PI_F / 180.f);
    FOCAL = (SCR_H * .5f) / t; TANX = (SCR_W * .5f) / FOCAL;
}
static V3 tv(float x, float y, float z) {
    float dx = x - C_x, dy = y - C_y, dz = z - C_z; V3 o;
    o.x = dx * AX[0] + dy * AX[1] + dz * AX[2]; o.y = dx * AY[0] + dy * AY[1] + dz * AY[2]; o.z = dx * AZ[0] + dy * AZ[1] + dz * AZ[2];
    return o;
}

/* ---------- emit ---------- */
static unsigned pack_fog(const CV *v) {
    float r = v->r, g = v->g, b = v->b;
    if (FOGON) {
        float d = v->z;
        if (TORCH && d < 24) { float k = 1.f - d / 24.f, m = 1.f + 1.6f * k * k; r *= m * 1.05f; g *= m; b *= m * .8f; }
        float f = clampf((d - FOGN) / (FOGF - FOGN), 0, 1);
        r += (FOGC[0] - r) * f; g += (FOGC[1] - g) * f; b += (FOGC[2] - b) * f;
    }
    int R = (int)clampf(r, 0, 255), Gc = (int)clampf(g, 0, 255), B = (int)clampf(b, 0, 255);
    return (unsigned)(R << 16 | Gc << 8 | B);
}
static int zmap(float z, int mode) {
    if (mode == 0) return (int)(clampf((1.f / z - 1.f / ZF) / (1.f / ZN - 1.f / ZF), 0, 1) * 60000.f);
    return 61000 + (int)(clampf((1.f / z - .5f) / (1.f / .06f - .5f), 0, 1) * 4000.f);
}
static CV lerp_cv(const CV *a, const CV *b, float t) {
    CV o; o.x = a->x + (b->x - a->x) * t; o.y = a->y + (b->y - a->y) * t; o.z = a->z + (b->z - a->z) * t;
    o.r = a->r + (b->r - a->r) * t; o.g = a->g + (b->g - a->g) * t; o.b = a->b + (b->b - a->b) * t; return o;
}
static void poly_emit(const CV *p, int n, float zn, int zmode) {
    CV q[7]; int m = 0;
    for (int i = 0; i < n; i++) {
        const CV *a = &p[i], *b = &p[(i + 1) % n]; int ai = a->z >= zn, bi = b->z >= zn;
        if (ai) q[m++] = *a;
        if (ai != bi) { q[m] = lerp_cv(a, b, (zn - a->z) / (b->z - a->z)); q[m].z = zn; m++; }
    }
    if (m < 3) return;
    GVert gv[7]; float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
    for (int i = 0; i < m; i++) {
        float iz = 1.f / q[i].z;
        gv[i].x = SCR_W * .5f + q[i].x * iz * FOCAL; gv[i].y = SCR_H * .5f - q[i].y * iz * FOCAL;
        gv[i].z = zmap(q[i].z, zmode); gv[i].rgb = pack_fog(&q[i]);
        if (gv[i].x < minx) minx = gv[i].x; if (gv[i].x > maxx) maxx = gv[i].x;
        if (gv[i].y < miny) miny = gv[i].y; if (gv[i].y > maxy) maxy = gv[i].y;
    }
    if (maxx < 0 || minx > SCR_W || maxy < 0 || miny > SCR_H) return;
    for (int i = 1; i < m - 1; i++) { gfx_tri(&gv[0], &gv[i], &gv[i + 1]); tri_count++; }
}
static void tri_cv(CV a, CV b, CV c) { CV p[3] = {a, b, c}; poly_emit(p, 3, ZN, 0); }


/* ---------- 2D helpers (HUD) ---------- */
#define HZ 65535
static void rect(float x0, float y0, float x1, float y1, unsigned rgb, int a) { gfx_rect(x0, y0, x1, y1, HZ, rgb, a); }
static void line2(float x0, float y0, float x1, float y1, float w, unsigned rgb) {
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy); if (l < .01f) return;
    float nx = -dy / l * w * .5f, ny = dx / l * w * .5f;
    GVert a = {x0 + nx, y0 + ny, HZ, rgb}, b = {x0 - nx, y0 - ny, HZ, rgb}, c = {x1 - nx, y1 - ny, HZ, rgb}, d = {x1 + nx, y1 + ny, HZ, rgb};
    gfx_tri(&a, &b, &c); gfx_tri(&a, &c, &d);
}

static const char FCH[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 /.:!-+()%,'?<>=";
static const unsigned char FNT[][7] = {
 {0b01110,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001},{0b11110,0b10001,0b10001,0b11110,0b10001,0b10001,0b11110},
 {0b01110,0b10001,0b10000,0b10000,0b10000,0b10001,0b01110},{0b11110,0b10001,0b10001,0b10001,0b10001,0b10001,0b11110},
 {0b11111,0b10000,0b10000,0b11110,0b10000,0b10000,0b11111},{0b11111,0b10000,0b10000,0b11110,0b10000,0b10000,0b10000},
 {0b01110,0b10001,0b10000,0b10111,0b10001,0b10001,0b01111},{0b10001,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001},
 {0b01110,0b00100,0b00100,0b00100,0b00100,0b00100,0b01110},{0b00111,0b00010,0b00010,0b00010,0b00010,0b10010,0b01100},
 {0b10001,0b10010,0b10100,0b11000,0b10100,0b10010,0b10001},{0b10000,0b10000,0b10000,0b10000,0b10000,0b10000,0b11111},
 {0b10001,0b11011,0b10101,0b10101,0b10001,0b10001,0b10001},{0b10001,0b11001,0b10101,0b10011,0b10001,0b10001,0b10001},
 {0b01110,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110},{0b11110,0b10001,0b10001,0b11110,0b10000,0b10000,0b10000},
 {0b01110,0b10001,0b10001,0b10001,0b10101,0b10010,0b01101},{0b11110,0b10001,0b10001,0b11110,0b10100,0b10010,0b10001},
 {0b01111,0b10000,0b10000,0b01110,0b00001,0b00001,0b11110},{0b11111,0b00100,0b00100,0b00100,0b00100,0b00100,0b00100},
 {0b10001,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110},{0b10001,0b10001,0b10001,0b10001,0b10001,0b01010,0b00100},
 {0b10001,0b10001,0b10001,0b10101,0b10101,0b11011,0b10001},{0b10001,0b10001,0b01010,0b00100,0b01010,0b10001,0b10001},
 {0b10001,0b10001,0b01010,0b00100,0b00100,0b00100,0b00100},{0b11111,0b00001,0b00010,0b00100,0b01000,0b10000,0b11111},
 {0b01110,0b10001,0b10011,0b10101,0b11001,0b10001,0b01110},{0b00100,0b01100,0b00100,0b00100,0b00100,0b00100,0b01110},
 {0b01110,0b10001,0b00001,0b00010,0b00100,0b01000,0b11111},{0b11110,0b00001,0b00001,0b01110,0b00001,0b00001,0b11110},
 {0b00010,0b00110,0b01010,0b10010,0b11111,0b00010,0b00010},{0b11111,0b10000,0b11110,0b00001,0b00001,0b10001,0b01110},
 {0b00110,0b01000,0b10000,0b11110,0b10001,0b10001,0b01110},{0b11111,0b00001,0b00010,0b00100,0b01000,0b01000,0b01000},
 {0b01110,0b10001,0b10001,0b01110,0b10001,0b10001,0b01110},{0b01110,0b10001,0b10001,0b01111,0b00001,0b00010,0b01100},
 {0,0,0,0,0,0,0},{0b00001,0b00001,0b00010,0b00100,0b01000,0b10000,0b10000},{0,0,0,0,0,0b01100,0b01100},
 {0,0b01100,0b01100,0,0b01100,0b01100,0},{0b00100,0b00100,0b00100,0b00100,0b00100,0,0b00100},
 {0,0,0,0b11111,0,0,0},{0,0b00100,0b00100,0b11111,0b00100,0b00100,0},
 {0b00010,0b00100,0b01000,0b01000,0b01000,0b00100,0b00010},{0b01000,0b00100,0b00010,0b00010,0b00010,0b00100,0b01000},
 {0b11001,0b11010,0b00010,0b00100,0b01000,0b01011,0b10011},{0,0,0,0,0b01100,0b00100,0b01000},
 {0b00100,0b00100,0b01000,0,0,0,0},{0b01110,0b10001,0b00001,0b00010,0b00100,0,0b00100},
 {0b00010,0b00100,0b01000,0b10000,0b01000,0b00100,0b00010},{0b01000,0b00100,0b00010,0b00001,0b00010,0b00100,0b01000},
 {0,0,0b11111,0,0b11111,0,0}};

static float text_w(const char *s, int sc) { return (float)(strlen(s) * 6 * sc - sc); }
static void text(float x, float y, int sc, unsigned rgb, int a, const char *s) {
    for (; *s; s++, x += 6 * sc) {
        const char *p = strchr(FCH, *s >= 'a' && *s <= 'z' ? *s - 32 : *s); if (!p) continue;
        const unsigned char *g = FNT[p - FCH];
        for (int r = 0; r < 7; r++) for (int c = 0; c < 5;) {
            if (!(g[r] >> (4 - c) & 1)) { c++; continue; }
            int e = c; while (e < 5 && (g[r] >> (4 - e) & 1)) e++;
            rect(x + c * sc, y + r * sc, x + e * sc, y + (r + 1) * sc, rgb, a); c = e;
        }
    }
}
static void text_sh(float x, float y, int sc, unsigned rgb, const char *s) { text(x + sc, y + sc, sc, 0x000000, 90, s); text(x, y, sc, rgb, 128, s); }
static void text_c(float cx, float y, int sc, unsigned rgb, const char *s) { text_sh(cx - text_w(s, sc) * .5f, y, sc, rgb, s); }

/* ---------- scene lighting ---------- */
static void scene_setup(void) {
    NIGHT = G.night; FOGON = 1; TORCH = G.night;
    if (G.night) { FOGC[0] = 7; FOGC[1] = 12; FOGC[2] = 26; FOGN = 6; FOGF = 70; LM[0] = .30f; LM[1] = .38f; LM[2] = .62f; }
    else { FOGC[0] = 169; FOGC[1] = 207; FOGC[2] = 230; FOGN = 22; FOGF = 100; LM[0] = 1.25f; LM[1] = 1.25f; LM[2] = 1.15f; }
}
static CV litcv(V3 v, float r, float g, float b, float k) { CV o = {v.x, v.y, v.z, r * LM[0] * k, g * LM[1] * k, b * LM[2] * k}; return o; }

static void draw_sky(float pitch) {
    float hy = clampf(SCR_H * .5f + FOCAL * tanf(pitch), -50, SCR_H + 50);
    unsigned top = G.night ? 0x040716 : 0x5a8cc8, hor = G.night ? 0x070c1a : 0xa9cfe6;
    GVert a = {0, 0, 0, top}, b = {SCR_W, 0, 0, top}, c = {SCR_W, hy, 0, hor}, d = {0, hy, 0, hor};
    gfx_tri(&a, &b, &c); gfx_tri(&a, &c, &d);
    if (hy < SCR_H) gfx_rect(0, hy, SCR_W, SCR_H, 0, hor, 128);
    if (G.night) {      /* a handful of stars */
        unsigned s = 12345;
        for (int i = 0; i < 50; i++) { s = s * 1664525u + 1013904223u; float x = (float)(s >> 16 & 1023) / 1023.f * SCR_W; s = s * 1664525u + 1013904223u; float y = (float)(s >> 16 & 1023) / 1023.f * hy * .9f; gfx_rect(x, y, x + 2, y + 2, 1, 0xb0b8d0, 128); }
    }
}

static void draw_ground(void) {
    const float GT = 10.f; int gx0 = (int)floorf(C_x / GT), gz0 = (int)floorf(C_z / GT);
    for (int j = -11; j <= 11; j++) for (int i = -11; i <= 11; i++) {
        float x0 = (gx0 + i) * GT, z0 = (gz0 + j) * GT; V3 c = tv(x0 + 5, 0, z0 + 5);
        if (c.z < -GT * .9f || c.z > FOGF + GT || fabsf(c.x) > c.z * TANX + GT * 1.3f) continue;
        unsigned h = (unsigned)(gx0 + i) * 73856093u ^ (unsigned)(gz0 + j) * 19349663u; h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
        float k = .86f + .14f * (float)(h & 255) / 255.f, gr = 61 * k, gg = 122 * k, gb = 44 * k;
        CV v0 = litcv(tv(x0, 0, z0), gr, gg, gb, 1), v1 = litcv(tv(x0 + GT, 0, z0), gr, gg, gb, 1),
           v2 = litcv(tv(x0 + GT, 0, z0 + GT), gr, gg, gb, 1), v3 = litcv(tv(x0, 0, z0 + GT), gr, gg, gb, 1);
        tri_cv(v0, v1, v2); tri_cv(v0, v2, v3);
    }
}

/* ---------- trees ---------- */
static float RC[7][6], RS[7][6], TRC[4], TRS[4];   /* unit-circle lookups (filled in render_init) */
static void cone(const Tree *t, float y0, float y1, float rad, int sides, int under, float r, float g, float b) {
    CV ring[6]; float s = t->s;
    for (int k = 0; k < sides; k++) {
        float ca = RC[sides][k], sa = RS[sides][k];
        float rx = ca * t->cr - sa * t->sr, rz = ca * t->sr + sa * t->cr;
        ring[k] = litcv(tv(t->x + rx * rad * s, y0 * s, t->z + rz * rad * s), r, g, b, .55f);
    }
    CV tip = litcv(tv(t->x, y1 * s, t->z), r, g, b, 1.25f), ctr = litcv(tv(t->x, y0 * s, t->z), r, g, b, .3f);
    for (int k = 0; k < sides; k++) {
        int n = (k + 1) % sides; tri_cv(ring[k], ring[n], tip);
        if (under) tri_cv(ring[n], ring[k], ctr);
    }
}
static void draw_tree(const Tree *t, float dist) {
    float s = t->s; const int near_lod = dist < 42.f;
    float r = t->r * 1.3f, g = t->g * 1.3f, b = t->b * 1.3f;
    if (near_lod) {
        CV bot[4], top[4];
        for (int k = 0; k < 4; k++) {
            float ca = TRC[k], sa = TRS[k], rx = ca * t->cr - sa * t->sr, rz = ca * t->sr + sa * t->cr;
            bot[k] = litcv(tv(t->x + rx * .4f * s, 0, t->z + rz * .4f * s), 91, 58, 30, .7f);
            top[k] = litcv(tv(t->x + rx * .25f * s, 3 * s, t->z + rz * .25f * s), 91, 58, 30, 1.f);
        }
        for (int k = 0; k < 4; k++) { int n = (k + 1) & 3; tri_cv(bot[k], bot[n], top[n]); tri_cv(bot[k], top[n], top[k]); }
        cone(t, 2.f, 6.f, 2.2f, 6, 1, r, g, b);
        cone(t, 4.6f, 7.8f, 1.5f, 6, 1, r, g, b);
    } else {
        CV a = litcv(tv(t->x - .3f * s, 0, t->z), 91, 58, 30, .7f), bb = litcv(tv(t->x + .3f * s, 0, t->z), 91, 58, 30, .7f), c = litcv(tv(t->x, 2.2f * s, t->z), 91, 58, 30, 1.f);
        tri_cv(a, bb, c);
        cone(t, 2.f, 6.f, 2.2f, 4, 0, r, g, b);
        cone(t, 4.6f, 7.8f, 1.5f, 4, 0, r, g, b);
    }
}
static void draw_trees(void) {
    for (int ci = 0; ci < NCH; ci++) {
        const Chunk *c = &G.ch[ci]; if (!c->valid) continue;
        for (int k = 0; k < c->n; k++) {
            const Tree *t = &c->t[k];
            float dx = t->x - C_x, dz = t->z - C_z, d2 = dx * dx + dz * dz;
            if (d2 > (FOGF + 4) * (FOGF + 4)) continue;
            V3 v = tv(t->x, 3 * t->s, t->z);
            if (v.z < -4.f || v.z > FOGF + 6 || fabsf(v.x) > (v.z > 0 ? v.z : 0) * TANX + 4.5f) continue;
            draw_tree(t, sqrtf(d2));
        }
    }
}
static void draw_apples(void) {
    for (int ci = 0; ci < NCH; ci++) {
        const Chunk *c = &G.ch[ci]; if (!c->valid) continue;
        for (int k = 0; k < c->n; k++) {
            const Tree *t = &c->t[k]; if (!t->apple) continue;
            float dx = t->ax - C_x, dz = t->az - C_z; if (dx * dx + dz * dz > 38 * 38) continue;
            V3 v = tv(t->ax, .2f, t->az); if (v.z < .4f || fabsf(v.x) > v.z * TANX + 1) continue;
            CV p[6]; CV ctr = {v.x, v.y, v.z, 216 * LM[0], 40 * LM[1], 30 * LM[2]};
            for (int i = 0; i < 6; i++) { float a = i * 1.0471976f; p[i] = ctr; p[i].x += cosf(a) * .2f; p[i].y += sinf(a) * .2f; }
            for (int i = 0; i < 6; i++) { CV hl = ctr; hl.r += 25; hl.g += 20; hl.b += 20; hl.x -= .04f; hl.y += .05f; CV t3[3] = {ctr, p[i], p[(i + 1) % 6]}; poly_emit(t3, 3, .3f, 0); (void)hl; }
            CV st = ctr; st.r = 74; st.g = 45; st.b = 18;
            CV s3[3] = {{v.x - .015f, v.y + .18f, v.z, st.r, st.g, st.b}, {v.x + .015f, v.y + .18f, v.z, st.r, st.g, st.b}, {v.x, v.y + .3f, v.z, st.r, st.g, st.b}};
            poly_emit(s3, 3, .3f, 0);
        }
    }
}

/* ---------- monster art (vector versions of the web version's canvas sprites) ---------- */
#include <stdint.h>
#include <stdio.h>
typedef struct { uint8_t k; float a, b, c, d, e, f; unsigned rgb; } Prim;   /* k: 0 ellipse 1 tri 2 line */
static Prim ART[3][120]; static int NART[3]; static unsigned GPAL[3][3];
#define SLOT(n) (0x1000000u | (n))
static void pe(int t, float cx, float cy, float rx, float ry, unsigned c) { Prim *p = &ART[t][NART[t]++]; p->k = 0; p->a = cx; p->b = cy; p->c = rx; p->d = ry; p->rgb = c; }
static void pt(int t, float x0, float y0, float x1, float y1, float x2, float y2, unsigned c) { Prim *p = &ART[t][NART[t]++]; p->k = 1; p->a = x0; p->b = y0; p->c = x1; p->d = y1; p->e = x2; p->f = y2; p->rgb = c; }
static void pl(int t, float x0, float y0, float x1, float y1, float w, unsigned c) { Prim *p = &ART[t][NART[t]++]; p->k = 2; p->a = x0; p->b = y0; p->c = x1; p->d = y1; p->e = w; p->rgb = c; }
static void qb(float x0, float y0, float cx, float cy, float x1, float y1, float t, float *ox, float *oy) {
    float u = 1 - t; *ox = u * u * x0 + 2 * u * t * cx + t * t * x1; *oy = u * u * y0 + 2 * u * t * cy + t * t * y1;
}
static unsigned hsl_pack(float h, float s, float l) {
    float q = l < .5f ? l * (1 + s) : l + s - l * s, p = 2 * l - q, t[3] = {h + 1.f / 3, h, h - 1.f / 3}, o[3];
    for (int i = 0; i < 3; i++) { float x = t[i]; if (x < 0) x += 1; if (x > 1) x -= 1; o[i] = x < 1.f / 6 ? p + (q - p) * 6 * x : x < .5f ? q : x < 2.f / 3 ? p + (q - p) * (2.f / 3 - x) * 6 : p; }
    return (unsigned)((int)(o[0] * 255) << 16 | (int)(o[1] * 255) << 8 | (int)(o[2] * 255));
}
void render_init(void) {
    for (int s = 3; s <= 6; s++) for (int k = 0; k < s; k++) { RC[s][k] = cosf(k * 6.2831853f / s); RS[s][k] = sinf(k * 6.2831853f / s); }
    for (int k = 0; k < 4; k++) { TRC[k] = cosf(k * 1.5707963f + .7853982f); TRS[k] = sinf(k * 1.5707963f + .7853982f); }
    static const float hues[3] = {100, 160, 30};
    for (int v = 0; v < 3; v++) { GPAL[v][0] = hsl_pack(hues[v] / 360, .5f, .25f); GPAL[v][1] = hsl_pack(hues[v] / 360, .55f, .38f); GPAL[v][2] = hsl_pack(hues[v] / 360, .45f, .5f); }
    /* goblin */
    pe(0, 36, 118, 14, 8, SLOT(0)); pe(0, 92, 118, 14, 8, SLOT(0)); pe(0, 64, 76, 42, 46, SLOT(1)); pe(0, 64, 92, 26, 26, SLOT(2));
    pt(0, 34, 46, 28, 8, 54, 38, 0xe8e0c0); pt(0, 94, 46, 100, 8, 74, 38, 0xe8e0c0);
    pe(0, 46, 64, 12, 13, 0xffffff); pe(0, 82, 64, 12, 13, 0xffffff); pe(0, 48, 66, 5, 6, 0xcc0000); pe(0, 80, 66, 5, 6, 0xcc0000);
    pe(0, 64, 92, 20, 11, 0x2a0a0a);
    for (int i = 0; i < 4; i++) pt(0, 50 + i * 9, 83, 55 + i * 9, 83, 52.5f + i * 9, 92, 0xffffff);
    /* night archer */
    { float px[40], py[40]; int n = 0;
      for (int i = 0; i <= 8; i++) qb(64, 6, 100, 30, 104, 122, i / 8.f, &px[n], &py[n]), n++;
      px[n] = 24; py[n] = 122; n++;
      for (int i = 1; i < 8; i++) qb(24, 122, 28, 30, 64, 6, i / 8.f, &px[n], &py[n]), n++;
      for (int i = 0; i < n; i++) pt(1, 64, 70, px[i], py[i], px[(i + 1) % n], py[(i + 1) % n], 0x2a1f3d); }
    pe(1, 60, 44, 20, 26, 0x07040d); pe(1, 50, 44, 5, 4, 0xff9a1a); pe(1, 70, 44, 5, 4, 0xff9a1a);
    for (int i = 0; i < 10; i++) { float a0 = -.9f + i * .18f, a1 = a0 + .18f; pl(1, 60 + 46 * cosf(a0), 70 + 46 * sinf(a0), 60 + 46 * cosf(a1), 70 + 46 * sinf(a1), 5, 0x8a5a2a); }
    pl(1, 88, 34, 88, 106, 1.5f, 0xdddddd);
    /* tall man */
    pl(2, 40, 106, 16, 234, 4, 0xd8d2c4); pl(2, 88, 106, 112, 240, 4, 0xd8d2c4);
    for (int i = 0; i < 4; i++) { pl(2, 16, 234, 8 + i * 4, 252, 1.5f, 0xd8d2c4); pl(2, 112, 240, 104 + i * 4, 256, 1.5f, 0xd8d2c4); }
    pl(2, 54, 176, 50, 250, 4, 0xd8d2c4); pl(2, 74, 176, 80, 250, 4, 0xd8d2c4);
    { float q[4][2] = {{40, 104}, {88, 104}, {80, 182}, {48, 182}};
      pt(2, q[0][0], q[0][1], q[1][0], q[1][1], q[2][0], q[2][1], 0x1b1b20); pt(2, q[0][0], q[0][1], q[2][0], q[2][1], q[3][0], q[3][1], 0x1b1b20); }
    pt(2, 56, 104, 72, 104, 64, 140, 0xeeeeee); pl(2, 64, 112, 64, 152, 4, 0xa01010);
    pl(2, 64, 74, 64, 106, 8, 0xd8d2c4);
    pe(2, 64, 40, 26, 36, 0xe6dfd0); pe(2, 50, 32, 10, 12, 0x000000); pe(2, 52, 30, 2, 2, 0xffffff); pe(2, 80, 44, 5, 5, 0x000000);
    { float lx = 36, ly = 60; for (int i = 1; i <= 8; i++) { float x, y; qb(36, 60, 66, 84, 94, 54, i / 8.f, &x, &y); pl(2, lx, ly, x, y, 2, 0x000000); if (i < 8) pl(2, x, y, x, y - 6, 1.5f, 0x000000); lx = x; ly = y; } }
}

static struct { V3 c; float ux, uy, cr, sr, Wt, Ht; } SC;
static V3 spt(float u, float v) {
    float ox = (u - SC.Wt * .5f) * SC.ux, oy = (SC.Ht * .5f - v) * SC.uy;
    V3 o = {SC.c.x + ox * SC.cr - oy * SC.sr, SC.c.y + ox * SC.sr + oy * SC.cr, SC.c.z}; return o;
}
static CV scv(V3 v, unsigned rgb, int var, int tint) {
    if (rgb >> 24) rgb = GPAL[var][rgb & 3];
    float r = (float)(rgb >> 16 & 255), g = (float)(rgb >> 8 & 255), b = (float)(rgb & 255);
    if (tint) { g *= .38f; b *= .38f; }
    CV o = {v.x, v.y, v.z, r, g, b}; return o;
}
static void art_draw(int t, int var, V3 c, float sw, float sh, float rot, float sx, float sy, int tint) {
    SC.c = c; SC.Wt = 128; SC.Ht = t == 2 ? 256.f : 128.f; SC.ux = sw * sx / SC.Wt; SC.uy = sh * sy / SC.Ht; SC.cr = cosf(rot); SC.sr = sinf(rot);
    int segs = c.z > 35 ? 8 : 12;
    for (int i = 0; i < NART[t]; i++) {
        const Prim *p = &ART[t][i];
        if (p->k == 0) {
            CV ctr = scv(spt(p->a, p->b), p->rgb, var, tint), ring[12];
            for (int k = 0; k < segs; k++) { float a = k * 6.2831853f / segs; ring[k] = scv(spt(p->a + p->c * cosf(a), p->b + p->d * sinf(a)), p->rgb, var, tint); }
            for (int k = 0; k < segs; k++) { CV tr[3] = {ctr, ring[k], ring[(k + 1) % segs]}; poly_emit(tr, 3, .3f, 0); }
        } else if (p->k == 1) {
            CV tr[3] = {scv(spt(p->a, p->b), p->rgb, var, tint), scv(spt(p->c, p->d), p->rgb, var, tint), scv(spt(p->e, p->f), p->rgb, var, tint)};
            poly_emit(tr, 3, .3f, 0);
        } else {
            float dx = p->c - p->a, dy = p->d - p->b, l = sqrtf(dx * dx + dy * dy); if (l < .01f) continue;
            float nx = -dy / l * p->e * .5f, ny = dx / l * p->e * .5f;
            CV q[4] = {scv(spt(p->a + nx, p->b + ny), p->rgb, var, tint), scv(spt(p->a - nx, p->b - ny), p->rgb, var, tint),
                       scv(spt(p->c - nx, p->d - ny), p->rgb, var, tint), scv(spt(p->c + nx, p->d + ny), p->rgb, var, tint)};
            poly_emit(q, 4, .3f, 0);
        }
    }
}
static void vquad(V3 c, float hw, float hh, float r, float g, float b) {
    CV q[4] = {{c.x - hw, c.y + hh, c.z, r, g, b}, {c.x + hw, c.y + hh, c.z, r, g, b}, {c.x + hw, c.y - hh, c.z, r, g, b}, {c.x - hw, c.y - hh, c.z, r, g, b}};
    poly_emit(q, 4, .3f, 0);
}
static unsigned frs = 777;
static float frnd(void) { frs = frs * 1664525u + 1013904223u; return (float)(frs >> 8) / 16777216.f; }

static void draw_monsters(void) {
    int idx[MAXMON]; float zz[MAXMON]; int n = 0;
    for (int i = 0; i < G.nmon; i++) {
        const Mon *m = &G.mon[i]; V3 v = tv(m->x, TYPES[m->type].cy, m->z);
        if (v.z < .8f || v.z > FOGF + 5 || fabsf(v.x) > v.z * TANX + 4) continue;
        int j = n++; while (j > 0 && zz[j - 1] < v.z) { zz[j] = zz[j - 1]; idx[j] = idx[j - 1]; j--; } zz[j] = v.z; idx[j] = i;
    }
    for (int q = 0; q < n; q++) {
        const Mon *m = &G.mon[idx[q]]; const TypeDef *T = &TYPES[m->type];
        float bob = sinf(G.t * 7 + m->ph), cyw = T->y + fabsf(bob) * .15f, rot = bob * .08f, sx = 1, sy = 1;
        if (m->type == MT_M && frnd() < .05f) { sx = 1 + frnd() * .7f; sy = .92f; rot = (frnd() - .5f) * .35f; }
        V3 c = tv(m->x, cyw, m->z);
        art_draw(m->type, m->variant, c, T->sw, T->sh, rot, sx, sy, m->hurt > 0);
        V3 bc = tv(m->x, T->by, m->z); float pct = clampf(m->hp / m->max, 0, 1);
        vquad(bc, .78f, .115f, 15, 15, 15);
        vquad((V3){bc.x - .75f * (1 - pct), bc.y, bc.z}, .75f * pct, .085f, pct > .5f ? 90 : pct > .25f ? 230 : 210, pct > .5f ? 200 : pct > .25f ? 150 : 50, pct > .5f ? 70 : 40);
    }
}
static void draw_arrows(void) {
    for (int i = 0; i < G.narr; i++) {
        const Arrow *a = &G.arr[i]; float sp = sqrtf(a->vx * a->vx + a->vy * a->vy + a->vz * a->vz); if (sp < .01f) continue;
        float k = 1.2f / sp; V3 p0 = tv(a->x, a->y, a->z), p1 = tv(a->x + a->vx * k, a->y + a->vy * k, a->z + a->vz * k);
        if (p0.z < .4f || p1.z < .4f) continue;
        float s0x = SCR_W * .5f + p0.x / p0.z * FOCAL, s0y = SCR_H * .5f - p0.y / p0.z * FOCAL, s1x = SCR_W * .5f + p1.x / p1.z * FOCAL, s1y = SCR_H * .5f - p1.y / p1.z * FOCAL;
        float zm = (p0.z + p1.z) * .5f, w = fmaxf(1.6f, .07f * FOCAL / zm), dx = s1x - s0x, dy = s1y - s0y, l = sqrtf(dx * dx + dy * dy);
        if (l < .01f) { dx = 1; dy = 0; l = 1; }
        float nx = -dy / l * w * .5f, ny = dx / l * w * .5f; int z = zmap(zm, 0);
        CV f = {0, 0, zm, a->b ? 255.f : 217.f, a->b ? 240.f : 184.f, a->b ? 150.f : 120.f}; unsigned col = pack_fog(&f), tc = a->b ? 0xffffff : 0xb4b4be;
        GVert q0 = {s0x + nx, s0y + ny, z, col}, q1 = {s0x - nx, s0y - ny, z, col}, q2 = {s1x - nx, s1y - ny, z, col}, q3 = {s1x + nx, s1y + ny, z, col};
        gfx_tri(&q0, &q1, &q2); gfx_tri(&q0, &q2, &q3);
        float ux = dx / l, uy = dy / l; GVert t0 = {s1x + nx * 2.2f, s1y + ny * 2.2f, z, tc}, t1 = {s1x - nx * 2.2f, s1y - ny * 2.2f, z, tc}, t2 = {s1x + ux * w * 3.f, s1y + uy * w * 3.f, z, tc};
        gfx_tri(&t0, &t1, &t2); tri_count += 3;
    }
}

/* ---------- first-person model ---------- */
static float GCS[3][2]; static V3 GP;
static void group_set(float rx, float ry, float rz, float x, float y, float z) {
    GCS[0][0] = cosf(rx); GCS[0][1] = sinf(rx); GCS[1][0] = cosf(ry); GCS[1][1] = sinf(ry); GCS[2][0] = cosf(rz); GCS[2][1] = sinf(rz); GP = (V3){x, y, z};
}
static V3 gdir(V3 v) {      /* Euler XYZ (Rx*Ry*Rz), then flip z so +z = away from camera */
    V3 t = {v.x * GCS[2][0] - v.y * GCS[2][1], v.x * GCS[2][1] + v.y * GCS[2][0], v.z};
    V3 u = {t.x * GCS[1][0] + t.z * GCS[1][1], t.y, -t.x * GCS[1][1] + t.z * GCS[1][0]};
    V3 w = {u.x, u.y * GCS[0][0] - u.z * GCS[0][1], u.y * GCS[0][1] + u.z * GCS[0][0]};
    w.z = -w.z; return w;
}
static V3 gpt(V3 v) { V3 w = gdir(v); w.x += GP.x; w.y += GP.y; w.z -= GP.z; return w; }
static V3 vadd(V3 a, V3 b, float s) { return (V3){a.x + b.x * s, a.y + b.y * s, a.z + b.z * s}; }
static V3 vcross(V3 a, V3 b) { return (V3){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static V3 vnorm(V3 a) { float l = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z); if (l < 1e-6f) return a; return (V3){a.x / l, a.y / l, a.z / l}; }

static void vbox(V3 c, V3 u, V3 v, V3 w, unsigned rgb) {
    V3 ax[3] = {u, v, w}; float night = G.night ? .6f : 1.f;
    for (int a = 0; a < 3; a++) for (int s = -1; s <= 1; s += 2) {
        V3 e = ax[a], o1 = ax[(a + 1) % 3], o2 = ax[(a + 2) % 3], fc = vadd(c, e, (float)s);
        V3 nv = gdir(vnorm((V3){e.x * s, e.y * s, e.z * s})), fcv = gpt(fc);
        if (nv.x * fcv.x + nv.y * fcv.y + nv.z * fcv.z >= 0) continue;
        float lam = -.3f * nv.x + .7f * nv.y - .6f * nv.z; if (lam < 0) lam = 0;
        float k = (.5f + .5f * lam) * night * 1.15f, r = (rgb >> 16 & 255) * k, g = (rgb >> 8 & 255) * k, b = (rgb & 255) * k;
        V3 cs[4] = {vadd(vadd(fc, o1, 1), o2, 1), vadd(vadd(fc, o1, -1), o2, 1), vadd(vadd(fc, o1, -1), o2, -1), vadd(vadd(fc, o1, 1), o2, -1)};
        CV q[4]; for (int i = 0; i < 4; i++) { V3 p = gpt(cs[i]); q[i] = (CV){p.x, p.y, p.z, r, g, b}; }
        poly_emit(q, 4, .06f, 1);
    }
}
static void vbox_a(float cx, float cy, float cz, float hx, float hy, float hz, float rx, unsigned rgb) {
    float c = cosf(rx), s = sinf(rx);
    vbox((V3){cx, cy, cz}, (V3){hx, 0, 0}, (V3){0, hy * c, hy * s}, (V3){0, -hz * s, hz * c}, rgb);
}
static void limb(float ax, float ay, float az, float dx, float dy, float dz, float th) {
    V3 d = vnorm((V3){dx, dy, dz}), up = {0, 1, 0}; V3 u = vnorm(vcross(up, d)); V3 v = vnorm(vcross(d, u));
    V3 mid = vadd((V3){ax, ay, az}, d, 1.5f);
    vbox(mid, (V3){u.x * th * .5f, u.y * th * .5f, u.z * th * .5f}, (V3){v.x * th * .5f, v.y * th * .5f, v.z * th * .5f}, (V3){d.x * 1.5f, d.y * 1.5f, d.z * 1.5f}, 0x3b4a2e);
}
static void hand(float x, float y, float z, float w, float h, float d) { vbox_a(x, y, z, w * .5f, h * .5f, d * .5f, 0, 0xe0b090); }
static void flash(float x, float y, float z) {
    float sc = .8f + frnd() * .6f, h = .1f * sc;
    for (int p = 0; p < 2; p++) {
        float a = p * .8f + frnd() * 6.28f, c = cosf(a) * h, s = sinf(a) * h; CV q[4]; V3 pts[4] = {{-c + s, -s - c, 0}, {c + s, s - c, 0}, {c - s, s + c, 0}, {-c - s, -s + c, 0}};
        for (int i = 0; i < 4; i++) { V3 pp = gpt((V3){x + pts[i].x, y + pts[i].y, z}); q[i] = (CV){pp.x, pp.y, pp.z, 255, 208, 112}; }
        poly_emit(q, 4, .06f, 1);
    }
}
static float es(float k) { k = clampf(k, 0, 1); return k * k * (3 - 2 * k); }
typedef struct { float t; unsigned col; int x0, x1, y0, y1; float ang; } PB;
#define KK .00048f
static const PB AKP[] = {
    {.024f,0x1d1d20,100,600,213,250,0},{.034f,0x1d1d20,60,180,205,260,0},{.012f,0x1d1d20,100,170,105,215,0},{.026f,0x1d1d20,92,172,100,150,0},
    {.034f,0x34373c,320,420,195,250,0},{.03f,0x34373c,380,600,150,200,0},{.04f,0x7a3b1e,600,890,130,200,0},{.052f,0x7a3b1e,610,1000,205,320,0},
    {.04f,0x34373c,900,1065,130,205,0},{.016f,0x1d1d20,930,1070,112,135,0},{.05f,0x34373c,1060,1680,150,245,0},{.054f,0x34373c,1010,1690,240,345,0},
    {.012f,0x34373c,1350,1500,398,420,0},{.05f,0x34373c,1690,1740,240,345,0},{.045f,0x5b2d14,1500,1680,345,610,-.25f},
    {.044f,0x2a2622,1200,1320,330,460,.12f},{.044f,0x2a2622,1120,1245,440,570,.3f},{.044f,0x2a2622,1035,1165,540,660,.5f},{.044f,0x2a2622,955,1085,630,750,.7f},
    {.045f,0x7a3b1e,1730,2060,255,420,0},{.05f,0x7a3b1e,2000,2200,340,520,0},{.05f,0x7a3b1e,2080,2230,300,590,.22f}};

static void draw_viewmodel(void) {
    FOGON = 0;
    const float hip[2][3] = {{.2f, -.21f, -.5f}, {.17f, -.17f, -.4f}}, aimp[2][3] = {{0, -.07f, -.42f}, {0, -.052f, -.3f}};
    int w = G.cur; float A = G.aim, b = sinf(G.bobT);
    float rl = G.reload > 0 ? sinf((1 - G.reload / 1.4f) * PI_F) : 0, sd = G.swT > 0 ? fmaxf(0.f, 1 - fabsf(G.swT - .2f) / .2f) : 0, ed = 0;
    if (G.eatT > 0) ed += fminf(1.f, fminf((3 - G.eatT) / .35f, G.eatT / .4f));
    if (G.punchT > 0) ed += fminf(1.f, fminf((G.punchD - G.punchT) / .08f, G.punchT / .08f));
    float a = 1 - A * .85f;
    float px = hip[w][0] + (aimp[w][0] - hip[w][0]) * A + b * .012f * G.bobA * a - .04f * G.sprA;
    float py = hip[w][1] + (aimp[w][1] - hip[w][1]) * A - fabsf(cosf(G.bobT)) * .014f * G.bobA * a - .05f * G.sprA - clampf(G.vy * .006f, -.08f, .08f) - rl * .2f - sd * .4f - ed * .8f;
    float pz = hip[w][2] + (aimp[w][2] - hip[w][2]) * A + G.rec * .07f * (1 - A * .5f);
    float rx = G.rec * .12f * (1 - A * .5f) - G.sprA * .15f - rl * .9f - sd * 1.f - ed * 1.2f + clampf(G.vy * .01f, -.12f, .12f);
    group_set(rx, G.sprA * .6f, rl * .5f, px, py, pz);
    if (w == 0) {
        for (unsigned i = 0; i < sizeof AKP / sizeof *AKP; i++) {
            const PB *p = &AKP[i];
            vbox_a(0, (225 - (p->y0 + p->y1) * .5f) * KK + .012f, ((p->x0 + p->x1) * .5f - 1350) * KK, p->t * .5f, (p->y1 - p->y0) * KK * .5f, (p->x1 - p->x0) * KK * .5f, p->ang, p->col);
        }
        hand(0, -.12f, .125f, .065f, .065f, .09f); hand(-.02f, -.055f, -.27f, .065f, .065f, .1f);
        limb(0, -.13f, .19f, .25f, -.6f, .76f, .07f); limb(-.02f, -.065f, -.21f, -.45f, -.7f, .9f, .07f);
        if (G.fl > 0) flash(0, .012f, -.64f);
    } else {
        vbox_a(0, .02f, -.05f, .02f, .0225f, .1f, 0, 0x1c1c1c); vbox_a(0, -.012f, -.04f, .018f, .015f, .1f, 0, 0x2c2c2c);
        vbox_a(0, -.07f, .04f, .019f, .055f, .025f, -.15f, 0x222222); vbox_a(0, .047f, -.14f, .003f, .005f, .003f, 0, 0xdddddd);
        vbox_a(0, .047f, .04f, .01f, .004f, .005f, 0, 0x111111);
        hand(0, -.08f, .06f, .06f, .06f, .08f); hand(-.045f, -.07f, 0, .06f, .06f, .08f);
        limb(0, -.09f, .11f, .25f, -.65f, .7f, .07f); limb(-.045f, -.08f, .05f, -.5f, -.65f, .7f, .07f);
        if (G.fl > 0) flash(0, .02f, -.17f);
    }
    if (G.eatT > 0) {
        float t = 3 - G.eatT, x = .03f, y = -.14f, z = -.28f, rr = -.5f, sc = 1;
        if (t < .8f) { float k = es(t / .8f); x = .2f - .05f * k; y = -.8f + .52f * k; z = -.4f; rr = 0; }
        else if (t < 1.4f) { float k = es((t - .8f) / .6f); x = .15f - .12f * k; y = -.28f + .14f * k; z = -.4f + .12f * k; rr = -.5f * k; }
        else if (t < 2.6f) { float u = (t - 1.4f) / 1.2f, ph = fmodf(u * 3, 1.f), mv = sinf(ph * PI_F); y = -.14f + .025f * mv; z = -.28f + .1f * mv; sc = 1 - .26f * (floorf(u * 3) + (ph > .5f ? 1 : 0)); }
        else y = -.14f - .7f * es((t - 2.6f) / .4f);
        group_set(rr, 0, 0, x, y, z);
        hand(0, 0, 0, .075f, .075f, .09f); limb(0, -.01f, .06f, .1f, -.5f, .8f, .07f);
        if (t < 2.6f) {
            V3 ac = gpt((V3){0, .07f, -.03f}); float r = .075f * sc;
            for (int i = 0; i < 8; i++) {
                float a0 = i * .785398f, a1 = a0 + .785398f; CV tr[3] = {{ac.x, ac.y, ac.z - r, 216, 40, 30}, {ac.x + cosf(a0) * r, ac.y + sinf(a0) * r, ac.z - r * .2f, 150, 25, 20}, {ac.x + cosf(a1) * r, ac.y + sinf(a1) * r, ac.z - r * .2f, 150, 25, 20}};
                poly_emit(tr, 3, .06f, 1);
            }
            vbox_a(0, .07f + .078f * sc, -.03f, .004f, .015f, .004f, 0, 0x4a2d12);
        }
    }
    if (G.punchT > 0) {
        float u = clampf(1 - G.punchT / G.punchD, 0, 1), k = u < .35f ? es(u / .35f) : u < .5f ? 1 : 1 - es((u - .5f) / .5f);
        group_set(-.6f * k, 0, 0, .22f - .2f * k, -.5f + .38f * k, -.25f - .5f * k);
        hand(0, 0, 0, .075f, .075f, .1f); limb(0, 0, .06f, .1f, -.5f, .8f, .07f);
    }
    FOGON = 1;
}

/* ---------- HUD & menus ---------- */
static void draw_hud(void) {
    char buf[48];
    if (G.vigT > 0) {                       /* red damage frame */
        float v = clampf(G.vigT, 0, 1);
        for (int l = 0; l < 4; l++) { float w = 14.f * (l + 1); int a = (int)(v * (22 + (3 - l) * 14));
            rect(0, 0, SCR_W, w, 0xb00000, a); rect(0, SCR_H - w, SCR_W, SCR_H, 0xb00000, a); rect(0, w, w, SCR_H - w, 0xb00000, a); rect(SCR_W - w, w, SCR_W, SCR_H - w, 0xb00000, a); }
    }
    float cx = SCR_W * .5f, cy = SCR_H * .5f, ca = 1.f - G.aim;
    if (ca > .05f) { int al = (int)(ca * 120); unsigned c = 0xffffff;
        rect(cx - 12, cy - 1, cx - 5, cy + 1, c, al); rect(cx + 5, cy - 1, cx + 12, cy + 1, c, al);
        rect(cx - 1, cy - 12, cx + 1, cy - 5, c, al); rect(cx - 1, cy + 5, cx + 1, cy + 12, c, al); }
    rect(cx - 1, cy - 1, cx + 1, cy + 1, 0xffffff, 100);
    if (G.hmT > 0) { line2(cx - 9, cy - 9, cx - 4, cy - 4, 2, 0xffffff); line2(cx + 9, cy - 9, cx + 4, cy - 4, 2, 0xffffff); line2(cx - 9, cy + 9, cx - 4, cy + 4, 2, 0xffffff); line2(cx + 9, cy + 9, cx + 4, cy + 4, 2, 0xffffff); }
    /* health */
    float hp = clampf(G.health / 100.f, 0, 1);
    rect(34, 386, 226, 404, 0x000000, 90); rect(36, 388, 36 + 188 * hp, 402, hp > .5f ? 0x5cc04a : hp > .25f ? 0xe0a030 : 0xd03030, 128);
    snprintf(buf, sizeof buf, "%d", (int)ceilf(G.health)); text_sh(234, 389, 2, 0xffffff, buf);
    text_sh(36, 372, 1, 0xdddddd, "HEALTH");
    /* ammo */
    const WeaponDef *w = &WEAP[G.cur];
    snprintf(buf, sizeof buf, "%d / %d", G.ammo[G.cur], w->mag);
    text_sh(SCR_W - 34 - text_w(buf, 3), 384, 3, G.ammo[G.cur] == 0 ? 0xff5050 : 0xffffff, buf);
    text_sh(SCR_W - 34 - text_w(w->name, 2), 364, 2, 0xdddddd, w->name);
    if (G.reload > 0) text_sh(SCR_W - 34 - text_w("RELOADING", 2), 346, 2, 0xffd060, "RELOADING");
    snprintf(buf, sizeof buf, "SLAIN: %d", G.score); text_sh(34, 28, 2, 0xc9e265, buf);
    if (G.hintApple) text_c(cx, 300, 2, 0xffffff, "PRESS SQUARE TO EAT APPLE");
    if (G.eatT > 0) text_c(cx, 300, 2, 0xffd9a0, "NOM...");
}
static void menu_button(float cx, float y, float w, const char *s, int sel) {
    rect(cx - w * .5f, y, cx + w * .5f, y + 30, sel ? 0x2a4a1a : 0x000000, sel ? 120 : 80);
    if (sel) { rect(cx - w * .5f, y, cx + w * .5f, y + 2, 0xc9e265, 128); rect(cx - w * .5f, y + 28, cx + w * .5f, y + 30, 0xc9e265, 128); }
    text_sh(cx - text_w(s, 2) * .5f, y + 8, 2, sel ? 0xe8ff90 : 0xcccccc, s);
}
static void draw_menus(void) {
    float cx = SCR_W * .5f; char b[64];
    if (G.state == ST_TITLE || G.state == ST_OPTIONS) rect(0, 0, SCR_W, SCR_H, 0x000000, 70);
    else rect(0, 0, SCR_W, SCR_H, G.state == ST_DEAD ? 0x400000 : 0x000000, 90);
    if (G.state == ST_TITLE) {
        text_c(cx, 70, 6, 0xc9e265, "HOLLOW PINES");
        text_c(cx, 126, 2, 0xb0b8a0, "SOMETHING IS MOVING BETWEEN THE TREES");
        menu_button(cx, 200, 260, "START", G.sel == 0); menu_button(cx, 244, 260, "OPTIONS", G.sel == 1);
    } else if (G.state == ST_PAUSE) {
        text_c(cx, 100, 5, 0xffffff, "PAUSED");
        menu_button(cx, 190, 260, "RESUME", G.sel == 0); menu_button(cx, 230, 260, "OPTIONS", G.sel == 1); menu_button(cx, 270, 260, "QUIT TO TITLE", G.sel == 2);
    } else if (G.state == ST_DEAD) {
        text_c(cx, 80, 5, 0xff4040, "YOU WERE OVERRUN");
        snprintf(b, sizeof b, "MONSTERS SLAIN: %d", G.score); text_c(cx, 140, 3, 0xffffff, b);
        menu_button(cx, 210, 260, "PLAY AGAIN", G.sel == 0); menu_button(cx, 250, 260, "TITLE", G.sel == 1);
    } else if (G.state == ST_OPTIONS) {
        text_c(cx, 60, 4, 0xc9e265, "OPTIONS");
        snprintf(b, sizeof b, "DIFFICULTY  < %s >", dif_name(G.dif)); menu_button(cx, 130, 360, b, G.sel == 0);
        snprintf(b, sizeof b, "NIGHT MODE  < %s >", G.night ? "ON" : "OFF"); menu_button(cx, 170, 360, b, G.sel == 1);
        snprintf(b, sizeof b, "FIELD OF VIEW  < %d >", G.fov); menu_button(cx, 210, 360, b, G.sel == 2);
        snprintf(b, sizeof b, "LOOK SPEED  < %d >", G.look); menu_button(cx, 250, 360, b, G.sel == 3);
        snprintf(b, sizeof b, "INVERT Y  < %s >", G.invy ? "ON" : "OFF"); menu_button(cx, 290, 360, b, G.sel == 4);
        menu_button(cx, 330, 360, "BACK", G.sel == 5);
    }
}

void render_frame(void) {
    scene_setup();
    int playing = G.started && G.state != ST_TITLE;
    float x = G.px, y = 1.7f, z = G.pz, yaw, pitch, fov;
    if (playing || (G.state == ST_OPTIONS && G.started)) {
        y = 1.7f + G.h + fabsf(sinf(G.bobT)) * .05f * G.bobA * (1 - G.aim * .7f); yaw = G.yaw; pitch = G.pitch + G.kick; fov = G.curfov;
        if (G.dead) y = .55f;
    } else { yaw = G.t * .1f; pitch = .04f; fov = 75; }
    unsigned sky = G.night ? 0x070c1a : 0xa9cfe6;
    gfx_begin(sky); tri_count = 0;
    cam_setup(x, y, z, yaw, pitch, fov);
    draw_sky(pitch); draw_ground(); draw_trees(); draw_apples(); draw_monsters(); draw_arrows();
    if (playing && !G.dead && (G.state == ST_PLAY || G.state == ST_PAUSE)) draw_viewmodel();
    if (G.state == ST_PLAY) draw_hud(); else draw_menus();
    gfx_end();
}
