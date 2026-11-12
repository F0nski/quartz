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

/* the planet's view: a 6-OP A track */
static void pframe(const int16_t k[4], uint32_t hits, uint32_t ms)
{
    q_sview v = {2, -1, {k[0], k[1], k[2], k[3]}, {hits, 0, 0}};
    scene_frame(&v, ms);
}

static void band0(void)
{
    cv_begin(240, 83, 0);
    scene_band(0);
}

static void planet_orange_then_pink(void)
{
    const int16_t k[4] = {64, 64, 64, 64};
    pframe(k, 0, 1000);
    band0();
    assert(is_orange(px(120, 62)));
    pframe(k, 1, 1010);
    band0();
    assert(is_pink(px(120, 62)));
    pframe(k, 1, 2000);
    band0();
    assert(is_orange(px(120, 62)));
}

static void harm_sets_ships(void)
{
    int16_t k[4] = {0, 64, 64, 64};
    pframe(k, 5, 40000);           /* the note long gone: no shots */
    pframe(k, 5, 50000);
    int n0 = p3d_count();
    k[0] = 127;
    pframe(k, 5, 50000);
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
    pframe(k, 7, 60000);
    band0();
    assert(!orange_above(30));
    k[2] = 127;
    pframe(k, 7, 60000);
    band0();
    assert(orange_above(30));
}

static uint16_t ref[240 * 83];

static void time_not_frames(void)
{
    const int16_t k[4] = {90, 30, 100, 10};
    pframe(k, 9, 5000);
    band0();
    memcpy(ref, cv_px, sizeof ref);
    for (uint32_t ms = 4000; ms < 5000; ms += 100) pframe(k, 9, ms);
    pframe(k, 9, 5000);
    band0();
    assert(!memcmp(ref, cv_px, sizeof ref));
}

static void scene_has_six_more(void)
{
    assert(scene_has(2) && scene_has(3) && scene_has(4) && scene_has(10));
    assert(scene_has(15) && scene_has(20) && scene_has(21) && scene_has(22) && scene_has(23) && scene_has(24));
    assert(!scene_has(0) && !scene_has(5) && !scene_has(19));
}

/* arriving on the scene (another track, or back from another page) after notes played elsewhere: no flash */
static void no_flash_on_arrival(void)
{
    const int16_t k[4] = {64, 64, 64, 64};
    pframe(k, 3, 1000);
    scene_enter();
    pframe(k, 10, 2000);
    band0();
    assert(is_orange(px(120, 62)));
    pframe(k, 11, 2010);           /* then a note flashes */
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

static q_mview MV;
static void mreset(void)
{
    memset(&MV, 0, sizeof MV);
    MV.tout = 2; MV.spread = 64; MV.xbias = 64; MV.len = 8; MV.pos = 0;
    MV.allowed = 0x2A5; MV.kept = 0x2A5; MV.step_ms = 125;
}
static void mframe(uint32_t ms) { marbles_frame(&MV, ms); }
static int count_px_from(uint16_t c, int b0)    /* in the bands from b0: band 1 is below the fleet */
{
    int o = 0;
    for (int b = b0; b < 2; b++) {
        cv_begin(240, 83, 0);
        marbles_band(-83 * b);
        for (int i = 0; i < 240 * 83; i++) o += swap16(cv_px[i]) == c;
    }
    return o;
}
static int count_px(uint16_t c) { return count_px_from(c, 0); }

static void marble_lands_in_its_bin(void)
{
    int n;
    mreset();
    marbles_enter();
    mframe(0);
    marbles_drop(62, 0, 500);                             /* D: bin 1, sounds at 500 ms */
    for (uint32_t ms = 0; ms <= 600; ms += 30) mframe(ms);
    assert(marbles_flying() == 0);
    assert(marbles_hist(marbles_bin(0x2A5, 62, &n)) == 1);
    for (uint32_t ms = 600; ms <= 4700; ms += 100) mframe(ms);
    assert(marbles_hist(1) == 0);
}

static void six_marbles_max(void)
{
    mreset();
    marbles_enter();
    mframe(10000);
    for (int i = 0; i < 9; i++) marbles_drop(60 + i, 10000, 10500);
    mframe(10010);
    assert(marbles_flying() == 6);
}

static void deja_vu_orange(void)
{
    for (int d = 0; d < 2; d++) {
        mreset();
        MV.dejavu = d ? 100 : 0;
        marbles_enter();
        mframe(20000);
        marbles_drop(60, 20000, 20500);
        mframe(20200);
        int o = count_px(ORANGE[1]) + count_px(ORANGE[0]);
        assert(d ? o > 0 : o == 0);
    }
}

/* a shot lands when its note sounds, not before: here 300 ms after it was fired */
static void lands_when_it_sounds(void)
{
    mreset();
    marbles_enter();
    mframe(30000);
    int h = marbles_hist(1);
    marbles_drop(62, 30000, 30300);
    mframe(30290);
    assert(marbles_flying() == 1 && marbles_hist(1) == h);
    mframe(30300);
    assert(marbles_flying() == 0 && marbles_hist(1) == h + 1);
}

static void ships_follow_tout(void)
{
    for (int k = 1; k <= 3; k++) {
        mreset();
        MV.tout = (uint8_t)k;
        mframe(40000);
        assert(marbles_ships() == k);
    }
}

/* STEPS keeping one note: the other crystals are outlines, so far fewer crystal pixels */
static void steps_dims_crystals(void)
{
    mreset();
    mframe(50000);
    int all = count_px_from(SHIP[0][1], 1) + count_px_from(SHIP[0][0], 1);
    MV.kept = 1u << 0;
    mframe(50000);
    int one = count_px_from(SHIP[0][1], 1) + count_px_from(SHIP[0][0], 1);
    assert(one > 0 && one * 3 < all);
}

/* 12 crystals, 3 ships, 6 shots, every rock: nothing dropped from the primitive list (and under the spec's 300) */
static void budget_under_300(void)
{
    mreset();
    MV.allowed = MV.kept = 0xFFF;
    MV.tout = 3;
    marbles_enter();
    mframe(60000);
    for (int i = 0; i < 6; i++) marbles_drop(60 + i, 60000, 60400);
    int worst = 0;
    for (uint32_t ms = 60000; ms < 64000; ms += 37) { mframe(ms); if (p3d_count() > worst) worst = p3d_count(); }
    assert(worst <= 300 && worst < P3D_MAX);
}


/* ---- 5c: KIT and the single drums ---- */
static uint16_t FULL[240 * 166];
static void scene_full(void)        /* both bands, into FULL */
{
    for (int b = 0; b < 2; b++) {
        cv_begin(240, 83, 0);
        scene_band(-83 * b);
        memcpy(FULL + 240 * 83 * b, cv_px, sizeof(uint16_t) * 240 * 83);
    }
}
static uint16_t fpx(int x, int y) { return swap16(FULL[y * 240 + x]); }
static int count_in(int x0, int x1, int (*is)(uint16_t))
{
    int n = 0;
    for (int y = 0; y < 166; y++)
        for (int x = x0; x < x1; x++) n += is(fpx(x, y));
    return n;
}
static int is_blue(uint16_t c) { return c == KBLUE[0] || c == KBLUE[1] || c == KBLUE[2]; }
static int is_green(uint16_t c) { return c == KGREEN[0] || c == KGREEN[1] || c == KGREEN[2]; }
static int is_cyan(uint16_t c) { return c == C_CYAN; }
static void sframe(int model, int pad_eng, int16_t k0, int16_t k1, int16_t k2, int16_t k3, uint32_t h0, uint32_t h1, uint32_t h2,
                   uint32_t ms)
{
    q_sview v = {(uint8_t)model, (int8_t)pad_eng, {k0, k1, k2, k3}, {h0, h1, h2}};
    scene_frame(&v, ms);
}

static void kit_engine_flashes_alone(void)
{
    scene_enter();
    sframe(24, 0, 0, 64, 64, 64, 0, 0, 0, 1000);
    sframe(24, 0, 0, 64, 64, 64, 0, 1, 0, 1010);
    scene_full();
    assert(count_in(108, 148, is_pink) > 0);
    assert(count_in(30, 90, is_pink) == 0 && count_in(170, 220, is_pink) == 0);
    sframe(24, 0, 0, 64, 64, 64, 0, 1, 0, 3000);
    scene_full();
    assert(count_in(0, 240, is_pink) == 0);
}

static uint16_t REF[240 * 166];
static void hh_full_turn_after_four(void)
{
    scene_enter();
    sframe(23, -1, 64, 64, 64, 64, 0, 0, 0, 10000);
    for (uint32_t i = 1; i <= 4; i++) sframe(23, -1, 64, 64, 64, 64, i, 0, 0, 10000 + 200 * i);
    sframe(23, -1, 64, 64, 64, 64, 4, 0, 0, 12000);
    scene_full();
    memcpy(REF, FULL, sizeof REF);
    scene_enter();
    sframe(23, -1, 64, 64, 64, 64, 0, 0, 0, 12000);
    scene_full();
    assert(!memcmp(REF, FULL, sizeof REF));
    scene_enter();
    sframe(23, -1, 64, 64, 64, 64, 1, 0, 0, 12000);
    scene_full();
    assert(memcmp(REF, FULL, sizeof REF));
}

static void pad_tune_sizes(void)
{
    scene_enter();
    sframe(24, 0, -24, 64, 64, 64, 0, 0, 0, 20000);
    scene_full();
    int low = count_in(0, 100, is_blue);
    sframe(24, 0, 24, 64, 64, 64, 0, 0, 0, 20000);
    scene_full();
    assert(low > count_in(0, 100, is_blue));
}

static void kit_no_flash_on_arrival(void)
{
    scene_enter();
    sframe(24, 0, 0, 64, 64, 64, 0, 0, 0, 1000);
    sframe(24, 0, 0, 64, 64, 64, 3, 3, 3, 1500);
    scene_enter();
    sframe(24, 0, 0, 64, 64, 64, 5, 5, 5, 1510);
    scene_full();
    assert(count_in(0, 240, is_pink) == 0);
}

static void cyan_under_edited_pad(void)
{
    scene_enter();
    sframe(24, 2, 0, 64, 64, 64, 0, 0, 0, 30000);
    scene_full();
    assert(count_in(180, 208, is_cyan) > 0 && count_in(40, 80, is_cyan) == 0);
}

static void single_drum_centred(void)
{
    scene_enter();
    sframe(22, -1, 64, 64, 64, 64, 0, 0, 0, 30000);
    scene_full();
    assert(count_in(100, 140, is_green) > 0);
    assert(count_in(30, 75, is_green) == 0 && count_in(175, 215, is_green) == 0);
}


/* ---- 5c: MODAL ---- */
static void modal_budget_and_harm(void)
{
    scene_enter();
    sframe(20, -1, 0, 64, 64, 64, 0, 0, 0, 40000);
    scene_full();
    memcpy(REF, FULL, sizeof REF);
    for (int h = 0; h < 2; h++) {
        int16_t harm = h ? 127 : 0;
        sframe(20, -1, harm, 127, 127, 64, 0, 0, 0, 40000);
        sframe(20, -1, harm, 127, 127, 64, 1, 0, 0, 40010);
        for (uint32_t ms = 40010; ms < 42000; ms += 23) {
            sframe(20, -1, harm, 127, 127, 64, 1, 0, 0, ms);
            assert(p3d_count() <= 300);
        }
    }
    sframe(20, -1, 127, 64, 64, 64, 1, 0, 0, 40000);
    scene_full();
    assert(memcmp(REF, FULL, sizeof REF));
}

static void nodal_lines_pink_on_note(void)
{
    scene_enter();
    sframe(20, -1, 64, 64, 64, 64, 0, 0, 0, 50000);
    sframe(20, -1, 64, 64, 64, 64, 1, 0, 0, 50020);
    scene_full();
    assert(count_in(0, 240, is_pink) > 0);
    sframe(20, -1, 64, 64, 64, 64, 1, 0, 0, 52000);
    scene_full();
    assert(count_in(0, 240, is_pink) == 0);
}

static void still_at_rest(void)
{
    scene_enter();
    sframe(20, -1, 64, 64, 64, 64, 0, 0, 0, 60000);
    scene_full();
    memcpy(REF, FULL, sizeof REF);
    sframe(20, -1, 64, 64, 64, 64, 0, 0, 0, 60010);
    scene_full();
    int diff = 0;
    for (int i = 0; i < 240 * 166; i++) diff += REF[i] != FULL[i];
    assert(diff < 240 * 166 / 50);
}


/* ---- 5c: SPEECH ---- */
static int is_cyan_pupil(uint16_t c) { return c == C_PUPIL; }
static int count_box(int x0, int x1, int y0, int y1, int (*is)(uint16_t))
{
    int n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) n += is(fpx(x, y));
    return n;
}

static void mouth_opens_on_note(void)
{
    scene_enter();
    sframe(15, -1, 0, 64, 127, 127, 0, 0, 0, 70000);
    sframe(15, -1, 0, 64, 127, 127, 1, 0, 0, 70010);
    sframe(15, -1, 0, 64, 127, 127, 1, 0, 0, 70070);
    scene_full();
    assert(count_box(100, 140, 88, 118, is_pink) > 0);
    sframe(15, -1, 0, 64, 127, 127, 1, 0, 0, 72000);
    scene_full();
    assert(count_in(0, 240, is_pink) == 0);
}

static int is_violet(uint16_t c) { return c == SP_VIOLET[0] || c == SP_VIOLET[1] || c == SP_VIOLET[2]; }
static void harm_tints(void)
{
    int (*is[3])(uint16_t) = {is_green, is_blue, is_violet};
    const int16_t harm[3] = {0, 64, 127};
    for (int h = 0; h < 3; h++) {
        scene_enter();
        sframe(15, -1, harm[h], 64, 64, 64, 0, 0, 0, 80000);
        scene_full();
        int mine = count_box(70, 170, 20, 140, is[h]);
        for (int o = 0; o < 3; o++)
            if (o != h) assert(mine > count_box(70, 170, 20, 140, is[o]));
    }
}

static void pupils_only_from_front(void)
{
    int seen = 0;
    scene_enter();
    for (uint32_t ms = 90000; ms < 110000; ms += 100) {
        sframe(15, -1, 64, 64, 64, 64, 0, 0, 0, ms);
        scene_full();
        int in = count_box(70, 170, 0, 166, is_cyan_pupil);
        seen += in > 0;
        assert(count_in(0, 240, is_cyan_pupil) == in);
    }
    assert(seen > 0);
}

static void head_budget(void)
{
    scene_enter();
    sframe(15, -1, 64, 127, 127, 127, 0, 0, 0, 120000);
    for (uint32_t ms = 120000, h = 0; ms < 122000; ms += 17) {
        if (ms % 400 < 17) h++;
        sframe(15, -1, 64, 127, 127, 127, h, 0, 0, ms);
        assert(p3d_count() <= 300);
    }
}


/* ---- 5c review fixes ---- */
static void modal_nine_figures(void)            /* HARM 0 and 20: (1, 2) then (1, 3) */
{
    scene_enter();
    sframe(20, -1, 0, 64, 64, 64, 0, 0, 0, 40000);
    scene_full();
    memcpy(REF, FULL, sizeof REF);
    sframe(20, -1, 20, 64, 64, 64, 0, 0, 0, 40000);
    scene_full();
    assert(memcmp(REF, FULL, sizeof REF));
}

static void hh_morph_sets_flip(void)            /* a single hi-hat's MORPH: how long its flip lasts */
{
    for (int k = 0; k < 2; k++) {
        int16_t morph = k ? 127 : 0;
        scene_enter();
        sframe(23, -1, 64, 64, morph, 64, 0, 0, 0, 10000);
        sframe(23, -1, 64, 64, morph, 64, 1, 0, 0, 10010);
        sframe(23, -1, 64, 64, morph, 64, 1, 0, 0, 10160);
        scene_full();
        if (!k) memcpy(REF, FULL, sizeof REF);
    }
    assert(memcmp(REF, FULL, sizeof REF));
}

static void snare_ends_smoothly(void)           /* no jump of the cube when its faces close */
{
    scene_enter();
    sframe(22, -1, 64, 64, 64, 64, 0, 0, 0, 10000);
    sframe(22, -1, 64, 64, 64, 64, 1, 0, 0, 10000);   /* react: 0.1 + 0.4 * 64 / 127 = 0.30 s */
    sframe(22, -1, 64, 64, 64, 64, 1, 0, 0, 10295);
    scene_full();
    memcpy(REF, FULL, sizeof REF);
    sframe(22, -1, 64, 64, 64, 64, 1, 0, 0, 10305);
    scene_full();
    int diff = 0;
    for (int i = 0; i < 240 * 166; i++) diff += REF[i] != FULL[i] && is_green(swap16(FULL[i]));
    assert(diff < 500);                 /* the faces closing (about 240 px); the old 1-rad jump: about 1050 */
}

int main(void)
{
    planet_orange_then_pink();
    harm_sets_ships();
    morph_tilts_ring();
    time_not_frames();
    scene_has_six_more();
    no_flash_on_arrival();
    bins_one_and_twelve();
    nearest_bin();
    marble_lands_in_its_bin();
    ships_follow_tout();
    steps_dims_crystals();
    budget_under_300();
    six_marbles_max();
    deja_vu_orange();
    lands_when_it_sounds();
    kit_engine_flashes_alone();
    hh_full_turn_after_four();
    pad_tune_sizes();
    kit_no_flash_on_arrival();
    cyan_under_edited_pad();
    single_drum_centred();
    modal_budget_and_harm();
    nodal_lines_pink_on_note();
    still_at_rest();
    mouth_opens_on_note();
    harm_tints();
    pupils_only_from_front();
    head_budget();
    modal_nine_figures();
    hh_morph_sets_flip();
    snare_ends_smoothly();
    printf("scene ok\n");
    return 0;
}
