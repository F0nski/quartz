#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* doubles: the panel */
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px) { (void)x, (void)y, (void)w, (void)h, (void)px; }
#include "gfx.c"
#include "p3d.c"
#include "plateau.c"

static q_plateau V;
static uint16_t px(int x, int y) { return swap16(cv_px[y * 240 + x]); }
static int is_block(uint16_t c) { return c == PL_WHITE[0] || c == PL_WHITE[1] || c == PL_WHITE[2]; }

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
    assert(count(0, PL_WHITE[0], 0, 128) + count(1, PL_WHITE[0], 0, 128) == 0);
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

int main(void)
{
    block_height_follows_vel();
    beyond_len_dark();
    extra_marker();
    held_is_orange();
    hit_turns_pink();
    no_hit_on_arrival();
    path_drawn();
    printf("plateau ok\n");
    return 0;
}
