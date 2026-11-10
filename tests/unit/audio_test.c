#include <assert.h>
#include <stdio.h>
#include <string.h>
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
typedef struct { char op; int v; float a; unsigned n; int core; } call;
static call LOG[4096];
static int NLOG;
static int silent[PQ_VOICES];
static float amp = 0.01f;           /* voice v renders amp * (v + 1) */
static void logc(char op, int v, float a, unsigned n) { LOG[NLOG++] = (call){ op, v, a, n, on_core2 }; }
void pq_init(void) { logc('i', -1, 0, 0); }
void pq_set_engine(int v, int model) { logc('e', v, (float)model, 0); }
void pq_set_patch(int v, float h, float t, float m, float d, float g) { (void)h; (void)t; (void)m; (void)d; (void)g; logc('p', v, 0, 0); }
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
    cpu2_mode = 1;
    cpu2_runs = 0;
    tick_step = 10;
    amp = 0.01f;
    memset(silent, 0, sizeof silent);
    audio_setup(&P);
    NLOG = 0;
}

static void block(uint32_t n) { app_render(LR, n, 4096); }

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
    P.pat[0].div = DIV_32;
    P.pat[0].steps[0].on = P.pat[0].steps[1].on = 1;
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
    P.pat[0].div = DIV_1;           /* one step: 2 s */
    P.pat[0].steps[0].on = 1;
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
    puts("audio_test ok");
    return 0;
}
