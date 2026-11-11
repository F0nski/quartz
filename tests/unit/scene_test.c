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

static uint16_t px(int x, int y) { return swap16(cv_px[y * 240 + x]); }
static int is_orange(uint16_t c) { return c == ORANGE[0] || c == ORANGE[1] || c == ORANGE[2]; }
static int is_pink(uint16_t c) { return c == PINK3[0] || c == PINK3[1] || c == PINK3[2]; }

static void band0(void)
{
    cv_begin(240, 83, 0);
    scene_band(0);
}

static void planet_orange_then_pink(void)
{
    const int16_t k[4] = {64, 64, 64, 64};
    scene_frame(k, 0, 1000);
    band0();
    assert(is_orange(px(120, 62)));
    scene_frame(k, 1, 1010);
    band0();
    assert(is_pink(px(120, 62)));
    scene_frame(k, 1, 2000);
    band0();
    assert(is_orange(px(120, 62)));
}

static void harm_sets_ships(void)
{
    int16_t k[4] = {0, 64, 64, 64};
    scene_frame(k, 5, 40000);           /* the note long gone: no shots */
    scene_frame(k, 5, 50000);
    int n0 = p3d_count();
    k[0] = 127;
    scene_frame(k, 5, 50000);
    assert(p3d_count() - n0 == 9);       /* 2 ships, then 5, of 3 triangles */
}

static int orange_above(int y)
{
    for (int r = 0; r < y; r++)
        if (is_orange(px(120, r))) return 1;
    return 0;
}

static void morph_tilts_ring(void)
{
    int16_t k[4] = {64, 64, 0, 64};
    scene_frame(k, 7, 60000);
    band0();
    assert(!orange_above(30));
    k[2] = 127;
    scene_frame(k, 7, 60000);
    band0();
    assert(orange_above(30));
}

static uint16_t ref[240 * 83];

static void time_not_frames(void)
{
    const int16_t k[4] = {90, 30, 100, 10};
    scene_frame(k, 9, 5000);
    band0();
    memcpy(ref, cv_px, sizeof ref);
    for (uint32_t ms = 4000; ms < 5000; ms += 100) scene_frame(k, 9, ms);
    scene_frame(k, 9, 5000);
    band0();
    assert(!memcmp(ref, cv_px, sizeof ref));
}

static void scene_has_fm_only(void)
{
    assert(scene_has(2) && scene_has(3) && scene_has(4) && scene_has(10));
    assert(!scene_has(0) && !scene_has(5) && !scene_has(20) && !scene_has(24));
}

/* arriving on the scene (another track, or back from another page) after notes played elsewhere: no flash */
static void no_flash_on_arrival(void)
{
    const int16_t k[4] = {64, 64, 64, 64};
    scene_frame(k, 3, 1000);
    scene_enter();
    scene_frame(k, 10, 2000);
    band0();
    assert(is_orange(px(120, 62)));
    scene_frame(k, 11, 2010);           /* then a note flashes */
    band0();
    assert(is_pink(px(120, 62)));
}

/* ---- MARBLES ---- */
static void bins_one_and_twelve(void)
{
    int n;
    assert(marbles_bin(1u << 2, 60, &n) == 0 && n == 1);
    assert(marbles_bin(1u << 2, 71, &n) == 0);
    assert(marbles_bin(0xFFF, 61, &n) == 1 && n == 12);
}

static void nearest_bin(void)
{
    int n;
    assert(marbles_bin(0x2A5, 61, &n) == 0 && n == 5);   /* C D F G A: C# between C and D, the lower */
    assert(marbles_bin(0x2A5, 64, &n) == 2);             /* E: F */
    assert(marbles_bin(0x2A5, 70, &n) == 4);             /* A#: A */
}

static const uint8_t MK[3] = {64, 64, 0};

static void mframe(const uint8_t *k, uint32_t ms) { marbles_frame(k, 0x2A5, "1/16", "C MAJOR", ms); }

static void marble_lands_in_its_bin(void)
{
    int n;
    marbles_enter();
    mframe(MK, 0);
    marbles_drop(62, 0, 500);                             /* D: bin 1, sounds at 500 ms */
    for (uint32_t ms = 0; ms <= 600; ms += 30) mframe(MK, ms);
    assert(marbles_flying() == 0);
    assert(marbles_hist(marbles_bin(0x2A5, 62, &n)) == 1);
    for (uint32_t ms = 600; ms <= 4700; ms += 100) mframe(MK, ms);
    assert(marbles_hist(1) == 0);
}

static void six_marbles_max(void)
{
    marbles_enter();
    mframe(MK, 10000);
    for (int i = 0; i < 9; i++) marbles_drop(60 + i, 10000, 10500);
    mframe(MK, 10010);
    assert(marbles_flying() == 6);
}

static void deja_vu_orange(void)
{
    static const uint8_t DV[3] = {64, 64, 100};
    for (int d = 0; d < 2; d++) {
        marbles_enter();
        mframe(d ? DV : MK, 20000);
        marbles_drop(60, 20000, 20500);
        mframe(d ? DV : MK, 20200);
        int o = 0;
        for (int b = 0; b < 2; b++) {
            cv_begin(240, 83, 0);
            marbles_band(-83 * b);
            for (int i = 0; i < 240 * 83; i++) o += swap16(cv_px[i]) == ORANGE[1] || swap16(cv_px[i]) == ORANGE[0];
        }
        assert(d ? o > 0 : o == 0);
    }
}

/* a marble lands when its note sounds, not before: here 300 ms after it was dropped */
static void lands_when_it_sounds(void)
{
    marbles_enter();
    mframe(MK, 30000);
    int h = marbles_hist(1);
    marbles_drop(62, 30000, 30300);
    mframe(MK, 30290);
    assert(marbles_flying() == 1 && marbles_hist(1) == h);
    mframe(MK, 30300);
    assert(marbles_flying() == 0 && marbles_hist(1) == h + 1);
}

int main(void)
{
    planet_orange_then_pink();
    harm_sets_ships();
    morph_tilts_ring();
    time_not_frames();
    scene_has_fm_only();
    no_flash_on_arrival();
    bins_one_and_twelve();
    nearest_bin();
    marble_lands_in_its_bin();
    six_marbles_max();
    deja_vu_orange();
    lands_when_it_sounds();
    printf("scene ok\n");
    return 0;
}
