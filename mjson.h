/* Tiny helpers for the flat JSON lines the browser game sends. Not a general JSON parser. */
#ifndef MJSON_H
#define MJSON_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline const char *mj_find(const char *s, const char *key) {
    char pat[24]; int n = snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = strstr(s, pat); return p ? p + n : 0;
}
static inline int mj_num(const char *s, const char *key, float *out) {
    const char *p = mj_find(s, key); if (!p) return 0;
    char *e; double v = strtod(p, &e); if (e == p) return 0; *out = (float)v; return 1;
}
static inline float mj_numd(const char *s, const char *key, float def) { float v; return mj_num(s, key, &v) ? v : def; }
static inline int mj_str(const char *s, const char *key, char *out, int max) {
    const char *p = mj_find(s, key); if (!p || *p != '"') return 0; p++;
    int n = 0; while (*p && *p != '"') { if (*p == '\\' && p[1]) p++; if (n < max - 1) out[n++] = *p; p++; }
    out[n] = 0; return 1;
}
/* numbers inside  "key":[ a, b, c ]  ->  count (at most max) */
static inline int mj_arr(const char *s, const char *key, float *out, int max) {
    const char *p = mj_find(s, key); if (!p || *p != '[') return 0; p++;
    int n = 0;
    while (*p && *p != ']') {
        char *e; double v = strtod(p, &e);
        if (e == p) { p++; continue; }
        if (n < max) out[n++] = (float)v;
        p = e;
    }
    return n;
}
#endif
