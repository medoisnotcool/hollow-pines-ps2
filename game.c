/* Hollow Pines - gameplay (platform independent). Port of the web version minus
 * monster customiser, cheats/MLG theme and multiplayer. */
#include "game.h"
#include "mp.h"
#include "arena.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

Game G;

const TypeDef TYPES[3] = {                       /* hp sp dmg cy r sw sh by y */
    {100, 2.5f,  9, 1.1f,  .9f, 2.3f, 2.3f, 2.7f, 1.15f},
    {170, 2.3f,  0, 1.25f, .95f, 2.4f, 2.6f, 3.0f, 1.3f},
    {340, 4.6f, 18, 2.3f,  1.2f, 2.2f, 4.6f, 5.1f, 2.3f}};
const WeaponDef WEAP[2] = {
    {"AK-47",   .10f, 1, 20, 30, 1.0f, .014f},
    {"GLOCK 17", .16f, 0, 34, 15, 1.1f, .008f}};
static const WeaponDef WSN = {"INTERVENTION", 1.05f, 0, 200, 5, 3.0f, .035f};
const WeaponDef *wdef(int i) { return (G.mlg && i == 0) ? &WSN : &WEAP[i]; }
int snp(void) { return G.mlg && G.cur == 0; }
static const float DM[5] = {.5f, .8f, 1.f, 1.4f, 1.9f};
static const char *DN[5] = {"EASY", "NORMAL", "HARD", "BRUTAL", "NIGHTMARE"};
const char *dif_name(int d) { return DN[d - 1]; }

#define PI_F 3.14159265f
static float fclampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float ds(float a, float b, float k, float dt) { return a + (b - a) * fminf(1.f, k * dt); }

/* ---------------- random ---------------- */
static uint32_t rs = 0x1234567u;
static float rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return (float)(rs >> 8) * (1.f / 16777216.f); }

static const char *POPS[9] = {"OH BABY A TRIPLE", "MOM GET THE CAMERA", "360 NO SCOPE", "420 BLAZE IT", "GET REKT SCRUB", "WOW", "NICE SHOT NOOB", "XX_NOSCOPE_XX", "SNIPED!"};
static void pop(const char *t) { G.popTxt = t ? t : POPS[(int)(rnd() * 9) % 9]; G.popT = .9f; }

static uint32_t mb;   /* mulberry32 - same generator as the web version, used for the forest */
static float mbr(void) {
    uint32_t a = (mb += 0x6D2B79F5u), t = (a ^ (a >> 15)) * (1u | a);
    t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
    float r = (float)(t ^ (t >> 14)) * (1.f / 4294967296.f);
    return r > .99999f ? .99999f : r;
}

/* ---------------- forest ---------------- */
static void hsl2rgb(float h, float s, float l, float *r, float *g, float *b) {
    float q = l < .5f ? l * (1 + s) : l + s - l * s, p = 2 * l - q, t[3] = {h + 1.f / 3, h, h - 1.f / 3}, o[3];
    for (int i = 0; i < 3; i++) {
        float x = t[i]; if (x < 0) x += 1; if (x > 1) x -= 1;
        o[i] = x < 1.f / 6 ? p + (q - p) * 6 * x : x < .5f ? q : x < 2.f / 3 ? p + (q - p) * (2.f / 3 - x) * 6 : p;
    }
    *r = o[0] * 255; *g = o[1] * 255; *b = o[2] * 255;
}

#define NEATEN 512
static struct { int cx, cz; uint64_t mask; } eaten[NEATEN];
static int neaten;
static uint64_t eaten_get(int cx, int cz) { for (int i = 0; i < neaten; i++) if (eaten[i].cx == cx && eaten[i].cz == cz) return eaten[i].mask; return 0; }
static void eaten_set(int cx, int cz, int idx) {
    for (int i = 0; i < neaten; i++) if (eaten[i].cx == cx && eaten[i].cz == cz) { eaten[i].mask |= 1ull << idx; return; }
    if (neaten < NEATEN) { eaten[neaten].cx = cx; eaten[neaten].cz = cz; eaten[neaten].mask = 1ull << idx; neaten++; }
}

static void chunk_gen(Chunk *c, int cx, int cz) {
    mb = (uint32_t)((int64_t)cx * 73856093LL) ^ (uint32_t)((int64_t)cz * 19349663LL) ^ 777u;
    const float cs = CS / 6;
    c->cx = cx; c->cz = cz; c->valid = 1; c->n = 0; c->eaten = eaten_get(cx, cz);
    for (int i = 0; i < 36; i++) {
        float on = mbr(), s = .85f + mbr() * .6f, jx = mbr() * 2 - 1, jz = mbr() * 2 - 1, ry = mbr() * 6.28f;
        float hue = .27f + mbr() * .07f, l = .2f + mbr() * .1f, hasA = mbr(), aa = mbr() * 6.28f, ad = 1 + mbr() * .8f;
        float jm = fmaxf(0.f, cs / 2 - 2.2f * s);
        float x = cx * CS + (i % 6 + .5f) * cs + jx * jm, z = cz * CS + (i / 6 + .5f) * cs + jz * jm;
        if (!(on < .55f) || x * x + z * z < 30) continue;
        Tree *t = &c->t[c->n++];
        t->x = x; t->z = z; t->s = s; t->cr = cosf(ry); t->sr = sinf(ry); t->idx = (uint8_t)i;
        hsl2rgb(hue, .5f, l, &t->r, &t->g, &t->b);
        t->apple = (hasA < .75f) && !(c->eaten >> i & 1);
        t->ax = x + cosf(aa) * ad * s; t->az = z + sinf(aa) * ad * s;
    }
    for (int i = 0; i < 120; i++) { c->weed[i][0] = cx * CS + mbr() * CS; c->weed[i][1] = cz * CS + mbr() * CS; c->weed[i][2] = .45f + mbr() * .9f; }
}

static int floordiv(float v) { return (int)floorf(v / CS); }
static void chunks_update(int force) {
    int cx = floordiv(G.px), cz = floordiv(G.pz);
    if (!force && cx == G.lastCx && cz == G.lastCz) return;
    G.lastCx = cx; G.lastCz = cz;
    for (int j = -CRAD; j <= CRAD; j++) for (int i = -CRAD; i <= CRAD; i++) {
        int x = cx + i, z = cz + j, sx = ((x % 5) + 5) % 5, sz = ((z % 5) + 5) % 5;
        Chunk *c = &G.ch[sx + sz * 5];
        if (force || !c->valid || c->cx != x || c->cz != z) chunk_gen(c, x, z);
    }
}

static void collide(float *px, float *pz, float rad) {
    int cx = floordiv(*px), cz = floordiv(*pz);
    for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) {
        int x = cx + i, z = cz + j;
        Chunk *c = &G.ch[(((x % 5) + 5) % 5) + (((z % 5) + 5) % 5) * 5];
        if (!c->valid || c->cx != x || c->cz != z) continue;
        for (int k = 0; k < c->n; k++) {
            Tree *t = &c->t[k];
            float dx = *px - t->x, dz = *pz - t->z, d = sqrtf(dx * dx + dz * dz), m = .45f * t->s + rad;
            if (d < m && d > 0) { *px += dx / d * (m - d); *pz += dz / d * (m - d); }
        }
    }
}

/* ray against vertical tree cylinders; returns hit distance or maxT */
static float tree_hit(float ox, float oy, float oz, float dx, float dy, float dz, float maxT) {
    float best = maxT, a = dx * dx + dz * dz;
    if (a < 1e-6f) return best;
    for (int ci = 0; ci < NCH; ci++) {
        Chunk *c = &G.ch[ci]; if (!c->valid) continue;
        for (int k = 0; k < c->n; k++) {
            Tree *t = &c->t[k]; float r = .45f * t->s;
            float fx = ox - t->x, fz = oz - t->z, b = fx * dx + fz * dz, disc = b * b - a * (fx * fx + fz * fz - r * r);
            if (disc < 0) continue;
            float tt = (-b - sqrtf(disc)) / a;
            if (tt < 0 || tt >= best) continue;
            float y = oy + dy * tt;
            if (y >= 0 && y <= 3 * t->s) best = tt;
        }
    }
    return best;
}

static void cam_dir(float *dx, float *dy, float *dz) {
    float cp = cosf(G.pitch + G.kick);
    *dx = -sinf(G.yaw) * cp; *dy = sinf(G.pitch + G.kick); *dz = -cosf(G.yaw) * cp;
}
static float eye_y(void) { return 1.7f + G.h; }

/* nearest apple under the crosshair (within reach) */
static Tree *look_apple(Chunk **oc) {
    float dx, dy, dz, ox = G.px, oy = eye_y(), oz = G.pz, bt = 3.5f; Tree *best = 0;
    cam_dir(&dx, &dy, &dz);
    for (int ci = 0; ci < NCH; ci++) {
        Chunk *c = &G.ch[ci]; if (!c->valid) continue;
        for (int k = 0; k < c->n; k++) {
            Tree *a = &c->t[k]; if (!a->apple) continue;
            float cx = a->ax - ox, cy = .18f - oy, cz = a->az - oz, t = cx * dx + cy * dy + cz * dz;
            if (t < 0 || t > bt) continue;
            float px = cx - dx * t, py = cy - dy * t, pz = cz - dz * t;
            if (px * px + py * py + pz * pz < .2f) { best = a; bt = t; *oc = c; }
        }
    }
    return best;
}

/* ---------------- monsters / arrows ---------------- */
static void kill_mon(int i) { G.mon[i] = G.mon[--G.nmon]; }
static int find_mon(int uid) { for (int i = 0; i < G.nmon; i++) if (G.mon[i].uid == uid) return i; return -1; }

static int pick_type(void) {
    if (!G.night) return MT_N;
    int hasM = 0; for (int i = 0; i < G.nmon; i++) if (G.mon[i].type == MT_M) hasM = 1;
    if (!hasM && rnd() < .08f) return MT_M;
    return rnd() < .7f ? MT_A : MT_N;
}

static void spawn(float minD, float maxD) {
    if (G.nmon >= MAXMON) return;
    for (int n = 0; n < 14; n++) {
        float a = rnd() * 6.283f, d = minD + rnd() * (maxD - minD), x = G.px + cosf(a) * d, z = G.pz + sinf(a) * d;
        int ok = 1;
        for (int i = 0; i < G.nmon; i++) if (hypotf(G.mon[i].x - x, G.mon[i].z - z) <= 16) { ok = 0; break; }
        if (!ok) continue;
        int ty = pick_type(); float hm = .7f + .3f * G.DMv;
        Mon *m = &G.mon[G.nmon++]; memset(m, 0, sizeof *m);
        m->x = x; m->z = z; m->type = ty; m->max = m->hp = TYPES[ty].hp * hm; m->sp = TYPES[ty].sp * (.75f + .25f * G.DMv);
        m->fire = 2 + rnd() * 2; m->sd = rnd() < .5f ? 1.f : -1.f; m->ph = rnd() * 6; m->hd = rnd() * 6.28f;
        m->variant = (int)(rnd() * 3) % 3; m->uid = ++G.nextUid;
        return;
    }
}

static void fire_arrow(Mon *m, float d) {
    if (G.narr >= MAXARR) return;
    Arrow *a = &G.arr[G.narr++]; memset(a, 0, sizeof *a);
    float tx = G.px + (rnd() - .5f) * .8f, ty = 1.3f + G.h, tz = G.pz + (rnd() - .5f) * .8f;
    float t = fmaxf(.3f, d / 30.f);
    a->x = m->x; a->y = 1.5f; a->z = m->z;
    a->vx = (tx - a->x) / t; a->vy = (ty - a->y) / t + .5f * 8 * t; a->vz = (tz - a->z) / t;
    a->life = 5; a->src = m->uid;
   
}

static void hurt_player(float n) {
    G.health -= n; G.lastHit = 0; G.vigT = 1;
    if (G.health <= 0 && !G.dead) {
        G.dead = 1; G.health = 0; G.state = ST_DEAD; G.sel = 0;
    }
}

static void update_arrows(float dt) {
    for (int i = G.narr - 1; i >= 0; i--) {
        Arrow *a = &G.arr[i]; int rm = 0;
        a->vy -= (a->b ? 1.5f : 8.f) * dt;
        a->x += a->vx * dt; a->y += a->vy * dt; a->z += a->vz * dt;
        float dx = a->x - G.px, dz = a->z - G.pz;
        a->life -= dt;
        if (a->life > 0 && !a->b && dx * dx + dz * dz < .6f && a->y > G.h && a->y < G.h + 1.9f) { hurt_player(12 * G.DMv); a->life = 0; }
        if (a->b) for (int k = 0; k < G.nmon; k++) {
            Mon *m = &G.mon[k]; float ex = a->x - m->x, ey = a->y - TYPES[m->type].cy, ez = a->z - m->z, rr = TYPES[m->type].r + .15f;
            if (ex * ex + ey * ey + ez * ez < rr * rr) {
                m->hp -= 70; m->hurt = .1f; G.hmT = .15f;
                if (m->hp <= 0) { kill_mon(k); G.score++; if (G.mlg) pop(0); }
                a->life = 0; break;
            }
        }
        if (a->y < 0 || a->life <= 0) { rm = 1; }
        if (rm) G.arr[i] = G.arr[--G.narr];
    }
}

/* ---------------- player actions ---------------- */
static void set_weapon(int n) { G.cur = n; }
static void swap_weapon(int n) {
    if (G.eatT > 0 || n == G.cur || G.pend >= 0) return;
    G.pend = n; G.swT = .4f; G.reload = 0;
}
static void start_reload(void) {
    if (G.reload <= 0 && G.ammo[G.cur] < wdef(G.cur)->mag) {
        G.reload = 1.4f;
    }
}

static void shoot(void) {
    const WeaponDef *w = wdef(G.cur);
    G.ammo[G.cur]--; G.cd = w->rate; G.shootT = .4f; G.rec = fminf(G.rec + 1, 1.5f); G.fl = .05f;
    G.kick += w->kick * (G.aiming ? .012f : .02f); G.yaw += (rnd() - .5f) * .006f;
   
    float dx, dy, dz, sp = w->spread * (G.aiming ? (snp() ? .03f : .25f) : 1.f);
    cam_dir(&dx, &dy, &dz);
    dx += (rnd() - .5f) * sp; dy += (rnd() - .5f) * sp; dz += (rnd() - .5f) * sp;
    float l = sqrtf(dx * dx + dy * dy + dz * dz); dx /= l; dy /= l; dz /= l;
    if (mp_in_match()) { mp_shoot(G.px, eye_y(), G.pz, dx, dy, dz, (int)w->dmg); return; }   /* multiplayer: hit other players */
    float ox = G.px, oy = eye_y(), oz = G.pz, bt = tree_hit(ox, oy, oz, dx, dy, dz, 120.f); int best = -1;
    for (int i = 0; i < G.nmon; i++) {
        Mon *m = &G.mon[i]; const TypeDef *T = &TYPES[m->type];
        float cx = m->x - ox, cy = T->cy - oy, cz = m->z - oz, t = cx * dx + cy * dy + cz * dz;
        if (t < 0 || t > bt) continue;
        float px = cx - dx * t, py = cy - dy * t, pz = cz - dz * t;
        if (px * px + py * py + pz * pz < T->r * T->r) { bt = t; best = i; }
    }
    if (best >= 0) {
        Mon *m = &G.mon[best]; m->hp -= w->dmg; m->hurt = .1f; G.hmT = G.mlg ? .32f : .15f;
        if (m->hp <= 0) { kill_mon(best); G.score++; if (G.mlg) pop(0); }
    }
}

static void try_catch(void) {             /* R1: catch / punch an incoming arrow back */
    if (G.punchT > 0 || G.eatT > 0) return;
    int best = -1; float bt = .3f;
    for (int i = 0; i < G.narr; i++) {
        Arrow *a = &G.arr[i]; if (a->b || a->life <= 0) continue;
        float dx = G.px - a->x, dy = 1.5f + G.h - a->y, dz = G.pz - a->z, sp = sqrtf(a->vx * a->vx + a->vy * a->vy + a->vz * a->vz);
        if (sp < .001f) sp = 1;
        if (dx * a->vx + dy * a->vy + dz * a->vz <= 0) continue;
        float tt = sqrtf(dx * dx + dy * dy + dz * dz) / sp;
        if (tt < bt) { bt = tt; best = i; }
    }
    G.punchGrab = best >= 0; G.punchD = G.punchT = (G.mlg && best >= 0) ? .7f : .38f;
    if (best < 0) return;
    if (G.mlg) { G.arr[best].life = -1; G.health = fminf(100.f, G.health + 8); pop("NACHO CHEESE +HP"); return; }
    Arrow *a = &G.arr[best]; int mi = find_mon(a->src);
    if (mi < 0) { float bd = 1e9f; for (int i = 0; i < G.nmon; i++) { float q = (G.mon[i].x - G.px) * (G.mon[i].x - G.px) + (G.mon[i].z - G.pz) * (G.mon[i].z - G.pz); if (q < bd) { bd = q; mi = i; } } }
    if (mi >= 0) {
        Mon *m = &G.mon[mi]; float tx = m->x - a->x, ty = TYPES[m->type].cy - a->y, tz = m->z - a->z;
        float dist = sqrtf(tx * tx + ty * ty + tz * tz); if (dist < .001f) dist = 1;
        float sp = 38, t = dist / sp;
        a->vx = tx / dist * sp; a->vy = ty / dist * sp + .5f * 1.5f * t; a->vz = tz / dist * sp;
    } else { a->vx = -a->vx; a->vy = fabsf(a->vy) * .3f; a->vz = -a->vz; }
    a->b = 1; a->life = 3;
}

static void set_dif(int v) { G.dif = v; G.DMv = DM[v - 1]; G.maxMon = (int)floorf(7 + 5 * G.DMv + .5f); if (G.maxMon > MAXMON) G.maxMon = MAXMON; }

static void reset_game(void) {
    G.health = 100; G.score = 0; G.px = G.pz = 0; G.yaw = G.pitch = G.kick = 0; neaten = 0;
    chunks_update(1); G.eatT = 0; G.h = G.vy = 0; G.onGround = 1; G.ammo[0] = wdef(0)->mag; G.ammo[1] = wdef(1)->mag;
    set_weapon(0); G.pend = -1; G.reload = 0; G.swT = 0; G.dead = 0; G.started = 1; G.nmon = 0; G.narr = 0;
    G.punchT = 0; G.hmT = G.vigT = 0; G.spawnT = 2; G.rec = G.fl = G.shootT = G.cd = 0;
    for (int i = 0; i < 8; i++) spawn(18, 85);
}

/* ---------------- gameplay tick ---------------- */
static void play_update(const Input *in, float dt) {
    if (in->pressed & B_START) { G.state = ST_PAUSE; G.sel = 0; return; }
    G.t += dt;
    const int mp = mp_in_match();
    if (mp && M.dead) {          /* eliminated: wait for the respawn timer (handled in mp_update) */
        G.vigT = fmaxf(0.f, G.vigT - dt * 1.5f); G.hmT -= dt; G.punchT = 0; G.fl = 0; G.moving = 0; G.run = 0; G.aiming = 0;
        G.aim = ds(G.aim, 0.f, 12, dt); G.kick *= expf(-7 * dt); return;
    }
    /* look (right stick) */
    float rate = .9f + G.look * .38f, ax = in->rx, ay = in->ry;
    ax = .35f * ax + .65f * ax * fabsf(ax); ay = .35f * ay + .65f * ay * fabsf(ay);
    float asc = G.aiming ? (snp() ? .25f : .55f) : 1.f;
    G.yaw -= ax * rate * dt * asc;
    G.pitch = fclampf(G.pitch + ay * rate * dt * asc * (G.invy ? -1.f : 1.f), -1.5f, 1.5f);
    /* move (left stick) */
    float fw = in->ly, st = in->lx;
    G.moving = fabsf(fw) > .001f || fabsf(st) > .001f;
    G.aiming = (in->btn & B_L2) && G.reload <= 0 && G.swT <= 0 && G.eatT <= 0;
    G.run = G.moving && fw > 0 && (in->btn & (B_L1 | B_L3)) && !G.aiming;
    if (G.moving) {
        float l = fmaxf(1.f, hypotf(fw, st)), sp = (G.aiming ? 2.6f : G.run ? 9.5f : 5.2f) * (G.cur == 1 ? 1.1f : 1.f);
        float s = sinf(G.yaw), c = cosf(G.yaw);
        G.px += (-s * fw + c * st) / l * sp * dt; G.pz += (-c * fw - s * st) / l * sp * dt;
        if (G.onGround) G.bobT += dt * (G.run ? 13 : 8);
    }
    if ((in->pressed & B_CROSS) && G.onGround) { G.vy = 7.5f; G.onGround = 0; }
    if (!G.onGround) {
        G.vy -= 22 * dt; G.h += G.vy * dt;
        if (G.h <= 0) { G.h = 0; G.vy = 0; G.onGround = 1; G.kick += .03f; }
    }
    if (mp) ar_collide(&G.px, &G.pz, .4f, G.h); else { collide(&G.px, &G.pz, .4f); chunks_update(0); }
    /* timers */
    G.cd -= dt; G.shootT -= dt;
    if (G.eatT > 0) {
        G.eatT -= dt; float el = 3 - G.eatT;
        if (el > 1.4f && el < 2.6f) G.health = fminf(100.f, G.health + dt * 25);
    }
    if (G.reload > 0) { G.reload -= dt; if (G.reload <= 0) G.ammo[G.cur] = wdef(G.cur)->mag; }
    if (G.swT > 0) { G.swT -= dt; if (G.pend >= 0 && G.swT < .2f) { set_weapon(G.pend); G.pend = -1; } }
    /* buttons */
    if (in->pressed & B_TRIANGLE) swap_weapon(1 - G.cur);
    if (in->pressed & B_CIRCLE) start_reload();
    if (in->pressed & B_R1) { if (mp) mp_punch(); else try_catch(); }
    if ((in->pressed & B_SQUARE) && !mp) {
        Chunk *c = 0; Tree *a = G.eatT <= 0 ? look_apple(&c) : 0;
        if (a) { a->apple = 0; c->eaten |= 1ull << a->idx; eaten_set(c->cx, c->cz, a->idx); G.eatT = 3; G.reload = 0; G.eatBites = 0; }
    }
    int want = wdef(G.cur)->automatic ? (in->btn & B_R2) != 0 : (in->pressed & B_R2) != 0;
    if (G.cd <= 0 && G.reload <= 0 && G.swT <= 0 && G.eatT <= 0 && want) {
        if (G.ammo[G.cur] > 0) shoot(); else start_reload();
    }
    if (!mp) update_arrows(dt);
    G.spawnT -= dt;
    if (!mp && G.spawnT <= 0 && G.nmon < G.maxMon) { spawn(60, 90); G.spawnT = 2 / G.DMv; }
    G.lastHit += dt;
    /* monsters */
    for (int i = G.nmon - 1; i >= 0; i--) {
        Mon *m = &G.mon[i]; const TypeDef *T = &TYPES[m->type];
        float dx = G.px - m->x, dz = G.pz - m->z, d = hypotf(dx, dz);
        if (d > 120) { kill_mon(i); continue; }
        float vx, vz, sp = m->sp;
        if (d >= 45) { m->hd += (rnd() - .5f) * dt * 3; vx = cosf(m->hd); vz = sinf(m->hd); sp = 1; }
        else if (m->type == MT_A) {
            if (d > 26) { vx = dx / d; vz = dz / d; } else if (d < 14) { vx = -dx / d; vz = -dz / d; } else { vx = -dz / d * m->sd; vz = dx / d * m->sd; }
            m->fire -= dt; if (m->fire <= 0) { fire_arrow(m, d); m->fire = (2.4f + rnd()) / G.DMv; }
        } else { vx = dx / d; vz = dz / d; }
        if (m->type == MT_A || d > 1.5f) { m->x += vx * sp * dt; m->z += vz * sp * dt; }
        for (int k = 0; k < G.nmon; k++) if (k != i) {
            float ax2 = m->x - G.mon[k].x, az = m->z - G.mon[k].z, ad = hypotf(ax2, az);
            if (ad < 2.8f && ad > .001f) { float f = (2.8f - ad) * fminf(1.f, dt * 8); m->x += ax2 / ad * f; m->z += az / ad * f; }
        }
        collide(&m->x, &m->z, .6f);
        m->atk -= dt;
        if (m->type != MT_A && d < 1.9f && m->atk <= 0 && G.h < 1) { hurt_player(T->dmg * G.DMv); m->atk = .8f; if (G.dead) return; }
        if (m->type == MT_M && d < 22) G.vigT = fmaxf(G.vigT, .3f);
        m->hurt -= dt;
    }
    /* weapon animation state */
    G.aim = ds(G.aim, G.aiming ? 1.f : 0.f, 12, dt);
    G.bobA = ds(G.bobA, G.moving && G.onGround ? (G.run && G.shootT <= 0 ? 1.f : .55f) : 0.f, 8, dt);
    G.sprA = ds(G.sprA, G.run && G.shootT <= 0 ? 1.f : 0.f, 8, dt);
    if (G.fl > 0) G.fl -= dt;
    if (G.punchT > 0) G.punchT -= dt;
    if (G.popT > 0) G.popT -= dt;
    G.rec *= expf(-14 * dt); G.kick *= expf(-7 * dt);
    float f = (float)G.fov * (G.aiming ? (snp() ? .26f : .62f) : 1.f) * (G.run ? 1.07f : 1.f);
    G.curfov = ds(G.curfov, f, 10, dt);
    G.hmT -= dt; G.vigT = fmaxf(0.f, G.vigT - dt * 1.5f);
    Chunk *c = 0; G.hintApple = !mp && G.eatT <= 0 && look_apple(&c) != 0;
}

/* ---------------- menus ---------------- */
static int menu_count(void) {
    switch (G.state) { case ST_TITLE: return 3; case ST_PAUSE: return 3; case ST_DEAD: return 2; case ST_OPTIONS: return 6; default: return 1; }
}
static void toggle_night(void) {
    G.night ^= 1; G.nmon = 0; G.narr = 0;
    if (G.started && !G.dead) for (int i = 0; i < 8; i++) spawn(18, 85);
}
static int prev_state = ST_TITLE;
static void menu_update(const Input *in, float dt) {
    G.t += dt; if (G.toastT > 0) G.toastT -= dt;
    int n = menu_count(), up = (in->pressed & B_UP) != 0, dn = (in->pressed & B_DOWN) != 0;
    int lf = (in->pressed & B_LEFT) != 0, rt = (in->pressed & B_RIGHT) != 0;
    int ok = (in->pressed & (B_CROSS | B_START)) != 0, back = (in->pressed & B_CIRCLE) != 0;
    if (up) { G.sel = (G.sel + n - 1) % n; }
    if (dn) { G.sel = (G.sel + 1) % n; }
    switch (G.state) {
    case ST_TITLE:
        if (ok && G.sel == 0) { reset_game(); G.state = ST_PLAY; }
        else if (ok && G.sel == 1) { G.state = ST_MPSETUP; M.field = 9; M.msg[0] = 0; }
        else if (ok && G.sel == 2) { prev_state = ST_TITLE; G.state = ST_OPTIONS; G.sel = 0; }
        break;
    case ST_MPSETUP: mp_setup_input(in); break;
    case ST_MPLOBBY: if (back) mp_leave(); break;
    case ST_PAUSE:
        if (back || (in->pressed & B_START)) { G.state = ST_PLAY; break; }
        if (ok && G.sel == 0) G.state = ST_PLAY;
        else if (ok && G.sel == 1) { prev_state = ST_PAUSE; G.state = ST_OPTIONS; G.sel = 0; }
        else if (ok && G.sel == 2) { mp_leave(); G.state = ST_TITLE; G.sel = 0; G.started = 0; G.nmon = 0; G.narr = 0; }
        break;
    case ST_DEAD:
        if (ok && G.sel == 0) { reset_game(); G.state = ST_PLAY; }
        else if (ok && G.sel == 1) { G.state = ST_TITLE; G.sel = 0; G.started = 0; G.nmon = 0; G.narr = 0; }
        break;
    case ST_OPTIONS: {
        int d = rt - lf;
        if (G.sel == 0 && d) { int v = G.dif + d; if (v >= 1 && v <= 5) set_dif(v); }
        if (G.sel == 1 && (d || ok)) { toggle_night(); }
        if (G.sel == 2 && d) { G.fov += d * 5; if (G.fov < 60) G.fov = 60; if (G.fov > 110) G.fov = 110; }
        if (G.sel == 3 && d) { G.look += d; if (G.look < 1) G.look = 1; if (G.look > 10) G.look = 10; }
        if (G.sel == 4 && (d || ok)) G.invy ^= 1;
        if ((ok && G.sel == 5) || back) { G.state = prev_state; G.sel = prev_state == ST_TITLE ? 2 : 1; }
        break; }
    }
}

void game_init(void) {
    memset(&G, 0, sizeof G);
    rs = 0xC0FFEEu; G.state = ST_TITLE; G.fov = 80; G.look = 5; G.curfov = 80; G.pend = -1; G.onGround = 1;
    set_dif(3); G.health = 100;
    chunks_update(1);
    mp_init();
}

/* Hidden MLG toggle: R1+R2+L1+L2 pressed together. Only acts in the title menu or the pause menu. */
static unsigned prev_combo;
static void toggle_mlg(void) {
    G.mlg ^= 1; G.ammo[0] = wdef(0)->mag; G.reload = 0; G.toastT = 2.5f; G.popT = 0;
}

void game_update(const Input *in, float dt) {
    if (dt > .05f) dt = .05f;
    mp_update(dt);
    {
        const unsigned COMBO = B_R1 | B_R2 | B_L1 | B_L2;
        int now = (in->btn & COMBO) == COMBO;
        if (now && !prev_combo && (G.state == ST_TITLE || G.state == ST_PAUSE)) toggle_mlg();
        prev_combo = now;
    }
    if (G.state == ST_PLAY) play_update(in, dt); else menu_update(in, dt);
}
