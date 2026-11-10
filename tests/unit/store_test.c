#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
                q->locks[k].value = (uint8_t)rnd(0, 255);
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

static void store_clamps_fields(void)
{
    static q_project a, b;
    uint8_t *raw = (uint8_t *)&b;
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
    b.pat[2][5].path = 99; b.pat[2][5].xlen = 0; b.pat[2][5].ylen = 9; b.pat[2][5].ydiv = 99;
    b.pattern[0] = 9;
    b.nsong = 99; b.click = 9;
    b.scene[1].pat[2] = 9;
    b.song[4].scene = 9; b.song[4].reps = 0; b.song[5].reps = 99;
    /* write b as chunks, by hand: the header of each is the one store_save wrote, the payload b's bytes */
    for (int i = 0; i < Q_CHUNKS; i++) {
        uint32_t off = (uint32_t)i * (PLAT_STORE_MAX - 8u);
        uint32_t n = sizeof b - off < PLAT_STORE_MAX - 8u ? (uint32_t)sizeof b - off : PLAT_STORE_MAX - 8u;
        if (off >= sizeof b) n = 0;
        memcpy(mem[i] + 8, raw + off, n);
    }
    memset(&a, 0xAA, sizeof a);
    assert(store_load(&a) == 0);
    assert(a.tempo == 40 && a.sound[1].poly == 1 && a.sound[2].model == 23);
    assert(a.pat[3][0].div == DIV_N - 1 && a.pat[3][0].len == 1);
    assert(a.pat[1][2].nlocks == Q_LOCKS && a.pat[1][2].locks[3].step < Q_STEPS && a.pat[1][2].locks[3].param < LK_N);
    assert(a.pat[2][5].path == PATH_N - 1 && a.pat[2][5].xlen == 1 && a.pat[2][5].ylen == 4 && a.pat[2][5].ydiv == DIV_N - 1);
    assert(a.pattern[0] == Q_PATTERNS - 1 && a.nsong == Q_SONG && a.scene[1].pat[2] == Q_PATTERNS - 1);
    assert(a.song[4].scene == Q_SCENES - 1 && a.song[4].reps == 1 && a.song[5].reps == 16);
    assert(a.click == 3);
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

int main(void)
{
    forget();
    roundtrip();
    roundtrip_full_project();
    store_mixed_generations();
    store_missing_chunk();
    store_rejects_bad_object();
    store_clamps_fields();
    save_refused_while_playing();
    puts("store ok");
    return 0;
}
