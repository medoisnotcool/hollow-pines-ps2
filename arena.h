#ifndef ARENA_H
#define ARENA_H
#include "game.h"
#define AR_BOX 200
#define AR_CIRC 200
#define AR_BAR 180
#define AR_TREE 32
#define AR_SPAWN 8
#define AR_ROOF 16
typedef struct { float x0, x1, y0, y1, z0, z1; unsigned rgb; } ABox;
typedef struct { float x, z, r; } ACirc;
typedef struct { float x, z, y, k; } ABarrel;
typedef struct { float x, z, w, d; } ARect;
typedef struct {
    int valid, idx; float half;
    int nb, nc, nbar, nt, ns, nroof, nfloor;
    ABox box[AR_BOX]; ACirc circ[AR_CIRC]; ABarrel bar[AR_BAR]; Tree tree[AR_TREE];
    float spawn[AR_SPAWN][2]; ARect roof[AR_ROOF], floor_[AR_ROOF];
} Arena;
extern Arena AR;

int   ar_load(const char *line);                          /* parse the host's {"t":"arena",...} line */
void  ar_collide(float *px, float *pz, float rad, float feet);
float ar_ray(float ox, float oy, float oz, float dx, float dy, float dz, float maxT);
float ray_cyl(float ox, float oy, float oz, float dx, float dy, float dz, float cx, float cz, float r, float y0, float y1, float mx);
float ray_sph(float ox, float oy, float oz, float dx, float dy, float dz, float cx, float cy, float cz, float r, float mx);
#endif
