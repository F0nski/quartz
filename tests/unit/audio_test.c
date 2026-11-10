#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "gen.c"
#include "seq.c"
#include "voices.c"
#include "dsp/pq.h"

/* doubles: the second core and the clock */
static int cpu2_mode;               /* 0: run refused, 1: runs the job, 2: takes it, then wait drops it,
                                     * 3: runs it, then wait drops it (what it added stays in the mix) */
static int on_core2, cpu2_runs;
static uint32_t ticks, tick_step = 10, xruns, cpu2_stack;
int plat_cpu2_set(int on) { (void)on; return 0; }
int plat_cpu2_on(void) { return cpu2_mode != 0; }
int plat_cpu2_run(void (*fn)(uint32_t), uint32_t arg)
{
    cpu2_runs++;
    if (cpu2_mode == 1 || cpu2_mode == 3) { on_core2 = 1; fn(arg); on_core2 = 0; }
    return cpu2_mode != 0;
}
int plat_cpu2_wait(void) { return cpu2_mode >= 2 ? -1 : 0; }
uint32_t plat_ticks24(void) { return ticks += tick_step; }
uint32_t plat_xruns(void) { return xruns; }
uint32_t plat_cpu2_stack(void) { return cpu2_stack; }

/* doubles: the voices, every call logged */
typedef struct { char op; int v; float a; unsigned n; int core, render; } call;
static call LOG[4096];
static int NLOG, in_render;         /* in_render: the call came from app_render, not the main loop */
static int silent[PQ_VOICES];
static float amp = 0.01f;           /* voice v renders amp * (v + 1) */
static float patch[PQ_VOICES][5];   /* the last pq_set_patch of each voice */
static void logc(char op, int v, float a, unsigned n) { LOG[NLOG++] = (call){ op, v, a, n, on_core2, in_render }; }
void pq_init(void) { logc('i', -1, 0, 0); }
void pq_set_engine(int v, int model) { logc('e', v, (float)model, 0); }
void pq_set_patch(int v, float h, float t, float m, float d, float g)
{
    patch[v][0] = h; patch[v][1] = t; patch[v][2] = m; patch[v][3] = d; patch[v][4] = g;
    logc('p', v, h, 0);
}
void pq_glide_to(int v, float note) { logc('g', v, note, 0); }
void pq_note_on(int v, float note, float vel) { (void)vel; logc('n', v, note, 0); }
void pq_note_off(int v) { logc('f', v, 0, 0); }
void pq_render(int v, float *out, unsigned n)
{
    logc('r', v, 0, n);
    for (unsigned i = 0; i < n; i++) out[i] += amp * (float)(v + 1);
}
int pq_silent(int v) { return silent[v]; }

#include "audio.c"

static q_project P;
static int32_t LR[2 * APP_BLOCK];

static void setup(void)
{
    project_defaults(&P);
    audio_rec(0);
    cpu2_mode = 1;
    cpu2_runs = 0;
    tick_step = 10;
    amp = 0.01f;
    memset(silent, 0, sizeof silent);
    audio_setup(&P);
    NLOG = 0;
}

static void block(uint32_t n) { in_render = 1; app_render(LR, n, 4096); in_render = 0; }

static int count(char op, int v)
{
    int c = 0;
    for (int i = 0; i < NLOG; i++) c += LOG[i].op == op && LOG[i].v == v;
    return c;
}

static void splits_block_at_events(void)
{
    setup();
    P.tempo = 120;
    P.pat[0][0].div = DIV_32;
    P.pat[0][0].steps[0].flags = P.pat[0][0].steps[1].flags = SF_ON;
    /* step 1 starts at frame spf: blocks before it so that it falls at offset 100 of a 256-frame block */
    uint32_t num, den;
    seq_step_ratio(120, DIV_32, &num, &den);
    uint32_t spf = (num + den - 1) / den, lead = spf - 100;
    audio_play(1);
    block(lead % 256);
    for (uint32_t k = 0; k < lead / 256; k++) block(256);
    NLOG = 0;
    block(256);
    int v = -1;
    for (int i = 0; i < NLOG; i++) if (LOG[i].op == 'r') { v = LOG[i].v; break; }
    assert(v >= 0);
    int r[4], nr = 0, on = -1;
    for (int i = 0; i < NLOG; i++) {
        if (LOG[i].op == 'r' && LOG[i].v == v) r[nr++] = i;
        if (LOG[i].op == 'n' && LOG[i].v == v) on = i;
    }
    assert(nr == 2);
    assert(LOG[r[0]].n == 100 && LOG[r[1]].n == 156);
    assert(r[0] < on && on < r[1]);
}

static void six_notes(void)
{
    P.sound[0].poly = P.sound[1].poly = 2;
    for (int t = 0; t < 4; t++) audio_live_note(t, 60, 1);
    audio_live_note(0, 62, 1);
    audio_live_note(1, 62, 1);
}

static void cores_get_their_voices(void)
{
    setup();
    six_notes();
    block(256);
    for (int v = 0; v < Q_VOICES; v++) {
        assert(count('r', v) == 1);
        for (int i = 0; i < NLOG; i++)
            if (LOG[i].op == 'r' && LOG[i].v == v) assert(LOG[i].core == (v >= 3));
    }
}

static void render_without_cpu2(void)
{
    int32_t ref[2 * APP_BLOCK];
    setup();
    six_notes();
    block(256);
    memcpy(ref, LR, sizeof ref);
    assert(ref[0] != 0);
    for (int mode = 0; mode <= 2; mode += 2) {
        setup();
        cpu2_mode = mode;
        six_notes();
        memset(LR, 0, sizeof LR);
        block(256);
        for (int v = 0; v < Q_VOICES; v++) assert(count('r', v) == 1);
        for (int i = 0; i < NLOG; i++) assert(LOG[i].core == 0);
        assert(!memcmp(ref, LR, sizeof ref));
    }
}

static void core2_dropped_after_adding(void)
{
    int32_t ref[2 * APP_BLOCK];
    setup();
    six_notes();
    block(256);
    memcpy(ref, LR, sizeof ref);
    setup();
    cpu2_mode = 3;
    six_notes();
    block(256);
    for (int v = 3; v < Q_VOICES; v++) assert(count('r', v) == 2);   /* on core 2, then again on core 1 */
    assert(!memcmp(ref, LR, sizeof ref));
}

static void no_empty_core2_job(void)
{
    setup();
    block(256);
    audio_live_note(0, 60, 1);
    block(256);
    assert(audio_load().active == 1 && cpu2_runs == 0);
    six_notes();
    block(256);
    assert(cpu2_runs == 1);
}

static int track_on(int t)
{
    int c = 0;
    for (int v = 0; v < Q_VOICES; v++) c += pool.v[v].state == V_ON && pool.v[v].track == t;
    return c;
}

static void play_while_playing_restarts(void)
{
    setup();
    P.sound[0].poly = 4;
    P.pat[0][0].div = DIV_1;           /* one step: 2 s */
    P.pat[0][0].steps[0].flags = SF_ON;
    audio_play(1);
    block(256);
    assert(track_on(0) == 1);
    audio_play(1);
    NLOG = 0;
    block(256);
    assert(audio_pos(0) == 0 && count('n', 0) + count('n', 1) + count('n', 2) + count('n', 3) == 1);
    assert(track_on(0) == 1);       /* the first one released, not stuck */
    audio_play(0);
    block(256);
    assert(track_on(0) == 0);
}

static void full_queue_keeps_note_off(void)
{
    setup();
    audio_live_note(0, 60, 1);
    for (int i = 0; i < 2 * CMD_N; i++) audio_live_note(1, 40 + i % 20, 1);
    audio_live_note(0, 60, 0);
    block(256);
    assert(track_on(0) == 0 && track_on(1) == 1);
}

static void model_change_fades_then_frees(void)
{
    setup();
    audio_live_note(0, 60, 1);
    audio_live_note(1, 60, 1);
    block(256);
    block(256);
    int v0 = -1;
    for (int v = 0; v < Q_VOICES; v++) if (pool.v[v].track == 0 && pool.v[v].state == V_ON) v0 = v;
    assert(v0 >= 0);
    int32_t prev = LR[2 * 255], jump = 0;
    audio_track_model(0);
    block(256);
    for (int i = 0; i < 256; i++) {
        int32_t d = LR[2 * i] - prev;
        if (d < 0) d = -d;
        if (d > jump) jump = d;
        prev = LR[2 * i];
    }
    assert(jump < 200);             /* a hard cut: about 16500 */
    assert(track_on(0) == 0 && pool.v[v0].state == V_FREE && track_on(1) == 1);
    NLOG = 0;
    block(256);
    assert(count('r', v0) == 0);
    assert(LR[0] - prev < 200 && prev - LR[0] < 200);
}

static void level_master_and_clip(void)
{
    setup();
    P.sound[0].level = 127;
    audio_live_note(0, 60, 1);
    block(256);
    block(256);
    int32_t full = (int32_t)(0.01f * 0.25f * 8388607.0f);
    assert(LR[0] == full && LR[511] == full);
    P.sound[0].level = 64;
    block(256);
    int32_t lv = (int32_t)(0.01f * (64 / 127.0f * 0.25f) * 8388607.0f);
    assert(LR[0] - lv <= 1 && lv - LR[0] <= 1);
    app_render(LR, 256, 2048);      /* MASTER halved: ramped across the block */
    assert(LR[0] < lv && LR[0] > lv - lv / 128);
    for (int i = 1; i < 256; i++) assert(LR[2 * i] <= LR[2 * i - 2]);
    assert(LR[510] - lv / 2 <= 1 && lv / 2 - LR[510] <= 1);
    amp = 1000.0f;
    app_render(LR, 256, 4096);
    block(256);
    for (int i = 0; i < 512; i++) assert(LR[i] == 8388607);
    amp = -1000.0f;
    block(256);
    for (int i = 0; i < 512; i++) assert(LR[i] == -8388608);
}

static void load_fields(void)
{
    setup();
    xruns = 7;
    cpu2_stack = 1234;
    tick_step = 10000;
    six_notes();
    block(256);
    q_load l = audio_load();
    assert(l.active == 6 && l.cpu2 == 1 && l.xruns == 7 && l.cpu2_stack == 1234);
    assert(l.core1_pct == 0 && l.core2_pct == 0);   /* not a second of blocks yet */
    for (int k = 1; k < 173; k++) block(256);       /* 173 blocks of 256: the first second of budget */
    l = audio_load();
    /* a block: core 2's job 1 step of ticks, core 1 4 (5 less 1 waiting); 173 x 256 x 544 ticks of budget */
    assert(l.core2_pct == 173u * 10000u / (173u * 256u * 544u / 100u));
    assert(l.core1_pct == 4u * 173u * 10000u / (173u * 256u * 544u / 100u));
    cpu2_mode = 0;
    assert(audio_load().cpu2 == 0);
    xruns = cpu2_stack = 0;
}

static void free_voices_skipped(void)
{
    setup();
    block(256);
    for (int v = 0; v < Q_VOICES; v++) assert(count('r', v) == 0);
    audio_live_note(2, 60, 1);
    block(256);
    int v = -1;
    for (int i = 0; i < NLOG; i++) if (LOG[i].op == 'n') v = LOG[i].v;
    assert(v >= 0 && count('r', v) == 1);
    for (int u = 0; u < Q_VOICES; u++) if (u != v) assert(count('r', u) == 0);
    audio_live_note(2, 60, 0);
    NLOG = 0;
    block(256);
    assert(count('f', v) == 1 && count('r', v) == 1);   /* released: still rendered */
    silent[v] = 1;
    block(256);
    NLOG = 0;
    block(256);
    assert(count('r', v) == 0);
    assert(audio_load().active == 0);
}

static void live_notes_reach_voices(void)
{
    setup();
    P.sound[1].tune = -5;
    audio_live_note(1, 64, 1);
    assert(NLOG == 0);
    block(256);
    int found = 0;
    for (int i = 0; i < NLOG; i++) if (LOG[i].op == 'n') { assert(LOG[i].a == 59.0f); found++; }
    assert(found == 1);
}

static int engine_calls(int v, float model)
{
    int c = 0;
    for (int i = 0; i < NLOG; i++) c += LOG[i].op == 'e' && LOG[i].v == v && LOG[i].a == model;
    return c;
}

static void slot_engine_follows_track(void)
{
    setup();
    P.sound[0].model = 8;
    P.sound[1].model = 13;
    audio_live_note(0, 60, 1);
    block(256);
    assert(count('e', 0) == 0);        /* slot 0 already holds model 8 */
    audio_live_note(0, 60, 0);
    silent[0] = 1;
    block(256);
    silent[0] = 0;
    NLOG = 0;
    audio_live_note(1, 60, 1);
    block(256);
    assert(engine_calls(0, 13) == 1);
    int e = -1, n = -1;
    for (int i = 0; i < NLOG; i++) {
        if (LOG[i].op == 'e') e = i;
        if (LOG[i].op == 'n' && LOG[i].v == 0) n = i;
    }
    assert(e >= 0 && e < n);
    audio_live_note(1, 60, 0);
    silent[0] = 1;
    block(256);
    silent[0] = 0;
    NLOG = 0;
    audio_live_note(1, 62, 1);
    block(256);
    assert(count('n', 0) == 1 && count('e', 0) == 0);
}

static void panic_silences(void)
{
    setup();
    six_notes();
    block(256);
    assert(LR[0] != 0);
    audio_mute();   /* what app_panic calls */
    NLOG = 0;
    memset(LR, 0x55, sizeof LR);
    block(256);
    for (int i = 0; i < 2 * 256; i++) assert(LR[i] == 0);
    assert(count('r', 0) + count('r', 1) + count('r', 2) + count('r', 3) + count('r', 4) + count('r', 5) == 0);
    block(256);
    assert(LR[0] == 0);
    audio_unmute();                 /* what app_ui_init calls: the app resumed */
    block(256);
    assert(LR[0] != 0 && count('r', 0) == 1);
}

static int render_switches(void)
{
    int c = 0;
    for (int i = 0; i < NLOG; i++) c += LOG[i].op == 'e' && LOG[i].render;
    return c;
}

static int preps(void)
{
    int c = 0;
    for (int v = 0; v < Q_VOICES; v++) c += pool.v[v].state == V_PREP;
    return c;
}

/* the main loop's frames, each followed by a block */
static void frames(int k)
{
    while (k--) {
        audio_prepare();
        block(256);
    }
}

static void boot_prepared_play_switches_nothing(void)
{
    setup();
    for (int t = 0; t < Q_TRACKS; t++) {
        P.sound[t].model = 20;
        P.pat[t][0].steps[0].flags = SF_ON;
    }
    frames(12);
    int prepared = 0, n[2] = {0, 0};
    for (int i = 0; i < NLOG; i++) prepared += LOG[i].op == 'e' && !LOG[i].render && LOG[i].a == 20.0f;
    assert(prepared == 4 && render_switches() == 0 && preps() == 0);   /* poly 1 x 4 tracks */
    for (int v = 0; v < Q_VOICES; v++) n[v / 3] += pool.v[v].model == 20 && pool.v[v].state == V_FREE;
    assert(n[0] == 2 && n[1] == 2);     /* two on each core */
    NLOG = 0;
    audio_play(1);
    block(256);
    assert(render_switches() == 0);
    int on = 0;
    for (int i = 0; i < NLOG; i++) on += LOG[i].op == 'n';
    assert(on == 4);
}

static void model_change_while_playing_one_switch_per_block(void)
{
    setup();
    for (int t = 0; t < Q_TRACKS; t++) {
        P.pat[t][0].div = DIV_32;
        for (int k = 0; k < 16; k++) P.pat[t][0].steps[k].flags = SF_ON;
    }
    frames(12);
    audio_play(1);
    frames(20);
    for (int t = 0; t < Q_TRACKS; t++) {
        P.sound[t].model = (uint8_t)(13 + t);
        audio_track_model(t);
    }
    for (int k = 0; k < 200; k++) {
        NLOG = 0;
        frames(1);
        assert(render_switches() <= 1);
    }
    NLOG = 0;
    frames(100);
    assert(render_switches() == 0);     /* the engines are in place */
    for (int t = 0; t < Q_TRACKS; t++) {
        int n = 0;
        for (int v = 0; v < Q_VOICES; v++) n += pool.v[v].model == 13 + t;
        assert(n >= 1);
    }
    assert(preps() == 0);
}

/* six note-ons each needing an engine switch: (track, note) in the order they start */
static const int SIX[6][2] = {{0, 60}, {1, 60}, {2, 60}, {3, 60}, {0, 62}, {1, 62}};

static void started(int *track, int *note)
{
    *track = *note = -1;
    for (int i = 0; i < NLOG; i++)
        if (LOG[i].op == 'n') {
            assert(*track < 0);     /* one note-on per block */
            *track = pool.v[LOG[i].v].track;
            *note = (int)LOG[i].a;
        }
}

static void switches_spread_over_blocks(void)
{
    for (int cancel = 0; cancel < 2; cancel++) {
        int exp[6][2], ne = 0;
        for (int i = 0; i < 6; i++)
            if (!cancel || i != 2) { exp[ne][0] = SIX[i][0]; exp[ne][1] = SIX[i][1]; ne++; }
        setup();
        for (int t = 0; t < Q_TRACKS; t++) P.sound[t].model = 20;
        six_notes();
        for (int k = 0; k < 7; k++) {
            if (cancel && k == 1)
                audio_live_note(2, 60, 0);  /* a note-off for a deferred note cancels it */
            NLOG = 0;
            block(256);
            int t, n;
            started(&t, &n);
            if (k < ne)
                assert(render_switches() == 1 && t == exp[k][0] && n == exp[k][1]);
            else
                assert(render_switches() == 0 && t < 0);
            for (int c = 0; c < NLOG; c++) assert(LOG[c].op != 'f');
        }
        assert(audio_load().active == (cancel ? 5u : 6u) && track_on(2) == !cancel);
    }
}

static void prep_hand_off(void)
{
    setup();
    P.sound[0].model = 20;
    audio_prepare();
    assert(preps() == 0 && NLOG == 0);  /* asked: the render answers at its next block */
    block(256);
    int s = -1;
    for (int v = 0; v < Q_VOICES; v++) if (pool.v[v].state == V_PREP) s = v;
    assert(s >= 0 && preps() == 1 && NLOG == 0);
    for (int t = 1; t < Q_TRACKS; t++) audio_live_note(t, 60, 1);
    block(256);
    assert(count('r', s) == 0 && count('n', s) == 0 && count('p', s) == 0 && count('e', s) == 0);
    assert(audio_load().active == 3);   /* V_PREP is not a voice playing */
    NLOG = 0;
    audio_prepare();                    /* the engine built in the main loop, then handed back */
    assert(NLOG == 1 && LOG[0].op == 'e' && LOG[0].v == s && LOG[0].a == 20.0f && !LOG[0].render);
    assert(pool.v[s].state == V_PREP);
    block(256);
    assert(pool.v[s].state == V_FREE && pool.v[s].model == 20);
    frames(4);
    assert(preps() == 0);
    NLOG = 0;
    audio_live_note(0, 60, 1);
    block(256);
    assert(count('n', s) == 1 && render_switches() == 0);
}

static void full_queue_keeps_the_hand_off(void)
{
    setup();
    P.sound[0].model = 20;
    audio_prepare();
    block(256);
    int s = -1;
    for (int v = 0; v < Q_VOICES; v++) if (pool.v[v].state == V_PREP) s = v;
    assert(s >= 0);
    audio_mute();                       /* the render drains nothing: the ring fills */
    for (int i = 0; i < 2 * CMD_N; i++) audio_live_note(1, 40, 0);
    audio_prepare();                    /* its hand-back fits; its next request does not */
    audio_unmute();
    block(256);
    assert(pool.v[s].state == V_FREE && pool.v[s].model == 20 && preps() == 0);
    P.sound[1].model = 21;              /* the preparation goes on */
    frames(4);
    int n = 0;
    for (int v = 0; v < Q_VOICES; v++) n += pool.v[v].model == 21 && pool.v[v].state == V_FREE;
    assert(n == 1 && preps() == 0);
}

static void mono_model_change_keeps_its_fade(void)
{
    setup();
    P.sound[1].model = 20;
    P.sound[1].poly = 2;
    frames(6);                          /* two engines on 20, built in the main loop */
    audio_live_note(0, 60, 1);
    block(256);
    block(256);
    int v0 = -1;
    for (int v = 0; v < Q_VOICES; v++) if (pool.v[v].track == 0 && pool.v[v].state == V_ON) v0 = v;
    assert(v0 >= 0 && pool.v[v0].model == 8);
    P.sound[0].model = 20;
    audio_track_model(0);
    audio_live_note(0, 62, 1);          /* in the same block: the fading slot is not the track's own any more */
    NLOG = 0;
    block(256);
    assert(render_switches() == 0 && count('n', v0) == 0 && track_on(0) == 1);
    assert(pool.v[v0].state == V_FREE);
}

static int track_slot(int t)
{
    for (int v = 0; v < Q_VOICES; v++) if (pool.v[v].track == t && pool.v[v].state != V_FREE) return v;
    return -1;
}

/* the last harm voice v was set to */
static float last_harm(int v)
{
    float h = -1;
    for (int i = 0; i < NLOG; i++) if (LOG[i].op == 'p' && LOG[i].v == v) h = LOG[i].a;
    return h;
}

/* blocks of 256 until the one holding frame f has begun: returns the frames rendered */
static uint32_t until(uint32_t at, uint32_t f)
{
    while (at + 256 <= f) { block(256); at += 256; }
    return at;
}

static uint32_t spf16(int tempo)
{
    uint32_t num, den;
    seq_step_ratio(tempo, DIV_16, &num, &den);
    return (num + den - 1) / den;
}

static void lock_reaches_its_voice(void)
{
    setup();
    P.pat[0][0].steps[0].flags = SF_ON;
    lock_set(&P.pat[0][0], 0, LK_HARM, 127);
    audio_live_note(1, 60, 1);
    audio_play(1);
    block(256);
    NLOG = 0;
    block(256);
    int v0 = track_slot(0), v1 = track_slot(1);
    assert(v0 >= 0 && v1 >= 0 && v0 != v1);
    assert(last_harm(v0) == 1.0f && last_harm(v1) == 64 / 127.0f);
}

static void lock_ends_with_note(void)
{
    setup();
    P.pat[0][0].steps[0].flags = P.pat[0][0].steps[1].flags = SF_ON;
    lock_set(&P.pat[0][0], 0, LK_HARM, 127);
    audio_play(1);
    until(0, spf16(120) + 256);
    NLOG = 0;
    block(256);
    assert(audio_pos(0) == 1 && count('n', track_slot(0)) == 0);   /* step 1 started a block before */
    assert(last_harm(track_slot(0)) == 64 / 127.0f);
}

static void legato_glides_without_retrigger(void)
{
    setup();
    P.pat[0][0].steps[0].flags = SF_ON | SF_SLIDE;
    P.pat[0][0].steps[1].flags = SF_ON;
    P.pat[0][0].steps[1].note[0] = 62;
    P.sound[0].tune = 2;
    audio_play(1);
    until(0, 2 * spf16(120));
    int n = 0, g = 0, f = 0;
    for (int i = 0; i < NLOG; i++) {
        if (LOG[i].op == 'n') { assert(LOG[i].a == 62.0f && !g); n++; }
        if (LOG[i].op == 'g') { assert(LOG[i].a == 64.0f && !f); g++; }
        f += LOG[i].op == 'f';
    }
    assert(n == 1 && g == 1 && f == 1);     /* 62's note-off, half a step in */
    assert(track_on(0) == 0);
}

static int32_t OUT[3 * 88236];

/* blocks of 256 until frame `frames`, the left channel kept in OUT */
static void record(uint32_t frames)
{
    for (uint32_t at = 0; at < frames; at += 256) {
        block(256);
        for (int i = 0; i < 256; i++) OUT[at + i] = LR[2 * i];
    }
}

static int ups(uint32_t from, uint32_t n)
{
    int c = 0;
    for (uint32_t i = from + 1; i < from + n; i++) c += OUT[i - 1] < 0 && OUT[i] >= 0;
    return c;
}

static void click_on_beats(void)
{
    setup();
    P.click = 1;
    audio_rec(1);
    audio_play(1);
    record(5 * 22060);
    uint32_t num, den;
    seq_step_ratio(120, DIV_4, &num, &den);
    for (uint32_t k = 0; k < 5; k++) {
        uint32_t b = (uint32_t)(((uint64_t)k * num + den - 1) / den);
        int on = 0;
        for (uint32_t i = b; i < b + 8; i++) on |= OUT[i] != 0;
        assert(on);
        for (uint32_t i = b + 8820; i < b + 22000 && i < 5 * 22060; i++) assert(OUT[i] == 0);   /* 0.2 s on */
        if (k) for (uint32_t i = b - 200; i < b; i++) assert(OUT[i] == 0);
        assert(ups(b, 232) == (k % 4 ? 5 : 10));    /* 5.2 ms: 1 kHz, 2 kHz on the bar */
    }
    setup();                        /* not armed: no metronome */
    P.click = 1;
    audio_play(1);
    record(22060);
    for (int i = 0; i < 22060; i++) assert(OUT[i] == 0);
}

static void count_in_then_play(void)
{
    uint32_t num, den;
    seq_step_ratio(120, DIV_1, &num, &den);
    uint32_t bar = (num + den - 1) / den;
    for (int c = 0; c < 2; c++) {
        setup();
        P.click = c ? 2 : 3;        /* COUNT alone: the count-in clicks all the same */
        P.pat[0][0].steps[0].flags = SF_ON;
        amp = 0;
        audio_rec(1);
        audio_play(1);
        int first = -1;
        for (uint32_t k = 0; k <= bar / 256; k++) {
            NLOG = 0;
            block(256);
            for (int i = 0; i < 256; i++) OUT[k * 256 + i] = LR[2 * i];
            if (count('n', track_slot(0)) && first < 0) first = (int)k;
            if (k < bar / 256) assert(audio_pos(0) == -1);
        }
        assert(first == (int)(bar / 256));
        for (uint32_t b = 0; b < 4; b++) assert(OUT[b * bar / 4 + 4] != 0);
    }
    setup();                        /* not armed: no count-in */
    P.click = 3;
    P.pat[0][0].steps[0].flags = SF_ON;
    audio_play(1);
    block(256);
    assert(count('n', track_slot(0)) == 1);
}

static void overdub_records_step(void)
{
    for (int q = 0; q < 2; q++) {
        setup();
        P.tempo = 60;
        P.quant = q ? 2 : 0;
        audio_rec(1);
        audio_play(1);
        uint32_t spf = spf16(60), on = 4 * spf + spf / 3;
        uint32_t at = until(0, on);
        audio_live_note(0, 60, 1);
        block(256);
        at = until(at + 256, on + spf);
        q_rec r;
        assert(!audio_take_rec(&r));
        audio_live_note(0, 60, 0);
        block(256);
        assert(audio_take_rec(&r));
        assert(r.track == 0 && r.pat == 0 && r.step == 4 && r.note == 60 && r.vel == 100 && r.len == 8);
        assert(r.micro == (q ? 0 : 8));
        assert(!audio_take_rec(&r));
    }
}

/* a quarter step before the wrap to pattern 1: its step 0 is the nearest, in pattern 1 */
static void overdub_early_at_pattern_change(void)
{
    setup();
    P.tempo = 60;
    P.quant = 0;
    audio_rec(1);
    audio_play(1);
    block(256);
    P.pattern[0] = 1;
    uint32_t spf = spf16(60), on = 16 * spf - spf / 4;
    uint32_t at = until(256, on);
    audio_live_note(0, 60, 1);
    block(256);
    until(at + 256, on + spf / 2);
    audio_live_note(0, 60, 0);
    block(256);
    q_rec r;
    assert(audio_take_rec(&r));
    assert(r.pat == 1 && r.step == 0 && r.micro < 0);
}

/* on 2: a note written to a held step sounds but is not recorded */
static void step_note_not_recorded(void)
{
    setup();
    audio_rec(1);
    audio_play(1);
    until(0, 4 * spf16(120));
    NLOG = 0;
    audio_live_note(1, 64, 2);
    block(256);
    assert(count('n', track_slot(1)) == 1);
    audio_live_note(1, 64, 0);
    block(256);
    q_rec r;
    assert(!audio_take_rec(&r));
}

static void stop_flushes_held_rec(void)
{
    setup();
    P.tempo = 60;
    audio_rec(1);
    audio_play(1);
    uint32_t spf = spf16(60);
    uint32_t at = until(0, 4 * spf);
    audio_live_note(1, 64, 1);
    block(256);
    until(at + 256, at + spf / 2);
    audio_play(0);
    block(256);
    q_rec r;
    assert(audio_take_rec(&r));
    assert(r.track == 1 && r.step == 4 && r.note == 64 && r.len == 4);
    audio_live_note(1, 64, 0);
    block(256);
    assert(!audio_take_rec(&r));
}

static void scene_reported_once(void)
{
    uint32_t num, den;
    seq_step_ratio(120, DIV_1, &num, &den);
    uint32_t bar = (num + den - 1) / den;
    setup();
    P.scene[1].used = 1;
    P.scene[1].pat[0] = 2;
    audio_play(1);
    block(256);
    audio_scene(1);
    assert(audio_scene_started() == -1);
    uint32_t at = until(256, bar);
    assert(audio_scene_started() == -1 && audio_pattern(0) == 0);
    block(256);
    assert(audio_scene_started() == 1 && audio_scene_started() == -1);
    assert(audio_pattern(0) == 2 && audio_song_entry() == -1);
    until(at + 256, 3 * bar);
    assert(audio_scene_started() == -1);
}

static void legato_beside_a_live_note(void)
{
    setup();
    P.sound[0].poly = 2;
    P.pat[0][0].steps[0].flags = SF_ON | SF_SLIDE;
    P.pat[0][0].steps[1].flags = SF_ON;
    P.pat[0][0].steps[1].note[0] = 62;
    audio_play(1);
    block(256);
    audio_live_note(0, 50, 1);          /* newer than the tied note, on the same track */
    until(256, 2 * spf16(120));
    audio_live_note(0, 50, 0);
    audio_play(0);
    block(256);
    assert(track_on(0) == 0);           /* no voice left without its note-off */
}

static void legato_while_tied_note_waits(void)
{
    setup();
    P.tempo = 240;
    P.sound[0].model = 20;
    P.sound[0].poly = 2;
    for (int t = 1; t < Q_TRACKS; t++) P.sound[t].model = (uint8_t)(12 + t);
    P.pat[0][0].div = DIV_32;
    P.pat[0][0].steps[0].flags = SF_ON | SF_SLIDE;
    P.pat[0][0].steps[1].flags = SF_ON;
    P.pat[0][0].steps[1].note[0] = 62;
    P.pat[0][0].steps[1].micro = -11;   /* at frame 747: the tied note still waits behind three switches */
    for (int t = 1; t < Q_TRACKS; t++) audio_live_note(t, 60, 1);
    audio_play(1);
    block(256);
    block(256);
    block(256);
    assert(track_on(0) == 0 && n_later == 1 && later[0].track == 0 && later[0].note == 62);
    NLOG = 0;
    for (int k = 0; k < 20; k++) block(256);
    int n = 0;
    for (int i = 0; i < NLOG; i++)
        if (LOG[i].op == 'n' && pool.v[LOG[i].v].track == 0) { assert(LOG[i].a == 62.0f); n++; }
    assert(n == 1);
    audio_play(0);
    block(256);
    assert(track_on(0) == 0);
}

/* a KIT on track 0, its three engines built by the main loop */
static void kit_setup(void)
{
    setup();
    P.sound[0].model = M_KIT;
    frames(12);
    NLOG = 0;
}

static void kit_choke_same_engine(void)
{
    kit_setup();
    audio_live_note(0, KIT_NOTE + 2, 1);   /* CH */
    block(256);
    int v = track_slot(0);
    assert(v >= 0 && pool.v[v].model == 23 && count('n', v) == 1);
    audio_live_note(0, KIT_NOTE + 3, 1);   /* OH cuts it */
    block(256);
    assert(track_slot(0) == v && count('n', v) == 2 && track_on(0) == 1);
    assert(pool.v[v].note == KIT_NOTE + 3 && pool.v[v].state == V_ON);
}

static void kit_three_engines_together(void)
{
    kit_setup();
    for (int p = 0; p < 3; p++) audio_live_note(0, KIT_NOTE + p, 1);   /* BD SN CH */
    block(256);
    assert(render_switches() == 0 && track_on(0) == 3);
    int seen = 0;
    for (int v = 0; v < Q_VOICES; v++)
        if (pool.v[v].state == V_ON) {
            assert(pool.v[v].model == 21 + pool.v[v].note - KIT_NOTE && count('n', v) == 1);
            seen |= 1 << (pool.v[v].model - 21);
        }
    assert(seen == 7);
}

static void kit_pad_patch(void)
{
    kit_setup();
    audio_live_note(0, 60, 1);             /* no pad: ignored */
    block(256);
    assert(track_on(0) == 0 && count('n', track_slot(0)) == 0);
    P.sound[0].level = 127;
    audio_live_note(0, KIT_NOTE + 4, 1);   /* RIM: SN, tune 12, harm 100, timb 100, decay 10, level 90 */
    block(256);
    int v = track_slot(0);
    assert(v >= 0 && pool.v[v].model == 22);
    assert(patch[v][0] == 100 / 127.0f && patch[v][1] == 100 / 127.0f && patch[v][2] == 10 / 127.0f);
    assert(patch[v][3] == 0.5f && patch[v][4] == 0.0f);
    for (int i = 0; i < NLOG; i++) if (LOG[i].op == 'n') assert(LOG[i].a == 48.0f + 12.0f);
    assert(count('n', v) == 1);
    block(256);
    int32_t g = (int32_t)(0.01f * (float)(v + 1) * (90 / 127.0f * 0.25f) * 8388607.0f);
    assert(LR[0] - g <= 1 && g - LR[0] <= 1);
}

static void kit_engine_change_while_sounding(void)
{
    kit_setup();
    audio_live_note(0, KIT_NOTE, 1);       /* BD */
    block(256);
    int v = track_slot(0);
    assert(v >= 0 && pool.v[v].model == 21);
    P.kit[0][0].engine = PAD_SN;
    for (int k = 0; k < 4; k++) frames(1);
    audio_live_note(0, KIT_NOTE, 1);
    block(256);
    block(256);
    int w = -1;
    for (int u = 0; u < Q_VOICES; u++)
        if (pool.v[u].state == V_ON && pool.v[u].model == 22) w = u;
    assert(w >= 0 && w != v && pool.v[w].note == KIT_NOTE);
    audio_live_note(0, KIT_NOTE, 0);
    audio_live_note(0, KIT_NOTE, 0);
    block(256);
    assert(track_on(0) == 0);
    int n = 0, f = 0;
    for (int i = 0; i < NLOG; i++) { n += LOG[i].op == 'n'; f += LOG[i].op == 'f'; }
    assert(n == 2 && f == 2);
}

static void kit_prepares_its_engines(void)
{
    setup();
    P.sound[0].model = M_KIT;
    P.pat[0][0].steps[0].flags = SF_ON;
    P.pat[0][0].steps[0].note[0] = KIT_NOTE;
    P.pat[0][0].steps[0].note[1] = KIT_NOTE + 1;
    P.pat[0][0].steps[0].note[2] = KIT_NOTE + 2;
    frames(12);
    int prepared[3] = {0, 0, 0};
    for (int i = 0; i < NLOG; i++)
        if (LOG[i].op == 'e' && !LOG[i].render && LOG[i].a >= 21.0f && LOG[i].a <= 23.0f) prepared[(int)LOG[i].a - 21]++;
    assert(prepared[0] == 1 && prepared[1] == 1 && prepared[2] == 1 && render_switches() == 0 && preps() == 0);
    for (int v = 0; v < Q_VOICES; v++) assert(pool.v[v].model != M_KIT);
    NLOG = 0;
    audio_play(1);
    block(256);
    assert(render_switches() == 0 && track_on(0) == 3);
}

/* a slide from BD to SN: no glide, SN struck, BD's voice released though its note-off is gone */
static void kit_legato_strikes(void)
{
    kit_setup();
    P.pat[0][0].steps[0].flags = SF_ON | SF_SLIDE;
    P.pat[0][0].steps[0].note[0] = KIT_NOTE;
    P.pat[0][0].steps[1].flags = SF_ON;
    P.pat[0][0].steps[1].note[0] = KIT_NOTE + 1;
    audio_play(1);
    until(0, spf16(120) + 256);
    int g = 0, n = 0;
    for (int i = 0; i < NLOG; i++) { g += LOG[i].op == 'g'; n += LOG[i].op == 'n'; }
    assert(g == 0 && n == 2 && track_on(0) == 1);
    int v = track_slot(0);
    for (int u = 0; u < Q_VOICES; u++)
        if (pool.v[u].state == V_ON) v = u;
    assert(pool.v[v].model == 22 && pool.v[v].note == KIT_NOTE + 1);
    audio_play(0);
    block(256);
    assert(track_on(0) == 0);
}

/* a synth note-on waiting for an engine when its track turns KIT (model written before the render takes
 * CMD_MODEL): no pad, dropped; and back, a pad's note-on waiting when the track turns synth plays as a synth note */
static void kit_switch_while_waiting(void)
{
    for (int back = 0; back < 2; back++) {
        setup();
        P.sound[0].model = back ? M_KIT : 20;
        P.sound[1].model = 20;
        audio_live_note(1, 60, 1);
        audio_live_note(0, back ? KIT_NOTE : 60, 1);   /* waits behind track 1's switch */
        block(256);
        assert(n_later == 1 && later[0].track == 0);
        P.sound[0].model = back ? 8 : M_KIT;
        audio_track_model(0);
        NLOG = 0;
        block(256);
        for (int i = 0; i < NLOG; i++) if (LOG[i].op == 'e') assert(LOG[i].a <= 23.0f);
        for (int v = 0; v < Q_VOICES; v++) assert(vpad[v] < Q_PADS && pool.v[v].model <= 23);
        assert(n_later == 0 && track_on(0) == 0);
        int n = 0;
        for (int i = 0; i < NLOG; i++) n += LOG[i].op == 'n';
        assert(n == back);              /* back: started, then faded by the model change */
    }
}

int main(void)
{
    splits_block_at_events();
    cores_get_their_voices();
    render_without_cpu2();
    core2_dropped_after_adding();
    no_empty_core2_job();
    play_while_playing_restarts();
    full_queue_keeps_note_off();
    model_change_fades_then_frees();
    level_master_and_clip();
    load_fields();
    free_voices_skipped();
    live_notes_reach_voices();
    slot_engine_follows_track();
    panic_silences();
    boot_prepared_play_switches_nothing();
    model_change_while_playing_one_switch_per_block();
    switches_spread_over_blocks();
    prep_hand_off();
    full_queue_keeps_the_hand_off();
    mono_model_change_keeps_its_fade();
    lock_reaches_its_voice();
    lock_ends_with_note();
    legato_glides_without_retrigger();
    legato_beside_a_live_note();
    legato_while_tied_note_waits();
    click_on_beats();
    count_in_then_play();
    overdub_records_step();
    overdub_early_at_pattern_change();
    step_note_not_recorded();
    stop_flushes_held_rec();
    scene_reported_once();
    kit_choke_same_engine();
    kit_three_engines_together();
    kit_pad_patch();
    kit_engine_change_while_sounding();
    kit_prepares_its_engines();
    kit_legato_strikes();
    kit_switch_while_waiting();
    puts("audio_test ok");
    return 0;
}
