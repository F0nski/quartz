#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "gen.c"
#include "seq.c"
#include "voices.c"
#include "mod.c"
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
    reused_slot_attack_starts_at_zero();
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
    puts("audio_test ok");
    return 0;
}
