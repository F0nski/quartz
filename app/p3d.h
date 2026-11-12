/* A small flat-shaded 3D renderer over fm1-base's canvas (gfx.c): primitives are projected once per frame into a
 * list, sorted far to near, then drawn into each band of the scene. Screen axes: x right, y down, z towards the
 * viewer; coordinates are the scene area's, the band's offset is p3d_draw's. */
#ifndef QUARTZ_P3D_H
#define QUARTZ_P3D_H
#include <stdint.h>

typedef struct { float x, y, z; } p3_vec;
typedef struct { uint8_t a, b, c, hide; } p3_face;        /* n = (b - a) x (c - a) points out of the mesh; hide: edges
                                                            a-b (bit 0), b-c (1), c-a (2) left out of an outline */
typedef struct { const p3_vec *v; const p3_face *f; uint16_t nv, nf; } p3_mesh;
typedef struct { float ay, ax, s, cx, cy, z; } p3_pose;   /* rotation about y then x, scale, screen centre, depth added */

void p3d_begin(void);                                     /* empties the list */
void p3d_tri(float x0, float y0, float x1, float y1, float x2, float y2, float z, uint16_t c);
void p3d_line(float x0, float y0, float x1, float y1, float z, uint8_t width, uint16_t c);   /* width 1 or 2 */
void p3d_mesh(const p3_mesh *m, const p3_pose *p, const uint16_t tones[3]);   /* culled, toned, one tri per face */
/* face i toned by tones[part[i]] (part NULL: tones[0]); edge not NULL: each face outlined in it, but its hidden edges */
void p3d_mesh_parts(const p3_mesh *m, const p3_pose *p, const uint16_t (*tones)[3], const uint8_t *part, const uint16_t *edge);
void p3d_draw(int32_t oy);                                /* sorted once per frame (far first), drawn into the band */
int  p3d_count(void);                                     /* primitives in the list */
void p3d_sphere(int lat, int lon, p3_vec *v, p3_face *f, p3_mesh *out);   /* (lat + 1) * lon vertices; quads'
                                                                             diagonals hidden */
float p3d_hash(uint32_t n);                               /* 0..1, the same for the same n: scattered, fixed */
void p3d_fill(float x0, float y0, float x1, float y1, float x2, float y2, uint16_t c);  /* drawn now */

#endif
