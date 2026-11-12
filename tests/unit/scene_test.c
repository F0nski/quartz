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
    for (int m = 0; m <= 24; m++) assert(scene_has(m));          /* (every model has its scene since 5d2) */
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
            assert(p3d_count() < 300);
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
        assert(p3d_count() < 300);
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


/* MODAL, SPEECH and MARBLES share their meshes' room: each scene comes back the same after another */
static uint16_t REF2[240 * 166];
static void scenes_share_meshes(void)
{
    scene_enter();
    sframe(20, -1, 40, 64, 64, 64, 0, 0, 0, 130000);
    scene_full();
    memcpy(REF, FULL, sizeof REF);
    sframe(15, -1, 64, 64, 64, 64, 0, 0, 0, 130000);
    scene_full();
    memcpy(REF2, FULL, sizeof REF2);
    mreset();
    MV.allowed = MV.kept = 0xFFF;
    marbles_enter();
    mframe(130000);
    scene_enter();
    sframe(20, -1, 40, 64, 64, 64, 0, 0, 0, 130000);
    scene_full();
    assert(!memcmp(REF, FULL, sizeof REF));
    sframe(15, -1, 64, 64, 64, 64, 0, 0, 0, 130000);
    scene_full();
    assert(!memcmp(REF2, FULL, sizeof REF2));
    mreset();
    MV.allowed = MV.kept = 0xFFF;
    marbles_enter();
    for (uint32_t ms = 130000; ms < 131000; ms += 37) mframe(ms);
    assert(p3d_count() > 50);           /* the canyon's rocks and crystals still there */
}


/* ---- 5d1: the crests (VA VCF 0, PHASE DIST 1, VA 8, WAVESHAPE 9) ---- */
static const int VA_MODELS[4] = {0, 1, 8, 9};

static void crests_budget(void)
{
    for (int m = 0; m < 4; m++)
        for (int e = 0; e < 2; e++) {
            int16_t k = e ? 127 : 0;
            scene_enter();
            sframe(VA_MODELS[m], -1, k, k, k, 64, 0, 0, 0, 200000);
            sframe(VA_MODELS[m], -1, k, k, k, 64, 1, 0, 0, 200010);
            for (uint32_t ms = 200010; ms < 202000; ms += 97) {
                sframe(VA_MODELS[m], -1, k, k, k, 64, 1, 0, 0, ms);
                assert(p3d_count() < 300);
            }
            scene_full();
        }
}

/* (the ship's flame is always pink: compared with the scene at rest) */
static void crests_note_pink(void)
{
    scene_enter();
    sframe(0, -1, 64, 64, 64, 64, 0, 0, 0, 210000);
    scene_full();
    int rest = count_in(0, 240, is_pink);
    sframe(0, -1, 64, 64, 64, 64, 1, 0, 0, 210020);
    scene_full();
    assert(count_in(0, 240, is_pink) > rest + 10);
    sframe(0, -1, 64, 64, 64, 64, 1, 0, 0, 212000);
    scene_full();
    int after = count_in(0, 240, is_pink);
    scene_enter();                          /* the same instant without the note: as much pink (the flame's) */
    sframe(0, -1, 64, 64, 64, 64, 1, 0, 0, 212000);
    scene_full();
    assert(after == count_in(0, 240, is_pink));
}

static void crests_harm_changes(void)
{
    scene_enter();
    sframe(0, -1, 0, 64, 64, 64, 0, 0, 0, 220000);
    scene_full();
    memcpy(REF, FULL, sizeof REF);
    sframe(0, -1, 127, 64, 64, 64, 0, 0, 0, 220000);
    scene_full();
    assert(memcmp(REF, FULL, sizeof REF));
}

static void crests_engine_switch(void)
{
    scene_enter();
    sframe(8, -1, 64, 64, 64, 64, 5, 0, 0, 230000);
    scene_full();
    memcpy(REF, FULL, sizeof REF);
    int rest = count_in(0, 240, is_pink);
    sframe(9, -1, 64, 64, 64, 64, 5, 0, 0, 230000);
    scene_full();
    assert(memcmp(REF, FULL, sizeof REF));
    assert(count_in(0, 240, is_pink) <= rest + 3);   /* no shot for the notes before */
}


/* ---- 5d1: the bowl (WAVE TERRAIN 5, WAVETABLE 13) ---- */
static uint16_t REF3[240 * 166];
/* the image at ms after a note at note_ms, and the same instant with the note unseen (scene_enter) */
static int note_shows(int model, uint32_t note_ms, uint32_t ms)
{
    scene_enter();
    sframe(model, -1, 64, 64, 64, 64, 0, 0, 0, note_ms - 10);
    sframe(model, -1, 64, 64, 64, 64, 1, 0, 0, note_ms);
    sframe(model, -1, 64, 64, 64, 64, 1, 0, 0, ms);
    scene_full();
    memcpy(REF3, FULL, sizeof REF3);
    scene_enter();
    sframe(model, -1, 64, 64, 64, 64, 1, 0, 0, ms);
    scene_full();
    return memcmp(REF3, FULL, sizeof REF3) != 0;
}

static void bowl_budget(void)
{
    const int M[2] = {5, 13};
    for (int m = 0; m < 2; m++)
        for (int e = 0; e < 2; e++) {
            int16_t k = e ? 127 : 0;
            scene_enter();
            sframe(M[m], -1, k, k, k, k, 0, 0, 0, 300000);
            for (uint32_t ms = 300000, h = 0; ms < 303000; ms += 61) {
                if (ms % 500 < 61) h++;
                sframe(M[m], -1, k, k, k, k, h, 0, 0, ms);
                assert(p3d_count() < 300);
            }
            scene_full();
        }
}

static void bowl_trail_on_note(void)
{
    assert(note_shows(13, 310000, 310050));
    assert(!note_shows(13, 310000, 312500));
    assert(note_shows(5, 320000, 320050));
}

/* every other note a kickflip: the second note's image differs from the first one's at the same delay */
static void bowl_kickflip_every_other(void)
{
    scene_enter();
    sframe(13, -1, 64, 64, 64, 0, 0, 0, 0, 330000);
    sframe(13, -1, 64, 64, 64, 0, 1, 0, 0, 330000);
    sframe(13, -1, 64, 64, 64, 0, 1, 0, 0, 330100);
    scene_full();
    memcpy(REF3, FULL, sizeof REF3);
    sframe(13, -1, 64, 64, 64, 0, 2, 0, 0, 330000);
    sframe(13, -1, 64, 64, 64, 0, 2, 0, 0, 330100);
    scene_full();
    assert(memcmp(REF3, FULL, sizeof REF3));
}

/* the crests, the bowl, MARBLES and MODAL share the meshes' room: each comes back the same */
static void scenes_share_room(void)
{
    static uint16_t A[240 * 166], B[240 * 166];
    scene_enter();
    sframe(0, -1, 30, 90, 40, 64, 0, 0, 0, 340000);
    scene_full();
    memcpy(A, FULL, sizeof A);
    sframe(5, -1, 30, 90, 40, 64, 0, 0, 0, 340000);
    scene_full();
    memcpy(B, FULL, sizeof B);
    sframe(20, -1, 30, 90, 40, 64, 0, 0, 0, 340000);
    mreset();
    MV.allowed = MV.kept = 0xFFF;
    marbles_enter();
    mframe(340000);
    scene_enter();
    sframe(0, -1, 30, 90, 40, 64, 0, 0, 0, 340000);
    scene_full();
    assert(!memcmp(A, FULL, sizeof A));
    sframe(5, -1, 30, 90, 40, 64, 0, 0, 0, 340000);
    scene_full();
    assert(!memcmp(B, FULL, sizeof B));
}


/* ---- 5d1: the crystal garden (GRAIN FORM 11, ADDITIVE 12) ---- */
static void garden_budget(void)
{
    const int M[2] = {11, 12};
    for (int m = 0; m < 2; m++)
        for (int e = 0; e < 2; e++) {
            int16_t k = e ? 127 : 0;
            scene_enter();
            sframe(M[m], -1, k, k, k, k, 0, 0, 0, 400000);
            sframe(M[m], -1, k, k, k, k, 1, 0, 0, 400010);
            for (uint32_t ms = 400010; ms < 402000; ms += 73) {
                sframe(M[m], -1, k, k, k, k, 1, 0, 0, ms);
                assert(p3d_count() < 300);
            }
            scene_full();
        }
}

static void garden_hearts_pink(void)
{
    scene_enter();
    sframe(12, -1, 64, 64, 64, 127, 0, 0, 0, 410000);
    scene_full();
    assert(count_in(0, 240, is_pink) == 0);
    sframe(12, -1, 64, 64, 64, 127, 1, 0, 0, 410020);
    scene_full();
    assert(count_in(0, 240, is_pink) > 0);
    sframe(12, -1, 64, 64, 64, 127, 1, 0, 0, 412000);
    scene_full();
    assert(count_in(0, 240, is_pink) == 0);
}

static int is_bloom(uint16_t c) { return c == GD_BLOOM[0][0] || c == GD_BLOOM[1][0] || c == GD_BLOOM[2][0] ||
                                         c == GD_BLOOM[0][1] || c == GD_BLOOM[1][1] || c == GD_BLOOM[2][1]; }
/* ADDITIVE: TIMB moves the spectrum's peak from the left of the garden to the right */
static void garden_timb_moves_peak(void)
{
    scene_enter();
    sframe(12, -1, 0, 0, 30, 64, 0, 0, 0, 420000);
    scene_full();
    int l0 = count_in(0, 120, is_bloom), r0 = count_in(120, 240, is_bloom);
    sframe(12, -1, 0, 127, 30, 64, 0, 0, 0, 420000);
    scene_full();
    int l1 = count_in(0, 120, is_bloom), r1 = count_in(120, 240, is_bloom);
    assert(l0 > r0 && r1 > l1);
}


/* ---- 5d1: the platforms (CHIPTUNE 7) ---- */
static void chip_budget(void)
{
    for (int e = 0; e < 2; e++) {
        int16_t k = e ? 127 : 0;
        scene_enter();
        sframe(7, -1, k, k, k, k, 0, 0, 0, 500000);
        for (uint32_t ms = 500000, h = 0; ms < 502000; ms += 41) {
            if (ms % 250 < 41) h++;
            sframe(7, -1, k, k, k, k, h, 0, 0, ms);
            assert(p3d_count() < 300);
        }
        scene_full();
    }
}

/* a note: 0.3 s later the robot stands on the block of the arpeggio's next note */
static void chip_hop_to_target(void)
{
    scene_enter();
    sframe(7, -1, 127, 0, 64, 64, 0, 0, 0, 510000);
    sframe(7, -1, 127, 0, 64, 64, 1, 0, 0, 510010);
    sframe(7, -1, 127, 0, 64, 64, 1, 0, 0, 510310);
    assert(fabsf(ch.rx - ch.bx[ch.target]) < 1e-3f && fabsf(ch.ry - ch.by[ch.target]) < 1e-3f);
    scene_full();
}

/* notes faster than a hop: the robot goes on from where it is, never back to a block it left */
static void chip_burst_no_jump_back(void)
{
    scene_enter();
    sframe(7, -1, 127, 0, 64, 64, 0, 0, 0, 520000);
    float last = ch.rx, step = 3.2f / 4.0f;
    for (uint32_t ms = 520000, h = 0; ms < 520600; ms += 10) {
        if (ms % 50 == 0 && h < 3) h++;
        sframe(7, -1, 127, 0, 64, 64, h, 0, 0, ms);
        assert(fabsf(ch.rx - last) < 0.5f * step);
        last = ch.rx;
    }
}

static void chip_score(void)
{
    static uint16_t S0[240 * 166];
    scene_enter();
    sframe(7, -1, 64, 64, 64, 64, 0, 0, 0, 530000);
    sframe(7, -1, 64, 64, 64, 64, 0, 0, 0, 532000);
    scene_full();
    memcpy(S0, FULL, sizeof S0);
    for (uint32_t h = 1; h <= 5; h++)
        sframe(7, -1, 64, 64, 64, 64, h, 0, 0, 530000 + 300 * h);
    sframe(7, -1, 64, 64, 64, 64, 5, 0, 0, 532000);
    scene_full();
    int diff = 0;
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 60; x++) diff += S0[y * 240 + x] != FULL[y * 240 + x];
    assert(diff > 0 && ch.score == 50);
}


/* ---- 5d1 review fixes ---- */
/* the VA VCF open with no resonance (HARM 0, TIMB 127) and every crossed extreme: finite heights */
static void crests_cross_knobs(void)
{
    const int16_t K[3] = {0, 64, 127};
    for (int m = 0; m < 4; m++)
        for (int a = 0; a < 3; a++)
            for (int b = 0; b < 3; b++)
                for (int c = 0; c < 3; c++) {
                    scene_enter();
                    sframe(VA_MODELS[m], -1, K[a], K[b], K[c], 64, 0, 0, 0, 600000);
                    for (int i = 0; i < 64; i++) assert(sb.va.tab[i] == sb.va.tab[i] && fabsf(sb.va.tab[i]) <= 1.0001f);
                    for (int i = 0; i < 11 * 13; i++) assert(sb.va.h[i] == sb.va.h[i] && fabsf(sb.va.h[i]) < 10.0f);
                }
}

/* a note mid-hop: the robot's shown height goes on smoothly, no drop from the arc */
static void chip_burst_no_drop(void)
{
    scene_enter();
    sframe(7, -1, 127, 0, 64, 64, 0, 0, 0, 610000);
    sframe(7, -1, 127, 0, 64, 64, 1, 0, 0, 610000);
    sframe(7, -1, 127, 0, 64, 64, 1, 0, 0, 610110);   /* the arc's top */
    float before = ch.shown_y;
    sframe(7, -1, 127, 0, 64, 64, 2, 0, 0, 610120);   /* the next note, 10 ms on */
    assert(fabsf(ch.shown_y - before) < 0.1f);
}

/* a kickflip goes on through a note that comes before it ends */
static void bowl_flip_not_cut(void)
{
    scene_enter();
    sframe(13, -1, 64, 64, 64, 0, 1, 0, 0, 620000);
    sframe(13, -1, 64, 64, 64, 0, 2, 0, 0, 620000);   /* an even note: a flip */
    sframe(13, -1, 64, 64, 64, 0, 3, 0, 0, 620100);   /* an odd one, 0.1 s in */
    sframe(13, -1, 64, 64, 64, 0, 3, 0, 0, 620150);
    assert(wv.flip && fabsf(wv.flip_t - 0.15f) < 1e-3f);
}


/* ---- 5d2: shared checks ---- */
static const int16_t KX[3] = {0, 64, 127};
/* every crossed extreme of HARM, TIMB, MORPH, a note: under 300 primitives, no NaN (the float-cast check) */
static void cross_knobs(int model)
{
    for (int a = 0; a < 3; a++)
        for (int b = 0; b < 3; b++)
            for (int c = 0; c < 3; c++) {
                scene_enter();
                sframe(model, -1, KX[a], KX[b], KX[c], 64, 0, 0, 0, 700000);
                sframe(model, -1, KX[a], KX[b], KX[c], 64, 1, 0, 0, 700010);
                for (uint32_t ms = 700010; ms < 700600; ms += 53) {
                    sframe(model, -1, KX[a], KX[b], KX[c], 64, 1, 0, 0, ms);
                    assert(p3d_count() < 300);
                }
                scene_full();
            }
}
/* a note shows at 50 ms, and is gone 2 s later */
static void note_reacts(int model)
{
    assert(note_shows(model, 710000, 710050));
    assert(!note_shows(model, 710000, 712500));
}

/* ---- 5d2: the android choir (STRING MACH 6, CHORDS 14) ---- */
static void choir_tests(void)
{
    cross_knobs(6);
    cross_knobs(14);
    note_reacts(14);
    note_reacts(6);
    scene_enter();
    sframe(14, -1, 127, 0, 64, 64, 0, 0, 0, 720000);
    assert(cr.n == 4);
    sframe(14, -1, 0, 0, 64, 64, 0, 0, 0, 720000);
    assert(cr.n == 2);
    sframe(14, -1, 0, 127, 64, 64, 0, 0, 0, 720000);     /* OCT, three inversions: both at 24 */
    assert(cr.n == 2 && cr.h[0] == cr.h[1]);
    /* a note during the conductor's beat: his hand goes on, no jump */
    scene_enter();
    sframe(14, -1, 64, 64, 64, 64, 0, 0, 0, 730000);
    sframe(14, -1, 64, 64, 64, 64, 1, 0, 0, 730000);
    sframe(14, -1, 64, 64, 64, 64, 1, 0, 0, 730100);
    float before = cr.beat;
    sframe(14, -1, 64, 64, 64, 64, 2, 0, 0, 730110);
    assert(fabsf(cr.beat - before) < 0.15f);
}


/* ---- 5d2: the tightrope walker (STRING 19) ---- */
static void rope_tests(void)
{
    cross_knobs(19);
    assert(note_shows(19, 740000, 740050));
    /* MORPH 127 (damped): 2 s after a note the rope is back at rest, the same image as without the note */
    scene_enter();
    sframe(19, -1, 64, 64, 127, 64, 0, 0, 0, 750000 - 10);
    sframe(19, -1, 64, 64, 127, 64, 1, 0, 0, 750000);
    sframe(19, -1, 64, 64, 127, 64, 1, 0, 0, 752000);
    scene_full();
    memcpy(REF3, FULL, sizeof REF3);
    scene_enter();
    sframe(19, -1, 64, 64, 127, 64, 1, 0, 0, 752000);
    scene_full();
    assert(!memcmp(REF3, FULL, sizeof REF3));
    /* a note mid-hop: the acrobat's shown height goes on */
    scene_enter();
    sframe(19, -1, 64, 64, 127, 64, 0, 0, 0, 760000);
    sframe(19, -1, 64, 64, 127, 64, 1, 0, 0, 760000);
    sframe(19, -1, 64, 64, 127, 64, 1, 0, 0, 760100);
    float before = rp.shown;
    sframe(19, -1, 64, 64, 127, 64, 2, 0, 0, 760110);
    assert(fabsf(rp.shown - before) < 0.05f);
    for (int tb = 0; tb <= 127; tb += 127) {   /* the first note too, bright or dull: the rope starts from where it is */
        scene_enter();
        sframe(19, -1, 64, (int16_t)tb, 64, 64, 0, 0, 0, 770000);
        before = rp.shown;
        sframe(19, -1, 64, (int16_t)tb, 64, 64, 1, 0, 0, 770010);
        assert(fabsf(rp.shown - before) < 0.05f);
    }
}


/* ---- 5d2: the dancefloor (SWARM 16, NOISE 17, PARTICLE 18) ---- */
static void dance_tests(void)
{
    cross_knobs(16);
    cross_knobs(17);
    cross_knobs(18);
    /* a note: pink on the lit tiles and the ball's rays, gone by 250 ms */
    scene_enter();
    sframe(17, -1, 64, 64, 64, 64, 0, 0, 0, 800000);
    sframe(17, -1, 64, 64, 64, 64, 1, 0, 0, 800000);
    sframe(17, -1, 64, 64, 64, 64, 1, 0, 0, 800040);
    scene_full();
    assert(count_in(0, 240, is_pink) > 0);
    sframe(17, -1, 64, 64, 64, 64, 1, 0, 0, 800250);
    scene_full();
    assert(count_in(0, 240, is_pink) == 0);
    /* PARTICLE and SWARM: MORPH visible (the sparks' life, the swarm's trail) */
    for (int m = 16; m <= 18; m += 2) {
        scene_enter();
        sframe(m, -1, 64, 100, 0, 64, 0, 0, 0, 810000);
        scene_full();
        memcpy(REF3, FULL, sizeof REF3);
        scene_enter();
        sframe(m, -1, 64, 100, 127, 64, 0, 0, 0, 810000);
        scene_full();
        assert(memcmp(REF3, FULL, sizeof REF3));
    }
    /* a grain off the floor lights no tile */
    assert(dance_tile(1.2f, 0.0f) < 0 && dance_tile(-1.05f, 0.3f) < 0 && dance_tile(0.2f, 1.01f) < 0);
    assert(dance_tile(-1.0f, -1.0f) == 0 && dance_tile(0.999f, 0.999f) == 47);
    /* the choir, the dancefloor, the crests and MODAL share the room: each comes back the same */
    static uint16_t A[240 * 166], B[240 * 166];
    scene_enter();
    sframe(14, -1, 100, 30, 64, 64, 0, 0, 0, 820000);
    scene_full();
    memcpy(A, FULL, sizeof A);
    sframe(17, -1, 100, 30, 64, 64, 0, 0, 0, 820000);
    scene_full();
    memcpy(B, FULL, sizeof B);
    sframe(0, -1, 100, 30, 64, 64, 0, 0, 0, 820000);
    sframe(20, -1, 100, 30, 64, 64, 0, 0, 0, 820000);
    sframe(14, -1, 100, 30, 64, 64, 0, 0, 0, 820000);
    scene_full();
    assert(!memcmp(A, FULL, sizeof A));
    sframe(17, -1, 100, 30, 64, 64, 0, 0, 0, 820000);
    scene_full();
    assert(!memcmp(B, FULL, sizeof B));
}



/* ---- 5e: HOME's tracks ---- */
static q_hview HV;
static void hreset(void)
{
    memset(&HV, 0, sizeof HV);
    const uint8_t M[4] = {2, 24, 12, 14};
    for (int i = 0; i < 4; i++) HV.model[i] = M[i], HV.pat[i] = 1, HV.pos[i] = (int8_t)i, HV.len[i] = 16;
}
static void hframe(uint32_t ms) { home_frame(&HV, ms); }
static void hfull(void)
{
    for (int b = 0; b < 2; b++) {
        cv_begin(240, 83, 0);
        home_band(-83 * b);
        memcpy(FULL + 240 * 83 * b, cv_px, sizeof(uint16_t) * 240 * 83);
    }
}
/* pink in the pedestals' area of track i (a quarter of the width each, above the info row) */
static int hpink(int i)
{
    int n = 0;
    for (int y = 0; y < 138; y++)
        for (int x = 60 * i; x < 60 * i + 60; x++) n += is_pink(fpx(x, y));
    return n;
}

static void home_tests(void)
{
    static const uint8_t FAM[11] = {2, 24, 20, 15, 0, 13, 12, 7, 14, 19, 17};
    for (int f = 0; f < 11; f += 4) {        /* every family on the pedestals, notes on all: the budget */
        hreset();
        for (int i = 0; i < 4; i++) HV.model[i] = FAM[(f + i) % 11];
        scene_enter();
        hframe(900000);
        for (uint32_t ms = 900000, h = 0; ms < 901500; ms += 37) {
            if (ms % 300 < 37) h++;
            for (int i = 0; i < 4; i++) HV.hits[i] = h;
            hframe(ms);
            assert(p3d_count() < 300);
        }
        hfull();
    }
    /* a note on track 2 alone: pink on its pedestal, not on the others (the step rings' pink too: compare with before) */
    hreset();
    for (int i = 0; i < 4; i++) HV.pos[i] = -1;
    scene_enter();
    hframe(910000);
    hfull();
    int before[4];
    for (int i = 0; i < 4; i++) before[i] = hpink(i);
    HV.hits[1] = 1;
    hframe(910010);
    hframe(910030);                     /* (its wire pink for 50 ms; the ring fades from pink at once) */
    hfull();
    assert(hpink(1) > before[1] && hpink(0) == before[0] && hpink(2) == before[2] && hpink(3) == before[3]);
    /* a muted track: no reaction */
    hreset();
    for (int i = 0; i < 4; i++) HV.pos[i] = -1;
    HV.mute[2] = 1;
    scene_enter();
    hframe(920000);
    HV.hits[2] = 1;
    hframe(920010);
    hframe(920060);
    hfull();
    assert(hpink(2) == 0);
    /* the selected track's chip is orange: track 2 (a KIT, blue) selected, then not */
    hreset();
    HV.sel = 1;
    scene_enter();
    hframe(930000);
    hfull();
    int orange = 0;
    for (int y = 138; y < 166; y++)
        for (int x = 63; x < 83; x++) orange += fpx(x, y) == RGB(0xff, 0x7a, 0x2e);
    assert(orange > 0);
    HV.sel = 0;
    hframe(930010);
    hfull();
    orange = 0;
    for (int y = 138; y < 166; y++)
        for (int x = 63; x < 83; x++) orange += fpx(x, y) == RGB(0xff, 0x7a, 0x2e);
    assert(orange == 0);
    /* four long loops: every pedestal keeps its current step (pink) */
    hreset();
    for (int i = 0; i < 4; i++) HV.len[i] = 64, HV.pos[i] = (int8_t)(10 + 13 * i);
    scene_enter();
    hframe(935000);
    hfull();
    for (int i = 0; i < 4; i++) assert(hpink(i) > 0);
    /* a long loop's last step, and stopped tracks */
    hreset();
    HV.len[0] = 64, HV.pos[0] = 63;
    scene_enter();
    hframe(940000);
    hfull();
    assert(hpink(0) > 0);
    hreset();
    for (int i = 0; i < 4; i++) HV.pos[i] = -1;
    scene_enter();
    hframe(950000);
    hfull();
    for (int i = 0; i < 4; i++) assert(hpink(i) == 0);
}

/* ---- 5f: the sound pages ---- */
static q_pview PV;
static void preset(int page, int16_t k0, int16_t k1, int16_t k2, int16_t k3)
{
    memset(&PV, 0, sizeof PV);
    PV.page = (uint8_t)page, PV.k[0] = k0, PV.k[1] = k1, PV.k[2] = k2, PV.k[3] = k3, PV.lfo_ph = -1.0f;
}
static void pvframe(uint32_t ms) { sound_frame(&PV, ms); }
static void pvfull(void)
{
    for (int b = 0; b < 2; b++) {
        cv_begin(240, 83, 0);
        sound_band(-83 * b);
        memcpy(FULL + 240 * 83 * b, cv_px, sizeof(uint16_t) * 240 * 83);
    }
}
/* pink in columns x0 .. x1 - 1 */
static int ppink(int x0, int x1) { return count_in(x0, x1, is_pink); }
/* nothing drawn above or below the scene area: the bands just outside it stay black */
static void pv_inside(void)
{
    for (int b = 0; b < 2; b++) {
        cv_begin(240, 83, 0);
        sound_band(b ? -166 : 83);
        for (int i = 0; i < 240 * 83; i++) assert(cv_px[i] == 0);
    }
}
/* a note every 300 ms over 1.5 s: the worst count */
static int pv_run(uint32_t ms0)
{
    int worst = 0;
    scene_enter();
    pvframe(ms0);
    for (uint32_t ms = ms0; ms < ms0 + 1500; ms += 37) {
        if (ms % 300 < 37) PV.hits++;
        PV.lfo_ph = (float)(ms % 1000) * 0.001f;
        pvframe(ms);
        if (p3d_count() > worst) worst = p3d_count();
        if (ms % 111 == 0) pvfull();
    }
    return worst;
}

static void sound_budget(void)
{
    for (int k = 0; k < 4; k++) {           /* ADSR: the knobs low to high */
        preset(SP_ADSR, (int16_t)(40 * k), (int16_t)(127 - 40 * k), (int16_t)(40 * k), (int16_t)(127 - 40 * k));
        assert(pv_run(1000000) < 300);
    }
    for (int ty = 0; ty < 3; ty++)
        for (int c = 0; c < 128; c += 42) {
            preset(SP_FILTER, (int16_t)ty, (int16_t)c, 90, 30);
            assert(pv_run(1100000) < 300);
        }
    for (int sh = 0; sh < 5; sh++)
        for (int r = 0; r <= 75; r += 15) {
            preset(SP_LFO, (int16_t)sh, (int16_t)r, 50, 4);
            assert(pv_run(1200000) < 300);
        }
    static const uint8_t HELD[8] = {67, 60, 72, 64, 71, 62, 69, 65};
    for (int m = 0; m < 6; m++)             /* ARP: 8 notes held, every MODE x OCT */
        for (int o = 1; o <= 4; o++) {
            preset(SP_ARP, (int16_t)m, (int16_t)o, 4, 100);
            memcpy(PV.held, HELD, 8);
            PV.nheld = 8, PV.sounding = 71;
            assert(pv_run(1210000) < 300);
            pv_inside();
        }
    for (int ty = 0; ty < 8; ty++)          /* INSERT: every type, the knobs low and high */
        for (int k = 0; k < 2; k++) {
            preset(SP_INSERT, (int16_t)(127 * k), (int16_t)(127 * k), (int16_t)(127 - 127 * k), 127);
            PV.itype = (uint8_t)ty;
            assert(pv_run(1220000) < 300);
            pv_inside();
        }
    for (int k = 0; k < 2; k++) {
        preset(SP_MIX, (int16_t)(127 * k), (int16_t)(127 * k), k ? 63 : -64, 127);
        assert(pv_run(1230000) < 300);
        pv_inside();
    }
}

static void sound_budget_extremes(void)
{
    static const int16_t ENV[2] = {-64, 63}, CUT[2] = {0, 127};
    for (int ty = 0; ty < 3; ty++)
        for (int e = 0; e < 2; e++)
            for (int c = 0; c < 2; c++) {
                preset(SP_FILTER, (int16_t)ty, CUT[c], 127, ENV[e]);
                assert(pv_run(1300000) < 300);
                pv_inside();
                PV.hits++;
                pvframe(1300000 + 1600);
                pv_inside();
            }
    for (int sh = 0; sh < 5; sh++)
        for (int d = 0; d < 2; d++) {
            preset(SP_LFO, (int16_t)sh, 75, d ? 63 : -64, 6);
            assert(pv_run(1400000) < 300);
            pv_inside();
        }
    preset(SP_ADSR, 0, 0, 127, 127);
    PV.own_env = 1;
    assert(pv_run(1450000) < 300);
    pv_inside();
}

/* ATTACK 127 (0.6 s, x 16-68), the rest 0: the attack pink first, then the hold (x 70-120) */
static void sound_adsr_pink(void)
{
    preset(SP_ADSR, 127, 0, 0, 0);
    scene_enter();
    pvframe(1500000);
    PV.hits = 1;
    pvframe(1500100);
    pvframe(1500400);
    pvfull();
    assert(ppink(0, 240) > 0 && ppink(68 + 12, 240) == 0);
    pvframe(1500900);
    pvfull();
    assert(ppink(68, 240) > 0 && ppink(0, 60) == 0);
    pvframe(1503000);                       /* the envelope is over */
    pvfull();
    assert(ppink(0, 240) == 0);
}

static void sound_adsr_own_env(void)
{
    palette_set(1);
    for (int o = 0; o < 2; o++) {
        preset(SP_ADSR, 64, 64, 64, 64);
        PV.own_env = (uint8_t)o;
        scene_enter();
        pvframe(1600000);
        pvfull();
        int n = 0;
        for (int y = 0; y <= 20; y++)
            for (int x = 0; x < 240; x++) n += fpx(x, y) == C_AMB;
        assert(o ? n > 0 : n == 0);
    }
}

static void sound_filter_pink(void)
{
    preset(SP_FILTER, 0, 64, 64, 40);
    scene_enter();
    pvframe(1700000);
    pvfull();
    assert(ppink(0, 240) == 0);
    PV.hits = 1;
    pvframe(1700100);
    pvframe(1700400);
    pvfull();
    assert(ppink(0, 240) > 0);
    pvframe(1701100);
    pvfull();
    assert(ppink(0, 240) == 0);
}

static void sound_lfo_idle(void)
{
    static uint16_t A[240 * 166];
    preset(SP_LFO, 0, 30, 40, 4);
    scene_enter();
    pvframe(1800000);
    PV.hits = 1;
    pvframe(1800050);
    pvfull();
    assert(ppink(0, 240) > 0);              /* the diamond: pink on the note */
    memcpy(A, FULL, sizeof A);
    pvframe(1800550);
    pvfull();
    for (int i = 0; i < 240 * 166; i++)
        assert(A[i] == FULL[i] || is_pink(swap16(A[i])) || is_pink(swap16(FULL[i])));
}

/* the diamond's centre height (the pixels of its white), -1 when absent */
static float pv_diamond_y(void)
{
    int n = 0, sy = 0;
    for (int y = 0; y < 166; y++)
        for (int x = 0; x < 240; x++)
            if (fpx(x, y) == RGB(0xf4, 0xf1, 0xff)) n++, sy += y;
    return n ? (float)sy / (float)n : -1.0f;
}

/* TRI as the audio's lfo_value: 0 at the phase 0, the top at 0.25 */
static void sound_lfo_phase(void)
{
    float y[2];
    for (int i = 0; i < 2; i++) {
        preset(SP_LFO, 1, 30, 64, 4);
        PV.lfo_ph = 0.25f * (float)i;
        scene_enter();
        pvframe(1900000);
        pvfull();
        y[i] = pv_diamond_y();
        assert(y[i] > 0);
    }
    assert(y[0] > 70.0f && y[0] < 90.0f);   /* the middle: the old triangle started at the bottom, 140 */
    assert(y[0] - y[1] > 50.0f && y[0] - y[1] < 60.0f);   /* DEPTH 64: the top is 55 px above */
}

/* RATE: the synced divisions and the slow free rates show different cycle counts */
static void sound_lfo_rate(void)
{
    static uint16_t A[240 * 166];
    for (int i = 0; i < 2; i++) {
        preset(SP_LFO, 2, i ? 11 : 0, 64, 4);
        scene_enter();
        pvframe(1950000);
        pvfull();
        if (!i) memcpy(A, FULL, sizeof A);
    }
    assert(memcmp(A, FULL, sizeof A));
}

static void pv_arp(int mode, int oct, uint8_t a, uint8_t b, uint8_t c, int sounding)
{
    preset(SP_ARP, (int16_t)mode, (int16_t)oct, 4, 80);
    PV.held[0] = a, PV.held[1] = b, PV.held[2] = c, PV.nheld = 3, PV.sounding = (int8_t)sounding;
}

/* UP, 3 notes: a step every 62.7 px from x 16; E4 (64) is the 2nd (x 78.7-141), the only pink one */
static void sound_arp_pink(void)
{
    pv_arp(1, 1, 64, 60, 67, 64);
    scene_enter();
    pvframe(2000000);
    pvfull();
    assert(ppink(78, 141) > 0 && ppink(0, 78) == 0 && ppink(141, 240) == 0);
}

/* OCT 4 UP-DN, 3 notes: 22 steps; the top (103), then 88 on the way down: the second page */
static void sound_arp_long(void)
{
    pv_arp(3, 4, 60, 64, 67, 103);
    scene_enter();
    pvframe(2100000);
    assert(p3d_count() < 300);
    PV.sounding = 88;
    pvframe(2100100);
    assert(p3d_count() < 300);
    pvfull();
    assert(ppink(0, 240) > 0);
}

/* nothing held: the tonic's chord in wire, no blue */
static void sound_arp_chord(void)
{
    preset(SP_ARP, 1, 2, 4, 80);
    PV.chord[0] = 60, PV.chord[1] = 64, PV.chord[2] = 67, PV.sounding = -1;
    scene_enter();
    pvframe(2200000);
    pvfull();
    assert(count_in(0, 240, is_blue) == 0 && count_in(0, 240, is_pink) == 0);
    int n = 0;
    for (int i = 0; i < 240 * 166; i++) n += swap16(FULL[i]) == RGB(0x2e, 0x28, 0x50);
    assert(n > 0);
}

static void sound_arp_kit(void)
{
    palette_set(1);
    pv_arp(1, 2, 60, 64, 67, 64);
    PV.kit = 1;
    scene_enter();
    pvframe(2300000);
    pvfull();
    assert(count_in(0, 240, is_blue) == 0 && count_in(0, 240, is_pink) == 0);
    int n = 0;
    for (int i = 0; i < 240 * 166; i++) n += swap16(FULL[i]) == C_GRAY;
    assert(n > 0);
}

static void sound_insert_off(void)
{
    preset(SP_INSERT, 64, 64, 64, 127);
    scene_enter();
    pvframe(2400000);
    PV.hits = 1;
    pvframe(2400300);
    pvfull();
    assert(count_in(0, 240, is_blue) == 0 && ppink(0, 240) > 0);
    PV.itype = 1;                           /* OVERDRIVE: the blue ribbon */
    pvframe(2400400);
    pvfull();
    assert(count_in(0, 240, is_blue) > 0);
}

/* a note, PAN hard left: the meter's L column (x 196-208) pink, its R one (x 210-222) not */
static void sound_mix_meter(void)
{
    preset(SP_MIX, 64, 64, -64, 127);
    scene_enter();
    pvframe(2500000);
    PV.hits = 1;
    pvframe(2500050);
    pvfull();
    assert(ppink(196, 209) > 0 && ppink(209, 240) == 0);
    pvframe(2501000);                       /* fallen back */
    pvfull();
    assert(ppink(0, 240) == 0);
}

static void sound_fresh(void)
{
    static const int16_t K[6][4] = {{64, 64, 64, 64}, {0, 64, 64, 40}, {0, 30, 40, 4}, {0}, {64, 64, 64, 64}, {64, 64, 0, 127}};
    for (int pg = SP_ADSR; pg <= SP_MIX; pg++) {    /* (not ARP: its pink is the note sounding now) */
        if (pg == SP_ARP)
            continue;
        preset(pg, K[pg][0], K[pg][1], K[pg][2], K[pg][3]);
        PV.hits = 5;
        scene_enter();
        for (uint32_t ms = 1900000; ms < 1901000; ms += 100) {
            pvframe(ms);
            pvfull();
            assert(ppink(0, 240) == 0);
        }
    }
}

static void sound_tests(void)
{
    sound_budget();
    sound_budget_extremes();
    sound_adsr_pink();
    sound_adsr_own_env();
    sound_filter_pink();
    sound_lfo_idle();
    sound_lfo_phase();
    sound_lfo_rate();
    sound_arp_pink();
    sound_arp_long();
    sound_arp_chord();
    sound_arp_kit();
    sound_insert_off();
    sound_mix_meter();
    sound_fresh();
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
    scenes_share_meshes();
    crests_budget();
    crests_note_pink();
    crests_harm_changes();
    crests_engine_switch();
    bowl_budget();
    bowl_trail_on_note();
    bowl_kickflip_every_other();
    scenes_share_room();
    garden_budget();
    garden_hearts_pink();
    garden_timb_moves_peak();
    chip_budget();
    chip_hop_to_target();
    chip_burst_no_jump_back();
    chip_score();
    crests_cross_knobs();
    chip_burst_no_drop();
    bowl_flip_not_cut();
    choir_tests();
    rope_tests();
    dance_tests();
    home_tests();
    sound_tests();
    printf("scene ok\n");
    return 0;
}
