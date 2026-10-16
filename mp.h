#ifndef MP_H
#define MP_H
#include "game.h"
typedef struct { int here, seen, dead, hp, w, mv, aim, k, d; char name[14];
                 float x, z, h, yaw, pitch, tx, tz, th, ty, flash, ph; } MPPlayer;
typedef struct { float ox, oy, oz, ex, ey, ez, t; } Tracer;
enum { MP_OFF, MP_NET, MP_CONNECT, MP_JOIN, MP_LOBBY, MP_MATCH, MP_FAIL };
typedef struct {
    int st; char msg[72];
    int octet[4], code[5], field;             /* connect screen */
    int myId, limit, arena, hostMatch;
    MPPlayer pl[4];
    float sendT, rt, inv, tmo; int dead;
    char res[40];
    char feed[3][44]; float feedT[3];
    Tracer tr[8]; int ntr;
} MPState;
extern MPState M;
extern const char MP_ALPHA[];                 /* letters/digits allowed in a game code */
#define MP_NALPHA 32

void mp_init(void);
void mp_setup_input(const Input *in);         /* connect screen buttons */
void mp_connect(void);
void mp_leave(void);
void mp_update(float dt);                     /* every frame: network + timers */
int  mp_in_match(void);
int  mp_shoot(float ox, float oy, float oz, float dx, float dy, float dz, int dmg);
void mp_punch(void);
void mp_code_text(char *out6);
#endif
