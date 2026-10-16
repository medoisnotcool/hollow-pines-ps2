/* Multiplayer arena: geometry received from the host, collision and ray tests (ported from the web game). */
#include "arena.h"
#include "mjson.h"
#include <math.h>

Arena AR;
#define INF 1e30f

int ar_load(const char *line) {
    static float tmp[AR_BOX * 7 + 8];
    int n; Arena *A = &AR; memset(A, 0, sizeof *A);
    A->idx = (int)mj_numd(line, "a", 0); A->half = mj_numd(line, "half", 30);
    n = mj_arr(line, "b", tmp, AR_BOX * 7) / 7; A->nb = n;
    for (int i = 0; i < n; i++) { const float *q = &tmp[i * 7]; A->box[i] = (ABox){q[0], q[1], q[2], q[3], q[4], q[5], (unsigned)q[6]}; }
    n = mj_arr(line, "c", tmp, AR_CIRC * 3) / 3; A->nc = n;
    for (int i = 0; i < n; i++) A->circ[i] = (ACirc){tmp[i * 3], tmp[i * 3 + 1], tmp[i * 3 + 2]};
    n = mj_arr(line, "r", tmp, AR_BAR * 4) / 4; A->nbar = n;
    for (int i = 0; i < n; i++) A->bar[i] = (ABarrel){tmp[i * 4], tmp[i * 4 + 1], tmp[i * 4 + 2], tmp[i * 4 + 3]};
    n = mj_arr(line, "e", tmp, AR_TREE * 3) / 3; A->nt = n;
    for (int i = 0; i < n; i++) {
        Tree *t = &A->tree[i]; memset(t, 0, sizeof *t);
        t->x = tmp[i * 3]; t->z = tmp[i * 3 + 1]; t->s = tmp[i * 3 + 2]; t->cr = cosf(i * 1.7f); t->sr = sinf(i * 1.7f);
        t->r = 46 + (i % 3) * 8; t->g = 104 + (i % 4) * 6; t->b = 46;
    }
    n = mj_arr(line, "s", tmp, AR_SPAWN * 2) / 2; A->ns = n;
    for (int i = 0; i < n; i++) { A->spawn[i][0] = tmp[i * 2]; A->spawn[i][1] = tmp[i * 2 + 1]; }
    n = mj_arr(line, "o", tmp, AR_ROOF * 4) / 4; A->nroof = n;
    for (int i = 0; i < n; i++) A->roof[i] = (ARect){tmp[i * 4], tmp[i * 4 + 1], tmp[i * 4 + 2], tmp[i * 4 + 3]};
    n = mj_arr(line, "g", tmp, AR_ROOF * 4) / 4; A->nfloor = n;
    for (int i = 0; i < n; i++) A->floor_[i] = (ARect){tmp[i * 4], tmp[i * 4 + 1], tmp[i * 4 + 2], tmp[i * 4 + 3]};
    if (A->ns == 0) { A->spawn[0][0] = 0; A->spawn[0][1] = 0; A->ns = 1; }
    A->valid = A->nb > 0;
    return A->valid;
}

void ar_collide(float *px, float *pz, float rad, float feet) {
    if (!AR.valid) return;
    for (int i = 0; i < AR.nb; i++) {
        const ABox *b = &AR.box[i];
        if (b->y0 >= feet + 1.8f || b->y1 <= feet) continue;
        float cx = fmaxf(b->x0, fminf(*px, b->x1)), cz = fmaxf(b->z0, fminf(*pz, b->z1)), dx = *px - cx, dz = *pz - cz, d2 = dx * dx + dz * dz;
        if (d2 < rad * rad) {
            if (d2 > 1e-8f) { float d = sqrtf(d2); *px += dx / d * (rad - d); *pz += dz / d * (rad - d); }
            else {
                float l = *px - b->x0, r = b->x1 - *px, u = *pz - b->z0, dn = b->z1 - *pz, m = fminf(fminf(l, r), fminf(u, dn));
                if (m == l) *px = b->x0 - rad; else if (m == r) *px = b->x1 + rad; else if (m == u) *pz = b->z0 - rad; else *pz = b->z1 + rad;
            }
        }
    }
    for (int i = 0; i < AR.nc; i++) {
        const ACirc *c = &AR.circ[i]; float dx = *px - c->x, dz = *pz - c->z, d = sqrtf(dx * dx + dz * dz), m = c->r + rad;
        if (d < m && d > 1e-6f) { *px += dx / d * (m - d); *pz += dz / d * (m - d); }
    }
    float lim = AR.half - rad - .1f;
    *px = fmaxf(-lim, fminf(*px, lim)); *pz = fmaxf(-lim, fminf(*pz, lim));
}

static float ray_box(float ox, float oy, float oz, float dx, float dy, float dz, const ABox *b, float mx) {
    float t0 = 0, t1 = mx, a, c, e;
    const float o[3] = {ox, oy, oz}, d[3] = {dx, dy, dz}, lo[3] = {b->x0, b->y0, b->z0}, hi[3] = {b->x1, b->y1, b->z1};
    for (int i = 0; i < 3; i++) {
        if (fabsf(d[i]) < 1e-9f) { if (o[i] < lo[i] || o[i] > hi[i]) return INF; }
        else { a = (lo[i] - o[i]) / d[i]; c = (hi[i] - o[i]) / d[i]; if (a > c) { e = a; a = c; c = e; } if (a > t0) t0 = a; if (c < t1) t1 = c; if (t0 > t1) return INF; }
    }
    return t0;
}
float ray_cyl(float ox, float oy, float oz, float dx, float dy, float dz, float cx, float cz, float r, float y0, float y1, float mx) {
    float a = dx * dx + dz * dz, fx = ox - cx, fz = oz - cz, tin, tout;
    (void)dz;
    if (a < 1e-9f) { if (fx * fx + fz * fz > r * r) return INF; tin = 0; tout = mx; }
    else {
        float b = fx * dx + fz * dz, c = fx * fx + fz * fz - r * r, disc = b * b - a * c;
        if (disc < 0) return INF;
        float s = sqrtf(disc); tin = (-b - s) / a; tout = (-b + s) / a;
    }
    if (tout < 0) return INF;
    float lo = fmaxf(tin, 0), hi = tout;
    if (fabsf(dy) < 1e-9f) { if (oy < y0 || oy > y1) return INF; }
    else { float a1 = (y0 - oy) / dy, a2 = (y1 - oy) / dy; if (a1 > a2) { float e = a1; a1 = a2; a2 = e; } lo = fmaxf(lo, a1); hi = fminf(hi, a2); }
    return (lo > hi || lo > mx) ? INF : lo;
}
float ray_sph(float ox, float oy, float oz, float dx, float dy, float dz, float cx, float cy, float cz, float r, float mx) {
    float fx = ox - cx, fy = oy - cy, fz = oz - cz, b = fx * dx + fy * dy + fz * dz, c = fx * fx + fy * fy + fz * fz - r * r, disc = b * b - c;
    if (disc < 0) return INF;
    float t = -b - sqrtf(disc); return (t >= 0 && t <= mx) ? t : INF;
}
float ar_ray(float ox, float oy, float oz, float dx, float dy, float dz, float mx) {
    float b = mx;
    for (int i = 0; i < AR.nb; i++) { float t = ray_box(ox, oy, oz, dx, dy, dz, &AR.box[i], b); if (t < b) b = t; }
    for (int i = 0; i < AR.nbar; i++) { const ABarrel *q = &AR.bar[i]; float t = ray_cyl(ox, oy, oz, dx, dy, dz, q->x, q->z, .45f, q->y, q->y + 1.05f, b); if (t < b) b = t; }
    for (int i = 0; i < AR.nt; i++) { float t = ray_cyl(ox, oy, oz, dx, dy, dz, AR.tree[i].x, AR.tree[i].z, .4f, 0, 7, b); if (t < b) b = t; }
    return b;
}
