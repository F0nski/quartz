#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "gen.c"
#include "seq.c"
#include "voices.c"
#include "mod.c"
#include "dsp/pq.h"
#include "dsp/qfx.h"

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
static float chr[PQ_VOICES];          /* the last pq_set_char of each voice */
void pq_set_char(int v, float c) { chr[v] = c; }
void pq_glide_to(int v, float note) { logc('g', v, note, 0); }
void pq_note_on(int v, float note, float vel) { (void)vel; logc('n', v, note, 0); }
void pq_note_off(int v) { logc('f', v, 0, 0); }
void pq_render(int v, float *out, unsigned n)
{
    logc('r', v, 0, n);
    for (unsigned i = 0; i < n; i++) out[i] += amp * (float)(v + 1);
}
int pq_silent(int v) { return silent[v]; }

/* doubles: the track effects, the last block's arguments kept; l is the buses' sum, r half of it */
static int qfx_inits, qfx_blocks;
static qfx_track QTR[QFX_TRACKS];
static qfx_master QM;
static int QDUCK;
static float QBUS[QFX_TRACKS][QFX_BLOCK];
void qfx_init(void) { qfx_inits++; }
static int qfx_reverbs, reverb_core, reverb_blocks;   /* calls; the core of the last; qfx_blocks then */
void qfx_reverb(void) { qfx_reverbs++; reverb_core = on_core2; reverb_blocks = qfx_blocks; }
void qfx_block(float *const bus[QFX_TRACKS], const qfx_track tr[QFX_TRACKS], const qfx_master *m,
               int duck_at, float *l, float *r, unsigned n)
{
    qfx_blocks++;
    memcpy(QTR, tr, sizeof QTR);
    QM = *m;
    QDUCK = duck_at;
    for (int t = 0; t < QFX_TRACKS; t++) memcpy(QBUS[t], bus[t], n * sizeof *QBUS[t]);
    for (unsigned i = 0; i < n; i++) {
        float x = 0;
        for (int t = 0; t < QFX_TRACKS; t++) x += bus[t][i];
        l[i] = x;
        r[i] = 0.5f * x;
    }
}

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
    qfx_inits = 0;
    audio_setup(&P);
    assert(qfx_inits == 1);
    NLOG = 0;
}

static void block(uint32_t n) { in_render = 1; app_render(LR, n, 4096); in_render = 0; }

/* what the UI does: the model written, then its command */
static void set_model(int t, int m) { P.sound[t].model = (uint8_t)m; audio_track_model(t); }

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

/* every voice on tracks 0 and 1 (POLY 3), track 0's 60 the oldest: a note on track 2, with none of its own sounding,
 * takes another track's */
static void six_notes_two_tracks(void)
{
    P.sound[0].poly = P.sound[1].poly = 3;
    for (int k = 0; k < 3; k++) {
        audio_live_note(0, 60 + 2 * k, 1);
        audio_live_note(1, 60 + 2 * k, 1);
    }
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

/* a job for core 2 per segment with voices there, else only the block's first, for the reverb */
static void core2_jobs(void)
{
    setup();
    block(256);
    audio_live_note(0, 60, 1);
    block(256);
    assert(audio_load().active == 1 && cpu2_runs == 2);
    six_notes();
    block(256);
    assert(cpu2_runs == 3);
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
    int32_t full = (int32_t)(0.01f * 0.25f * 8388607.0f), half = (int32_t)(0.5f * 0.01f * 0.25f * 8388607.0f);
    assert(LR[0] == full && LR[510] == full && LR[511] == half);
    app_render(LR, 256, 2048);      /* MASTER halved: ramped across the block */
    assert(LR[0] < full && LR[0] > full - full / 128);
    for (int i = 1; i < 256; i++) assert(LR[2 * i] <= LR[2 * i - 2]);
    assert(LR[510] - full / 2 <= 1 && full / 2 - LR[510] <= 1);
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
    /* a block: core 2's job 1 step of ticks, core 1 6 (7 less 1 waiting), qfx 1 of them; 173 x 256 x 544 ticks of budget */
    assert(l.core2_pct == 173u * 10000u / (173u * 256u * 544u / 100u));
    assert(l.core1_pct == 6u * 173u * 10000u / (173u * 256u * 544u / 100u));
    assert(l.fx_pct == 173u * 10000u / (173u * 256u * 544u / 100u));
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

static void audio_lfo_phase_none(void)
{
    setup();
    block(256);
    assert(audio_lfo_phase(0) == -1);
}

static void audio_lfo_phase_voice(void)
{
    setup();
    audio_live_note(0, 60, 1);
    block(256);
    assert(audio_lfo_phase(0) >= 0);
}

static void audio_arp_held(void)
{
    uint8_t held[ARP_HELD]; int snd;
    setup();
    P.sound[0].amode = AM_UP;
    audio_live_note(0, 64, 1);
    audio_live_note(0, 60, 1);
    for (int i = 0; i < 4; i++) block(256);
    assert(audio_arp(0, held, &snd) == 2);
    assert(held[0] == 64 && held[1] == 60);
    assert(snd == 60 || snd == 64);
}

static void audio_arp_off(void)
{
    uint8_t held[ARP_HELD]; int snd = 0;
    setup();
    block(256);
    assert(audio_arp(0, held, &snd) == 0 && snd == -1);
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
    set_model(0, 8);
    set_model(1, 13);
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
        set_model(t, 20);
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
        for (int t = 0; t < Q_TRACKS; t++) set_model(t, 20);
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
    set_model(0, 20);
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
    set_model(0, 20);
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
    set_model(1, 21);                   /* the preparation goes on */
    frames(4);
    int n = 0;
    for (int v = 0; v < Q_VOICES; v++) n += pool.v[v].model == 21 && pool.v[v].state == V_FREE;
    assert(n == 1 && preps() == 0);
}

/* a slot freed by a model-change fade never saw its note-off: the next note's attack still starts from 0 */
static void reused_slot_attack_starts_at_zero(void)
{
    setup();
    P.sound[0].atk = 80;
    audio_live_note(0, 60, 1);
    P.sound[1].poly = P.sound[2].poly = 2;   /* the other slots busy: the faded one is the only one free */
    audio_live_note(1, 60, 1);
    audio_live_note(1, 62, 1);
    audio_live_note(2, 60, 1);
    audio_live_note(2, 62, 1);
    audio_live_note(3, 60, 1);
    block(256);
    int v0 = -1;
    for (int v = 0; v < Q_VOICES; v++) if (pool.v[v].track == 0 && pool.v[v].state == V_ON) v0 = v;
    assert(v0 >= 0);
    pq_mod m;                                          /* pq_render is a double here: the envelope is advanced by hand */
    for (int i = 0; i < 3000; i++) mod_voice_block(v0, &m);
    assert(MV[v0].stage == E_DEC && MV[v0].env > 0.5f);
    audio_track_model(0);
    block(256);
    assert(pool.v[v0].state == V_FREE);
    audio_live_note(0, 64, 1);
    block(24);
    int v1 = -1;
    for (int v = 0; v < Q_VOICES; v++) if (pool.v[v].track == 0 && pool.v[v].state == V_ON) v1 = v;
    assert(v1 == v0 && MV[v1].stage == E_ATK && MV[v1].env < 0.1f);
}

static void mono_model_change_keeps_its_fade(void)
{
    setup();
    set_model(1, 20);
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

/* a note-on taking another track's sounding voice: that voice fades out from its level, then the note strikes on it */
static void stolen_voice_fades_first(void)
{
    setup();
    six_notes_two_tracks();
    block(256);
    int v0 = -1;
    for (int v = 0; v < Q_VOICES; v++) if (pool.v[v].track == 0 && pool.v[v].note == 60) v0 = v;
    assert(v0 >= 0);
    float full = QBUS[0][255];
    audio_live_note(2, 64, 1);
    NLOG = 0;
    block(32);                          /* half its fade */
    assert(count('n', v0) == 0 && pool.v[v0].track == 0);
    assert(QBUS[0][0] == full && QBUS[0][31] < QBUS[0][0]);
    float mid = QBUS[0][31];
    block(32);                          /* the rest: silent, freed */
    assert(count('n', v0) == 0 && pool.v[v0].state == V_FREE);
    assert(QBUS[0][0] < mid && QBUS[0][31] < QBUS[0][0]);
    float end = QBUS[0][31];
    block(32);
    assert(count('n', v0) == 1 && pool.v[v0].track == 2);
    assert(QBUS[0][0] < end);           /* track 0's other note alone */
    assert(count('e', v0) == 1);        /* its engine built anew: what it held of the old note does not come back */
}

/* the voices taken from a sounding note are counted (the CPU probe's), once each, and apart those cutting another
 * track's held note; a model change's fades are not */
static void steals_counted(void)
{
    setup();
    uint32_t n0 = audio_load().steals, c0 = audio_load().cuts;
    six_notes_two_tracks();
    block(256);
    audio_live_note(2, 64, 1);
    block(32);
    block(32);
    block(32);
    assert(audio_load().steals == n0 + 1 && audio_load().cuts == c0 + 1);   /* track 0's held 60 */
    audio_track_model(1);
    block(256);
    assert(audio_load().steals == n0 + 1);
}

/* a KIT's pad taking the voice its other engine still sounds on: that one fades out first too */
static void kit_pad_fades_its_other_engine(void)
{
    setup();
    set_model(0, M_KIT);
    frames(6);
    audio_live_note(0, KIT_NOTE + 1, 1);   /* SN */
    audio_live_note(0, KIT_NOTE + 1, 0);
    P.sound[1].poly = P.sound[2].poly = 2;
    audio_live_note(1, 60, 1);
    audio_live_note(1, 62, 1);
    audio_live_note(2, 60, 1);
    audio_live_note(2, 62, 1);
    audio_live_note(3, 60, 1);
    block(256);
    block(256);                            /* one engine switch a block: the last note waited for its own */
    int sn = -1;
    for (int v = 0; v < Q_VOICES; v++) if (pool.v[v].track == 0) sn = v;
    assert(sn >= 0 && pool.v[sn].state == V_RELEASED && pool.v[sn].model == 22);
    for (int v = 0; v < Q_VOICES; v++) assert(pool.v[v].state != V_FREE);
    audio_live_note(0, KIT_NOTE, 1);       /* BD: no slot on its engine, none free */
    NLOG = 0;
    block(32);
    assert(count('n', sn) == 0 && fading[sn]);
    block(32);
    block(32);
    assert(count('n', sn) == 1 && pool.v[sn].model == 21);
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

/* the step's locks reach its voice's mod settings; the next step, without them, gets the track's */
static void locks_to_mod(int pad)
{
    P.sound[0].cutoff = 90;
    P.sound[0].envamt = 7;
    P.pat[0][0].steps[0].flags = P.pat[0][0].steps[1].flags = SF_ON;
    if (pad) P.pat[0][0].steps[0].note[0] = P.pat[0][0].steps[1].note[0] = KIT_NOTE + 4;
    lock_set(&P.pat[0][0], 0, LK_CUT, 10);
    lock_set(&P.pat[0][0], 0, LK_ENVAMT, (uint8_t)-20);
    P.tempo = 120;
    audio_play(1);
    block(256);
    int v = track_slot(0);
    assert(v >= 0 && MV[v].p.cutoff == 10 && MV[v].p.envamt == -20);
    until(256, spf16(120) + 256);
    v = track_slot(0);
    assert(audio_pos(0) == 1 && v >= 0 && MV[v].p.cutoff == 90 && MV[v].p.envamt == 7);
}

static void voice_takes_new_locks(void)
{
    setup();
    locks_to_mod(0);
}

static void play_restarts_lfo_clock(void)
{
    setup();
    for (int i = 0; i < 4; i++) block(256);
    assert(RPH[40] > RHZ[28] * 1024 / Q_SR * 0.99f);
    audio_play(1);
    block(100);
    assert(RPH[40] > RHZ[28] * 99 / Q_SR && RPH[40] < RHZ[28] * 101 / Q_SR);
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

/* a note pressed on a generator pattern is not held for REC: turned STEP before its release, nothing is written */
static void generator_note_not_recorded(void)
{
    setup();
    P.pat[0][0].src = SRC_GRIDS;
    audio_rec(1);
    audio_play(1);
    until(0, 4 * spf16(120));
    audio_live_note(0, 60, 1);
    block(256);
    P.pat[0][0].src = SRC_STEP;
    audio_live_note(0, 60, 0);
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
    set_model(0, 20);
    P.sound[0].poly = 2;
    for (int t = 1; t < Q_TRACKS; t++) set_model(t, 12 + t);
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

/* every voice busy, the KIT holding SN: its HH takes the KIT's own SN (a steal), not a chord's held note (a cut) */
static void kit_takes_its_own_voice_first(void)
{
    setup();
    set_model(0, M_KIT);
    P.sound[1].poly = P.sound[2].poly = 3;
    frames(12);
    for (int k = 0; k < 3; k++) audio_live_note(1, 60 + 2 * k, 1);
    for (int k = 0; k < 2; k++) audio_live_note(2, 48 + 2 * k, 1);
    block(256);
    audio_live_note(0, KIT_NOTE + 1, 1);   /* SN: the sixth voice */
    for (int i = 0; i < 4; i++) block(256);
    int sn = track_slot(0);
    assert(sn >= 0 && pool.v[sn].model == 22);
    uint32_t c0 = audio_load().cuts;
    audio_live_note(0, KIT_NOTE + 2, 1);   /* CH */
    for (int i = 0; i < 4; i++) block(256);
    assert(pool.v[sn].track == 0 && pool.v[sn].model == 23 && audio_load().cuts == c0);
    assert(track_on(1) == 3 && track_on(2) == 2);
}

/* a KIT on track 0, its three engines built by the main loop */
static void kit_setup(void)
{
    setup();
    set_model(0, M_KIT);
    frames(12);
    NLOG = 0;
}

static void kit_voice_takes_filter_locks(void)
{
    kit_setup();
    locks_to_mod(1);
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

/* a pad's CHAR reaches its voice; a synth's voice is back at 0 */
static void kit_pad_char(void)
{
    kit_setup();
    audio_live_note(0, KIT_NOTE + 9, 1);   /* pad 10: the synthetic BD, CHAR 127 */
    block(256);
    int v = track_slot(0);
    assert(v >= 0 && chr[v] == 1.0f);
    audio_live_note(0, KIT_NOTE + 9, 0);
    block(256);
    P.kit[0][0].chr = 64;
    audio_live_note(0, KIT_NOTE, 1);       /* pad 1 */
    block(256);
    v = track_slot(0);
    assert(v >= 0 && chr[v] == 64 / 127.0f);
    audio_live_note(0, KIT_NOTE, 0);
    block(256);
    set_model(0, 21);                      /* BASS DRUM, the same engine: a synth voice */
    frames(12);
    for (int i = 0; i < Q_VOICES; i++) chr[i] = -1.0f;   /* unset: the synth's voice must be given 0 */
    audio_live_note(0, 48, 1);
    block(256);
    v = track_slot(0);
    assert(v >= 0 && chr[v] == 0.0f);
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
    int32_t g = (int32_t)(0.01f * (float)(v + 1) * (90 / 127.0f * 0.25f * MODEL_GAIN[22]) * 8388607.0f);
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
    set_model(0, M_KIT);
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

/* a note-on waiting for an engine when its track turns KIT or back: started on the model the render plays at the
 * block's start, then faded by CMD_MODEL; no pad read for a synth note */
static void kit_switch_while_waiting(void)
{
    for (int back = 0; back < 2; back++) {
        setup();
        set_model(0, back ? M_KIT : 20);
        set_model(1, 20);
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
        assert(n == 1);
    }
}

/* the model written, CMD_MODEL not yet taken (the render ran between the main loop's write and its push): the
 * render keeps the track's model until CMD_MODEL, its synth note-off ends its voice, its synth note-on plays */
static void render_model_changes_at_its_command(void)
{
    setup();
    audio_live_note(0, 60, 1);
    block(256);
    int v = track_slot(0);
    assert(v >= 0 && vpad[v] < 0);
    P.sound[0].model = M_KIT;
    NLOG = 0;
    audio_live_note(0, 60, 0);
    audio_live_note(0, 62, 1);
    block(256);
    assert(count('f', v) == 1);
    int n = 0;
    for (int i = 0; i < NLOG; i++) {
        n += LOG[i].op == 'n' && LOG[i].a == 62.0f;
        if (LOG[i].op == 'e') assert(LOG[i].a <= 23.0f);
    }
    assert(n == 1);
    for (int u = 0; u < Q_VOICES; u++) assert(vpad[u] < 0 && pool.v[u].model <= 23);
    audio_track_model(0);
    frames(12);
    NLOG = 0;
    audio_live_note(0, KIT_NOTE + 1, 1);   /* SN */
    block(256);
    v = track_slot(0);
    for (int u = 0; u < Q_VOICES; u++) if (pool.v[u].state == V_ON) v = u;
    assert(pool.v[v].state == V_ON && vpad[v] == 1 && pool.v[v].model == 22);
}

/* a pad sounds at 48 + its tune + the track's tune */
static void kit_pad_pitch_adds_track_tune(void)
{
    kit_setup();
    P.sound[0].tune = -5;
    P.kit[0][4].tune = 12;
    audio_live_note(0, KIT_NOTE + 4, 1);
    block(256);
    int n = 0;
    for (int i = 0; i < NLOG; i++) if (LOG[i].op == 'n') { assert(LOG[i].a == 48.0f + 12.0f - 5.0f); n++; }
    assert(n == 1);
}

/* the note-ons (frame, pitch) of the next n frames, rendered a frame at a time */
static int note_ons(uint32_t n, int *at, float *pitch, int max)
{
    int k = 0;
    for (uint32_t f = 0; f < n; f++) {
        NLOG = 0;                   /* (the log is short) */
        block(1);
        for (int i = 0; i < NLOG; i++)
            if (LOG[i].op == 'n' && k < max) { at[k] = (int)f; pitch[k++] = LOG[i].a; }
    }
    return k;
}

/* track 0's step 0: a chord C E G over its whole length, the arp at 1/32 (2757 frames at 120) */
static void chord_step(void)
{
    q_step *st = &P.pat[0][0].steps[0];
    st->flags = SF_ON;
    st->len = 8;
    st->note[0] = 60; st->note[1] = 64; st->note[2] = 67;
    P.sound[0].poly = 4;
    P.sound[0].adiv = DIV_32;
}

static void arp_plays_step_chord(void)
{
    int at[16], dat[16];
    float pc[16], dpc[16];
    setup();
    chord_step();
    audio_play(1);
    assert(note_ons(5000, dat, dpc, 16) == 3);      /* ARP OFF: the chord, direct */
    setup();
    chord_step();
    P.sound[0].amode = AM_UP;
    audio_play(1);
    assert(note_ons(5000, at, pc, 16) == 2);        /* one note per arp step */
    assert(at[0] == dat[0] && pc[0] == 60.0f && at[1] >= 2756 && at[1] <= 2758 && pc[1] == 64.0f);
}

/* step 2 starts in the middle of a 256-frame block: its MODE OFF lock switches the arp there, the chord of step 1
 * (still held at that frame) is not struck again */
static void arp_mode_lock_off(void)
{
    int at[16];
    float pc[16];
    setup();
    chord_step();
    for (int i = 1; i < 3; i++) P.pat[0][0].steps[i] = P.pat[0][0].steps[0];
    P.sound[0].amode = AM_UP;
    lock_set(&P.pat[0][0], 2, LK_AMODE, AM_OFF);
    audio_play(1);
    note_ons(43 * 256, at, pc, 16);                 /* frames 0..11007 */
    NLOG = 0;
    block(256);                                     /* step 2 at frame ~11028, in this block */
    int n = 0;
    for (int i = 0; i < NLOG; i++) if (LOG[i].op == 'n') pc[n++] = LOG[i].a;
    assert(n == 4 && pc[1] == 60.0f && pc[2] == 64.0f && pc[3] == 67.0f);   /* (pc[0]: the arp's last note) */
}

static void arp_keyboard_latch(void)
{
    int at[16];
    float pc[16];
    setup();
    P.sound[0].amode = AM_UP;
    P.sound[0].adiv = DIV_32;
    audio_live_note(0, 60, 3);
    audio_live_note(0, 60, 0);                      /* the key released: the latch goes on */
    assert(note_ons(9000, at, pc, 16) >= 3 && pc[2] == 60.0f);
    audio_live_note(0, 62, 1);                      /* a key without latch ends it: only 62 */
    assert(note_ons(300, at, pc, 16) == 1 && pc[0] == 62.0f);   /* held 300 frames: one note, none of the latched 60 */
    audio_live_note(0, 62, 0);
    assert(note_ons(9000, at, pc, 16) == 0 && track_on(0) == 0);
}

static void arp_stop_and_kit_end_notes(void)
{
    int at[16];
    float pc[16];
    setup();
    chord_step();
    P.sound[0].amode = AM_UP;
    audio_live_note(0, 72, 3);
    audio_live_note(0, 72, 0);
    audio_play(1);
    note_ons(1000, at, pc, 16);
    audio_play(0);                                  /* the step's notes end, the latch goes on */
    note_ons(300, at, pc, 16);
    int n = note_ons(9000, at, pc, 16);
    assert(n >= 3);
    for (int i = 0; i < n; i++) assert(pc[i] == 72.0f);
    set_model(0, M_KIT);                            /* out of the arp: its notes end, no arp note-on after */
    block(256);
    assert(track_on(0) == 0);
    assert(note_ons(9000, at, pc, 16) == 0);
}

/* a key reads as step -1: a lock stored on step 255 does not reach it */
static void keyboard_note_takes_no_lock(void)
{
    setup();
    P.pat[0][0].locks[0] = (q_lock){ 255, LK_HARM, 127 };
    P.pat[0][0].nlocks = 1;
    audio_live_note(0, 60, 1);
    block(256);
    assert(track_slot(0) >= 0 && last_harm(track_slot(0)) == 64 / 127.0f);
}

static void arp_ignored_on_kit(void)
{
    kit_setup();
    P.sound[0].amode = AM_UP;
    audio_live_note(0, KIT_NOTE + 4, 1);
    audio_live_note(0, KIT_NOTE + 5, 1);
    block(256);
    int n = 0;
    for (int i = 0; i < NLOG; i++) n += LOG[i].op == 'n';
    assert(n == 2);
}

/* track t's voices into its bus: amp * (v + 1) at gain 0.25 each */
static float bus_of(int t)
{
    float x = 0;
    for (int v = 0; v < Q_VOICES; v++) if (pool.v[v].track == t && sounding(v)) x += 0.01f * (float)(v + 1) * 0.25f;
    return x;
}

static void voices_reach_their_track_bus(void)
{
    setup();
    P.sound[2].poly = 4;
    audio_live_note(0, 60, 1);
    for (int k = 0; k < 4; k++) audio_live_note(2, 60 + k, 1);
    block(256);
    block(256);
    assert(track_slot(0) == 0);
    int core2 = 0;
    for (int v = 3; v < Q_VOICES; v++) core2 |= pool.v[v].track == 2 && sounding(v);
    assert(core2);
    /* core 2 running, refused, or given up after adding: the same buses */
    for (int mode = 1; mode <= 3; mode++) {
        cpu2_mode = mode;
        block(256);
        for (int i = 0; i < 256; i++) {
            assert(QBUS[0][i] == bus_of(0) && QBUS[2][i] == bus_of(2));
            assert(QBUS[1][i] == 0 && QBUS[3][i] == 0);
        }
    }
}

static void level_moves_to_fx(void)
{
    setup();
    P.sound[0].level = 50;
    audio_live_note(0, 60, 1);
    block(256);
    block(256);
    assert(QBUS[0][0] == 0.01f * 0.25f && QBUS[0][255] == 0.01f * 0.25f);
    assert(QTR[0].level == 50);
    P.dtime = DIV_8;
    P.dfb = 33;
    block(256);
    uint32_t num, den;
    seq_step_ratio(P.tempo, DIV_8, &num, &den);
    assert(QM.dly == (float)num / (float)den && QM.dfb == 33 && QM.duck == P.duck);
}

/* track 0: DSEND 10, PAN 5; steps 1 and 2 on, step 1 locked to DSEND 90, PAN -20 */
static void fx_lock_setup(int micro)
{
    setup();
    P.sound[0].dsend = 10;
    P.sound[0].pan = 5;
    P.pat[0][0].steps[1].flags = P.pat[0][0].steps[2].flags = SF_ON;
    P.pat[0][0].steps[1].micro = (int8_t)micro;
    lock_set(&P.pat[0][0], 1, LK_DSEND, 90);
    lock_set(&P.pat[0][0], 1, LK_PAN, (uint8_t)-20);
    audio_play(1);
}

/* through step 2: a block from step `from` (its start) to its end within step 1 plays the lock */
static void fx_lock_run(int from)
{
    int seen1 = 0, seen2 = 0;
    for (int k = 0; k < 3 * 5516 / 256; k++) {
        int p = audio_pos(0);
        block(256);
        int locked = QTR[0].dsend == 90 && QTR[0].pan == -20;
        assert(locked || (QTR[0].dsend == 10 && QTR[0].pan == 5));
        if (p >= from && p <= 1 && audio_pos(0) == 1) { assert(locked); seen1 = 1; }
        if (p == 2) { assert(!locked); seen2 = 1; }
    }
    assert(seen1 && seen2);
}

static void fx_lock_on_its_step(void)
{
    fx_lock_setup(0);
    fx_lock_run(1);
}

static void fx_lock_negative_micro(void)
{
    fx_lock_setup(-11);
    until(0, spf16(120));           /* 11/24 of a step early: in step 0's second half */
    assert(audio_pos(0) == 0 && QTR[0].dsend == 90);
    fx_lock_run(0);
}

/* step 1 late by micro and swing, step 2 off: its note-on in the block where step 2 begins, the lock gone from
 * step 2 on */
static void fx_lock_positive_micro_clears(void)
{
    fx_lock_setup(11);
    P.swing = 75;
    P.pat[0][0].steps[2].flags = 0;
    block(32);                      /* the note-on in the block where step 2 begins */
    int edge = 0, seen2 = 0;
    for (int k = 0; k < 4 * 5516 / 256; k++) {
        int p = audio_pos(0), was = QTR[0].dsend == 90;
        block(256);
        if (!was && QTR[0].dsend == 90) edge = p == 1 && audio_pos(0) == 2;
        if (p >= 2) { assert(QTR[0].dsend == 10 && QTR[0].pan == 5); seen2 = 1; }
    }
    assert(edge && seen2);
}

/* step 1 (DSEND 90) ratchet 4, late by micro: its last hit in step 2, locked to DSEND 50 */
static void fx_lock_ratchet_swing_not_rearmed(void)
{
    fx_lock_setup(11);
    P.swing = 75;
    P.pat[0][0].steps[1].flags |= 3 << 3;
    lock_set(&P.pat[0][0], 2, LK_DSEND, 50);
    int seen1 = 0, seen2 = 0, seen3 = 0;
    for (int k = 0; k < 4 * 5516 / 256; k++) {
        int p = audio_pos(0);
        block(256);
        seen1 |= QTR[0].dsend == 90;
        if (p == 2) { assert(QTR[0].dsend == 50); seen2 = 1; }
        if (p >= 3) { assert(QTR[0].dsend == 10); seen3 = 1; }
    }
    assert(seen1 && seen2 && seen3);
}

static void stop_clears_fx_locks(void)
{
    fx_lock_setup(-11);
    while (QTR[0].dsend != 90) block(256);   /* the step's note-on: the lock not yet seen at a block's start */
    audio_play(0);
    audio_live_note(0, 60, 1);
    block(256);
    assert(QTR[0].dsend == 10 && QTR[0].pan == 5);
}

static void fx_lock_not_stuck_by_arp(void)
{
    setup();
    P.sound[0].dsend = 10;
    P.sound[0].amode = AM_UP;
    P.sound[0].adiv = DIV_32;
    P.pat[0][0].steps[1].flags = SF_ON;
    P.pat[0][0].steps[1].len = 16;  /* two steps: the arp plays it on through step 2 */
    lock_set(&P.pat[0][0], 1, LK_DSEND, 90);
    audio_play(1);
    int seen1 = 0, arp2 = 0;
    for (int k = 0; k < 4 * 5516 / 256; k++) {
        int p = audio_pos(0);
        NLOG = 0;
        block(256);
        if (p == 1 && audio_pos(0) == 1) { assert(QTR[0].dsend == 90); seen1 = 1; }
        if (p >= 2) {
            assert(QTR[0].dsend == 10);
            for (int v = 0; v < Q_VOICES; v++) arp2 += p == 2 && count('n', v);
        }
    }
    assert(seen1 && arp2);
}

static void kit_bd_sets_duck_at(void)
{
    for (int sn = 0; sn < 2; sn++) {
        kit_setup();
        int pad = 0;
        while (P.kit[0][pad].engine != (sn ? PAD_SN : PAD_BD)) pad++;
        P.tempo = 120;
        P.pat[0][0].div = DIV_32;
        P.pat[0][0].steps[1].flags = SF_ON;
        P.pat[0][0].steps[1].note[0] = (uint8_t)(KIT_NOTE + pad);
        uint32_t num, den;
        seq_step_ratio(120, DIV_32, &num, &den);
        uint32_t spf = (num + den - 1) / den, lead = spf - 100;   /* step 1 at offset 100 */
        audio_play(1);
        block(lead % 256);
        assert(QDUCK == -1);
        for (uint32_t k = 0; k < lead / 256; k++) { block(256); assert(QDUCK == -1); }
        block(256);
        assert(QDUCK == (sn ? -1 : 100));
        block(256);
        assert(QDUCK == -1);
    }
}

static void duck_follows_kit_membership(void)
{
    setup();
    set_model(1, M_KIT);
    block(256);
    for (int t = 0; t < Q_TRACKS; t++) assert(QTR[t].duck == (t != 1));
    set_model(1, 3);
    block(256);
    for (int t = 0; t < Q_TRACKS; t++) assert(QTR[t].duck == 1);
}

static void click_after_fx(void)
{
    setup();
    P.click = 1;
    audio_rec(1);
    audio_play(1);
    block(256);
    assert(LR[0] != 0 || LR[2] != 0);
    assert(LR[2] == LR[3]);         /* the click on both sides, unpanned */
    for (int t = 0; t < QFX_TRACKS; t++)
        for (int i = 0; i < 256; i++) assert(QBUS[t][i] == 0);
}

static void stereo_out(void)
{
    setup();
    audio_live_note(0, 60, 1);
    block(256);
    block(256);
    assert(LR[0] != 0 && LR[0] != LR[1]);
}


/* the reverb rides with the block's first job for core 2, before qfx_block; without core 2, on core 1 */
static void reverb_on_core2_once_per_block(void)
{
    static const int RUNS[4] = {1, 1, 1, 2};   /* mode 3: core 2 ran it, then it was dropped: core 1 runs it again */
    for (int mode = 0; mode < 4; mode++) {
        setup();
        cpu2_mode = mode;
        six_notes();
        qfx_reverbs = 0;
        int before = qfx_blocks;
        block(256);
        assert(qfx_reverbs == RUNS[mode]);
        assert(reverb_core == (mode == 1));   /* the last run's core */
        assert(reverb_blocks == before);      /* before this block's qfx_block */
        qfx_reverbs = 0;
        block(256);                           /* no voice on core 2: still a job, for the reverb */
        assert(qfx_reverbs == RUNS[mode]);
    }
    cpu2_mode = 1;
}

static void held_punch_reaches_qfx(void)
{
    setup();
    audio_punch(0x0021);
    block(256);
    assert(QM.punch == 0x0021);
    audio_punch(0);
    block(256);
    assert(QM.punch == 0);
}

/* through 4 steps of track 1, its step 2 locked: the punch-ins seen on the blocks starting in each step */
static void punch_run(uint16_t want, int on_held)
{
    int seen = 0;
    for (int k = 0; k < 4 * 5516 / 256; k++) {
        int p = audio_pos(1);
        block(256);
        if (p == 2) { assert(QM.punch == (want | on_held)); seen = 1; }
        else assert(QM.punch == on_held);
    }
    assert(seen || !want);
}

static void recorded_punch_on_its_step(void)
{
    setup();
    lock_set(&P.pat[1][0], 2, LK_PUNCH_HI, 0x04);   /* an empty step */
    audio_play(1);
    punch_run(0x0400, 0);
    audio_play(0);
}

static void muted_track_no_punch(void)
{
    setup();
    P.mute = 2;
    lock_set(&P.pat[1][0], 2, LK_PUNCH_HI, 0x04);
    audio_play(1);
    punch_run(0, 0);
    audio_play(0);
}

static void held_and_recorded_or(void)
{
    setup();
    lock_set(&P.pat[1][0], 2, LK_PUNCH_HI, 0x04);
    audio_punch(1);
    audio_play(1);
    punch_run(0x0400, 1);
    audio_play(0);
    audio_punch(0);
}

static void recorded_punch_follows_pattern(void)
{
    setup();
    for (int k = 0; k < 16; k++) lock_set(&P.pat[1][0], k, LK_PUNCH_LO, 0x01);
    audio_play(1);
    block(256);                             /* the play's block: no step planned at its start yet */
    block(256);
    assert(QM.punch == 1);
    P.pattern[1] = 1;                       /* taken at the wrap */
    for (int k = 0; k < 20 * 5516 / 256; k++) {
        block(256);
        if (audio_pattern(1) == 1 && audio_pos(1) >= 1) assert(QM.punch == 0);
    }
    assert(audio_pattern(1) == 1 && QM.punch == 0);
    audio_play(0);
}

static void free_phase_when_stopped(void)
{
    setup();
    audio_punch(1);
    block(256);
    assert(QM.bpos == 0.0f);
    assert(fabsf(QM.fpb - 22058.8f) <= 0.5f);
    block(256);
    assert(QM.bpos == 256.0f);
    block(256);
    assert(QM.bpos == 512.0f);
    audio_punch(0);
    block(256);
    audio_punch(1);
    block(256);
    assert(QM.bpos == 0.0f);                /* from the press again */
    audio_punch(0);
}

/* a STOP with a punch-in held: the free bar carries on from where the playing bar was, not from a stale place */
static void free_phase_carries_on_stop(void)
{
    setup();
    audio_punch(1);
    block(256);                             /* stopped: the free bar from the press */
    audio_play(1);
    for (int i = 0; i < 20; i++) block(256);
    float at = QM.bpos;
    audio_play(0);
    block(256);
    assert(fabsf(QM.bpos - (at + 256.0f)) <= 1.0f);
    audio_punch(0);
}

/* the scenes' note counter: note-ons that sound, per track; a deferred note-on counts in the block it sounds */
static void hits_count_note_ons(void)
{
    uint32_t h[Q_TRACKS];
    setup();
    for (int t = 0; t < Q_TRACKS; t++) h[t] = audio_hits(t);
    audio_live_note(1, 64, 1);
    block(256);
    for (int t = 0; t < Q_TRACKS; t++) assert(audio_hits(t) - h[t] == (t == 1));
    audio_live_note(1, 65, 1);
    audio_live_note(1, 67, 1);
    block(256);
    assert(audio_hits(1) - h[1] == 3);
    audio_live_note(1, 64, 0);
    block(256);
    assert(audio_hits(1) - h[1] == 3);

    setup();
    for (int t = 0; t < Q_TRACKS; t++) set_model(t, 20);
    uint32_t sum0 = 0, sum = 0;
    for (int t = 0; t < Q_TRACKS; t++) sum0 += audio_hits(t);
    six_notes();
    block(256);                         /* one engine switch per block: one note sounds */
    for (int t = 0; t < Q_TRACKS; t++) sum += audio_hits(t);
    assert(sum - sum0 == 1);
}

/* a KIT's note-ons per pad engine, taken at the note-on */
static void kit_hits_per_engine(void)
{
    uint32_t h[2][3];
    setup();
    set_model(1, M_KIT);
    P.kit[1][2].engine = PAD_SN;
    P.kit[1][0].engine = PAD_BD;
    for (int t = 0; t < 2; t++)
        for (int e = 0; e < 3; e++) h[t][e] = audio_kit_hits(t, e);
    audio_live_note(1, KIT_NOTE + 2, 1);
    block(256);
    assert(audio_kit_hits(1, PAD_SN) - h[1][PAD_SN] == 1);
    assert(audio_kit_hits(1, PAD_BD) == h[1][PAD_BD] && audio_kit_hits(1, PAD_HH) == h[1][PAD_HH]);
    for (int e = 0; e < 3; e++) assert(audio_kit_hits(0, e) == h[0][e]);
    audio_live_note(1, KIT_NOTE + 2, 0);
    audio_live_note(1, KIT_NOTE, 1);
    block(256);
    assert(audio_kit_hits(1, PAD_BD) - h[1][PAD_BD] == 1);
    audio_live_note(1, KIT_NOTE, 0);
    audio_live_note(1, KIT_NOTE + 16, 1);
    block(256);
    assert(audio_kit_hits(1, PAD_BD) - h[1][PAD_BD] == 1 && audio_kit_hits(1, PAD_SN) - h[1][PAD_SN] == 1);
    assert(audio_kit_hits(1, PAD_HH) == h[1][PAD_HH]);
}

/* the MARBLES scene's notes: decided a step ahead, with the ms until they sound; not a STEP track's */
static void marbles_notes_ahead(void)
{
    uint8_t out[16];
    uint32_t in[16], s = 0, s1 = 0;
    int got = 0, ahead = 0;
    setup();
    P.pat[0][0].src = SRC_MARBLES;
    P.pat[0][0].marbles.tout = 1;
    for (int k = 0; k < 16; k++) P.pat[1][0].steps[k].flags = SF_ON;
    audio_play(1);
    for (int b = 0; b < 400; b++) {
        frames(1);
        int n = audio_notes(0, &s, out, in, 16);
        for (int i = 0; i < n; i++, got++) {
            assert(out[i] >= 1 && out[i] <= 127);
            assert(in[i] <= 130);       /* a step at 1/16, 120 BPM: 125 ms */
            ahead += in[i] >= 100;
        }
    }
    assert(got >= 8 && ahead > got / 2);   /* T OUT 1 does not play every step */
    assert(audio_notes(1, &s1, out, in, 16) == 0);
}

/* stop then play: the reader keeps its place, no note comes back that was not decided after the restart */
static void notes_survive_restart(void)
{
    uint8_t out[16];
    uint32_t in[16], s = 0;
    setup();
    P.pat[0][0].src = SRC_MARBLES;
    P.pat[0][0].marbles.tout = 3;
    audio_play(1);
    for (int b = 0; b < 300; b++) { frames(1); while (audio_notes(0, &s, out, in, 16)) {} }
    audio_play(0);
    frames(2);
    audio_play(1);
    for (int b = 0; b < 3; b++) {
        frames(1);
        int n = audio_notes(0, &s, out, in, 16);
        assert(n <= 3);                             /* at most a step's worth so far */
        for (int i = 0; i < n; i++) assert(out[i] >= 1 && out[i] <= 127);
    }
}

/* a voice is scaled by its model's level: ARCADE's square below CREST, a SNARE above */
static void model_gain_matches_levels(void)
{
    float lvl[3];
    static const int M[3] = { 7, 8, 22 };
    for (int k = 0; k < 3; k++) {
        setup();
        set_model(0, M[k]);
        for (int i = 0; i < 4; i++) block(256);
        audio_live_note(0, 60, 1);
        block(256);
        block(256);
        lvl[k] = QBUS[0][0] / (float)(track_slot(0) + 1);   /* the double renders 0.01 * (slot + 1) */
    }
    assert(lvl[1] == 0.01f * 0.25f && lvl[0] < 0.6f * lvl[1] && lvl[2] > 2.0f * lvl[1]);
}

/* POLY 1, a 4-step note at step 0 and the same pitch at step 2: the first note's note-off, moved to step 2, does
 * not cut the second, which holds to step 6 */
static void same_pitch_long_notes_not_cut(void)
{
    setup();
    q_pattern *pt = &P.pat[0][0];
    pt->steps[0].flags = pt->steps[2].flags = SF_ON;
    pt->steps[0].len = pt->steps[2].len = 32;
    uint32_t num, den;
    seq_step_ratio(120, DIV_16, &num, &den);
    uint32_t spf = num / den, n = 0;
    audio_play(1);
    while (n < spf * 5 + spf / 2) { block(256); n += 256; }   /* step 5: past the first note's own end */
    assert(track_on(0) == 1);
    while (n < spf * 6 + spf / 2) { block(256); n += 256; }   /* step 6: the second's end */
    assert(track_on(0) == 0);
}

/* a key held on the track playing its pattern, POLY 1: the steps do not take its voice */
static void held_key_survives_the_pattern(void)
{
    setup();
    for (int k = 0; k < 16; k++) P.pat[0][0].steps[k].flags = SF_ON;
    audio_play(1);
    block(256);
    audio_live_note(0, 72, 1);
    for (int i = 0; i < 100; i++) block(256);      /* 1.6 s: three steps and more */
    int held = 0;
    for (int v = 0; v < Q_VOICES; v++) held += pool.v[v].state == V_ON && pool.v[v].note == 72 && pool.v[v].live;
    assert(held == 1 && audio_hits(0) > 3);
    audio_live_note(0, 72, 0);
    block(256);
    for (int v = 0; v < Q_VOICES; v++) assert(!(pool.v[v].state == V_ON && pool.v[v].note == 72));
}

int main(void)
{
    reverb_on_core2_once_per_block();
    splits_block_at_events();
    cores_get_their_voices();
    render_without_cpu2();
    core2_dropped_after_adding();
    core2_jobs();
    play_while_playing_restarts();
    full_queue_keeps_note_off();
    model_change_fades_then_frees();
    reused_slot_attack_starts_at_zero();
    level_master_and_clip();
    load_fields();
    free_voices_skipped();
    live_notes_reach_voices();
    audio_lfo_phase_none(); audio_lfo_phase_voice(); audio_arp_held(); audio_arp_off();
    slot_engine_follows_track();
    panic_silences();
    boot_prepared_play_switches_nothing();
    model_change_while_playing_one_switch_per_block();
    switches_spread_over_blocks();
    prep_hand_off();
    full_queue_keeps_the_hand_off();
    mono_model_change_keeps_its_fade();
    stolen_voice_fades_first();
    steals_counted();
    kit_takes_its_own_voice_first();
    kit_pad_fades_its_other_engine();
    lock_reaches_its_voice();
    lock_ends_with_note();
    voice_takes_new_locks();
    play_restarts_lfo_clock();
    legato_glides_without_retrigger();
    legato_beside_a_live_note();
    legato_while_tied_note_waits();
    click_on_beats();
    count_in_then_play();
    overdub_records_step();
    overdub_early_at_pattern_change();
    step_note_not_recorded();
    generator_note_not_recorded();
    stop_flushes_held_rec();
    scene_reported_once();
    kit_voice_takes_filter_locks();
    kit_choke_same_engine();
    kit_three_engines_together();
    kit_pad_patch();
    kit_pad_char();
    kit_engine_change_while_sounding();
    kit_prepares_its_engines();
    kit_legato_strikes();
    kit_switch_while_waiting();
    render_model_changes_at_its_command();
    kit_pad_pitch_adds_track_tune();
    arp_plays_step_chord();
    arp_mode_lock_off();
    arp_keyboard_latch();
    arp_stop_and_kit_end_notes();
    arp_ignored_on_kit();
    keyboard_note_takes_no_lock();
    voices_reach_their_track_bus();
    level_moves_to_fx();
    fx_lock_on_its_step();
    fx_lock_negative_micro();
    fx_lock_positive_micro_clears();
    fx_lock_ratchet_swing_not_rearmed();
    stop_clears_fx_locks();
    fx_lock_not_stuck_by_arp();
    kit_bd_sets_duck_at();
    duck_follows_kit_membership();
    click_after_fx();
    stereo_out();
    held_punch_reaches_qfx();
    recorded_punch_on_its_step();
    muted_track_no_punch();
    held_and_recorded_or();
    recorded_punch_follows_pattern();
    free_phase_when_stopped();
    held_key_survives_the_pattern();
    same_pitch_long_notes_not_cut();
    model_gain_matches_levels();
    free_phase_carries_on_stop();
    hits_count_note_ons();
    kit_hits_per_engine();
    marbles_notes_ahead();
    notes_survive_restart();
    puts("audio_test ok");
    return 0;
}
