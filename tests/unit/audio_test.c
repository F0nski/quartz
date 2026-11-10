#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "seq.c"
#include "voices.c"
#include "dsp/pq.h"

/* doubles: the second core and the clock */
static int cpu2_mode;               /* 0: run refused, 1: runs the job, 2: takes it, then wait drops it */
static int on_core2;
static uint32_t ticks;
int plat_cpu2_set(int on) { (void)on; return 0; }
int plat_cpu2_on(void) { return cpu2_mode != 0; }
int plat_cpu2_run(void (*fn)(uint32_t), uint32_t arg)
{
    if (cpu2_mode == 1) { on_core2 = 1; fn(arg); on_core2 = 0; }
    return cpu2_mode != 0;
}
int plat_cpu2_wait(void) { return cpu2_mode == 2 ? -1 : 0; }
uint32_t plat_ticks24(void) { return ticks += 10; }
uint32_t plat_xruns(void) { return 0; }

/* doubles: the voices, every call logged */
typedef struct { char op; int v; float a; unsigned n; int core; } call;
static call LOG[4096];
static int NLOG;
static int silent[PQ_VOICES];
static void logc(char op, int v, float a, unsigned n) { LOG[NLOG++] = (call){ op, v, a, n, on_core2 }; }
void pq_init(void) { logc('i', -1, 0, 0); }
void pq_set_engine(int v, int model) { logc('e', v, (float)model, 0); }
void pq_set_patch(int v, float h, float t, float m, float d, float g) { (void)h; (void)t; (void)m; (void)d; (void)g; logc('p', v, 0, 0); }
void pq_note_on(int v, float note, float vel) { (void)vel; logc('n', v, note, 0); }
void pq_note_off(int v) { logc('f', v, 0, 0); }
void pq_render(int v, float *out, unsigned n)
{
    logc('r', v, 0, n);
    for (unsigned i = 0; i < n; i++) out[i] += 0.01f * (float)(v + 1);
}
int pq_silent(int v) { return silent[v]; }

#include "audio.c"

static q_project P;
static int32_t LR[2 * APP_BLOCK];

static void setup(void)
{
    project_defaults(&P);
    cpu2_mode = 1;
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
    uint32_t spf = (uint32_t)ceil(seq_step_frames(120, DIV_32)), lead = spf - 100;
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

int main(void)
{
    splits_block_at_events();
    cores_get_their_voices();
    render_without_cpu2();
    free_voices_skipped();
    live_notes_reach_voices();
    slot_engine_follows_track();
    puts("audio_test ok");
    return 0;
}
