/* The flat 3D renderer (p3d.h). Part of the unity build (app.c), after fm1-base's gfx.c: it draws with cv_rect and
 * cv_line. Depth is in screen units (a mesh's z times its scale): larger is nearer. */
#include "p3d.h"
#include "fastmath.h"
#include "plat.h"

#define P3D_MAX 300                 /* the spec's budget: the planet uses about 90, MODAL 244, the MARBLES canyon 268 */
#define P3D_MAXV 160                /* vertices of one mesh: the SPEECH head has 154 */

/* coordinates in quarter pixels and depth in quarter screen units, 16 bits: 256 of them in the room of 128 floats */
/* width 0: a triangle; emask bits 0-2 its edges outlined, bits 3-7 their colour in edges[] (18 bytes: the pool is short) */
typedef struct { int16_t x[3], y[3], z; uint16_t c; uint8_t width, emask; } p3_prim;
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
static uint16_t edges[32];          /* the frame's outline colours */
static int nedges;

void p3d_begin(void) { nprims = sorted = nedges = 0; }
int p3d_count(void) { return nprims; }

/* an outline colour's place in the frame's table (32 at most: the last one shared) */
static int edge_index(uint16_t c)
{
    int e = 0;
    while (e < nedges && edges[e] != c)
        e++;
    if (e == nedges && nedges < 32)
        edges[nedges++] = c;
    if (e == 32)
        e = 31, edges[31] = c;
    return e;
}

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
                    seg(F4(p->x[k]), F4(p->y[k]) + o, F4(p->x[(k + 1) % 3]), F4(p->y[(k + 1) % 3]) + o, 1, edges[p->emask >> 3]);
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
    int i, e = 0;
    if (edge)
        e = edge_index(*edge);
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
        if (q && edge)
            q->emask = (uint8_t)(e << 3 | (~m->f[i].hide & 7u));
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

/* ---- the perspective camera (p3d.h) ---- */
static struct { p3_cam c; float cy, sy, cp, sp, l[3], far; uint16_t fog; } pc;

void p3d_camera(const p3_cam *c, p3_vec light, uint16_t fog, float far)
{
    float n = fm_sqrtf(light.x * light.x + light.y * light.y + light.z * light.z);
    pc.c = *c;
    pc.cy = fm_cosf(c->yaw), pc.sy = fm_sinf(c->yaw), pc.cp = fm_cosf(c->pitch), pc.sp = fm_sinf(c->pitch);
    pc.l[0] = light.x / n, pc.l[1] = light.y / n, pc.l[2] = light.z / n;
    pc.fog = fog;
    pc.far = far;
}

static p3_vec view(p3_vec w)
{
    float X = w.x * pc.cy - w.z * pc.sy, Z = w.x * pc.sy + w.z * pc.cy + pc.c.dist, Y = w.y - pc.c.h;
    return (p3_vec){X, Y * pc.cp + Z * pc.sp, Z * pc.cp - Y * pc.sp};
}

int p3d_project(p3_vec w, float *sx, float *sy, float *z)
{
    p3_vec v = view(w);
    if (v.z < 0.3f)
        return 0;
    *sx = 120.0f + v.x * pc.c.f / v.z;
    *sy = pc.c.cy - v.y * pc.c.f / v.z;
    *z = v.z;
    return 1;
}

uint16_t p3d_mix(uint16_t a, uint16_t b, float k)
{
    int r = (a >> 11) + (int)((float)((b >> 11) - (a >> 11)) * k + ((b >> 11) >= (a >> 11) ? 0.5f : -0.5f));
    int g = (a >> 5 & 63) + (int)((float)((b >> 5 & 63) - (a >> 5 & 63)) * k + ((b >> 5 & 63) >= (a >> 5 & 63) ? 0.5f : -0.5f));
    int u = (a & 31) + (int)((float)((b & 31) - (a & 31)) * k + ((b & 31) >= (a & 31) ? 0.5f : -0.5f));
    return (uint16_t)(r << 11 | g << 5 | u);
}

/* the fog at sort depth z: towards pc.fog, in eighths */
static float fog_at(float z)
{
    float k = fm_clampf((z - 0.35f * pc.far) / (0.65f * pc.far), 0.0f, 0.85f);
    return fm_floorf(k * 8.0f + 0.5f) * 0.125f;
}

uint16_t p3d_fogged(uint16_t c, float z) { return p3d_mix(c, pc.fog, fog_at(z)); }

/* n view-space points (3 or 4) as one face: culled when it faces away, toned, fogged; a quad is two triangles with
   its p0-p2 diagonal hidden */
static void face(const p3_vec *w, int n, const uint16_t tones[3], const uint16_t *edge, float bias)
{
    p3_vec v[4];
    float x[4], y[4], z = 0.0f;
    for (int i = 0; i < n; i++) {
        v[i] = view(w[i]);
        if (v[i].z < 0.3f)
            return;
        x[i] = 120.0f + v[i].x * pc.c.f / v[i].z;
        y[i] = pc.c.cy - v[i].y * pc.c.f / v[i].z;
        z += v[i].z;
    }
    float ux = v[1].x - v[0].x, uy = v[1].y - v[0].y, uz = v[1].z - v[0].z;
    float wx = v[2].x - v[0].x, wy = v[2].y - v[0].y, wz = v[2].z - v[0].z;
    float nx = uy * wz - uz * wy, ny = uz * wx - ux * wz, nz = ux * wy - uy * wx;
    if (nx * v[0].x + ny * v[0].y + nz * v[0].z >= 0.0f)      /* facing away */
        return;
    float len = fm_sqrtf(nx * nx + ny * ny + nz * nz), d = len > 0.0f ? (nx * pc.l[0] + ny * pc.l[1] + nz * pc.l[2]) / len : 0.0f;
    z = z / (float)n - bias;
    float k = fog_at(z);
    uint16_t c = p3d_mix(tones[d > 0.6f ? 0 : d > 0.2f ? 1 : 2], pc.fog, k);
    int e = edge ? edge_index(p3d_mix(*edge, pc.fog, k)) : 0;
    for (int t = 0; t < n - 2; t++) {
        p3_prim *q = tri(x[0], y[0], x[t + 1], y[t + 1], x[t + 2], y[t + 2], -25.0f * z, c);
        if (q && edge)                      /* a quad's diagonal p0-p2 is hidden */
            q->emask = (uint8_t)(e << 3 | (n == 3 ? 7u : t == 0 ? 3u : 6u));
    }
}

void p3d_tri3(const p3_vec p[3], const uint16_t tones[3], const uint16_t *edge, float bias) { face(p, 3, tones, edge, bias); }
void p3d_quad3(const p3_vec p[4], const uint16_t tones[3], const uint16_t *edge, float bias) { face(p, 4, tones, edge, bias); }

/* the faces of a box whose corner i is at (bit 0: +x, bit 1: +y, bit 2: +z), each a loop */
static const uint8_t BOX_Q[6][4] = {{0, 1, 3, 2}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 3, 7, 5}};

static void box_faces(const p3_vec v[8], const uint16_t tones[3], const uint16_t *edge)
{
    p3_vec c = {0, 0, 0};
    for (int i = 0; i < 8; i++)
        c.x += v[i].x * 0.125f, c.y += v[i].y * 0.125f, c.z += v[i].z * 0.125f;
    for (int f = 0; f < 6; f++) {           /* each face turned out of the box, then culled by face() */
        p3_vec q[4] = {v[BOX_Q[f][0]], v[BOX_Q[f][1]], v[BOX_Q[f][2]], v[BOX_Q[f][3]]};
        float ux = q[1].x - q[0].x, uy = q[1].y - q[0].y, uz = q[1].z - q[0].z;
        float wx = q[2].x - q[0].x, wy = q[2].y - q[0].y, wz = q[2].z - q[0].z;
        float mx = (q[0].x + q[2].x) * 0.5f - c.x, my = (q[0].y + q[2].y) * 0.5f - c.y, mz = (q[0].z + q[2].z) * 0.5f - c.z;
        if ((uy * wz - uz * wy) * mx + (uz * wx - ux * wz) * my + (ux * wy - uy * wx) * mz < 0.0f) {
            p3_vec t = q[1];
            q[1] = q[3], q[3] = t;
        }
        face(q, 4, tones, edge, 0.0f);
    }
}

void p3d_corners3(const p3_vec v[8], const uint16_t tones[3], const uint16_t *edge) { box_faces(v, tones, edge); }

void p3d_box3(p3_vec c, p3_vec h, const uint16_t tones[3], const uint16_t *edge)
{
    p3_vec v[8];
    for (int i = 0; i < 8; i++)
        v[i] = (p3_vec){c.x + (i & 1 ? h.x : -h.x), c.y + (i & 2 ? h.y : -h.y), c.z + (i & 4 ? h.z : -h.z)};
    box_faces(v, tones, edge);
}

void p3d_beam3(p3_vec a, p3_vec b, float w, const uint16_t tones[3], const uint16_t *edge)
{
    float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z, l = fm_sqrtf(dx * dx + dy * dy + dz * dz);
    if (l <= 0.0f)
        return;
    dx /= l, dy /= l, dz /= l;
    float rx = fm_fabsf(dy) < 0.9f ? 0.0f : 1.0f, ry = fm_fabsf(dy) < 0.9f ? 1.0f : 0.0f;   /* a reference not along u */
    float n1x = dy * 0 - dz * ry, n1y = dz * rx - dx * 0, n1z = dx * ry - dy * rx, n1 = fm_sqrtf(n1x * n1x + n1y * n1y + n1z * n1z);
    n1x /= n1, n1y /= n1, n1z /= n1;
    float n2x = dy * n1z - dz * n1y, n2y = dz * n1x - dx * n1z, n2z = dx * n1y - dy * n1x;
    p3_vec v[8];
    for (int i = 0; i < 8; i++) {
        const p3_vec *p = i & 4 ? &b : &a;
        float s1 = (i & 1 ? w : -w), s2 = (i & 2 ? w : -w);
        v[i] = (p3_vec){p->x + n1x * s1 + n2x * s2, p->y + n1y * s1 + n2y * s2, p->z + n1z * s1 + n2z * s2};
    }
    box_faces(v, tones, edge);
}

void p3d_line3(p3_vec a, p3_vec b, uint8_t width, uint16_t c, float bias)
{
    float xa, ya, za, xb, yb, zb;
    if (!p3d_project(a, &xa, &ya, &za) || !p3d_project(b, &xb, &yb, &zb))
        return;
    float z = 0.5f * (za + zb) - bias;
    p3d_line(xa, ya, xb, yb, -25.0f * z, width, p3d_mix(c, pc.fog, fog_at(z)));
}
