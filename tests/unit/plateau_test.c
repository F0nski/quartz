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
    grids_stack_order();
    grids_threshold();
    grids_hit_pink();
    grids_sight_on_its_step();
    printf("plateau ok\n");
    return 0;
}
