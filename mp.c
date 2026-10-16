/* Hollow Pines multiplayer client (PS2 side). Speaks the same JSON-lines messages as the web game, through the relay. */
#include "mp.h"
#include "arena.h"
#include "net.h"
#include "net_config.h"
#include "mjson.h"
#include <math.h>

MPState M;
const char MP_ALPHA[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
static char rbuf[32768]; static int rlen, rskip;

static void say(const char *m) { snprintf(M.msg, sizeof M.msg, "%s", m); }
void mp_code_text(char *o) { for (int i = 0; i < 5; i++) o[i] = MP_ALPHA[M.code[i] & 31]; o[5] = 0; }

void mp_init(void) {
    memset(&M, 0, sizeof M); const int ip[4] = {RELAY_IP_DEFAULT};
    for (int i = 0; i < 4; i++) M.octet[i] = ip[i];
    M.field = 9; M.myId = -1; M.limit = 10;
}

static void send_line(const char *s) { net_write(s, (int)strlen(s)); }
static void send_msg(const char *s) { char b[256]; int n = snprintf(b, sizeof b, "%s\n", s); if (n > 0 && n < (int)sizeof b) net_write(b, n); }

static void reset_players(void) { memset(M.pl, 0, sizeof M.pl); memset(M.tr, 0, sizeof M.tr); M.ntr = 0; memset(M.feed, 0, sizeof M.feed); memset(M.feedT, 0, sizeof M.feedT); }

static void fail(const char *why) {
    net_close(); say(why); M.st = MP_FAIL; rlen = rskip = 0;
    G.state = ST_MPSETUP; G.started = 0; G.dead = 0; M.dead = 0; G.vigT = 0;
}
void mp_leave(void) {
    net_close(); M.st = MP_OFF; M.dead = 0; rlen = rskip = 0; reset_players(); M.res[0] = 0; M.msg[0] = 0;
    G.started = 0; G.dead = 0; G.vigT = 0; G.state = ST_TITLE; G.sel = 0; G.nmon = 0; G.narr = 0;
}
int mp_in_match(void) { return M.st == MP_MATCH; }

/* ---------------------------------------------------------------- connecting */
void mp_connect(void) {
    net_close(); reset_players(); M.res[0] = 0; M.myId = -1; M.tmo = 0; M.hostMatch = 0; AR.valid = 0;
    M.st = MP_NET; say("STARTING NETWORK..."); G.state = ST_MPLOBBY; G.sel = 0;
    if (net_init() < 0) fail(net_status());
}
static void start_connect(void) {
    char ip[20]; snprintf(ip, sizeof ip, "%d.%d.%d.%d", M.octet[0], M.octet[1], M.octet[2], M.octet[3]);
    if (net_open(ip, NET_PORT) < 0) { fail("COULD NOT OPEN CONNECTION"); return; }
    M.st = MP_CONNECT; M.tmo = 0; say("CONNECTING TO RELAY...");
}
static void join_now(void) {
    char code[6], b[200]; mp_code_text(code);
    snprintf(b, sizeof b, "JOIN %s\n{\"t\":\"hello\",\"name\":\"PS2\",\"dev\":\"ps2\"}\n", code); send_line(b);
    M.st = MP_JOIN; M.tmo = 0; say("JOINING GAME...");
}

/* ---------------------------------------------------------------- gameplay helpers */
static const char *pname(int id) { return (id >= 0 && id < 4 && M.pl[id].name[0]) ? M.pl[id].name : "?"; }
static void feed_add(const char *t) {
    for (int i = 0; i < 2; i++) { memcpy(M.feed[i], M.feed[i + 1], sizeof M.feed[i]); M.feedT[i] = M.feedT[i + 1]; }
    snprintf(M.feed[2], sizeof M.feed[2], "%s", t); M.feedT[2] = 5.f;
}
static void apply_kill(int k, int v) {
    char b[64];
    if (v >= 0 && v < 4) M.pl[v].d++;
    if (k >= 0 && k < 4 && k != v) M.pl[k].k++;
    snprintf(b, sizeof b, "%s  X  %s", pname(k), pname(v)); feed_add(b);
}
static void send_state(void) {
    char b[200];
    snprintf(b, sizeof b, "{\"t\":\"st\",\"x\":%.2f,\"z\":%.2f,\"h\":%.2f,\"y\":%.2f,\"p\":%.2f,\"w\":%d,\"v\":%d,\"a\":%d,\"d\":%d,\"hp\":%d}",
             G.px, G.pz, G.h, G.yaw, G.pitch, G.cur, G.moving ? (G.run ? 2 : 1) : 0, G.aiming ? 1 : 0, M.dead, (int)ceilf(G.health));
    send_msg(b);
}
static void respawn(int first) {
    int n = AR.ns > 0 ? AR.ns : 1, pick = 0;
    if (first) pick = (M.myId * 2) % n;
    else {
        float bs = -1;
        for (int i = 0; i < n; i++) {
            float md = 1e9f;
            for (int id = 0; id < 4; id++) { const MPPlayer *p = &M.pl[id]; if (id == M.myId || !p->here || !p->seen || p->dead) continue; md = fminf(md, hypotf(p->x - AR.spawn[i][0], p->z - AR.spawn[i][1])); }
            float sc = fminf(md, 60.f) + (float)(rand() % 800) / 100.f;
            if (sc > bs) { bs = sc; pick = i; }
        }
    }
    float sx = AR.spawn[pick][0], sz = AR.spawn[pick][1];
    G.px = sx; G.pz = sz; G.yaw = atan2f(sx, sz); G.pitch = 0; G.kick = 0; G.h = G.vy = 0; G.onGround = 1;
    G.health = 100; G.ammo[0] = wdef(0)->mag; G.ammo[1] = wdef(1)->mag; G.reload = 0; G.swT = 0; G.pend = -1; G.eatT = 0;
    G.dead = 0; M.dead = 0; M.inv = 2.f; G.vigT = 0; G.punchT = 0;
}
static void start_match(void) {
    M.st = MP_MATCH; G.state = ST_PLAY; G.started = 1; G.mlg = 0; G.nmon = 0; G.narr = 0; G.score = 0;
    M.res[0] = 0; M.sendT = 0; memset(M.feed, 0, sizeof M.feed); M.ntr = 0;
    for (int i = 0; i < 4; i++) M.pl[i].seen = 0;
    respawn(1);
}
static void end_match(const char *res) {
    if (M.st != MP_MATCH) return;
    M.st = MP_LOBBY; G.state = ST_MPLOBBY; G.started = 0; G.dead = 0; M.dead = 0; G.vigT = 0; G.sel = 0;
    if (res) snprintf(M.res, sizeof M.res, "%s", res);
}
static void die(int killer) {
    G.health = 0; M.dead = 1; G.dead = 1; M.rt = 3.2f; G.eatT = 0; G.reload = 0; G.aiming = 0;
    char b[96]; snprintf(b, sizeof b, "{\"t\":\"kill\",\"k\":%d,\"v\":%d}", killer, M.myId); send_msg(b);
    apply_kill(killer, M.myId);
}
static void take_hit(float d, int from) {
    if (M.dead || M.inv > 0 || M.st != MP_MATCH) return;
    G.health -= d; G.vigT = 1.f;
    if (G.health <= 0) die(from);
}

int mp_shoot(float ox, float oy, float oz, float dx, float dy, float dz, int dmg) {
    float bd = ar_ray(ox, oy, oz, dx, dy, dz, 120.f); int best = -1, head = 0;
    for (int id = 0; id < 4; id++) {
        const MPPlayer *p = &M.pl[id]; if (id == M.myId || !p->here || !p->seen || p->dead) continue;
        float tb = ray_cyl(ox, oy, oz, dx, dy, dz, p->x, p->z, .38f, p->h, p->h + 1.5f, bd), th = ray_sph(ox, oy, oz, dx, dy, dz, p->x, p->h + 1.66f, p->z, .24f, bd), tt = fminf(tb, th);
        if (tt < bd) { bd = tt; best = id; head = th <= tb; }
    }
    char b[160]; float ex = ox + dx * bd, ey = oy + dy * bd, ez = oz + dz * bd; int imp = 0;
    if (best >= 0) { snprintf(b, sizeof b, "{\"t\":\"hit\",\"to\":%d,\"d\":%d,\"hs\":%d}", best, dmg * (head ? 2 : 1), head); send_msg(b); G.hmT = .15f; imp = 2; }
    else if (bd < 120.f) imp = 1;
    snprintf(b, sizeof b, "{\"t\":\"shot\",\"w\":%d,\"e\":[%.2f,%.2f,%.2f],\"i\":%d}", G.cur, ex, ey, ez, imp); send_msg(b);
    return best >= 0;
}
void mp_punch(void) {
    if (M.dead || G.punchT > 0) return;
    G.punchGrab = 0; G.punchD = G.punchT = .38f;
    float fx = -sinf(G.yaw), fz = -cosf(G.yaw), oy = 1.7f + G.h, bd = 2.1f; int best = -1;
    for (int id = 0; id < 4; id++) {
        const MPPlayer *p = &M.pl[id]; if (id == M.myId || !p->here || !p->seen || p->dead) continue;
        float dx = p->x - G.px, dz = p->z - G.pz, d = hypotf(dx, dz);
        if (d >= bd || (dx * fx + dz * fz) / (d > 0 ? d : 1.f) < .55f) continue;
        float vy = 1.3f + p->h - oy, len = sqrtf(dx * dx + vy * vy + dz * dz);
        if (ar_ray(G.px, oy, G.pz, dx / len, vy / len, dz / len, len) < len - .3f) continue;
        bd = d; best = id;
    }
    if (best >= 0) { char b[96]; snprintf(b, sizeof b, "{\"t\":\"hit\",\"to\":%d,\"d\":35,\"hs\":0}", best); send_msg(b); G.hmT = .15f; }
}

/* ---------------------------------------------------------------- incoming messages */
static void on_state(const char *s) {
    int id = (int)mj_numd(s, "f", -1); if (id < 0 || id > 3 || id == M.myId) return;
    MPPlayer *p = &M.pl[id]; int dead = mj_numd(s, "d", 0) != 0;
    p->here = 1;
    p->tx = mj_numd(s, "x", p->tx); p->tz = mj_numd(s, "z", p->tz); p->th = mj_numd(s, "h", 0); p->ty = mj_numd(s, "y", p->ty);
    p->pitch = mj_numd(s, "p", 0); p->w = (int)mj_numd(s, "w", 0); p->mv = (int)mj_numd(s, "v", 0); p->aim = (int)mj_numd(s, "a", 0); p->hp = (int)mj_numd(s, "hp", 100);
    if (!p->seen || (p->dead && !dead)) { p->x = p->tx; p->z = p->tz; p->h = p->th; p->yaw = p->ty; p->seen = 1; }
    p->dead = dead;
}
static void on_shot(const char *s) {
    int id = (int)mj_numd(s, "f", -1); if (id < 0 || id > 3 || id == M.myId) return;
    MPPlayer *p = &M.pl[id]; if (!p->seen || p->dead || M.st != MP_MATCH) return;
    float e[3]; if (mj_arr(s, "e", e, 3) < 3) return;
    float sn = sinf(p->yaw), cs = cosf(p->yaw); p->flash = .06f;
    Tracer *t = &M.tr[M.ntr < 8 ? M.ntr++ : 7];
    if (M.ntr == 8) memmove(M.tr, M.tr + 1, sizeof(Tracer) * 7);
    t->ox = p->x - sn * .9f + cs * .3f; t->oy = p->h + 1.38f; t->oz = p->z - cs * .9f - sn * .3f; t->ex = e[0]; t->ey = e[1]; t->ez = e[2]; t->t = .08f;
}
static void on_lobby(const char *s) {
    char st[12] = "";
    for (int i = 0; i < 4; i++) M.pl[i].here = 0;
    const char *p = mj_find(s, "pl");
    if (p && *p == '[') {
        p++;
        while ((p = strchr(p, '{'))) {
            const char *e = strchr(p, '}'); if (!e) break;
            char obj[128]; int n = (int)(e - p + 1); if (n > (int)sizeof obj - 1) n = (int)sizeof obj - 1; memcpy(obj, p, n); obj[n] = 0;
            int id = (int)mj_numd(obj, "id", -1);
            if (id >= 0 && id < 4) { MPPlayer *q = &M.pl[id]; q->here = 1; q->k = (int)mj_numd(obj, "k", 0); q->d = (int)mj_numd(obj, "d", 0); if (!mj_str(obj, "n", q->name, sizeof q->name)) strcpy(q->name, "?"); }
            p = e + 1;
        }
    }
    for (int i = 0; i < 4; i++) if (!M.pl[i].here) M.pl[i].seen = 0;
    M.limit = (int)mj_numd(s, "l", 10); M.arena = (int)mj_numd(s, "a", 0);
    if (!mj_str(s, "s", st, sizeof st)) return;
    if (!strcmp(st, "match")) { M.hostMatch = 1; if (M.st == MP_LOBBY && AR.valid && AR.idx == M.arena) start_match(); }
    else { M.hostMatch = 0; if (M.st == MP_MATCH) end_match("MATCH OVER"); }
}
static void handle(const char *s) {
    char t[12]; if (!mj_str(s, "t", t, sizeof t)) return;
    if (!strcmp(t, "st")) on_state(s);
    else if (!strcmp(t, "shot")) on_shot(s);
    else if (!strcmp(t, "hit")) { if ((int)mj_numd(s, "to", -1) == M.myId) take_hit(mj_numd(s, "d", 0), (int)mj_numd(s, "f", -1)); }
    else if (!strcmp(t, "kill")) { if (M.st == MP_MATCH) apply_kill((int)mj_numd(s, "k", -1), (int)mj_numd(s, "v", -1)); }
    else if (!strcmp(t, "lobby")) on_lobby(s);
    else if (!strcmp(t, "arena")) ar_load(s);
    else if (!strcmp(t, "welcome")) { M.myId = (int)mj_numd(s, "id", 1); if (M.myId < 0 || M.myId > 3) M.myId = 1; if (M.st == MP_JOIN) { M.st = MP_LOBBY; say("IN LOBBY"); } }
    else if (!strcmp(t, "deny")) { char w[60]; if (!mj_str(s, "why", w, sizeof w)) strcpy(w, "COULD NOT JOIN"); fail(w); }
    else if (!strcmp(t, "end")) {
        char b[40]; int w = (int)mj_numd(s, "w", -1); snprintf(b, sizeof b, "%s WINS!", pname(w));
        if (M.st == MP_MATCH) end_match(b); else snprintf(M.res, sizeof M.res, "%s", b);
    }
}
static void pump(void) {
    char tmp[2048]; int n;
    while ((n = net_read(tmp, sizeof tmp)) > 0) {
        for (int i = 0; i < n; i++) {
            char c = tmp[i];
            if (c == '\n') {
                if (!rskip && rlen) { rbuf[rlen] = 0; handle(rbuf); }
                rlen = 0; rskip = 0;
                if (M.st == MP_OFF || M.st == MP_FAIL) return;
            } else if (c != '\r') { if (rlen < (int)sizeof rbuf - 1) rbuf[rlen++] = c; else rskip = 1; }
        }
    }
    if (n < 0 && M.st != MP_OFF && M.st != MP_FAIL) fail("DISCONNECTED FROM RELAY");
}

/* ---------------------------------------------------------------- per frame */
void mp_update(float dt) {
    if (M.st == MP_OFF || M.st == MP_FAIL) return;
    net_poll(); M.tmo += dt;
    if (M.st == MP_NET) {
        if (net_ready()) start_connect();
        else { say(net_status()); if (M.tmo > 25.f) fail("NO IP ADDRESS. CHECK THE CABLE"); }
        return;
    }
    if (M.st == MP_CONNECT) {
        int s = net_state();
        if (s == 2) join_now();
        else if (s < 0 || M.tmo > 10.f) fail("COULD NOT REACH RELAY. CHECK THE IP");
        return;
    }
    pump();
    if (M.st == MP_JOIN && M.tmo > 10.f) { fail("NO ANSWER. CHECK THE GAME CODE"); return; }
    if (M.st != MP_MATCH) return;
    if (M.inv > 0) M.inv -= dt;
    if (M.dead) { M.rt -= dt; if (M.rt <= 0) respawn(0); }
    M.sendT -= dt; if (M.sendT <= 0) { M.sendT = .066f; send_state(); }
    float k = 1.f - expf(-dt * 16.f);
    for (int i = 0; i < 4; i++) {
        MPPlayer *p = &M.pl[i]; if (!p->here || !p->seen || i == M.myId) continue;
        p->x += (p->tx - p->x) * k; p->z += (p->tz - p->z) * k; p->h += (p->th - p->h) * k;
        float dy = p->ty - p->yaw; dy = atan2f(sinf(dy), cosf(dy)); p->yaw += dy * k;
        if (p->mv && !p->dead) p->ph += dt * (p->mv == 2 ? 12.f : 8.f);
        if (p->flash > 0) p->flash -= dt;
    }
    for (int i = M.ntr - 1; i >= 0; i--) { M.tr[i].t -= dt; if (M.tr[i].t <= 0) { M.tr[i] = M.tr[--M.ntr]; } }
    for (int i = 0; i < 3; i++) if (M.feedT[i] > 0) M.feedT[i] -= dt;
}

/* ---------------------------------------------------------------- connect screen buttons */
void mp_setup_input(const Input *in) {
    int lf = (in->pressed & B_LEFT) != 0, rt = (in->pressed & B_RIGHT) != 0, up = (in->pressed & B_UP) != 0, dn = (in->pressed & B_DOWN) != 0;
    int ok = (in->pressed & B_CROSS) != 0, back = (in->pressed & B_CIRCLE) != 0, big = ((in->pressed & B_R1) != 0) - ((in->pressed & B_L1) != 0);
    if (lf) M.field = (M.field + 10) % 11;
    if (rt) M.field = (M.field + 1) % 11;
    int d = up - dn;
    if (M.field < 4) {
        int v = M.octet[M.field] + d + big * 10; if (v < 0) v = 255; if (v > 255) v = 0; M.octet[M.field] = v;
    } else if (M.field < 9) {
        int i = M.field - 4; M.code[i] = (M.code[i] + d + MP_NALPHA) % MP_NALPHA;
    } else if (d) M.field = d > 0 ? 8 : M.field;
    if (ok && M.field == 9) mp_connect();
    if (back || (ok && M.field == 10)) { G.state = ST_TITLE; G.sel = 1; M.msg[0] = 0; M.st = MP_OFF; }
}
