#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* doubles: the panel */
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px) { (void)x, (void)y, (void)w, (void)h, (void)px; }
#include "gfx.c"
#include "p3d.c"
#include "scene.c"
#include "scene_va.c"
#include "scene_wave.c"
#include "scene_garden.c"
#include "scene_chip.c"
#include "scene_choir.c"
#include "scene_rope.c"
#include "scene_dance.c"
#include "scene_home.c"
#include "scene_sound.c"
#include "scene_glo.c"
#include "scene_life.c"
#include "plateau.c"

static q_plateau V;
static uint16_t px(int x, int y) { return swap16(cv_px[y * 240 + x]); }
static int is_block(uint16_t c) { return c == PL_WHITE[1] || c == PL_WHITE[2]; }   /* (the white top is also a big star's colour) */

static void base(void)
{
    memset(&V, 0, sizeof V);
    for (int i = 0; i < 16; i++) {
        V.c[i].on = 1;
        V.c[i].vel = 100;
        V.c[i].len = 4;
        strcpy(V.c[i].name, "C");
    }
    V.live = 0xFFFF;
    V.pos = V.cursor = V.end = V.held = -1;
    strcpy(V.top_l, "1-16");
    strcpy(V.top_r, "--");
    plateau_enter();
}

static void band(int b)
{
    cv_begin(240, 83, 0);
    plateau_band(-83 * b);
}

/* pixels of colour c in band b (zone rows), in x0 <= x < x1 */
static int count(int b, uint16_t c, int x0, int x1)
{
    int n = 0;
    band(b);
    for (int y = 0; y < 83; y++)
        for (int x = x0; x < x1; x++) n += px(x, y) == c;
    return n;
}

static void block_height_follows_vel(void)
{
    for (int v = 0; v < 2; v++) {
        base();
        for (int i = 1; i < 16; i++) V.c[i].on = 0;
        V.c[0].vel = v ? 127 : 0;
        plateau_frame(&V, 0, 1000);
        band(0);
        int n = 0;
        for (int y = 20; y < 30; y++)    /* under the top texts, whose antialiasing may match a tone */
            for (int x = 0; x < 240; x++) n += is_block(px(x, y));
        assert(v ? n >= 20 : n == 0);
    }
}

static void beyond_len_dark(void)
{
    base();
    V.live = 0x00FF;
    V.end = 7;
    plateau_frame(&V, 0, 1000);
    band(1);
    assert(px(120, 121 - 83) == PL_RGB(0x06, 0x03, 0x1a));
    for (int y = 106 - 83; y < 83; y++)
        for (int x = 0; x < 240; x++) assert(!is_block(px(x, y)));
    assert(count(0, PL_ORANGE[1], 0, 240) >= 10);
}

static void extra_marker(void)
{
    base();
    plateau_frame(&V, 0, 1000);
    assert(count(0, PL_ORANGE[1], 0, 240) == 0);
    V.c[5].extra = 1;
    plateau_frame(&V, 0, 1000);
    assert(count(0, PL_ORANGE[1], 0, 240) > 0);
}

static void held_is_orange(void)
{
    base();
    V.held = 5;
    V.attr = 2;
    for (int r = 0; r < 9; r++) { strcpy(V.rows[r][0], "VEL"); strcpy(V.rows[r][1], "100"); }
    strcpy(V.held_name, "C4");
    plateau_frame(&V, 0, 1000);
    assert(count(0, PL_ORANGE[1], 0, 128) + count(1, PL_ORANGE[1], 0, 128) > 0);
    assert(count(0, PL_WHITE[1], 0, 128) + count(1, PL_WHITE[1], 0, 128) == 0);   /* no lit block face (white is also a star) */
    assert(count(0, PL_ORANGE[1], 128, 240) + count(1, PL_ORANGE[1], 128, 240) > 0);
}

static void hit_turns_pink(void)
{
    base();
    V.pos = 0;
    plateau_frame(&V, 1, 1000);
    plateau_frame(&V, 2, 1010);
    assert(count(0, PL_PINK[1], 0, 240) > 0);
    plateau_frame(&V, 2, 1400);
    assert(count(0, PL_PINK[1], 0, 240) == 0);
}

static void no_hit_on_arrival(void)
{
    base();
    V.pos = 0;
    plateau_frame(&V, 50, 1000);
    assert(count(0, PL_PINK[1], 0, 240) == 0);
}

static void path_drawn(void)
{
    static const uint8_t SNAKE[16] = {0, 1, 2, 3, 7, 6, 5, 4, 8, 9, 10, 11, 15, 14, 13, 12};
    base();
    plateau_frame(&V, 0, 1000);
    assert(count(0, PL_ARROW, 0, 240) + count(1, PL_ARROW, 0, 240) == 0);
    V.npath = 16;
    memcpy(V.path, SNAKE, 16);
    plateau_frame(&V, 0, 1000);
    assert(count(0, PL_ARROW, 0, 240) + count(1, PL_ARROW, 0, 240) > 0);
}

/* the city: tower edges above the horizon (y 48), none below */
static void city_above_horizon(void)
{
    base();
    plateau_frame(&V, 0, 5000);
    band(0);
    int above = 0, below = 0;
    for (int y = 0; y < 83; y++)
        for (int x = 0; x < 240; x++)
            if (px(x, y) == PL_TOWER2) { if (y <= 48) above++; else below++; }
    assert(above > 0 && below == 0);
}

/* ---- GRIDS stacks ---- */
static q_gview G;
static void gbase(void)
{
    memset(&G, 0, sizeof G);
    G.pos = -1; G.voice = 1; G.x = 64; G.y = 64;
    strcpy(G.top_l, "1-32");
    grids_enter();
}
static void gband(int b) { cv_begin(240, 83, 0); grids_band(-83 * b); }
static int gcount(uint16_t c) { int n = 0; for (int b = 0; b < 2; b++) { gband(b); for (int i = 0; i < 240 * 83; i++) n += swap16(cv_px[i]) == c; } return n; }

static void grids_stack_order(void)
{
    static const uint16_t *T[3] = {PL_ORANGE, PL_GREEN, PL_BLUE};
    long sy[3] = {0}, n[3] = {0};
    gbase();
    for (int v = 0; v < 3; v++) { G.level[v][0] = 200; G.dens[v] = 127; }
    G.voice = 0;                                   /* (every voice in its own colours: none picked) */
    grids_frame(&G, 1000);
    for (int b = 0; b < 2; b++) {
        gband(b);
        for (int y = 0; y < 83; y++) for (int x = 0; x < 240; x++)
            for (int v = 0; v < 3; v++) if (swap16(cv_px[y * 240 + x]) == T[v][2]) { sy[v] += y + 83 * b; n[v]++; }
    }
    for (int v = 0; v < 3; v++) assert(n[v] > 0);
    assert(sy[0] / n[0] > sy[1] / n[1] && sy[1] / n[1] > sy[2] / n[2]);   /* BD lowest, HH on top */
}

static void grids_threshold(void)
{
    gbase();
    for (int v = 0; v < 3; v++) for (int s2 = 0; s2 < 32; s2++) G.level[v][s2] = 200;
    grids_frame(&G, 1000);
    assert(gcount(PL_BLUE[2]) == 0 && gcount(PL_GREEN[2]) == 0 && gcount(PL_ORANGE[2]) == 0);
    for (int v = 0; v < 3; v++) G.dens[v] = 127;
    grids_frame(&G, 1000);
    assert(gcount(PL_BLUE[2]) > 0 && gcount(PL_GREEN[2]) > 0 && gcount(PL_ORANGE[2]) > 0);
}

static void grids_hit_pink(void)
{
    gbase();
    G.level[0][0] = 200; G.dens[0] = 127; G.pos = 0;
    grids_frame(&G, 1000);
    grids_drop(0, 1005);
    grids_frame(&G, 1010);
    assert(gcount(PL_PINK[1]) > 0);
    grids_frame(&G, 1400);
    assert(gcount(PL_PINK[1]) == 0);
}

/* the sight sits on the step playing: step 13 is column 5 of row 2 on the 8-column plate */
static void grids_sight_on_its_step(void)
{
    gbase();
    G.pos = 13;
    grids_frame(&G, 1000);
    float cx = 83 + (5.5f - 1.5f) * 18.5f, cy = 42 + (5.5f + 1.5f) * 9.25f;   /* the cell's centre */
    int in = 0, out = 0;
    for (int b = 0; b < 2; b++) {
        gband(b);
        for (int y = 0; y < 83; y++) for (int x = 0; x < 240; x++) {
            if (swap16(cv_px[y * 240 + x]) != PL_GREEN[0] || y + 83 * b < 30 || x < 60) continue;   /* (the top text and the row number are green too) */
            float dx = x - cx, dy = y + 83 * b - cy;
            if (dx * dx + 4 * dy * dy < 30 * 30) in++; else out++;
        }
    }
    assert(in > 0 && out == 0);
}

/* a cell's floor centre on screen (the plain layout), and the pixels of colour c within r of it, both bands */
static int near(int cell, uint16_t c, float x0, float y0, int r)
{
    int n = 0;
    for (int b = 0; b < 2; b++) {
        band(b);
        for (int y = 0; y < 83; y++)
            for (int x = 0; x < 240; x++) {
                float dx = (float)x - x0, dy = (float)(y + 83 * b) - y0;
                n += px(x, y) == c && dx * dx + dy * dy <= (float)(r * r);
            }
    }
    (void)cell;
    return n;
}
#define PCX(gx, gy) (120.0f + ((gx) - (gy)) * 25.0f)
#define PCY(gx, gy) (34.0f + ((gx) + (gy)) * 12.5f)

/* only step 3 lit, 4 steps long: the strip on cell 4, then 5 and 6 on the next row, a chevron where it leaves row 1,
   the end mark on cell 6 */
static void band_two_rows(void)
{
    base();
    for (int i = 0; i < 16; i++) V.c[i].on = 0;
    V.c[2].on = 1;
    V.c[2].len = 32;
    plateau_frame(&V, 0, 1000);
    assert(near(3, PL_GREEN[2], PCX(3.5f, 0.5f), PCY(3.5f, 0.5f), 6) > 0);
    assert(near(4, PL_GREEN[2], PCX(0.4f, 1.5f), PCY(0.4f, 1.5f), 6) > 0);
    assert(near(5, PL_GREEN[2], PCX(1.3f, 1.5f), PCY(1.3f, 1.5f), 4) > 0);
    assert(near(6, PL_GREEN[2], PCX(2.5f, 1.5f), PCY(2.5f, 1.5f), 6) == 0);       /* it ends in cell 6 */
    assert(near(3, PL_GREEN[1], PCX(4.08f, 0.5f), PCY(4.08f, 0.5f), 5) > 0);     /* the chevron off row 1 */
    assert(near(4, PL_GREEN[1], PCX(-0.08f, 1.5f), PCY(-0.08f, 1.5f), 5) > 0);   /* and onto row 2 */
    assert(near(5, PL_GREEN[0], PCX(1.64f, 1.5f), PCY(1.64f, 1.5f), 4) > 0);     /* the end */
    V.c[2].len = 8;                                                             /* one step: no strip */
    plateau_frame(&V, 0, 1000);
    assert(near(3, PL_GREEN[2], PCX(3.5f, 0.5f), PCY(3.5f, 0.5f), 6) == 0);
}

/* step 15, 5 steps long: 3 past the page, "+3 >" at the plate's front edge */
static void band_overflow(void)
{
    base();
    for (int i = 0; i < 16; i++) V.c[i].on = 0;
    V.c[14].on = 1;
    V.c[14].len = 40;
    plateau_frame(&V, 0, 1000);
    int a = count(1, PL_GREEN[1], 120, 240);
    V.c[14].len = 16;
    plateau_frame(&V, 0, 1000);
    assert(a > count(1, PL_GREEN[1], 120, 240) + 5);
}

/* step 3 sounding, the playhead on step 5: cells 3 to 5 pink, 6 green */
static void band_pink_fill(void)
{
    base();
    for (int i = 0; i < 16; i++) V.c[i].on = 0;
    V.c[2].on = 1;
    V.c[2].len = 32;
    V.pos = 4;
    plateau_frame(&V, 0, 1000);
    assert(near(3, PL_PINK[1], PCX(3.5f, 0.5f), PCY(3.5f, 0.5f), 6) > 0);
    assert(near(4, PL_PINK[1], PCX(0.4f, 1.5f), PCY(0.4f, 1.5f), 6) > 0);
    assert(near(5, PL_GREEN[2], PCX(1.3f, 1.5f), PCY(1.3f, 1.5f), 4) > 0);
    assert(near(5, PL_PINK[1], PCX(1.3f, 1.5f), PCY(1.3f, 1.5f), 4) == 0);
}

/* a selected step: orange, not lifted (its top as high as a plain block's) */
static void sel_orange(void)
{
    base();
    for (int i = 0; i < 16; i++) V.c[i].on = i == 2 || i == 1;
    plateau_frame(&V, 0, 1000);
    int top = 99, y0 = 0;
    band(0);
    for (int y = 82; y >= 0; y--)
        for (int x = (int)PCX(1.5f, 0.5f) - 3; x <= (int)PCX(1.5f, 0.5f) + 3; x++)
            if (px(x, y) == PL_WHITE[0]) top = y;
    assert(top < 99);
    V.c[1].sel = 1;
    plateau_frame(&V, 0, 1000);
    band(0);
    for (int y = 82; y >= 0; y--)
        for (int x = (int)PCX(1.5f, 0.5f) - 3; x <= (int)PCX(1.5f, 0.5f) + 3; x++)
            if (px(x, y) == PL_ORANGE[0]) y0 = y;
    assert(y0 == top);
    assert(count(0, PL_ORANGE[0], 0, 240) > 20);
}

int main(void)
{
    block_height_follows_vel();
    beyond_len_dark();
    extra_marker();
    held_is_orange();
    hit_turns_pink();
    no_hit_on_arrival();
    path_drawn();
    city_above_horizon();
    band_two_rows();
    band_overflow();
    band_pink_fill();
    sel_orange();
    grids_stack_order();
    grids_threshold();
    grids_hit_pink();
    grids_sight_on_its_step();
    printf("plateau ok\n");
    return 0;
}
