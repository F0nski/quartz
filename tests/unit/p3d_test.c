#include <assert.h>
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
    printf("p3d ok\n");
    return 0;
}
