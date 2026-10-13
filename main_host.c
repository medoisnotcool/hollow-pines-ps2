/* Desktop harness: scripted play-through with screenshots, random soak test, audio dump. */
#include "../src/game.h"
#include "../src/gfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
void gfx_host_save(const char *path);
int render_stat_tris(void);
static unsigned prev;
static Input mk(unsigned btn, float lx, float ly, float rx, float ry) { Input i = {lx, ly, rx, ry, btn, btn & ~prev}; prev = btn; return i; }
static void frames(int n, unsigned btn, float lx, float ly, float rx, float ry, int render) {
    for (int k = 0; k < n; k++) { Input i = mk(btn, lx, ly, rx, ry); game_update(&i, 1.f / 60); if (render) render_frame(); }
}
static void tap(unsigned b) { frames(1, b, 0, 0, 0, 0, 0); frames(2, 0, 0, 0, 0, 0, 0); }
static void shot(const char *name) { render_frame(); char p[128]; snprintf(p, sizeof p, "out/%s.ppm", name); gfx_host_save(p); printf("%-10s tris=%d mon=%d hp=%.0f ammo=%d/%d state=%d\n", name, render_stat_tris(), G.nmon, G.health, G.ammo[0], G.ammo[1], G.state); }

int main(int argc, char **argv) {
    gfx_init(); game_init(); render_init();
    if (argc > 1 && !strcmp(argv[1], "soak")) {
        unsigned s = 4242; int frames_n = argc > 2 ? atoi(argv[2]) : 20000;
        for (int rep = 0; rep < 3; rep++) {
            game_init(); G.dif = 1 + rep * 2; tap(B_CROSS);
            if (rep == 1) { G.night = 1; }
            for (int f = 0; f < frames_n; f++) {
                s = s * 1664525u + 1013904223u; unsigned r = s >> 8; unsigned btn = 0;
                if (r & 1) btn |= B_R2; if (r & 2) btn |= B_L2; if (r & 4) btn |= B_L1; if ((r >> 3 & 31) == 0) btn |= B_CROSS;
                if ((r >> 8 & 63) == 0) btn |= B_CIRCLE; if ((r >> 10 & 63) == 0) btn |= B_TRIANGLE; if ((r >> 12 & 63) == 0) btn |= B_R1; if ((r >> 14 & 63) == 0) btn |= B_SQUARE;
                if ((r >> 16 & 255) == 0) btn |= B_START;
                if (G.state == ST_DEAD && (f & 15) == 0) btn |= B_CROSS;
                float lx = ((r >> 3 & 255) / 127.5f - 1), ly = ((r >> 11 & 255) / 127.5f - 1), rx = ((r >> 5 & 255) / 127.5f - 1), ry = ((r >> 13 & 255) / 127.5f - 1) * .3f;
                if (f % 600 < 300) { ly = 1; rx *= .4f; }
                Input in = mk(btn, lx, ly, rx, ry); game_update(&in, 1.f / 60);
                if (f % 4 == 0) render_frame();
                if (!isfinite(G.px) || !isfinite(G.pz) || !isfinite(G.health) || !isfinite(G.yaw)) { printf("NaN at frame %d\n", f); return 1; }
                if (G.nmon > MAXMON || G.narr > MAXARR || G.nmon < 0 || G.narr < 0) { printf("bounds\n"); return 1; }
            }
            printf("soak rep %d ok: pos=(%.0f,%.0f) score=%d nmon=%d tris=%d\n", rep, G.px, G.pz, G.score, G.nmon, render_stat_tris());
        }
        return 0;
    }
    shot("1_title");
    tap(B_DOWN); tap(B_CROSS); shot("2_options"); tap(B_CIRCLE); tap(B_UP); tap(B_CROSS);   /* options, back, then START */
    frames(60, 0, 0, 0, 0, 0, 0); shot("3_start");
    frames(240, 0, 0, 1, .3f, 0, 0); shot("4_walk");
    frames(10, B_R2, 0, 0, 0, 0, 0); shot("5_fire");
    frames(30, B_L2, 0, 0, 0, 0, 0); shot("6_aim");
    tap(B_TRIANGLE); frames(30, 0, 0, 0, 0, 0, 0); frames(3, B_R2, 0, 0, 0, 0, 0); shot("7_glock");
    G.eatT = 1.9f; shot("8_eat");
    G.eatT = 0; frames(2, B_R1, 0, 0, 0, 0, 0); shot("9_punch");
    G.night = 1; G.nmon = 0; for (int i = 0; i < 6; i++) { G.mon[i].type = i % 3; G.mon[i].x = G.px - sinf(G.yaw + (i - 3) * .35f) * (10 + i * 5); G.mon[i].z = G.pz - cosf(G.yaw + (i - 3) * .35f) * (10 + i * 5); G.mon[i].max = G.mon[i].hp = 100; G.mon[i].uid = i + 100; G.nmon++; }
    G.mon[1].hp = 50; G.mon[2].hurt = .1f; G.vigT = .8f; frames(1, 0, 0, 0, 0, 0, 0); shot("10_night");
    G.night = 0; shot("11_day_mon");
    G.health = 0; G.dead = 1; G.state = ST_DEAD; shot("12_dead");
    return 0;
}
