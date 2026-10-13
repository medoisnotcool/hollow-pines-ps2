#ifndef GAME_H
#define GAME_H
#include <stdint.h>

#define MAXMON 24
#define MAXARR 32
#define CS 50.0f          /* forest chunk size */
#define CRAD 2            /* chunks kept around the player (5x5) */
#define NCH ((2*CRAD+1)*(2*CRAD+1))

/* ---- controller state (filled by the platform layer) ---- */
typedef struct {
    float lx, ly, rx, ry;     /* sticks -1..1 (ly, ry: +1 = pushed UP), dead-zone already applied */
    unsigned btn, pressed;    /* held / newly pressed this frame */
} Input;
enum { B_UP=1<<0, B_DOWN=1<<1, B_LEFT=1<<2, B_RIGHT=1<<3, B_CROSS=1<<4, B_CIRCLE=1<<5,
       B_SQUARE=1<<6, B_TRIANGLE=1<<7, B_L1=1<<8, B_R1=1<<9, B_L2=1<<10, B_R2=1<<11,
       B_START=1<<12, B_SELECT=1<<13, B_L3=1<<14, B_R3=1<<15 };

enum { ST_TITLE, ST_OPTIONS, ST_PLAY, ST_PAUSE, ST_DEAD };
enum { MT_N, MT_A, MT_M };      /* goblin, night archer, "Mandela monster" */

typedef struct { float hp, sp, dmg, cy, r, sw, sh, by, y; } TypeDef;
typedef struct { const char *name; float rate; int automatic; float dmg; int mag; float kick, spread; } WeaponDef;
extern const TypeDef TYPES[3];
extern const WeaponDef WEAP[2];

typedef struct { float x, z, s, cr, sr, r, g, b, ax, az; uint8_t apple, idx; } Tree;
typedef struct { int cx, cz, valid, n; uint64_t eaten; Tree t[36]; } Chunk;
typedef struct { float x, z, hp, max, sp, fire, hurt, atk, ph, hd, sd; int type, variant, uid; } Mon;
typedef struct { float x, y, z, vx, vy, vz, life; int b, src; } Arrow;   /* b: caught & thrown back */

typedef struct {
    int state, sel, started, dead;
    /* options */
    int dif, night, fov, look, invy;
    float DMv; int maxMon;
    /* time */
    float t;
    /* player */
    float px, pz, h, vy, yaw, pitch, kick; int onGround;
    int cur, pend; float swT, reload, rec, cd, fl, aim, bobA, sprA, bobT, shootT;
    int aiming, run, moving;
    int ammo[2]; float health; int score;
    float lastHit, hmT, vigT, spawnT, eatT; int eatBites;
    float punchT, punchD; int punchGrab;
    float curfov; int hintApple;
    /* world */
    Mon mon[MAXMON]; int nmon, nextUid;
    Arrow arr[MAXARR]; int narr;
    Chunk ch[NCH]; int lastCx, lastCz;
    char msg[64];
} Game;
extern Game G;

void game_init(void);
void game_update(const Input *in, float dt);
void render_init(void);
void render_frame(void);
const char *dif_name(int d);
#endif
