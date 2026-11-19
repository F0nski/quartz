#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gen.c"
#include "seq.c"
#include "../../app/app.h"
#include "store.c"

/* doubles: the nine chunk objects in memory (indexed by obj - OBJ_CHUNK0) and the playing state */
static uint8_t mem[Q_CHUNKS][PLAT_STORE_MAX];
static int mem_len[Q_CHUNKS];
static int saves;
static int load_fails;
static int playing;
int audio_playing(void) { return playing; }
int plat_store_size(uint32_t obj) { assert(obj >= OBJ_CHUNK0 && obj < OBJ_NOBJ); return mem_len[obj - OBJ_CHUNK0]; }
int plat_store_load(uint32_t obj, void *dst, uint32_t max)
{
    int i = (int)obj - OBJ_CHUNK0;
    assert(obj >= OBJ_CHUNK0 && obj < OBJ_NOBJ);
    if (mem_len[i] < 0 || load_fails) return -1;
    uint32_t n = (uint32_t)mem_len[i] > max ? max : (uint32_t)mem_len[i];   /* as storage.c: cut to fit */
    memcpy(dst, mem[i], n);
    return (int)n;
}
int plat_store_save(uint32_t obj, const void *src, uint32_t len)
{
    int i = (int)obj - OBJ_CHUNK0;
    assert(obj >= OBJ_CHUNK0 && obj < OBJ_NOBJ && len <= sizeof mem[0]);
    memcpy(mem[i], src, len);
    mem_len[i] = (int)len;
    saves++;
    return 0;
}

static void forget(void) { for (int i = 0; i < Q_CHUNKS; i++) mem_len[i] = -1; }

static uint32_t rng = 12345;
static int rnd(int lo, int hi) { rng = rng * 1664525u + 1013904223u; return lo + (int)((rng >> 8) % (uint32_t)(hi - lo + 1)); }

/* every field pseudo-random and in bounds, so that the clamp changes nothing */
static void fill_random(q_project *p)
{
    project_defaults(p);
    p->tempo = (uint16_t)rnd(40, 240);
    p->swing = (uint8_t)rnd(50, 75); p->tonic = (uint8_t)rnd(0, 11); p->mode = (uint8_t)rnd(0, 10);
    p->quant = (uint8_t)rnd(0, 2); p->click = (uint8_t)rnd(0, 3); p->mute = (uint8_t)rnd(0, 15);
    p->song_on = (uint8_t)rnd(0, 1); p->nsong = (uint8_t)rnd(0, Q_SONG);
    for (int t = 0; t < Q_TRACKS; t++) {
        q_sound *s = &p->sound[t];
        s->model = (uint8_t)rnd(0, 23); s->poly = (uint8_t)rnd(1, 4); s->glide = (uint8_t)rnd(0, 127);
        s->level = (uint8_t)rnd(0, 127); s->tune = (int8_t)rnd(-24, 24); s->harm = (uint8_t)rnd(0, 127);
        s->timb = (uint8_t)rnd(0, 127); s->morph = (uint8_t)rnd(0, 127); s->decay = (uint8_t)rnd(0, 127);
        p->pattern[t] = (uint8_t)rnd(0, Q_PATTERNS - 1);
        for (int n = 0; n < Q_PATTERNS; n++) {
            q_pattern *q = &p->pat[t][n];
            q->len = (uint8_t)rnd(1, Q_STEPS); q->div = (uint8_t)rnd(0, DIV_N - 1); q->nlocks = (uint8_t)rnd(0, Q_LOCKS);
            q->path = (uint8_t)rnd(0, PATH_N - 1); q->xlen = (uint8_t)rnd(1, 4); q->ylen = (uint8_t)rnd(1, 4);
            q->ydiv = (uint8_t)rnd(0, DIV_N - 1);
            for (int k = 0; k < Q_STEPS; k++) {
                q_step *st = &q->steps[k];
                for (int i = 0; i < 4; i++) st->note[i] = (uint8_t)rnd(0, 127);
                st->vel = (uint8_t)rnd(1, 127); st->len = (uint8_t)rnd(1, 64); st->prob = (uint8_t)rnd(0, 20);
                st->cond = (uint8_t)rnd(0, 255); st->flags = (uint8_t)rnd(0, 31); st->micro = (int8_t)rnd(-11, 11);
            }
            for (int k = 0; k < Q_LOCKS; k++) {
                q->locks[k].step = (uint8_t)rnd(0, Q_STEPS - 1); q->locks[k].param = (uint8_t)rnd(0, LK_N - 1);
                q->locks[k].value = (uint8_t)(q->locks[k].param == LK_AGATE ? rnd(5, 100) : rnd(1, 2));   /* in every lock's range */
            }
        }
    }
    for (int i = 0; i < Q_SCENES; i++) {
        p->scene[i].used = (uint8_t)rnd(0, 1); p->scene[i].mute = (uint8_t)rnd(0, 15);
        for (int t = 0; t < Q_TRACKS; t++) p->scene[i].pat[t] = (uint8_t)rnd(0, Q_PATTERNS - 1);
    }
    for (int i = 0; i < Q_SONG; i++) { p->song[i].scene = (uint8_t)rnd(0, Q_SCENES - 1); p->song[i].reps = (uint8_t)rnd(1, 16); }
}

static void check_defaults(void)
{
    q_project d, b;
    project_defaults(&d);
    memset(&b, 0xAA, sizeof b);
    assert(store_load(&b) == -1 && !memcmp(&b, &d, sizeof d));
}

static void roundtrip(void)
{
    q_project a, b;
    project_defaults(&a);
    a.tempo = 97;
    a.sound[2].model = 13;
    a.pat[0][0].steps[5].flags = SF_ON;
    a.pat[0][0].steps[5].note[0] = 67;
    assert(store_save(&a) == 0);
    memset(&b, 0xAA, sizeof b);
    assert(store_load(&b) == 0);
    assert(!memcmp(&a, &b, sizeof a));
}

static void roundtrip_full_project(void)
{
    static q_project a, b;
    fill_random(&a);
    assert(store_save(&a) == 0);
    memset(&b, 0xAA, sizeof b);
    assert(store_load(&b) == 0);
    assert(!memcmp(&a, &b, sizeof a));
}

static void store_mixed_generations(void)
{
    static q_project a;
    uint8_t keep[PLAT_STORE_MAX];
    int keep_len;
    fill_random(&a);
    store_save(&a);
    memcpy(keep, mem[4], sizeof keep);                  /* chunk 4 of this save... */
    keep_len = mem_len[4];
    a.tempo = 99;
    store_save(&a);                                     /* ...then a newer one */
    memcpy(mem[4], keep, sizeof keep);
    mem_len[4] = keep_len;
    check_defaults();
}

/* a save cut off after chunk 3: the later chunks keep the save before */
static void torn_save(const q_project *p)
{
    static uint8_t keep[Q_CHUNKS][PLAT_STORE_MAX];
    static int keep_len[Q_CHUNKS];
    memcpy(keep, mem, sizeof keep);
    memcpy(keep_len, mem_len, sizeof keep_len);
    store_save(p);
    memcpy(mem[4], keep[4], (Q_CHUNKS - 4) * sizeof mem[0]);
    memcpy(&mem_len[4], &keep_len[4], (Q_CHUNKS - 4) * sizeof mem_len[0]);
}

/* after a failed load (a reboot: save_gen 0), the next torn save must not pair with the older save's chunks */
static void store_torn_twice(void)
{
    static q_project a;
    q_project b;
    forget();
    save_gen = 0;
    fill_random(&a);
    store_save(&a);
    a.tempo = a.tempo == 99 ? 98 : 99;
    torn_save(&a);
    save_gen = 0;
    check_defaults();
    torn_save(&a);
    assert(store_load(&b) == -1);
}

static void store_missing_chunk(void)
{
    static q_project a;
    fill_random(&a);
    store_save(&a);
    mem_len[Q_CHUNKS - 1] = -1;
    check_defaults();
    store_save(&a);
    mem_len[0] = -1;
    check_defaults();
}

static void store_rejects_bad_object(void)
{
    q_project a, b;
    project_defaults(&a);
    a.tempo = 97;
    assert(a.version == Q_VERSION);
    store_save(&a);
    int good = mem_len[2];

    forget();                                       /* absent */
    check_defaults();
    store_save(&a);

    mem_len[2] = good - 10;                         /* truncated */
    check_defaults();
    store_save(&a);

    mem_len[2] = good + 10;                         /* longer */
    check_defaults();
    store_save(&a);

    load_fails = 1;                                 /* the size right, the load failing */
    check_defaults();
    load_fails = 0;

    mem[0][2] = 1;                                  /* version 1 (header: magic 2 bytes, then the version) */
    check_defaults();
    store_save(&a);
    mem[0][0] ^= 0xFF;                              /* wrong magic */
    check_defaults();
    store_save(&a);
    assert(store_load(&b) == 0 && !memcmp(&a, &b, sizeof a));
}

/* p as chunks, by hand: the header of each is the one store_save wrote, the payload p's bytes */
static void write_raw(const q_project *p)
{
    const uint8_t *raw = (const uint8_t *)p;
    for (int i = 0; i < Q_CHUNKS; i++) {
        uint32_t off = (uint32_t)i * (PLAT_STORE_MAX - 8u);
        uint32_t n = sizeof *p - off < PLAT_STORE_MAX - 8u ? (uint32_t)sizeof *p - off : PLAT_STORE_MAX - 8u;
        if (off >= sizeof *p) n = 0;
        memcpy(mem[i] + 8, raw + off, n);
    }
}

static void store_clamps_fields(void)
{
    static q_project a, b;
    project_defaults(&a);
    store_save(&a);
    b = a;
    b.tempo = 0;
    b.sound[1].poly = 0;
    b.sound[2].model = 99;
    b.pat[3][0].div = 99;
    b.pat[3][0].len = 0;
    b.pat[1][2].nlocks = 200;
    b.pat[1][2].locks[3].step = 99;
    b.pat[1][2].locks[3].param = 99;
    b.pat[1][2].locks[4] = (q_lock){ 1, LK_RATE, 200 };
    b.pat[1][2].locks[5] = (q_lock){ 1, LK_ADIV, 99 };
    b.pat[1][2].locks[6] = (q_lock){ 1, LK_AOCT, 0 };
    b.pat[1][2].locks[7] = (q_lock){ 1, LK_DEPTH, (uint8_t)-100 };
    b.pat[1][2].locks[8] = (q_lock){ 1, LK_TUNE, (uint8_t)-5 };
    b.pat[1][2].locks[9] = (q_lock){ 1, LK_AGATE, 1 };
    b.pat[1][2].locks[10] = (q_lock){ 1, LK_DEST, 250 };
    b.pat[2][5].path = 99; b.pat[2][5].xlen = 0; b.pat[2][5].ylen = 9; b.pat[2][5].ydiv = 99;
    b.pattern[0] = 9;
    b.nsong = 99; b.click = 9;
    b.scene[1].pat[2] = 9;
    b.song[4].scene = 9; b.song[4].reps = 0; b.song[5].reps = 99;
    write_raw(&b);
    memset(&a, 0xAA, sizeof a);
    assert(store_load(&a) == 0);
    assert(a.tempo == 40 && a.sound[1].poly == 1 && a.sound[2].model == M_KIT);
    assert(a.pat[3][0].div == DIV_N - 1 && a.pat[3][0].len == 1);
    assert(a.pat[1][2].nlocks == Q_LOCKS && a.pat[1][2].locks[3].step < Q_STEPS && a.pat[1][2].locks[3].param <= LK_PUNCH_HI);
    {
        const q_lock *l = a.pat[1][2].locks;
        assert(l[4].value == 75 && l[5].value == DIV_N - 1 && l[6].value == 1 && (int8_t)l[7].value == -64);
        assert((int8_t)l[8].value == -5 && l[9].value == 5 && l[10].value == LD_LEVEL);
    }
    assert(a.pat[2][5].path == PATH_N - 1 && a.pat[2][5].xlen == 1 && a.pat[2][5].ylen == 4 && a.pat[2][5].ydiv == DIV_N - 1);
    assert(a.pattern[0] == Q_PATTERNS - 1 && a.nsong == Q_SONG && a.scene[1].pat[2] == Q_PATTERNS - 1);
    assert(a.song[4].scene == Q_SCENES - 1 && a.song[4].reps == 1 && a.song[5].reps == 16);
    assert(a.click == 3);
}

static void store_roundtrip_v3(void)
{
    static q_project a, b;
    project_defaults(&a);
    q_pattern *q = &a.pat[1][3];
    q->src = SRC_MARBLES;
    q->grids = (q_grids){ 10, 20, 30, { 40, 50, 60 }, 2, 70, { 3, 4, 5 } };
    q->marbles = (q_marbles){ 2, 12, 5, 11, 22, 33, 44, 55, 66, 77, 3, { 0x55, 0x0A }, 4 };
    a.kit[2][9] = (q_pad){ 1, -7, 11, 22, 33, 44 };
    assert(store_save(&a) == 0);
    memset(&b, 0xAA, sizeof b);
    assert(store_load(&b) == 0);
    assert(!memcmp(&a, &b, sizeof a));
}

static void store_clamps_v3(void)
{
    static q_project a, b;
    project_defaults(&a);
    store_save(&a);
    b = a;
    q_pattern *q = &b.pat[0][1];
    q->src = 9; q->grids.voice = 0; q->marbles.tout = 7; q->marbles.len = 0; q->marbles.mode = 9;
    q->marbles.range = 0; q->marbles.steps = 200;
    b.pat[0][2].grids.voice = 9; b.pat[0][2].marbles.tout = 0; b.pat[0][2].marbles.len = 99;
    b.pat[0][2].marbles.mode = 7; b.pat[0][2].marbles.range = 9; b.pat[0][2].marbles.gate = 9;
    b.pat[0][3].grids.x = 200; b.pat[0][3].grids.dens[1] = 255; b.pat[0][3].grids.pad[2] = 200; b.pat[0][3].marbles.bias = 250;
    b.pat[0][3].marbles.note = 130;
    b.pat[0][2].grids.note = 0; b.pat[0][2].marbles.note = 0;     /* note 0 is no note */
    b.kit[1][4].engine = 5; b.kit[1][4].tune = 100; b.kit[1][5].tune = -100; b.kit[1][5].level = 200;
    b.sound[0].model = 30;
    write_raw(&b);
    memset(&a, 0xAA, sizeof a);
    assert(store_load(&a) == 0);
    q = &a.pat[0][1];
    assert(q->src == SRC_N - 1 && q->grids.voice == 1 && q->marbles.tout == 3 && q->marbles.len == 1);
    assert(q->marbles.mode == 6 && q->marbles.range == 1 && q->marbles.steps == 127);
    assert(a.pat[0][2].grids.voice == 3 && a.pat[0][2].marbles.tout == 1 && a.pat[0][2].marbles.len == 16);
    assert(a.pat[0][2].marbles.gate == 5);
    assert(a.pat[0][2].marbles.mode == 6 && a.pat[0][2].marbles.range == 4);
    assert(a.pat[0][2].grids.note == 1 && a.pat[0][2].marbles.note == 1);
    assert(a.pat[0][3].grids.x == 127 && a.pat[0][3].grids.dens[1] == 127 && a.pat[0][3].grids.pad[2] == Q_PADS - 1 && a.pat[0][3].marbles.bias == 127);
    assert(a.pat[0][3].marbles.note == 127);
    assert(a.kit[1][4].engine == PAD_HH && a.kit[1][4].tune == 24 && a.kit[1][5].tune == -24 && a.kit[1][5].level == 127);
    assert(a.sound[0].model == M_KIT);
}

/* a project saved before v3 has another layout: the defaults, not a misread */
static void store_v2_gives_defaults(void)
{
    q_project a;
    project_defaults(&a);
    store_save(&a);
    for (int i = 0; i < Q_CHUNKS; i++) mem[i][2] = 2;
    check_defaults();
}

static void store_roundtrip_v4(void)
{
    static q_project a, b;
    project_defaults(&a);
    q_sound *s = &a.sound[2];
    s->atk = 11; s->dec = 22; s->sus = 33; s->rel = 44; s->ftype = FT_BP; s->cutoff = 55; s->res = 66; s->envamt = -33;
    s->lshape = LS_SH; s->ltrig = 1; s->lrate = 70; s->ldepth = -20; s->ldest = LD_LEVEL;
    s->amode = AM_ORDER; s->aoct = 3; s->adiv = DIV_8T; s->agate = 77;
    assert(store_save(&a) == 0);
    memset(&b, 0xAA, sizeof b);
    assert(store_load(&b) == 0);
    assert(!memcmp(&a, &b, sizeof a));
}

static void store_clamps_v4(void)
{
    static q_project a, b;
    project_defaults(&a);
    store_save(&a);
    b = a;
    q_sound *s = &b.sound[1];
    s->ftype = 7; s->lshape = 9; s->lrate = 200; s->ldest = 9; s->amode = 9; s->aoct = 0; s->adiv = 20; s->agate = 0;
    s->ltrig = 5; s->envamt = -128; s->ldepth = -128;
    s = &b.sound[3];
    s->envamt = 100; s->ldepth = 100; s->aoct = 9; s->agate = 200; s->cutoff = 250; s->res = 200;
    write_raw(&b);
    memset(&a, 0xAA, sizeof a);
    assert(store_load(&a) == 0);
    s = &a.sound[1];
    assert(s->ftype == FT_BP && s->lshape == LS_SH && s->lrate == 75 && s->ldest == LD_LEVEL && s->amode == AM_ORDER);
    assert(s->aoct == 1 && s->adiv == DIV_N - 1 && s->agate == 5 && s->ltrig == 1);
    assert(s->envamt == -64 && s->ldepth == -64);
    s = &a.sound[3];
    assert(s->envamt == 63 && s->ldepth == 63 && s->aoct == 4 && s->agate == 100 && s->cutoff == 127 && s->res == 127);
}

/* a project saved before v4 has another layout: the defaults (the v4 ones too), not a misread */
static void store_v3_gives_defaults(void)
{
    q_project a;
    project_defaults(&a);
    store_save(&a);
    for (int i = 0; i < Q_CHUNKS; i++) mem[i][2] = 3;
    check_defaults();
    store_load(&a);
    for (int t = 0; t < Q_TRACKS; t++) {
        const q_sound *s = &a.sound[t];
        assert(s->atk == 0 && s->dec == 64 && s->sus == 127 && s->rel == 20);
        assert(s->ftype == FT_LP && s->cutoff == 127 && s->res == 0 && s->envamt == 0);
        assert(s->lshape == LS_SIN && s->ltrig == 0 && s->lrate == 5 && s->ldepth == 0 && s->ldest == LD_CUT);
        assert(s->amode == AM_OFF && s->aoct == 1 && s->adiv == DIV_16 && s->agate == 50);
    }
}

static void store_roundtrip_v5(void)
{
    static q_project a, b;
    project_defaults(&a);
    q_sound *s = &a.sound[3];
    s->itype = IT_COMB; s->i1 = 11; s->i2 = 22; s->i3 = 33; s->imix = 44; s->dsend = 55; s->rsend = 66; s->pan = -33;
    a.dtime = DIV_4T; a.dfb = 77; a.rsize = 88; a.rtone = 99; a.comp = 111; a.duck = 122;
    assert(store_save(&a) == 0);
    memset(&b, 0xAA, sizeof b);
    assert(store_load(&b) == 0);
    assert(!memcmp(&a, &b, sizeof a));
}

static void store_clamps_v5(void)
{
    static q_project a, b;
    project_defaults(&a);
    store_save(&a);
    b = a;
    q_sound *s = &b.sound[1];
    s->itype = 9; s->i1 = 200; s->i2 = 255; s->i3 = 128; s->imix = 130; s->dsend = 200; s->rsend = 250; s->pan = -100;
    b.sound[2].pan = 100;
    b.dtime = 20; b.dfb = 200; b.rsize = 255; b.rtone = 128; b.comp = 200; b.duck = 140;
    b.pat[0][0].locks[0] = (q_lock){ 1, LK_PAN, (uint8_t)-100 };
    b.pat[0][0].locks[1] = (q_lock){ 1, LK_LEVEL, 200 };
    b.pat[0][0].nlocks = 2;
    write_raw(&b);
    memset(&a, 0xAA, sizeof a);
    assert(store_load(&a) == 0);
    s = &a.sound[1];
    assert(s->itype == IT_N - 1 && s->i1 == 127 && s->i2 == 127 && s->i3 == 127 && s->imix == 127);
    assert(s->dsend == 127 && s->rsend == 127 && s->pan == -64 && a.sound[2].pan == 63);
    assert(a.dtime == DIV_N - 1 && a.dfb == 127 && a.rsize == 127 && a.rtone == 127 && a.comp == 127 && a.duck == 127);
    assert((int8_t)a.pat[0][0].locks[0].value == -64 && a.pat[0][0].locks[1].value == 127);
}

/* a project saved before v5 has another layout: the defaults (the v5 ones too), not a misread */
static void store_v4_gives_defaults(void)
{
    q_project a;
    project_defaults(&a);
    store_save(&a);
    for (int i = 0; i < Q_CHUNKS; i++) mem[i][2] = 4;
    check_defaults();
    store_load(&a);
    assert(a.dtime == DIV_8 && a.dfb == 40 && a.rsize == 80 && a.rtone == 64 && a.comp == 0 && a.duck == 0);
    for (int t = 0; t < Q_TRACKS; t++) {
        const q_sound *s = &a.sound[t];
        assert(s->itype == IT_OFF && s->i1 == 64 && s->i2 == 64 && s->i3 == 64 && s->imix == 127);
        assert(s->dsend == 0 && s->rsend == 0 && s->pan == 0 && s->level == 100);
    }
}

static void save_refused_while_playing(void)
{
    q_project a;
    project_defaults(&a);
    playing = 1;
    saves = 0;
    assert(store_save(&a) == -1);
    assert(saves == 0);
    playing = 0;
}

/* punch-in locks are masks past LK_N: kept as they are */
static void store_keeps_punch_locks(void)
{
    static q_project a, b;
    project_defaults(&a);
    lock_set(&a.pat[2][5], 3, LK_PUNCH_LO, 0xFF);
    lock_set(&a.pat[2][5], 3, LK_PUNCH_HI, 0x80);
    assert(store_save(&a) == 0);
    memset(&b, 0xAA, sizeof b);
    assert(store_load(&b) == 0);
    assert(!memcmp(&a, &b, sizeof a));
}

/* LIFE's settings out of range in flash: brought back (poly 9 would overrun gen_life_rows' rows[4]) */
static void store_clamps_life(void)
{
    static q_project a, b;
    project_defaults(&a);
    store_save(&a);
    b = a;
    b.pat[1][0].life = (q_life){ 9, 0, 5, 200, 200, 7, 40, 200 };
    b.pat[1][1].life = (q_life){ 0, 200, 0, 0, 0, 0, 0, 0 };
    write_raw(&b);
    memset(&a, 0xAA, sizeof a);
    assert(store_load(&a) == 0);
    const q_life *l = &a.pat[1][0].life;
    assert(l->poly == 4 && l->note == 1 && l->map == 1 && l->seed == 127 && l->dens == 127 && l->autos == 1 && l->gen == 3 && l->mut == 127);
    l = &a.pat[1][1].life;
    assert(l->poly == 1 && l->note == 127);
}

/* LANES' values out of range in flash: brought back (a nibble past the tables' ends) */
static void lanes_clamped_on_load(void)
{
    static q_project a, b;
    project_defaults(&a);
    store_save(&a);
    b = a;
    q_lanes *w = &b.pat[2][0].lanes;
    w->note = 0;
    lanes_set(w, L_OCT, 0, 9); lanes_set(w, L_DUR, 2, 15); lanes_set(w, L_VEL, 3, 12); lanes_set(w, L_NOTE, 1, 15);
    write_raw(&b);
    memset(&a, 0xAA, sizeof a);
    assert(store_load(&a) == 0);
    const q_lanes *l = &a.pat[2][0].lanes;
    assert(l->note == 1 && lanes_get(l, L_OCT, 0) == 4 && lanes_get(l, L_DUR, 2) == 4 && lanes_get(l, L_VEL, 3) == 3);
    assert(lanes_get(l, L_NOTE, 1) == 15 && lanes_get(l, L_OCT, 1) == 2);   /* in range: kept, the neighbour untouched */
}

/* CHORDS' settings out of range in flash: brought back (len 0 would divide by zero, a degree 7 past the scale) */
static void chords_clamped_on_load(void)
{
    static q_project a, b;
    project_defaults(&a);
    store_save(&a);
    b = a;
    q_chords *w = &b.pat[2][1].chords;
    w->note = 0; w->len = 0; w->rate = 9; w->strum = 9; w->voice = 4; w->ch[0] = CH_MAKE(7, CS_TRIAD);
    b.pat[2][2].chords.len = 12;
    write_raw(&b);
    memset(&a, 0xAA, sizeof a);
    assert(store_load(&a) == 0);
    const q_chords *c = &a.pat[2][1].chords;
    assert(c->note == 1 && c->len == 1 && c->rate == 4 && c->strum == 7 && c->voice == 1 && CH_DEG(c->ch[0]) == 6);
    assert(a.pat[2][2].chords.len == 8);
}

/* TONNETZ's settings out of range in flash: brought back */
static void tonnetz_clamped_on_load(void)
{
    static q_project a, b;
    project_defaults(&a);
    store_save(&a);
    b = a;
    b.pat[3][1].tonnetz = (q_tonnetz){ 0, 0, 7, 9, 3, 200, 5, 4, 0x5555 };
    b.pat[3][2].tonnetz.len = 9;
    write_raw(&b);
    memset(&a, 0xAA, sizeof a);
    assert(store_load(&a) == 0);
    const q_tonnetz *t = &a.pat[3][1].tonnetz;
    assert(t->note == 1 && t->len == 1 && t->rate == 4 && t->strum == 7 && t->voice == 1 && t->chaos == 127 && t->scale == 1 && t->ret == 1 && t->seq == 0x5555);
    assert(a.pat[3][2].tonnetz.len == 8);
}

/* ORBITS' settings out of range in flash: brought back (a period 0 would divide by zero) */
static void orbits_clamped_on_load(void)
{
    static q_project a, b;
    project_defaults(&a);
    store_save(&a);
    b = a;
    b.pat[1][3].orbits = (q_orbits){ 0, 9, { 0, 40, 8, 8 }, { 50, 0, 0, 0 }, { 20, 0, 0, 0 } };
    write_raw(&b);
    memset(&a, 0xAA, sizeof a);
    assert(store_load(&a) == 0);
    const q_orbits *o = &a.pat[1][3].orbits;
    assert(o->note == 1 && o->moons == 4 && o->per[0] == 1 && o->per[1] == 32 && o->pha[0] == 31 && o->val[0] == 15);
}

/* MIMIC's settings out of range in flash: brought back (a count past the phrase would read past it) */
static void mimic_clamped_on_load(void)
{
    static q_project a, b;
    project_defaults(&a);
    store_save(&a);
    b = a;
    q_mimic *m = &b.pat[2][1].mimic;
    m->n = 40; m->order = 0; m->drift = 200; m->loop = 20; m->ev[0][0] = 0; m->ev[0][1] = 9;
    b.pat[2][2].mimic.order = 3;
    write_raw(&b);
    memset(&a, 0xAA, sizeof a);
    assert(store_load(&a) == 0);
    m = &a.pat[2][1].mimic;
    assert(m->n == 32 && m->order == 1 && m->drift == 127 && m->loop == 16 && m->ev[0][0] == 60 && m->ev[0][1] == 7);
    assert(a.pat[2][2].mimic.order == 2);
}

int main(void)
{
    forget();
    store_keeps_punch_locks();
    roundtrip();
    roundtrip_full_project();
    store_mixed_generations();
    store_missing_chunk();
    store_torn_twice();
    store_rejects_bad_object();
    store_clamps_fields();
    store_roundtrip_v3();
    store_clamps_v3();
    store_clamps_life();
    lanes_clamped_on_load();
    chords_clamped_on_load();
    tonnetz_clamped_on_load();
    orbits_clamped_on_load();
    mimic_clamped_on_load();
    store_v2_gives_defaults();
    store_roundtrip_v4();
    store_clamps_v4();
    store_v3_gives_defaults();
    store_roundtrip_v5();
    store_clamps_v5();
    store_v4_gives_defaults();
    save_refused_while_playing();
    puts("store ok");
    return 0;
}
