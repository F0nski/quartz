/* The render: per block, the main loop's commands, the sequencer's events, then the voices segment by segment
 * between the events' offsets. Voices 0..2 render on core 1, 3..5 on core 2 (or on core 1 when it is off or gives a
 * job up). pq's calls for a voice never overlap its render: core 1 applies a segment's events before handing over. */
#include "app.h"
#include "audio.h"
#include "seq.h"
#include "voices.h"
#include "dsp/pq.h"

#define CMD_N 64                    /* a power of two */
#define OFF_ROOM 32                 /* kept for note-offs, one per held key (27), and the preparation's request (1): the
                                     * render drains the ring each block. The last place is the hand-back's (push) */
#define EV_MAX 64                   /* more stays in the sequencer's queues for the next block */
#define LATER_N 16                  /* note-ons waiting for an engine switch: beyond, a note-on is dropped */
#define PREP_WAIT (-2)              /* prep_slot: asked, the render has not answered */
#define BUDGET_TICKS(n) ((n) * 544u)   /* n frames at 44117.6 Hz, in 24 MHz ticks */
#define LOAD_TICKS (24u * 1000000u)    /* the load is averaged over this much budget: 1 s */

enum { CMD_NOTE, CMD_PLAY, CMD_MODEL, CMD_PREP_REQ, CMD_PREP_DONE };   /* PREP_REQ: note = model; DONE: track = slot, note = model */
typedef struct { uint8_t kind, track, note, on; } q_cmd;

static q_project *prj;
static q_seq seq;
static q_voices pool;
static uint8_t fading[Q_VOICES];    /* a model change: faded out over this block, then freed */
/* one engine switch per block in the render: the note-ons needing another wait here, in order */
typedef struct { uint8_t track, note, vel; } q_later;
static q_later later[LATER_N];
static int n_later, switched;
static volatile int prep_slot;      /* render -> main loop: the V_PREP slot, -1 none, PREP_WAIT */
static int prep_model;              /* the main loop's own: the model it asked for */
static volatile int playing;
static volatile int muted;

/* main loop -> render, one producer and one consumer, both on core 1 (the main loop and the audio interrupt) */
static q_cmd cmd[CMD_N];
static volatile uint32_t cmd_w, cmd_r;

static float mix1[APP_BLOCK], mix2[APP_BLOCK], tmp1[APP_BLOCK], tmp2[APP_BLOCK];
static float gain[Q_VOICES];
static float inv_n;                 /* 1 / the block's frames: the fades */
static float out_gain;              /* MASTER's gain at the end of the last block: ramped from there */
static uint32_t seg_pos, seg_len;

static uint32_t sum1, sum2, sum_budget;
static volatile uint32_t pct1, pct2;

/* 0: full. The UI cannot send 32 commands within a block: a note-off still fits. The last place is kept for the
 * hand-back, of which the ring holds one at most (the next comes after the render answered the request behind it):
 * never refused, no slot stays V_PREP */
static int push(uint8_t kind, int track, int note, int on)
{
    uint32_t w = cmd_w, used = w - cmd_r;
    if (used == CMD_N || (used == CMD_N - 1 && kind != CMD_PREP_DONE) ||
        (used >= CMD_N - OFF_ROOM && !(kind == CMD_NOTE && !on) && kind < CMD_PREP_REQ))
        return 0;
    cmd[w % CMD_N] = (q_cmd){ kind, (uint8_t)track, (uint8_t)note, (uint8_t)on };
    __asm__ volatile("" ::: "memory");   /* the ring's publish barrier (fm1-base RING_PUBLISH) */
    cmd_w = w + 1;
    return 1;
}

void audio_setup(q_project *p)
{
    prj = p;
    __builtin_memset(&seq, 0, sizeof seq);
    voices_init(&pool);
    pq_init();
    for (int v = 0; v < Q_VOICES; v++)
        pool.v[v].model = 8;        /* pq_init's model */
    __builtin_memset(fading, 0, sizeof fading);
    n_later = 0;
    prep_slot = -1;
    out_gain = 0;
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
void audio_unmute(void) { muted = 0; }
int audio_playing(void) { return playing; }
int audio_pos(int track) { return seq_pos(&seq, track); }

static int sounding(int v) { return pool.v[v].state == V_ON || pool.v[v].state == V_RELEASED; }

q_load audio_load(void)
{
    q_load l = { pct1, pct2, plat_xruns(), 0, plat_cpu2_on(), plat_cpu2_stack() };
    for (int v = 0; v < Q_VOICES; v++)
        l.active += (uint32_t)sounding(v);
    return l;
}

/* A free slot whose engine is spare (no track wants that many of its model) when `model` lacks one, else -1.
 * The tracks want poly slots each on their model; the slots holding it, free or playing, count, V_PREP not.
 * Called by the render and, reading the pool as it stands, by the main loop: the render's answer is the one. */
static int spare_slot(int model)
{
    static const uint8_t ORDER[Q_VOICES] = {0, 3, 1, 4, 2, 5};   /* the cores in turn */
    int8_t want[24] = {0}, have[24] = {0};
    for (int t = 0; t < Q_TRACKS; t++)
        want[prj->sound[t].model] += (int8_t)prj->sound[t].poly;
    for (int v = 0; v < Q_VOICES; v++)
        if (pool.v[v].state != V_PREP && pool.v[v].model >= 0)
            have[pool.v[v].model]++;
    if (have[model] >= want[model])
        return -1;
    for (int k = 0; k < Q_VOICES; k++) {
        int v = ORDER[k], m = pool.v[v].model;
        if (pool.v[v].state == V_FREE && (m < 0 || have[m] > want[m]))
            return v;
    }
    return -1;
}

/* One engine per call, outside the render: the render marks a spare slot V_PREP (neither rendered nor touched
 * by it, nor by core 2, which renders only the slots it is handed), this builds the engine, the render frees it. */
void audio_prepare(void)
{
    int s = prep_slot;
    if (s == PREP_WAIT)
        return;
    if (s >= 0) {
        pq_set_engine(s, prep_model);
        push(CMD_PREP_DONE, s, prep_model, 0);
        prep_slot = -1;
    }
    for (int t = 0; t < Q_TRACKS; t++)
        if (spare_slot(prj->sound[t].model) >= 0) {
            prep_model = prj->sound[t].model;
            prep_slot = PREP_WAIT;  /* before the request: push publishes both */
            if (!push(CMD_PREP_REQ, 0, prep_model, 0))
                prep_slot = -1;     /* the ring full: asked again next frame */
            return;
        }
}

static void set_patch(int v, const q_sound *s)
{
    pq_set_patch(v, s->harm / 127.0f, s->timb / 127.0f, s->morph / 127.0f, s->decay / 127.0f, s->glide / 127.0f);
}

static uint32_t wanted(void)
{
    uint32_t w = 0;
    for (int t = 0; t < Q_TRACKS; t++)
        w |= 1u << prj->sound[t].model;
    return w;
}

static int needs_switch(int track)
{
    const q_sound *s = &prj->sound[track];
    return pool.v[voices_pick(&pool, track, s->poly, s->model, wanted())].model != s->model;
}

static void note_on(int track, int note, int vel)
{
    const q_sound *s = &prj->sound[track];
    int sw = needs_switch(track);
    int v = voices_note_on(&pool, track, note, s->poly, s->model, wanted());
    fading[v] = 0;                  /* (a fading slot taken: the new note at full level) */
    if (sw) {
        pq_set_engine(v, s->model);
        switched = 1;
    }
    set_patch(v, s);
    pq_note_on(v, (float)(note + s->tune), vel / 127.0f);
}

static int waits(int track)
{
    for (int i = 0; i < n_later; i++)
        if (later[i].track == track)
            return 1;
    return 0;
}

/* A note-on needing a switch once this block made one waits for the next block, and the track's next note-ons
 * wait behind it, in order. A note-off for a waiting note-on cancels it; the others go to the voices. */
static void event(int track, int note, int vel, int on)
{
    if (on) {
        if (waits(track) || (switched && needs_switch(track))) {
            if (n_later < LATER_N)
                later[n_later++] = (q_later){ (uint8_t)track, (uint8_t)note, (uint8_t)vel };
        } else {
            note_on(track, note, vel);
        }
        return;
    }
    for (int i = 0; i < n_later; i++)
        if (later[i].track == track && later[i].note == note) {
            for (n_later--; i < n_later; i++)
                later[i] = later[i + 1];
            return;
        }
    int v = voices_note_off(&pool, track, note);
    if (v >= 0)
        pq_note_off(v);
}

/* the waiting note-ons, at the block's start: in order, a track's stopping at its first that still waits */
static void start_later(void)
{
    int n = 0, held = 0;
    for (int i = 0; i < n_later; i++) {
        q_later e = later[i];
        if (!(held >> e.track & 1) && !(switched && needs_switch(e.track))) {
            note_on(e.track, e.note, e.vel);
        } else {
            held |= 1 << e.track;
            later[n++] = e;
        }
    }
    n_later = n;
}

static void apply(const q_event *e) { event(e->track, e->note, e->vel, e->on); }

static void commands(void)
{
    while (cmd_r != cmd_w) {
        __asm__ volatile("" ::: "memory");   /* the command after its index */
        q_cmd c = cmd[cmd_r % CMD_N];
        cmd_r++;
        if (c.kind == CMD_NOTE) {
            event(c.track, c.note, 100, c.on);
        } else if (c.kind == CMD_MODEL) {
            int n = 0;
            for (int v = 0; v < Q_VOICES; v++)   /* released, no longer V_ON: no note-off nor note-on lands on them */
                if (sounding(v) && pool.v[v].track == c.track) {
                    pool.v[v].state = V_RELEASED;
                    fading[v] = 1;
                }
            for (int i = 0; i < n_later; i++)    /* its waiting note-ons would have been faded out too */
                if (later[i].track != c.track)
                    later[n++] = later[i];
            n_later = n;
        } else if (c.kind == CMD_PREP_REQ) {
            int v = spare_slot(c.note);
            if (v >= 0) {
                pool.v[v].state = V_PREP;
                pool.v[v].model = -1;
            }
            __asm__ volatile("" ::: "memory");   /* the slot's state before the main loop reads its index */
            prep_slot = v;
        } else if (c.kind == CMD_PREP_DONE) {
            pool.v[c.track].state = V_FREE;
            pool.v[c.track].model = (int8_t)c.note;
        } else {
            static q_event off[Q_TRACKS * SEQ_QUEUE];   /* a start while playing: the sounding notes' offs first;
                                                         * static: 3 KB off the render's stack */
            int n;
            seq_stop(&seq, off, &n);
            for (int i = 0; i < n; i++)
                apply(&off[i]);
            if (c.on)
                seq_start(&seq, prj, 0);
        }
    }
}

/* voices [from, from + 3) of the segment, each at its track's level, into out */
static void render_voices(int from, float *out, float *tmp)
{
    for (int v = from; v < from + 3; v++) {
        if (!sounding(v))
            continue;
        float g = gain[v], dg = 0;
        if (fading[v]) {            /* down to 0 at the block's end */
            dg = -g * inv_n;
            g += dg * (float)seg_pos;
        }
        __builtin_memset(tmp, 0, seg_len * sizeof *tmp);
        pq_render(v, tmp, seg_len);
        for (uint32_t i = 0; i < seg_len; i++, g += dg)
            out[seg_pos + i] += tmp[i] * g;
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
    int two = 0, r = 0, need2 = 0;
    for (int v = 0; v < Q_VOICES; v++)
        if (sounding(v)) {
            gain[v] = prj->sound[pool.v[v].track].level / 127.0f * 0.25f;
            need2 |= v >= 3;
        }
    if (need2)                      /* no empty job for the second core */
        two = plat_cpu2_run(job2, 0);
    render_voices(0, mix1, tmp1);
    if (two) {
        tw = plat_ticks24();
        r = plat_cpu2_wait();
        sum1 -= plat_ticks24() - tw;   /* core 1 waiting is not its work */
    }
    if (r < 0)                      /* given up: what it added to the segment goes */
        __builtin_memset(mix2 + seg_pos, 0, seg_len * sizeof *mix2);
    if (!two || r < 0)
        render_voices(3, mix2, tmp2);  /* the second core's share, on this one */
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    q_event ev[EV_MAX];
    uint32_t t0 = plat_ticks24(), pos = 0;
    int ne, ei = 0;
    float target = 8388607.0f * (float)master_q12 / 4096.0f, g = out_gain, dg;

    if (muted) {                    /* before an update rewrites the flash: nothing runs, nothing sounds */
        __builtin_memset(lr, 0, 2 * n * sizeof *lr);
        out_gain = 0;               /* (unmuted: from silence) */
        return;
    }
    inv_n = 1.0f / (float)n;
    dg = (target - g) * inv_n;      /* MASTER ramped across the block: no zipper noise */
    switched = 0;
    start_later();
    commands();
    ne = seq_block(&seq, prj, n, ev, EV_MAX);
    for (int v = 0; v < Q_VOICES; v++)
        if (sounding(v))
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
        if (fading[v] || (pool.v[v].state == V_RELEASED && pq_silent(v))) {
            voices_free(&pool, v);
            fading[v] = 0;
        }
    for (uint32_t i = 0; i < n; i++) {
        g += dg;
        float x = (mix1[i] + mix2[i]) * g;
        int32_t s = x > 8388607.0f ? 8388607 : x < -8388608.0f ? -8388608 : (int32_t)x;
        lr[2 * i] = s;
        lr[2 * i + 1] = s;
    }
    out_gain = target;

    sum1 += plat_ticks24() - t0;
    sum_budget += BUDGET_TICKS(n);
    if (sum_budget >= LOAD_TICKS) {
        pct1 = sum1 / (sum_budget / 100u);   /* no 64-bit multiply: fm1-base bsp/docs/CPU.md */
        pct2 = sum2 / (sum_budget / 100u);
        sum1 = sum2 = sum_budget = 0;
    }
}
