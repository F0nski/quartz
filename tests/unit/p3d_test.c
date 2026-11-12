#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* doubles: the panel */
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px) { (void)x, (void)y, (void)w, (void)h, (void)px; }
#include "gfx.c"
#include "p3d.c"

#define RED RGB(255, 0, 0)
#define BLUE RGB(0, 0, 255)
static const uint16_t ORANGE[3] = {RGB(255, 193, 150), RGB(255, 122, 46), RGB(163, 65, 12)};

static uint16_t px(int x, int y) { return swap16(cv_px[y * 240 + x]); }

/* the reference: pixel centre strictly inside the three edges (vertices are chosen so no centre lies on an edge) */
static int inside(const float *t, float x, float y)
{
    float s = 0;
    int i, pos = 0, neg = 0;
    for (i = 0; i < 3; i++) {
        float ax = t[2 * i], ay = t[2 * i + 1], bx = t[2 * ((i + 1) % 3)], by = t[2 * ((i + 1) % 3) + 1];
        s = (bx - ax) * (y - ay) - (by - ay) * (x - ax);
        pos += s > 0;
        neg += s < 0;
    }
    return pos == 3 || neg == 3;
}

static const float TRIS[5][6] = {
    {10.25f, 5.25f, 60.75f, 70.25f, 5.25f, 40.75f},
    {100.25f, 2.75f, 230.75f, 30.25f, 120.25f, 80.75f},
    {3.25f, 60.25f, 236.75f, 62.75f, 120.25f, 64.25f},          /* very flat */
    {50.75f, 10.25f, 52.25f, 78.75f, 49.25f, 40.25f},           /* very thin */
    {200.25f, 70.25f, 150.75f, 20.25f, 220.75f, 5.25f},
};

static void fill_matches_centres(void)
{
    for (int k = 0; k < 5; k++) {
        const float *t = TRIS[k];
        cv_begin(240, 83, 0);
        p3d_fill(t[0], t[1], t[2], t[3], t[4], t[5], RED);
        for (int y = 0; y < 83; y++)
            for (int x = 0; x < 240; x++)
                assert((px(x, y) == RED) == inside(t, x + 0.5f, y + 0.5f));
    }
}

static void band_split_identical(void)
{
    const float t[6] = {30.25f, 40.25f, 210.75f, 100.75f, 90.25f, 140.75f};
    for (int b = 0; b < 2; b++) {
        cv_begin(240, 83, 0);
        p3d_begin();
        p3d_tri(t[0], t[1], t[2], t[3], t[4], t[5], 0, RED);
        p3d_draw(-83 * b);
        for (int y = 0; y < 83; y++)
            for (int x = 0; x < 240; x++)
                assert((px(x, y) == RED) == inside(t, x + 0.5f, y + 83 * b + 0.5f));
    }
}

static int changed(void)
{
    int n = 0;
    for (int i = 0; i < 240 * 83; i++) n += cv_px[i] != 0;
    return n;
}

static void degenerate_triangle(void)
{
    cv_begin(240, 83, 0);
    p3d_fill(10, 10, 50, 50, 90, 90, RED);
    p3d_fill(10, 20, 50, 20, 90, 20, RED);
    p3d_fill(10.5f, 20.5f, 10.5f, 20.5f, 10.5f, 20.5f, RED);
    assert(changed() == 0);
}

static void offscreen_clipped(void)
{
    cv_begin(240, 83, 0);
    p3d_begin();
    p3d_tri(-500, -500, 800, 10, 10, 900, 0, RED);
    p3d_line(-300, 20, 600, 60, 1, 2, BLUE);
    p3d_draw(0);
    p3d_draw(-83);
    assert(changed() > 0);
}

static void far_drawn_first(void)
{
    cv_begin(240, 83, 0);
    p3d_begin();
    p3d_tri(10, 10, 100, 10, 10, 70, 1, RED);
    p3d_tri(20, 20, 110, 20, 20, 80, -1, BLUE);
    p3d_draw(0);
    assert(px(30, 30) == RED);
    assert(px(25, 75) == BLUE);
}

static p3_vec SV[64];
static p3_face SF[96];

static void sphere_closed(void)
{
    p3_mesh m;
    p3d_sphere(5, 10, SV, SF, &m);
    assert(m.nv == 60 && m.nf == 80);
    for (int i = 0; i < m.nf; i++) {
        p3_vec a = SV[SF[i].a], b = SV[SF[i].b], c = SV[SF[i].c];
        float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z, vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
        float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        float cx = (a.x + b.x + c.x) / 3, cy = (a.y + b.y + c.y) / 3, cz = (a.z + b.z + c.z) / 3;
        assert(nx * cx + ny * cy + nz * cz > 0);
    }
}

static void mesh_culls_and_tones(void)
{
    p3_mesh m;
    p3_pose p = {0, 0, 26, 120, 60};
    p3d_sphere(5, 10, SV, SF, &m);
    cv_begin(240, 83, 0);
    p3d_begin();
    p3d_mesh(&m, &p, ORANGE);
    assert(p3d_count() >= 30 && p3d_count() <= 50);
    p3d_draw(0);
    assert(px(108, 48) == ORANGE[0]);
    assert(px(132, 72) == ORANGE[2]);
}

/* rows whose centre lies on a horizontal edge: the top one in, the bottom one out, and no 0/0 */
static void flat_on_centres(void)
{
    static const float T[2][6] = {{10, 10.5f, 60, 10.5f, 30, 40.5f}, {10, 40.5f, 60, 40.5f, 30, 10.5f}};
    for (int k = 0; k < 2; k++) {
        cv_begin(240, 83, 0);
        p3d_fill(T[k][0], T[k][1], T[k][2], T[k][3], T[k][4], T[k][5], RED);
        int top = 0, bottom = 0;
        for (int x = 0; x < 240; x++) { top += px(x, 10) == RED; bottom += px(x, 40) == RED; }
        assert(k == 0 ? top > 0 : top == 0);
        assert(bottom == 0);
        for (int y = 11; y < 40; y++)
            for (int x = 0; x < 240; x++)
                if (x + 0.5f != 30.0f) {
                    float t[6];
                    memcpy(t, T[k], sizeof t);
                    if (inside(t, x + 0.5f, y + 0.5f)) assert(px(x, y) == RED);
                }
    }
}


/* a 40-px square at (10, 10) facing the viewer, cut along its diagonal 0-2 */
static const p3_vec SQ[4] = {{0, 0, 0}, {40, 0, 0}, {40, 40, 0}, {0, 40, 0}};
static const p3_face SQF[2] = {{0, 1, 2, 4}, {0, 2, 3, 1}};
static const uint16_t RB[2][3] = {{RED, RED, RED}, {BLUE, BLUE, BLUE}};

static void parts_take_their_tones(void)
{
    static const uint8_t part[2] = {0, 1};
    p3_mesh m = {SQ, SQF, 4, 2};
    p3_pose p = {0, 0, 1, 10, 10};
    cv_begin(240, 83, 0);
    p3d_begin();
    p3d_mesh_parts(&m, &p, RB, part, NULL);
    p3d_draw(0);
    assert(px(40, 20) == RED);           /* above the diagonal: face 0 */
    assert(px(20, 40) == BLUE);
}

static void edge_skips_diagonal(void)
{
    static const uint8_t part[2] = {1, 1};
    const uint16_t red = RED;
    p3_mesh m = {SQ, SQF, 4, 2};
    p3_pose p = {0, 0, 1, 10, 10};
    cv_begin(240, 83, 0);
    p3d_begin();
    p3d_mesh_parts(&m, &p, RB, part, &red);
    assert(p3d_count() == 2);
    p3d_draw(0);
    int top = 0, left = 0, right = 0, bottom = 0;
    for (int k = 12; k < 48; k++) {
        top += px(k, 10) == RED;
        bottom += px(k, 50) == RED;
        left += px(10, k) == RED;
        right += px(50, k) == RED;
    }
    assert(top && bottom && left && right);
    assert(px(30, 30) == BLUE);
    assert(px(20, 20) == BLUE);
}

static void edge_drawn_in_depth_order(void)
{
    static const uint8_t part[2] = {1, 1};
    const uint16_t red = RED;
    p3_mesh m = {SQ, SQF, 4, 2};
    p3_pose p = {0, 0, 1, 10, 10, -5};
    cv_begin(240, 83, 0);
    p3d_begin();
    p3d_mesh_parts(&m, &p, RB, part, &red);
    p3d_tri(0, 0, 30, 0, 0, 70, 5, ORANGE[1]);   /* nearer, over the square's left side */
    p3d_draw(0);
    for (int y = 12; y < 44; y++) assert(px(10, y) == ORANGE[1]);
}

static void sphere_marks_diagonals(void)
{
    p3_mesh m;
    p3d_sphere(5, 10, SV, SF, &m);
    for (int i = 0; i < m.nf; i++) {
        int bits = __builtin_popcount(SF[i].hide);
        assert(i < 10 || i >= m.nf - 10 ? bits == 0 : bits == 1);
    }
}


/* ---- 5d1: the perspective camera ---- */
static const p3_cam CAM0 = {0, 0, 4, 0, 100, 41.5f};
static const p3_vec LUP = {-0.5f, 0.75f, -0.45f};
static const uint16_t BL3[3] = {BLUE, BLUE, BLUE};

static void world_quad_projects(void)
{
    const p3_vec q[4] = {{-1, -1, 0}, {-1, 1, 0}, {1, 1, 0}, {1, -1, 0}};
    cv_begin(240, 83, 0);
    p3d_begin();
    p3d_camera(&CAM0, LUP, 0, 12);
    p3d_quad3(q, BL3, NULL, 0);
    assert(p3d_count() == 2);
    p3d_draw(0);
    assert(px(120, 41) == BLUE);
    assert(px(80, 41) != BLUE && px(160, 41) != BLUE);
}

static void world_back_face_culled(void)
{
    const p3_vec q[4] = {{1, -1, 0}, {1, 1, 0}, {-1, 1, 0}, {-1, -1, 0}};
    p3d_begin();
    p3d_camera(&CAM0, LUP, 0, 12);
    p3d_quad3(q, BL3, NULL, 0);
    assert(p3d_count() == 0);
}

static void world_fog_darkens(void)
{
    const p3_vec n[4] = {{-1, -1, 0}, {-1, 1, 0}, {1, 1, 0}, {1, -1, 0}};
    const p3_vec f[4] = {{-1, -1, 10}, {-1, 1, 10}, {1, 1, 10}, {1, -1, 10}};
    cv_begin(240, 83, 0);
    p3d_begin();
    p3d_camera(&CAM0, LUP, 0, 12);
    p3d_quad3(n, BL3, NULL, 0);
    p3d_draw(0);
    uint16_t near = px(120, 41);
    cv_begin(240, 83, 0);
    p3d_begin();
    p3d_quad3(f, BL3, NULL, 0);
    p3d_draw(0);
    uint16_t far = px(120, 41);
    assert((far & 31) < (near & 31));       /* blue channel */
}

static void world_clip_behind(void)
{
    const p3_vec q[4] = {{-1, -1, -3.9f}, {-1, 1, 0}, {1, 1, 0}, {1, -1, 0}};
    float x, y, z;
    p3d_begin();
    p3d_camera(&CAM0, LUP, 0, 12);
    p3d_quad3(q, BL3, NULL, 0);
    assert(p3d_count() == 0);
    assert(!p3d_project((p3_vec){0, 0, -3.9f}, &x, &y, &z));
    assert(p3d_project((p3_vec){0, 0, 0}, &x, &y, &z));
    assert(fabsf(x - 120.0f) < 1e-3f && fabsf(y - 41.5f) < 1e-3f && fabsf(z - 4.0f) < 1e-3f);   /* (fastmath: cos 0 is not exactly 1) */
}

static void edge_palette_32(void)
{
    cv_begin(240, 83, 0);
    p3d_begin();
    p3d_camera(&CAM0, LUP, 0, 100);
    for (int i = 0; i < 20; i++) {
        float x = -4.5f + 0.45f * (float)i;
        const p3_vec t[3] = {{x, -1, 0}, {x, 1, 0}, {x + 0.3f, 1, 0}};
        const uint16_t e = (uint16_t)RGB(10 + 10 * i, 255, 0);
        p3d_tri3(t, BL3, &e, 0);
    }
    p3d_draw(0);
    for (int i = 0; i < 20; i++) {
        int found = 0;
        for (int y = 0; y < 83 && !found; y++)
            for (int x = 0; x < 240 && !found; x++)
                found = px(x, y) == (uint16_t)RGB(10 + 10 * i, 255, 0);
        assert(found);
    }
}


/* a box seen from the front and above: 3 faces (6 triangles), the front one around the centre */
static void world_box_three_faces(void)
{
    const p3_cam c = {0.4f, 0.4f, 4, 1.5f, 100, 41.5f};
    p3d_begin();
    p3d_camera(&c, LUP, 0, 12);
    p3d_box3((p3_vec){0, 0, 0}, (p3_vec){0.5f, 0.5f, 0.5f}, BL3, NULL);
    assert(p3d_count() == 6);
    p3d_begin();
    p3d_beam3((p3_vec){0, -0.5f, 0}, (p3_vec){0, 0.5f, 0}, 0.5f, BL3, NULL);
    assert(p3d_count() == 6);
}

int main(void)
{
    fill_matches_centres();
    band_split_identical();
    degenerate_triangle();
    offscreen_clipped();
    far_drawn_first();
    sphere_closed();
    mesh_culls_and_tones();
    flat_on_centres();
    parts_take_their_tones();
    edge_skips_diagonal();
    edge_drawn_in_depth_order();
    sphere_marks_diagonals();
    world_quad_projects();
    world_back_face_culled();
    world_fog_darkens();
    world_clip_behind();
    edge_palette_32();
    world_box_three_faces();
    printf("p3d ok\n");
    return 0;
}
