/* The render: per block, the main loop's commands, the sequencer's events, then the voices segment by segment
 * between the events' offsets. Voices 0..2 render on core 1, 3..5 on core 2 (or on core 1 when it is off or gives a
 * job up). pq's calls for a voice never overlap its render: core 1 applies a segment's events before handing over. */
#include "app.h"
#include "audio.h"
#include "seq.h"
#include "voices.h"
#include "dsp/pq.h"

#define CMD_N 64                    /* a power of two */
#define EV_MAX 40                   /* 4 tracks x 2 steps x on+off per block at 240 BPM in 1/32, with room */
#define BUDGET_TICKS(n) ((n) * 544u)   /* n frames at 44117.6 Hz, in 24 MHz ticks */
#define LOAD_TICKS (24u * 1000000u)    /* the load is averaged over this much budget: 1 s */

enum { CMD_NOTE, CMD_PLAY, CMD_MODEL };
typedef struct { uint8_t kind, track, note, on; } q_cmd;

static q_project *prj;
static q_seq seq;
static q_voices pool;
static uint8_t slot_model[Q_VOICES];
static volatile int playing;
static volatile int muted;

/* main loop -> render, one producer and one consumer, both on core 1 (the main loop and the audio interrupt) */
static q_cmd cmd[CMD_N];
static volatile uint32_t cmd_w, cmd_r;

static float mix1[APP_BLOCK], mix2[APP_BLOCK], tmp1[APP_BLOCK], tmp2[APP_BLOCK];
static float gain[Q_VOICES];
static uint32_t seg_pos, seg_len;

static uint32_t sum1, sum2, sum_budget;
static volatile uint32_t pct1, pct2;

static void push(uint8_t kind, int track, int note, int on)
{
    uint32_t w = cmd_w;
    if (w - cmd_r == CMD_N)
        return;                     /* full: the UI cannot send 64 commands within a block */
    cmd[w % CMD_N] = (q_cmd){ kind, (uint8_t)track, (uint8_t)note, (uint8_t)on };
    __asm__ volatile("" ::: "memory");   /* the ring's publish barrier (fm1-base RING_PUBLISH) */
    cmd_w = w + 1;
}

void audio_setup(q_project *p)
{
    prj = p;
    __builtin_memset(&seq, 0, sizeof seq);
    voices_init(&pool);
    pq_init();
    __builtin_memset(slot_model, 8, sizeof slot_model);   /* pq_init's model */
    playing = 0;
    muted = 0;
    cmd_w = cmd_r = 0;
    sum1 = sum2 = sum_budget = pct1 = pct2 = 0;
    plat_cpu2_set(1);
}

void audio_play(int on)
{
    playing = on;
    push(CMD_PLAY, 0, 0, on);
}

void audio_live_note(int track, int note, int on) { push(CMD_NOTE, track, note, on); }
void audio_track_model(int track) { push(CMD_MODEL, track, 0, 0); }
void audio_mute(void) { muted = 1; }
int audio_playing(void) { return playing; }
int audio_pos(int track) { return seq_pos(&seq, track); }

q_load audio_load(void)
{
    q_load l = { pct1, pct2, plat_xruns(), 0, plat_cpu2_on(), plat_cpu2_stack() };
    for (int v = 0; v < Q_VOICES; v++)
        l.active += pool.v[v].state != V_FREE;
    return l;
}

static void set_patch(int v, const q_sound *s)
{
    pq_set_patch(v, s->harm / 127.0f, s->timb / 127.0f, s->morph / 127.0f, s->decay / 127.0f, s->glide / 127.0f);
}

static void note_on(int track, int note, int vel)
{
    const q_sound *s = &prj->sound[track];
    int v = voices_note_on(&pool, track, note, s->poly);
    if (slot_model[v] != s->model) {
        pq_set_engine(v, s->model);
        slot_model[v] = s->model;
    }
    set_patch(v, s);
    pq_note_on(v, (float)(note + s->tune), vel / 127.0f);
}

static void note_off(int track, int note)
{
    int v = voices_note_off(&pool, track, note);
    if (v >= 0)
        pq_note_off(v);
}

static void apply(const q_event *e)
{
    if (e->on)
        note_on(e->track, e->note, e->vel);
    else
        note_off(e->track, e->note);
}

static void commands(void)
{
    while (cmd_r != cmd_w) {
        __asm__ volatile("" ::: "memory");   /* the command after its index */
        q_cmd c = cmd[cmd_r % CMD_N];
        cmd_r++;
        if (c.kind == CMD_NOTE) {
            if (c.on)
                note_on(c.track, c.note, 100);
            else
                note_off(c.track, c.note);
        } else if (c.kind == CMD_MODEL) {
            voices_release_track(&pool, c.track);
        } else if (c.on) {
            seq_start(&seq);
        } else {
            q_event off[Q_TRACKS];
            int n;
            seq_stop(&seq, off, &n);
            for (int i = 0; i < n; i++)
                apply(&off[i]);
        }
    }
}

/* voices [from, from + 3) of the segment, each at its track's level, into out */
static void render_voices(int from, float *out, float *tmp)
{
    for (int v = from; v < from + 3; v++) {
        if (pool.v[v].state == V_FREE)
            continue;
        __builtin_memset(tmp, 0, seg_len * sizeof *tmp);
        pq_render(v, tmp, seg_len);
        for (uint32_t i = 0; i < seg_len; i++)
            out[seg_pos + i] += tmp[i] * gain[v];
    }
}

static void job2(uint32_t arg)
{
    uint32_t t0 = plat_ticks24();
    (void)arg;
    render_voices(3, mix2, tmp2);
    sum2 += plat_ticks24() - t0;
}

static void render_segment(void)
{
    uint32_t tw;
    int two, r = 0;
    for (int v = 0; v < Q_VOICES; v++)
        if (pool.v[v].state != V_FREE)
            gain[v] = prj->sound[pool.v[v].track].level / 127.0f * 0.25f;
    two = plat_cpu2_run(job2, 0);
    render_voices(0, mix1, tmp1);
    if (two) {
        tw = plat_ticks24();
        r = plat_cpu2_wait();
        sum1 -= plat_ticks24() - tw;   /* core 1 waiting is not its work */
    }
    if (!two || r < 0)
        render_voices(3, mix2, tmp2);  /* the second core's share, on this one */
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    q_event ev[EV_MAX];
    uint32_t t0 = plat_ticks24(), pos = 0;
    int ne, ei = 0;
    float out_gain = 8388607.0f * (float)master_q12 / 4096.0f;

    if (muted) {                    /* before an update rewrites the flash: nothing runs, nothing sounds */
        __builtin_memset(lr, 0, 2 * n * sizeof *lr);
        return;
    }
    commands();
    ne = seq_block(&seq, prj, n, ev, EV_MAX);
    for (int v = 0; v < Q_VOICES; v++)
        if (pool.v[v].state != V_FREE)
            set_patch(v, &prj->sound[pool.v[v].track]);   /* the knobs reach the held notes */
    __builtin_memset(mix1, 0, n * sizeof *mix1);
    __builtin_memset(mix2, 0, n * sizeof *mix2);
    while (pos < n) {
        while (ei < ne && ev[ei].offset <= pos)
            apply(&ev[ei++]);
        seg_pos = pos;
        seg_len = (ei < ne ? ev[ei].offset : n) - pos;
        render_segment();
        pos += seg_len;
    }
    for (int v = 0; v < Q_VOICES; v++)
        if (pool.v[v].state == V_RELEASED && pq_silent(v))
            voices_free(&pool, v);
    for (uint32_t i = 0; i < n; i++) {
        float x = (mix1[i] + mix2[i]) * out_gain;
        int32_t s = x > 8388607.0f ? 8388607 : x < -8388608.0f ? -8388608 : (int32_t)x;
        lr[2 * i] = s;
        lr[2 * i + 1] = s;
    }

    sum1 += plat_ticks24() - t0;
    sum_budget += BUDGET_TICKS(n);
    if (sum_budget >= LOAD_TICKS) {
        pct1 = sum1 / (sum_budget / 100u);   /* no 64-bit multiply: fm1-base bsp/docs/CPU.md */
        pct2 = sum2 / (sum_budget / 100u);
        sum1 = sum2 = sum_budget = 0;
    }
}
