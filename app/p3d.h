/* A small flat-shaded 3D renderer over fm1-base's canvas (gfx.c): primitives are projected once per frame into a
 * list, sorted far to near, then drawn into each band of the scene. Screen axes: x right, y down, z towards the
 * viewer; coordinates are the scene area's, the band's offset is p3d_draw's. */
#ifndef QUARTZ_P3D_H
#define QUARTZ_P3D_H
#include <stdint.h>

typedef struct { float x, y, z; } p3_vec;
typedef struct { uint8_t a, b, c; } p3_face;              /* n = (b - a) x (c - a) points out of the mesh */
typedef struct { const p3_vec *v; const p3_face *f; uint16_t nv, nf; } p3_mesh;
typedef struct { float ay, ax, s, cx, cy; } p3_pose;      /* rotation about y then x, scale, screen centre */

void p3d_begin(void);                                     /* empties the list */
void p3d_tri(float x0, float y0, float x1, float y1, float x2, float y2, float z, uint16_t c);
void p3d_line(float x0, float y0, float x1, float y1, float z, uint8_t width, uint16_t c);   /* width 1 or 2 */
void p3d_mesh(const p3_mesh *m, const p3_pose *p, const uint16_t tones[3]);   /* culled, toned, one tri per face */
void p3d_draw(int32_t oy);                                /* sorted once per frame (far first), drawn into the band */
int  p3d_count(void);                                     /* primitives in the list */
void p3d_sphere(int lat, int lon, p3_vec *v, p3_face *f, p3_mesh *out);   /* (lat + 1) * lon vertices */
float p3d_hash(uint32_t n);                               /* 0..1, the same for the same n: scattered, fixed */
void p3d_fill(float x0, float y0, float x1, float y1, float x2, float y2, uint16_t c);  /* drawn now */

#endif
