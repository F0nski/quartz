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

int main(void)
{
    planet_orange_then_pink();
    harm_sets_ships();
    morph_tilts_ring();
    time_not_frames();
    scene_has_fm_only();
    no_flash_on_arrival();
    printf("scene ok\n");
    return 0;
}
