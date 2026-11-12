/* The flat 3D renderer (p3d.h). Part of the unity build (app.c), after fm1-base's gfx.c: it draws with cv_rect and
 * cv_line. Depth is in screen units (a mesh's z times its scale): larger is nearer. */
#include "p3d.h"
#include "fastmath.h"
#include "plat.h"

#define P3D_MAX 320                 /* a scene keeps under it: the planet uses about 90, the MARBLES canyon under 300 */
#define P3D_MAXV 160                /* vertices of one mesh: the SPEECH head has 154 */

/* coordinates in quarter pixels and depth in quarter screen units, 16 bits: 256 of them in the room of 128 floats */
typedef struct { int16_t x[3], y[3], z; uint16_t c, edge; uint8_t width, emask; } p3_prim;   /* width 0: a triangle, its
                                                                                          emask edges outlined in edge */
static int16_t q4(float v) { return (int16_t)fm_clampf(v * 4.0f, -8000.0f, 8000.0f); }
#define F4(v) ((float)(v) * 0.25f)

float p3d_hash(uint32_t n)
{
    n = (n ^ 0x9e3779b9u) * 0x85ebca6bu;
    n ^= n >> 13;
    n *= 0xc2b2ae35u;
    n ^= n >> 16;
    return (float)(n >> 8) * (1.0f / 16777216.0f);
}

static p3_prim prims[P3D_MAX] PLAT_POOL;
static p3_prim *order[P3D_MAX];
static int nprims, sorted;

void p3d_begin(void) { nprims = sorted = 0; }
int p3d_count(void) { return nprims; }

static p3_prim *add(float z, uint16_t c, uint8_t width)
{
    p3_prim *p;
    if (nprims == P3D_MAX)
        return 0;
    p = &prims[nprims];
    order[nprims++] = p;
    p->z = q4(z);
    p->c = c;
    p->width = width;
    p->emask = 0;
    sorted = 0;
    return p;
}

static p3_prim *tri(float x0, float y0, float x1, float y1, float x2, float y2, float z, uint16_t c)
{
    p3_prim *p = add(z, c, 0);
    if (p)
        p->x[0] = q4(x0), p->y[0] = q4(y0), p->x[1] = q4(x1), p->y[1] = q4(y1), p->x[2] = q4(x2), p->y[2] = q4(y2);
    return p;
}

void p3d_tri(float x0, float y0, float x1, float y1, float x2, float y2, float z, uint16_t c) { tri(x0, y0, x1, y1, x2, y2, z, c); }

void p3d_line(float x0, float y0, float x1, float y1, float z, uint8_t width, uint16_t c)
{
    p3_prim *p = add(z, c, width);
    if (!p)
        return;
    p->x[0] = q4(x0), p->y[0] = q4(y0), p->x[1] = q4(x1), p->y[1] = q4(y1);
}

static int32_t pix(float v) { return (int32_t)fm_floorf(fm_clampf(v, -2.0f, 242.0f)); }   /* kept near the canvas */
static int32_t ceil_px(float v) { return -(int32_t)fm_floorf(-fm_clampf(v, -2.0f, 242.0f)); }

/* pixel (x, y) is filled when its centre is inside: left and top edges in, right and bottom out */
void p3d_fill(float x0, float y0, float x1, float y1, float x2, float y2, uint16_t c)
{
    float t, area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    int32_t y, ya, yb;
    if (area == 0.0f)
        return;
    if (y1 < y0) { t = x0, x0 = x1, x1 = t; t = y0, y0 = y1, y1 = t; }
    if (y2 < y1) { t = x1, x1 = x2, x2 = t; t = y1, y1 = y2, y2 = t; }
    if (y1 < y0) { t = x0, x0 = x1, x1 = t; t = y0, y0 = y1, y1 = t; }
    ya = (int32_t)fm_clampf((float)ceil_px(fm_clampf(y0, -300.0f, 300.0f) - 0.5f), 0.0f, (float)cv_h);   /* rows whose */
    yb = (int32_t)fm_clampf((float)ceil_px(fm_clampf(y2, -300.0f, 300.0f) - 0.5f), 0.0f, (float)cv_h);   /* centre is in [y0, y2) */
    for (y = ya; y < yb; y++) {
        float yc = (float)y + 0.5f, xl = x0 + (yc - y0) * (x2 - x0) / (y2 - y0);
        float xs = yc < y1 ? x0 + (yc - y0) * (x1 - x0) / (y1 - y0) : x1 + (yc - y1) * (x2 - x1) / (y2 - y1);
        int32_t a, b;
        if (xs < xl)
            t = xl, xl = xs, xs = t;
        a = ceil_px(xl - 0.5f);             /* x + 0.5 in [xl, xs) */
        b = ceil_px(xs - 0.5f);
        if (b > a)
            cv_rect(a, y, b - a, 1, c);
    }
}

static void sort(void)
{
    int i, j;
    for (i = 1; i < nprims; i++) {          /* insertion: stable, and the list is short */
        p3_prim *p = order[i];
        for (j = i; j > 0 && order[j - 1]->z > p->z; j--)
            order[j] = order[j - 1];
        order[j] = p;
    }
    sorted = 1;
}

/* a line in band coordinates, skipped when wholly above or below the band */
static void seg(float xa, float ya, float xb, float yb, int width, uint16_t c)
{
    int32_t x0 = pix(xa + 0.5f), y0 = (int32_t)fm_floorf(fm_clampf(ya, -300.0f, 300.0f) + 0.5f);
    int32_t x1 = pix(xb + 0.5f), y1 = (int32_t)fm_floorf(fm_clampf(yb, -300.0f, 300.0f) + 0.5f);
    if ((y0 < 0 && y1 < 0) || (y0 >= (int32_t)cv_h && y1 >= (int32_t)cv_h))
        return;
    cv_line(x0, y0, x1, y1, c);
    if (width > 1)
        cv_line(x0 + 1, y0, x1 + 1, y1, c);
}

void p3d_draw(int32_t oy)
{
    int i;
    if (!sorted)
        sort();
    for (i = 0; i < nprims; i++) {
        const p3_prim *p = order[i];
        float o = (float)oy;
        if (!p->width) {
            p3d_fill(F4(p->x[0]), F4(p->y[0]) + o, F4(p->x[1]), F4(p->y[1]) + o, F4(p->x[2]), F4(p->y[2]) + o, p->c);
            for (int k = 0; k < 3; k++)
                if (p->emask >> k & 1)
                    seg(F4(p->x[k]), F4(p->y[k]) + o, F4(p->x[(k + 1) % 3]), F4(p->y[(k + 1) % 3]) + o, 1, p->edge);
        } else {
            seg(F4(p->x[0]), F4(p->y[0]) + o, F4(p->x[1]), F4(p->y[1]) + o, p->width, p->c);
        }
    }
}

void p3d_mesh(const p3_mesh *m, const p3_pose *p, const uint16_t tones[3])
{
    p3d_mesh_parts(m, p, (const uint16_t (*)[3])tones, 0, 0);
}

void p3d_mesh_parts(const p3_mesh *m, const p3_pose *p, const uint16_t (*tones)[3], const uint8_t *part, const uint16_t *edge)
{
    static p3_vec r[P3D_MAXV];
    static const float L[3] = {-0.48795f, -0.63434f, 0.58554f};     /* (-0.5, -0.65, 0.6) normalised: up left front */
    float sy = fm_sinf(p->ay), cy = fm_cosf(p->ay), sx = fm_sinf(p->ax), cx = fm_cosf(p->ax);
    int i;
    for (i = 0; i < m->nv && i < P3D_MAXV; i++) {
        const p3_vec *v = &m->v[i];
        float x = v->x * cy + v->z * sy, z = -v->x * sy + v->z * cy;
        r[i].x = x;
        r[i].y = v->y * cx - z * sx;
        r[i].z = v->y * sx + z * cx;
    }
    for (i = 0; i < m->nf; i++) {
        const p3_vec *a = &r[m->f[i].a], *b = &r[m->f[i].b], *c = &r[m->f[i].c];
        float ux = b->x - a->x, uy = b->y - a->y, uz = b->z - a->z, vx = c->x - a->x, vy = c->y - a->y, vz = c->z - a->z;
        float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx, n, d;
        p3_prim *q;
        if (nz <= 0.0f)
            continue;
        n = fm_sqrtf(nx * nx + ny * ny + nz * nz);
        d = (nx * L[0] + ny * L[1] + nz * L[2]) / n;
        q = tri(p->cx + p->s * a->x, p->cy + p->s * a->y, p->cx + p->s * b->x, p->cy + p->s * b->y,
                p->cx + p->s * c->x, p->cy + p->s * c->y, p->z + p->s * (a->z + b->z + c->z) * (1.0f / 3.0f),
                tones[part ? part[i] : 0][d > 0.55f ? 0 : d > 0.15f ? 1 : 2]);
        if (q && edge) {
            q->edge = *edge;
            q->emask = (uint8_t)(~m->f[i].hide & 7u);
        }
    }
}

void p3d_sphere(int lat, int lon, p3_vec *v, p3_face *f, p3_mesh *out)
{
    int i, j, nf = 0;
    for (i = 0; i <= lat; i++)
        for (j = 0; j < lon; j++) {
            float th = (float)i / (float)lat * FM_PI - 0.5f * FM_PI, ph = (float)j / (float)lon * FM_TWO_PI;
            p3_vec *q = &v[i * lon + j];
            q->x = fm_cosf(th) * fm_sinf(ph);
            q->y = fm_sinf(th);
            q->z = fm_cosf(th) * fm_cosf(ph);
        }
    for (i = 0; i < lat; i++)
        for (j = 0; j < lon; j++) {
            uint8_t a = (uint8_t)(i * lon + j), b = (uint8_t)(i * lon + (j + 1) % lon);
            uint8_t c = (uint8_t)((i + 1) * lon + (j + 1) % lon), d = (uint8_t)((i + 1) * lon + j);
            if (i == 0) {                   /* the poles' rows are triangles */
                f[nf++] = (p3_face){a, c, d, 0};
            } else if (i == lat - 1) {
                f[nf++] = (p3_face){a, b, d, 0};
            } else {
                f[nf++] = (p3_face){a, b, c, 4};   /* c-a, the diagonal */
                f[nf++] = (p3_face){a, c, d, 1};   /* a-c */
            }
        }
    out->v = v;
    out->f = f;
    out->nv = (uint16_t)((lat + 1) * lon);
    out->nf = (uint16_t)nf;
}
