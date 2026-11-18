/* The render: per block, the main loop's commands, the sequencer's events, then the voices segment by segment
 * between the events' offsets, each into its track's bus, the track effects over the buses, the metronome's click
 * over them. Voices 0..2 render on core 1, 3..5 on core 2 (or on core 1 when it is off or gives a
 * job up). pq's calls for a voice never overlap its render: core 1 applies a segment's events before handing over. */
#include "app.h"
#include "audio.h"
#include "seq.h"
#include "voices.h"
#include "mod.h"
#include "dsp/pq.h"
#include "dsp/qfx.h"

#define CMD_N 64                    /* a power of two */
#define OFF_ROOM 32                 /* kept for note-offs, one per held key (27), and the preparation's request (1): the
                                     * render drains the ring each block. The last place is the hand-back's (push) */
#define EV_MAX 64                   /* more stays in the sequencer's queues for the next block */
#define EV_ALL (CMD_N + Q_TRACKS * SEQ_QUEUE + EV_MAX + 2 * Q_TRACKS * ARP_OUT)   /* the main loop's keys and the
                                     * stop's note-offs, the sequencer's, each track's arp's (two runs at most) */
#define LATER_N 16                  /* note-ons waiting for an engine switch: beyond, a note-on is dropped */
#define STEAL_FADE 64               /* frames: a voice taken from another track fades out over them (1.5 ms) first */
#define HELD_N 8                    /* live notes held, followed for the overdub */
#define REC_N 32                    /* recorded notes waiting for the main loop, a power of two */
#define PREP_WAIT (-2)              /* prep_slot: asked, the render has not answered */
#define BUDGET_TICKS(n) ((n) * 544u)   /* n frames at 44117.6 Hz, in 24 MHz ticks */
#define LOAD_TICKS (24u * 1000000u)    /* the load is averaged over this much budget: 1 s */
#define CLICK_AMP 0.3f
#define CLICK_DECAY 0.99849003f        /* per frame: a 15 ms time constant */
#define CLICK_END (CLICK_AMP * 1e-4f)  /* ~140 ms on */
#define KIT_MODEL 21                /* PAD_BD's Plaits engine; PAD_SN's and PAD_HH's follow */
#define FX_N (LK_LEVEL - LK_I1 + 1) /* the effect locks: LK_I1..LK_LEVEL */

_Static_assert(APP_BLOCK <= QFX_BLOCK && APP_BLOCK % 2 == 0 && Q_TRACKS == QFX_TRACKS, "qfx_block's contract");

/* SCENE: note = scene; PREP_REQ: note = model; DONE: track = slot, note = model */
enum { CMD_NOTE, CMD_PLAY, CMD_MODEL, CMD_SCENE, CMD_PREP_REQ, CMD_PREP_DONE, CMD_DRAW, CMD_LISTEN };
typedef struct { uint8_t kind, track, note, on; } q_cmd;

static q_project *prj;
/* the tracks' models as the render plays them, taken at CMD_MODEL: the main loop writes sound[t].model whenever,
 * and a voice must keep the model it started on until its track's CMD_MODEL. The main loop's preparation reads
 * sound[t].model: its request follows the CMD_MODEL in the ring, so the render's answer sees the same model */
static uint8_t rmodel[Q_TRACKS];
static q_seq seq;
static q_voices pool;
static uint8_t fading[Q_VOICES];    /* a model change, a stolen voice: faded out, then freed at the block's end */
static float fade[Q_VOICES], fstep[Q_VOICES];   /* its level left, 1 down to 0, and its fall a frame */
/* one engine switch per block in the render: the note-ons needing another wait here, in order */
typedef struct { uint8_t track, note, vel, pat, accent; int8_t step; } q_later;   /* step -1: the keyboard's */
static q_later later[LATER_N];
/* a track not on M_KIT plays through its arp (MODE OFF: a pass-through). apat/astep: the step of the last note-on
 * it was given, whose locks are its MODE, OCT, DIV and GATE; -1 for the keyboard's */
static q_arp arp[Q_TRACKS];
static uint8_t apat[Q_TRACKS];
static int8_t astep[Q_TRACKS];
static q_event ev[EV_ALL];          /* the block's events: off the render's stack */
static int ncmd;                    /* the commands' events (keys, the stop's note-offs), at the start of ev: they play with the block, in order */
static int n_later, switched;
static volatile int prep_slot;      /* render -> main loop: the V_PREP slot, -1 none, PREP_WAIT */
static int prep_model;              /* the main loop's own: the model it asked for */
static volatile int playing;
static volatile int muted;
static volatile int recording;      /* audio_rec */
/* the step's p-locks each voice plays (bit LK_* of mask), taken at its note-on */
static uint32_t lk_mask[Q_VOICES];
static uint8_t lk_val[Q_VOICES][LK_N];
static int8_t vpad[Q_VOICES];       /* the KIT pad each voice plays, -1 none: taken at its note-on */
/* each track's effect locks (bit k of mask: LK_I1 + k), taken at a step's note-on (fxl_step). They hold until a
 * block starts past that step once a block has started on it (fxl_seen): a note early by its micro keeps them.
 * fxl_pos: the step at the block's start */
static uint8_t fxl_val[Q_TRACKS][FX_N];
static uint8_t fxl_mask[Q_TRACKS], fxl_step[Q_TRACKS], fxl_seen[Q_TRACKS];
static int fxl_pos[Q_TRACKS];
static int duck_at;                 /* the block's first KIT BD note-on, -1 none */
static volatile uint16_t punch_held;  /* audio_punch's: the main loop writes, the render reads */
static uint16_t punch_last;         /* the last block's punch-ins */
static float punch_free;            /* stopped: the bar's position since the punch-ins began, or since the STOP */
static float clk_c, clk_s, clk_cw, clk_sw, clk_env;   /* the click: a phasor turning by (clk_cw, clk_sw) a frame */
/* live notes held while recording; now: the render's clock, since audio_setup */
typedef struct { uint8_t on, track, note, pat, step; int8_t micro; uint32_t t0; } q_held;
static q_held held[HELD_N];
static uint32_t now;

/* main loop -> render, one producer and one consumer, both on core 1 (the main loop and the audio interrupt) */
static q_cmd cmd[CMD_N];
static volatile uint32_t cmd_w, cmd_r;
/* render -> main loop, one producer and one consumer: the recorded notes; the scene started, one place (a
 * scene started while the last is unread is not reported) */
static q_rec rec[REC_N];
static volatile uint32_t rec_w, rec_r;
static volatile int scene_v;
static volatile uint32_t hits[Q_TRACKS];   /* note-ons sounded per track: the render counts, the scenes read */
static volatile uint32_t kit_hits[Q_TRACKS][3];   /* a KIT's, per pad engine at the note-on */
static volatile uint32_t scene_w, scene_r;

/* bus[0]: core 1's voices, bus[1]: core 2's (or core 1's in its place), each in its track's bus; added up at the
 * block's end. tmp1, tmp2 then take qfx's l and r */
static float bus[2][Q_TRACKS][APP_BLOCK], tmp1[APP_BLOCK], tmp2[APP_BLOCK];
static float clk[APP_BLOCK];        /* the click, after the effects */
static float gain[Q_VOICES];
static float inv_n;                 /* 1 / the block's frames: the fades */
static float out_gain;              /* MASTER's gain at the end of the last block: ramped from there */
static uint32_t seg_pos, seg_len;
static int rev_done;                /* this block's qfx_reverb has run */

static uint32_t sum1, sum2, sumfx, sum_budget;
static volatile uint32_t pct1, pct2, pctfx;
static volatile uint32_t nsteals, ncuts;   /* audio_load's steals and cuts */

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
    for (int t = 0; t < Q_TRACKS; t++)
        rmodel[t] = p->sound[t].model;
    __builtin_memset(&seq, 0, sizeof seq);
    voices_init(&pool);
    pq_init();
    mod_init();
    qfx_init();
    for (int t = 0; t < Q_TRACKS; t++) {
        arp_reset(&arp[t]);
        astep[t] = -1;
    }
    for (int v = 0; v < Q_VOICES; v++)
        pool.v[v].model = 8;        /* pq_init's model */
    __builtin_memset(fading, 0, sizeof fading);
    __builtin_memset(lk_mask, 0, sizeof lk_mask);
    __builtin_memset(fxl_mask, 0, sizeof fxl_mask);
    __builtin_memset(vpad, -1, sizeof vpad);
    __builtin_memset(held, 0, sizeof held);
    clk_env = 0;
    now = 0;
    recording = 0;
    rec_w = rec_r = scene_w = scene_r = 0;
    n_later = 0;
    prep_slot = -1;
    out_gain = 0;
    playing = 0;
    muted = 0;
    cmd_w = cmd_r = 0;
    sum1 = sum2 = sumfx = sum_budget = pct1 = pct2 = pctfx = 0;
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
void audio_life(int track, q_life_run *out) { __builtin_memcpy(out, (const void *)&seq.g[track].life, sizeof *out); }
uint32_t audio_lanes(int track) { return seq.g[track].lanes_n; }
void audio_chords(int track, q_chords_run *out) { __builtin_memcpy(out, (const void *)&seq.g[track].chords, sizeof *out); }
uint32_t audio_orbits(int track) { return seq.g[track].orbits_n; }
void audio_mimic(int track, q_mimic_run *out) { __builtin_memcpy(out, (const void *)&seq.g[track].mimic, sizeof *out); }
void audio_tonnetz(int track, q_tonnetz_run *out) { __builtin_memcpy(out, (const void *)&seq.g[track].tonnetz, sizeof *out); }
uint32_t audio_hits(int track) { return hits[track]; }
uint32_t audio_kit_hits(int track, int engine) { return kit_hits[track][engine]; }

int audio_notes(int track, uint32_t *seen, uint8_t *out, uint32_t *in_ms, int max)
{
    uint32_t at[16];
    int n = seq_gen_notes(&seq, track, seen, out, at, max < 16 ? max : 16);
    for (int i = 0; i < n; i++) {
        int32_t d = (int32_t)(at[i] - (uint32_t)seq.now);   /* frames from the next block: 750000/17 a second */
        in_ms[i] = d > 0 ? (uint32_t)d * 17u / 750u : 0u;
    }
    return n;
}
void audio_punch(uint16_t mask) { punch_held = mask; }
int audio_pattern(int track) { return seq_pattern(&seq, track); }
int audio_song_entry(void) { return seq_song_entry(&seq); }
void audio_rec(int on) { recording = on; }
void audio_scene(int scene) { push(CMD_SCENE, 0, scene, 0); }
void audio_marbles_draw(int track) { push(CMD_DRAW, track, 0, 0); }
void audio_mimic_listen(int track, int on) { push(CMD_LISTEN, track, on, 0); }

int audio_take_rec(q_rec *r)
{
    uint32_t i = rec_r;
    if (i == rec_w)
        return 0;
    __asm__ volatile("" ::: "memory");   /* the note after its index */
    *r = rec[i % REC_N];
    __asm__ volatile("" ::: "memory");   /* copied before the render may write its place again */
    rec_r = i + 1;
    return 1;
}

int audio_scene_started(void)
{
    if (scene_r == scene_w)
        return -1;
    int s = scene_v;                /* (volatile: read before the place is given back) */
    scene_r++;
    return s;
}

static int sounding(int v) { return pool.v[v].state == V_ON || pool.v[v].state == V_RELEASED; }

q_load audio_load(void)
{
    q_load l = { pct1, pct2, plat_xruns(), 0, plat_cpu2_on(), plat_cpu2_stack(), pctfx, nsteals, ncuts };
    for (int v = 0; v < Q_VOICES; v++)
        l.active += (uint32_t)sounding(v);
    return l;
}

/* the engines track t plays on as model m (bit e: engine e): a KIT's pads' engines, else m */
static uint32_t models(int t, int m)
{
    if (m != M_KIT)
        return 1u << m;
    uint32_t w = 0;
    for (int p = 0; p < Q_PADS; p++)
        w |= 1u << (KIT_MODEL + prj->kit[t][p].engine);
    return w;
}

/* a KIT has one voice per engine: a pad cuts the one sounding on its engine (CH cuts OH) */
static int note_poly(int t, int m) { return m == M_KIT ? 1 : prj->sound[t].poly; }

/* a KIT's note outside its pads: ignored */
static int no_pad(int t, int note)
{
    return rmodel[t] == M_KIT && (note < KIT_NOTE || note >= KIT_NOTE + Q_PADS);
}

static int note_model(int t, int note)
{
    return rmodel[t] == M_KIT ? KIT_MODEL + prj->kit[t][note - KIT_NOTE].engine : rmodel[t];
}

/* A free slot whose engine is spare (no track wants that many of its model) when `model` lacks one, else -1.
 * The tracks want poly slots each on their model; the slots holding it, free or playing, count, V_PREP not.
 * Called by the render and, reading the pool as it stands, by the main loop: the render's answer is the one.
 * mod: the tracks' models, the caller's view. */
static int spare_slot(int model, const uint8_t *mod)
{
    static const uint8_t ORDER[Q_VOICES] = {0, 3, 1, 4, 2, 5};   /* the cores in turn */
    int8_t want[24] = {0}, have[24] = {0};
    for (int t = 0; t < Q_TRACKS; t++) {
        uint32_t w = models(t, mod[t]);
        for (int m = 0; m < 24; m++)
            if (w >> m & 1)
                want[m] += (int8_t)note_poly(t, mod[t]);
    }
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
    uint8_t mod[Q_TRACKS];
    if (s == PREP_WAIT)
        return;
    if (s >= 0) {
        pq_set_engine(s, prep_model);
        push(CMD_PREP_DONE, s, prep_model, 0);
        prep_slot = -1;
    }
    for (int t = 0; t < Q_TRACKS; t++)
        mod[t] = prj->sound[t].model;
    for (int t = 0; t < Q_TRACKS; t++) {
        uint32_t w = models(t, mod[t]);
        for (int m = 0; m < 24; m++)
            if (w >> m & 1 && spare_slot(m, mod) >= 0) {
                prep_model = m;
                prep_slot = PREP_WAIT;  /* before the request: push publishes both */
                if (!push(CMD_PREP_REQ, 0, prep_model, 0))
                    prep_slot = -1;     /* the ring full: asked again next frame */
                return;
            }
    }
}

/* the voice's lock for q_sound field k, else the track's value */
static int param(int v, int k, int track_value) { return lk_mask[v] >> k & 1 ? lk_val[v][k] : track_value; }
static int tune(int v, const q_sound *s) { return lk_mask[v] >> LK_TUNE & 1 ? (int8_t)lk_val[v][LK_TUNE] : s->tune; }

static void take_locks(int v, int track, int pat, int step)
{
    const q_pattern *pt = &prj->pat[track][pat];
    lk_mask[v] = 0;
    if (step < 0)
        return;
    for (int i = 0; i < pt->nlocks; i++) {      /* one pass; the first lock of a (step, param) wins, as lock_find */
        const q_lock *l = &pt->locks[i];
        if (l->step != step || l->param >= LK_N || lk_mask[v] >> l->param & 1)
            continue;
        lk_mask[v] |= 1u << l->param;
        lk_val[v][l->param] = l->value;
    }
}

/* the voice's envelope, filter and LFO settings, locks applied; signed fields come back as int8_t */
static void vparam(int v, const q_sound *s, q_vparam *q)
{
    *q = (q_vparam){ (uint8_t)param(v, LK_ATK, s->atk), (uint8_t)param(v, LK_DEC, s->dec),
                     (uint8_t)param(v, LK_SUS, s->sus), (uint8_t)param(v, LK_REL, s->rel),
                     (uint8_t)param(v, LK_FTYPE, s->ftype), (uint8_t)param(v, LK_CUT, s->cutoff),
                     (uint8_t)param(v, LK_RES, s->res), (int8_t)param(v, LK_ENVAMT, (uint8_t)s->envamt),
                     (uint8_t)param(v, LK_SHAPE, s->lshape), s->ltrig, (uint8_t)param(v, LK_RATE, s->lrate),
                     (int8_t)param(v, LK_DEPTH, (uint8_t)s->ldepth), (uint8_t)param(v, LK_DEST, s->ldest) };
}

/* a KIT's voice: its pad's harm, timb and decay (its locks of those do not apply); the pad's decay is Plaits' drum decay (morph) */
static void set_patch(int v, const q_sound *s)
{
    if (vpad[v] >= 0) {
        const q_pad *pd = &prj->kit[pool.v[v].track][vpad[v]];
        pq_set_patch(v, pd->harm / 127.0f, pd->timb / 127.0f, pd->decay / 127.0f, 0.5f, 0);
        pq_set_char(v, pd->chr / 127.0f);
        return;
    }
    pq_set_patch(v, param(v, LK_HARM, s->harm) / 127.0f, param(v, LK_TIMB, s->timb) / 127.0f,
                 param(v, LK_MORPH, s->morph) / 127.0f, param(v, LK_DECAY, s->decay) / 127.0f,
                 param(v, LK_GLIDE, s->glide) / 127.0f);
    pq_set_char(v, 0.0f);
}

static uint32_t wanted(void)
{
    uint32_t w = 0;
    for (int t = 0; t < Q_TRACKS; t++)
        w |= models(t, rmodel[t]);
    return w;
}

/* the keyboard's note (step -1) has its own voices beside the sequencer's; a KIT's pad still cuts its engine's */
static int live(int track, int step) { return step < 0 && rmodel[track] != M_KIT; }

static int needs_switch(int track, int note, int step)
{
    int m = note_model(track, note);
    return pool.v[voices_pick(&pool, track, note_poly(track, rmodel[track]), m, wanted(), live(track, step))].model != m;
}

/* step -1: the keyboard's, no locks. A KIT's pad sounds at 48 + its tune */
static void note_on(int track, int note, int vel, int pat, int step, int accent)
{
    const q_sound *s = &prj->sound[track];
    q_vparam q;
    int m = note_model(track, note), sw = needs_switch(track, note, step), kit = rmodel[track] == M_KIT;
    int pv = voices_pick(&pool, track, note_poly(track, rmodel[track]), m, wanted(), live(track, step));
    int fresh = pool.v[pv].state == V_FREE || pool.v[pv].track != track || pool.v[pv].live != live(track, step);
    int v = voices_note_on(&pool, track, note, note_poly(track, rmodel[track]), m, wanted(), live(track, step));
    if (fresh) mod_slot_reset(v);   /* (a retrigger of the track's own voice attacks from its level) */
    if (sw) {
        pq_set_engine(v, m);
        switched = 1;
    }
    vpad[v] = (int8_t)(kit ? note - KIT_NOTE : -1);
    take_locks(v, track, pat, step);
    set_patch(v, s);
    vparam(v, s, &q);
    mod_set(v, &q, prj->tempo);
    mod_note_on(v, accent);
    pq_note_on(v, (float)(kit ? 48 + prj->kit[track][vpad[v]].tune + s->tune : note + tune(v, s)), vel / 127.0f);
    hits[track]++;
    if (kit)
        kit_hits[track][prj->kit[track][vpad[v]].engine]++;
}

static void fade_out(int v, float step)
{
    pool.v[v].state = V_RELEASED;   /* no note-off nor note-on lands on it */
    fading[v] = 1;
    fade[v] = 1;
    fstep[v] = step;
}

/* The slot the note-on would take sounds for another track, origin or engine (a KIT's), or fades: the note waits
 * for it to have faded out (a cut is a click). Starts that fade */
static int steals(int track, int note, int step)
{
    int m = note_model(track, note);
    int v = voices_pick(&pool, track, note_poly(track, rmodel[track]), m, wanted(), live(track, step));
    if (!sounding(v) || (!fading[v] && pool.v[v].track == track && pool.v[v].live == live(track, step) &&
                         pool.v[v].model == m))
        return 0;
    if (!fading[v]) {
        ncuts += pool.v[v].state == V_ON && pool.v[v].track != track;
        fade_out(v, 1.0f / STEAL_FADE);
        nsteals++;
    }
    return 1;
}

static int waits(int track)
{
    for (int i = 0; i < n_later; i++)
        if (later[i].track == track)
            return 1;
    return 0;
}

/* A note-on needing a switch once this block made one, or stealing a voice, waits for the next block, and the
 * track's next note-ons wait behind it, in order. A note-off for a waiting note-on cancels it; the others go to the voices. */
static void event(int track, int note, int vel, int on, int pat, int step, int accent)
{
    if (no_pad(track, note))
        return;
    if (on) {
        if (waits(track) || (switched && needs_switch(track, note, step)) || steals(track, note, step)) {
            if (n_later < LATER_N)
                later[n_later++] = (q_later){ (uint8_t)track, (uint8_t)note, (uint8_t)vel, (uint8_t)pat, (uint8_t)accent, (int8_t)step };
        } else {
            note_on(track, note, vel, pat, step, accent);
        }
        return;
    }
    for (int i = 0; i < n_later; i++)
        if (later[i].track == track && later[i].note == note && live(track, later[i].step) == live(track, step)) {
            for (n_later--; i < n_later; i++)
                later[i] = later[i + 1];
            return;
        }
    int v = voices_note_off(&pool, track, note, live(track, step));
    if (v >= 0) {
        mod_note_off(v);
        pq_note_off(v);
    }
}

/* the waiting note-ons, at the block's start: in order, a track's stopping at its first that still waits */
static void start_later(void)
{
    int n = 0, held = 0;
    for (int i = 0; i < n_later; i++) {
        q_later e = later[i];
        if (!(held >> e.track & 1) && !(switched && needs_switch(e.track, e.note, e.step)) &&
            !steals(e.track, e.note, e.step)) {
            note_on(e.track, e.note, e.vel, e.pat, e.step, e.accent);
        } else {
            held |= 1 << e.track;
            later[n++] = e;
        }
    }
    n_later = n;
}

static void click_start(int bar)
{
    clk_c = 1;
    clk_s = 0;
    clk_cw = bar ? 0.95970726f : 0.98987556f;   /* cos, sin of 2 kHz, 1 kHz a frame */
    clk_sw = bar ? 0.28100173f : 0.14193790f;
    clk_env = CLICK_AMP;
}

static void click_render(float *out, uint32_t n)
{
    for (uint32_t i = 0; i < n && clk_env > CLICK_END; i++) {
        float c = clk_c * clk_cw - clk_s * clk_sw;
        out[i] += clk_env * clk_s;
        clk_s = clk_s * clk_cw + clk_c * clk_sw;
        clk_c = c;
        clk_env *= CLICK_DECAY;
    }
}

/* A legato note-on replaces the tied note, whose note-off is gone: the tied note still waiting for an engine becomes
 * it; the tied note's voice takes its pitch (and its step's locks) without a new attack. 0: neither, a plain note-on */
static int legato(const q_event *e)
{
    for (int i = n_later - 1; i >= 0; i--)
        if (later[i].track == e->track && later[i].note == e->from) {
            later[i] = (q_later){ e->track, e->note, e->vel, e->pat, (uint8_t)(e->flags >> 1 & 1), (int8_t)e->step };
            return 1;
        }
    int v = voices_retarget(&pool, e->track, e->from, e->note);
    if (v < 0)
        return 0;
    const q_sound *s = &prj->sound[e->track];
    q_vparam q;
    take_locks(v, e->track, e->pat, e->step);
    set_patch(v, s);
    vparam(v, s, &q);
    mod_set(v, &q, prj->tempo);
    pq_glide_to(v, (float)(e->note + tune(v, s)));
    return 1;
}

/* a sequencer's step note-on takes the step's effect locks. Not the arp's: they carry a held note's step, which
 * would keep its locks past the step. Nor a ratchet's repeat: late by micro or swing, it can fall in the next step */
static void fx_take(const q_event *e)
{
    int t = e->track;
    const q_pattern *pt = &prj->pat[t][e->pat];
    if (e->flags & EV_REPEAT) return;
    fxl_mask[t] = 0;
    fxl_step[t] = e->step;
    fxl_seen[t] = fxl_pos[t] == e->step;    /* late by its micro, in the block where the next step begins */
    for (int k = 0; k < FX_N; k++) {
        int i = lock_find(pt, e->step, LK_I1 + k);
        if (i >= 0) {
            fxl_mask[t] |= (uint8_t)(1u << k);
            fxl_val[t][k] = pt->locks[i].value;
        }
    }
}

/* the track's effect setting k (LK_I1..), its lock or the track's value */
static int fx(int t, int k, int track_value)
{
    return fxl_mask[t] >> (k - LK_I1) & 1 ? fxl_val[t][k - LK_I1] : track_value;
}

static void apply(const q_event *e)
{
    int t = e->track;               /* a KIT's BD marks the duck */
    if (t < Q_TRACKS && e->on && rmodel[t] == M_KIT && duck_at < 0 && !no_pad(t, e->note) &&
        prj->kit[t][e->note - KIT_NOTE].engine == PAD_BD)
        duck_at = (int)e->offset;
    if (e->track == Q_CLICK_TRACK) {
        click_start(e->note);
    } else if (e->on && (e->flags & EV_LEGATO) && rmodel[e->track] == M_KIT) {
        /* a pad does not glide (a retargeted voice could stay on another pad's engine): the tied note, whose
         * note-off is gone, ends, and the new one strikes */
        event(e->track, e->from, 0, 0, e->pat, e->step, 0);
        event(e->track, e->note, e->vel, 1, e->pat, e->step, e->flags & EV_ACCENT ? 1 : 0);
    } else if (!(e->on && (e->flags & EV_LEGATO) && legato(e))) {
        event(e->track, e->note, e->vel, e->on, e->pat, (int8_t)e->step, e->flags & EV_ACCENT ? 1 : 0);   /* 255, the keyboard's: -1 */
    }
}

/* a held note recorded, its length in eighths of a step, at the tempo and div playing */
static void rec_send(const q_held *h)
{
    uint32_t num, den, w = rec_w;
    seq_step_ratio(prj->tempo, prj->pat[h->track][h->pat].div, &num, &den);
    int len = (int)((float)(now - h->t0) * 8.0f * (float)den / (float)num + 0.5f);
    if (w - rec_r == REC_N)
        return;                     /* full: the note is lost */
    rec[w % REC_N] = (q_rec){ h->track, h->pat, h->step, h->note, 100, (uint8_t)(len < 1 ? 1 : len > 64 ? 64 : len),
                              (int8_t)seq_quantize(h->micro, prj->quant) };
    __asm__ volatile("" ::: "memory");   /* the ring's publish barrier */
    rec_w = w + 1;
}

/* a live note while recording: held from its nearest step (none while stopped or counting in) to its note-off */
static void rec_note(int track, int note, int on)
{
    int micro, pat, step;
    for (int i = 0; i < HELD_N; i++)
        if (held[i].on && held[i].track == track && held[i].note == note) {
            if (!on) {
                rec_send(&held[i]);
                held[i].on = 0;
            }
            return;
        }
    if (!on || !recording || (step = seq_locate(&seq, prj, track, &micro, &pat)) < 0)
        return;
    if (prj->pat[track][pat].src != SRC_STEP)   /* a generator's pattern has no steps to write: the note only plays */
        return;
    for (int i = 0; i < HELD_N; i++)
        if (!held[i].on) {
            held[i] = (q_held){ 1, (uint8_t)track, (uint8_t)note, (uint8_t)pat, (uint8_t)step,
                                (int8_t)micro, now };
            return;
        }
}

static int arping(int t) { return t < Q_TRACKS && rmodel[t] != M_KIT; }

/* an event of a track in arp: into the arp, in the order it came */
static void arp_feed(const q_event *e)
{
    if (e->on) {
        apat[e->track] = e->pat;
        astep[e->track] = (int8_t)e->step;   /* 255, the keyboard's: -1 */
    }
    arp_input(&arp[e->track], e);
}

/* the track's arp settings: the locks of a note-on's step (step -1: the keyboard's), else the track's */
static q_arpp arp_params(int t, int pat, int step)
{
    const q_sound *s = &prj->sound[t];
    q_arpp p = { s->amode, s->aoct, s->adiv, s->agate };
    if (step >= 0) {
        const q_pattern *pt = &prj->pat[t][pat];
        int i;
        if ((i = lock_find(pt, step, LK_AMODE)) >= 0) p.mode = pt->locks[i].value;
        if ((i = lock_find(pt, step, LK_AOCT)) >= 0) p.oct = pt->locks[i].value;
        if ((i = lock_find(pt, step, LK_ADIV)) >= 0) p.div = pt->locks[i].value;
        if ((i = lock_find(pt, step, LK_AGATE)) >= 0) p.gate = pt->locks[i].value;
    }
    return p;
}

/* A note-on changing the arp's MODE sits after the note-offs of its own offset, but the arp switches mode at the
 * start of its run: the run is cut after that offset, so the notes still held are not struck again by a switch to
 * OFF (nor the new chord arpeggiated by the old mode). The event's index, -1 when there is no cut: none changes the
 * mode, or it is the block's last frame */
static int arp_cut(int t, int ne, uint32_t n)
{
    int mode = arp[t].mode;
    for (int i = 0; i < ne; i++) {
        const q_event *e = &ev[i];
        if (e->track != t || !e->on)
            continue;
        int m = arp_params(t, e->pat, (int8_t)e->step).mode;
        if (m != mode && e->offset + 1 < n)
            return i;
        mode = m;
    }
    return -1;
}

/* track t's events through its arp: its own events leave ev, the arp's join it at ev + *ne. They are kept when the
 * arp is a pass-through (MODE OFF before and after: its events as they came, in order with the other tracks') */
static void arp_run(int t, int ni, int *ne, uint32_t n)
{
    static q_event sink[ARP_OUT];
    int cut = arp_cut(t, ni, n), pass, k;
    uint32_t num, den, n1 = cut < 0 ? n : ev[cut].offset + 1;
    q_arpp p;
    for (int i = 0; i < (cut < 0 ? ni : cut); i++)
        if (ev[i].track == t)
            arp_feed(&ev[i]);
    p = arp_params(t, apat[t], astep[t]);
    pass = cut < 0 && p.mode == AM_OFF && arp[t].mode == AM_OFF;
    seq_step_ratio(prj->tempo, p.div, &num, &den);
    k = arp_block(&arp[t], &p, num, den, n1, pass ? sink : ev + *ne);
    *ne += pass ? 0 : k;
    if (cut >= 0) {
        for (int i = cut; i < ni; i++)
            if (ev[i].track == t) {
                q_event e = ev[i];
                e.offset = e.offset >= n1 ? e.offset - n1 : 0;
                arp_feed(&e);
            }
        p = arp_params(t, apat[t], astep[t]);
        seq_step_ratio(prj->tempo, p.div, &num, &den);
        k = arp_block(&arp[t], &p, num, den, n - n1, ev + *ne);
        for (int i = 0; i < k; i++)
            ev[*ne + i].offset += n1;
        *ne += k;
    }
    if (!pass)
        for (int i = 0; i < ni; i++)
            if (ev[i].track == t)
                ev[i].track = 255;
}

static void commands(void)
{
    while (cmd_r != cmd_w) {
        __asm__ volatile("" ::: "memory");   /* the command after its index */
        q_cmd c = cmd[cmd_r % CMD_N];
        cmd_r++;
        if (c.kind == CMD_NOTE) {
            if (c.on < 2)
                rec_note(c.track, c.note, c.on);
            if (arping(c.track) && (c.on == 1 || c.on == 2))
                arp_unlatch(&arp[c.track]);   /* a key without latch ends the latch */
            ev[ncmd++] = (q_event){ 0, (uint8_t)c.track, (uint8_t)c.note, 100, (uint8_t)(c.on != 0), 0, 255,
                                    c.on == 3 ? EV_LATCH : 0, 0 };
        } else if (c.kind == CMD_MODEL) {
            int n = 0;
            rmodel[c.track] = prj->sound[c.track].model;
            for (int v = 0; v < Q_VOICES; v++)   /* over this block */
                if (sounding(v) && pool.v[v].track == c.track)
                    fade_out(v, inv_n);
            for (int i = 0; i < n_later; i++)    /* its waiting note-ons would have been faded out too */
                if (later[i].track != c.track)
                    later[n++] = later[i];
            n_later = n;
            if (!arping(c.track)) {              /* a KIT's pads play direct: its arp ends, its notes with the fade */
                arp_reset(&arp[c.track]);
                astep[c.track] = -1;
            }
        } else if (c.kind == CMD_SCENE) {
            seq_launch(&seq, c.note);
        } else if (c.kind == CMD_DRAW) {          /* MARBLES: a new draw of the track's loops */
            gen_marbles_draw(&seq.g[c.track], &seq.rng);
        } else if (c.kind == CMD_LISTEN) {        /* MIMIC: silent while it learns; after, a new phrase: as at the reset */
            q_mimic_run *r = &seq.g[c.track].mimic;
            if (c.note)
                r->listen = 1;
            else
                __builtin_memset(r, 0, sizeof *r);
        } else if (c.kind == CMD_PREP_REQ) {
            int v = spare_slot(c.note, rmodel);
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
            for (int i = 0; i < HELD_N; i++)   /* the notes still held: recorded as long as they were */
                if (held[i].on) {
                    rec_send(&held[i]);
                    held[i].on = 0;
                }
            seq_stop(&seq, off, &n);
            __builtin_memset(fxl_mask, 0, sizeof fxl_mask);
            for (int i = 0; i < n; i++) {
                ev[ncmd++] = off[i];             /* (an arp ends the step's notes; a keyboard latch goes on) */
            }
            if (c.on) {
                mod_restart();
                seq_start(&seq, prj, recording && (prj->click & 2));
            }
        }
    }
}

/* voices [from, from + 3) of the segment, each into its track's bus of b */
static void render_voices(int from, float (*b)[APP_BLOCK], float *tmp)
{
    for (int v = from; v < from + 3; v++) {
        if (!sounding(v) || (fading[v] && fade[v] == 0))
            continue;
        float g = gain[v], dg = 0;
        uint32_t len = seg_len;
        if (fading[v]) {            /* down to 0 from where it got to */
            uint32_t left = (uint32_t)(fade[v] / fstep[v] + 0.5f);
            dg = -g * fstep[v];
            g *= fade[v];
            if (len >= left) {
                len = left;
                fade[v] = 0;
            } else {
                fade[v] -= fstep[v] * (float)len;
            }
        }
        __builtin_memset(tmp, 0, seg_len * sizeof *tmp);
        pq_render(v, tmp, seg_len);
        float *out = b[pool.v[v].track] + seg_pos;
        for (uint32_t i = 0; i < len; i++, g += dg)
            out[i] += tmp[i] * g;
    }
}

/* arg 1: the block's reverb first (qfx_reverb), which core 2 runs beside core 1's voices */
static void job2(uint32_t arg)
{
    uint32_t t0 = plat_ticks24();
    if (arg)
        qfx_reverb();
    render_voices(3, bus[1], tmp2);
    sum2 += plat_ticks24() - t0;
}

/* each model's level at its default settings, CREST's as the reference (K-weighted loudness, the loudest 100 ms of a
 * held note 48 at velocity 100): Plaits' own output levels differ by up to 20 dB. Applied after the voice's clamp, so a
 * raised drum keeps its peaks */
static const float MODEL_GAIN[M_KIT] = {
    1.14f, 0.70f, 0.79f, 1.15f, 1.30f, 1.05f, 2.48f, 0.52f, 1.00f, 1.04f, 0.82f, 1.23f,
    1.14f, 1.88f, 0.91f, 1.57f, 1.40f, 0.88f, 2.69f, 5.01f, 1.60f, 1.26f, 2.21f, 2.57f};

static void render_segment(void)
{
    uint32_t tw;
    int two = 0, r = 0, need2 = 0;
    for (int v = 0; v < Q_VOICES; v++)
        if (sounding(v)) {
            gain[v] = 0.25f * MODEL_GAIN[pool.v[v].model];   /* the track's LEVEL is qfx's */
            if (vpad[v] >= 0)
                gain[v] *= prj->kit[pool.v[v].track][vpad[v]].level / 127.0f;
            need2 |= v >= 3;
        }
    int rev = !rev_done;            /* the block's first segment carries the reverb */
    rev_done = 1;
    if (need2 || rev)               /* no empty job for the second core */
        two = plat_cpu2_run(job2, (uint32_t)rev);
    render_voices(0, bus[0], tmp1);
    if (two) {
        tw = plat_ticks24();
        r = plat_cpu2_wait();
        sum1 -= plat_ticks24() - tw;   /* core 1 waiting is not its work */
    }
    if (r < 0)                      /* given up: what it added to the segment goes */
        for (int t = 0; t < Q_TRACKS; t++)
            __builtin_memset(bus[1][t] + seg_pos, 0, seg_len * sizeof *bus[1][t]);
    if (!two || r < 0) {            /* the second core's share, on this one (a reverb given up may have run part of
                                     * its block: one block of its tail is then off, once, as core 2 goes off) */
        if (rev)
            qfx_reverb();
        render_voices(3, bus[1], tmp2);
    }
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t t0 = plat_ticks24(), pos = 0;
    int ne, ei = 0;
    float *l = tmp1, *r = tmp2, *b[Q_TRACKS];
    qfx_track tr[Q_TRACKS];
    qfx_master fm;
    float target = 8388607.0f * (float)master_q12 / 4096.0f, g = out_gain, dg;

    if (muted) {                    /* before an update rewrites the flash: nothing runs, nothing sounds */
        __builtin_memset(lr, 0, 2 * n * sizeof *lr);
        out_gain = 0;               /* (unmuted: from silence) */
        return;
    }
    inv_n = 1.0f / (float)n;
    dg = (target - g) * inv_n;      /* MASTER ramped across the block: no zipper noise */
    switched = 0;
    rev_done = 0;
    start_later();
    ncmd = 0;
    commands();
    for (int t = 0; t < Q_TRACKS; t++)    /* seq_pos: the block's start */
        if ((fxl_pos[t] = seq_pos(&seq, t)) == fxl_step[t])
            fxl_seen[t] = 1;
        else if (fxl_seen[t])
            fxl_mask[t] = 0;
    uint16_t punch = punch_held;    /* the keys held, and the steps' punch-ins of the tracks heard */
    for (int t = 0; t < Q_TRACKS; t++) {
        int k = fxl_pos[t], i;
        if (k < 0 || seq.mute >> t & 1)
            continue;
        const q_pattern *pt = &prj->pat[t][seq_pattern(&seq, t)];
        if ((i = lock_find(pt, k, LK_PUNCH_LO)) >= 0) punch |= pt->locks[i].value;
        if ((i = lock_find(pt, k, LK_PUNCH_HI)) >= 0) punch |= (uint16_t)(pt->locks[i].value << 8);
    }
    uint32_t bnum, bden;
    seq_step_ratio(prj->tempo, DIV_4, &bnum, &bden);
    float fpb = (float)bnum / (float)bden, bpos = seq_bar_frames(&seq);
    if (bpos < 0.0f) {              /* stopped or counting in: a bar from the punch-ins' start */
        if (punch && !punch_last) punch_free = 0.0f;
        bpos = punch_free;
        for (punch_free += (float)n; punch_free >= 4.0f * fpb; punch_free -= 4.0f * fpb) {}
    } else {
        for (punch_free = bpos + (float)n; punch_free >= 4.0f * fpb; punch_free -= 4.0f * fpb) {}   /* the next block's
                                       place: a STOP with a punch-in held carries on from there, no phase jump */
    }
    punch_last = punch;
    duck_at = -1;
    seq_click(&seq, recording && (prj->click & 1));
    ne = ncmd + seq_block(&seq, prj, n, ev + ncmd, EV_MAX);
    int ni = ne;
    for (int i = ncmd; i < ni; i++)   /* by date: the last note-on's step wins */
        if (ev[i].track < Q_TRACKS && ev[i].on)
            fx_take(&ev[i]);
    for (int t = 0; t < Q_TRACKS; t++)
        if (arping(t))
            arp_run(t, ni, &ne, n);
    int m = 0;
    for (int i = 0; i < ne; i++)
        if (ev[i].track != 255)
            ev[m++] = ev[i];
    ne = m;
    for (int i = 1; i < ne; i++) {  /* by offset, stable */
        q_event e = ev[i];
        int j = i;
        for (; j > 0 && ev[j - 1].offset > e.offset; j--)
            ev[j] = ev[j - 1];
        ev[j] = e;
    }
    int sc = seq_scene_started(&seq);
    if (sc >= 0 && scene_w == scene_r) {
        scene_v = sc;
        __asm__ volatile("" ::: "memory");
        scene_w++;
    }
    for (int v = 0; v < Q_VOICES; v++)
        if (sounding(v)) {
            q_vparam q;
            set_patch(v, &prj->sound[pool.v[v].track]);   /* the knobs reach the held notes */
            vparam(v, &prj->sound[pool.v[v].track], &q);
            mod_set(v, &q, prj->tempo);
        }
    for (int t = 0; t < Q_TRACKS; t++) {
        __builtin_memset(bus[0][t], 0, n * sizeof *bus[0][t]);
        __builtin_memset(bus[1][t], 0, n * sizeof *bus[1][t]);
    }
    __builtin_memset(clk, 0, n * sizeof *clk);
    while (pos < n) {
        while (ei < ne && ev[ei].offset <= pos)
            apply(&ev[ei++]);
        seg_pos = pos;
        seg_len = (ei < ne ? ev[ei].offset : n) - pos;
        render_segment();
        click_render(clk + seg_pos, seg_len);
        pos += seg_len;
    }
    for (int v = 0; v < Q_VOICES; v++)
        if ((fading[v] && fade[v] == 0) || (pool.v[v].state == V_RELEASED && pq_silent(v))) {
            if (fading[v])
                pool.v[v].model = -1;   /* its engine still holds the sound cut: built anew before a note */
            voices_free(&pool, v);
            fading[v] = 0;
        }
    for (int t = 0; t < Q_TRACKS; t++) {
        const q_sound *s = &prj->sound[t];
        b[t] = bus[0][t];
        for (uint32_t i = 0; i < n; i++)
            b[t][i] += bus[1][t][i];
        tr[t] = (qfx_track){ s->itype, (uint8_t)fx(t, LK_I1, s->i1), (uint8_t)fx(t, LK_I2, s->i2),
                             (uint8_t)fx(t, LK_I3, s->i3), (uint8_t)fx(t, LK_IMIX, s->imix),
                             (uint8_t)fx(t, LK_DSEND, s->dsend), (uint8_t)fx(t, LK_RSEND, s->rsend),
                             (uint8_t)fx(t, LK_LEVEL, s->level), (uint8_t)(rmodel[t] != M_KIT),
                             (int8_t)fx(t, LK_PAN, (uint8_t)s->pan) };
    }
    uint32_t num, den;
    seq_step_ratio(prj->tempo, prj->dtime, &num, &den);
    fm = (qfx_master){ (float)num / (float)den, prj->dfb, prj->rsize, prj->rtone, prj->comp, prj->duck, punch, fpb, bpos };
    uint32_t tfx = plat_ticks24();
    qfx_block(b, tr, &fm, duck_at, l, r, n);
    sumfx += plat_ticks24() - tfx;
    for (uint32_t i = 0; i < n; i++) {
        g += dg;
        float xl = (l[i] + clk[i]) * g, xr = (r[i] + clk[i]) * g;
        lr[2 * i] = xl > 8388607.0f ? 8388607 : xl < -8388608.0f ? -8388608 : (int32_t)xl;
        lr[2 * i + 1] = xr > 8388607.0f ? 8388607 : xr < -8388608.0f ? -8388608 : (int32_t)xr;
    }
    out_gain = target;
    now += n;
    mod_clock(prj->tempo, n);

    sum1 += plat_ticks24() - t0;
    sum_budget += BUDGET_TICKS(n);
    if (sum_budget >= LOAD_TICKS) {
        pct1 = sum1 / (sum_budget / 100u);   /* no 64-bit multiply: fm1-base bsp/docs/CPU.md */
        pct2 = sum2 / (sum_budget / 100u);
        pctfx = sumfx / (sum_budget / 100u);
        sum1 = sum2 = sumfx = sum_budget = 0;
    }
}

/* The screen's accessors (ui.c, later in the same unity build): static, so that clang places them where the screen uses
 * them, after the render, and the audio's code keeps its place in the flash (app.h APP_LATE, docs/cpu.md). The LFO phase
 * of the track's first sounding voice, -1 without one */
static float audio_lfo_phase(int track)
{
    for (int v = 0; v < PQ_VOICES; v++) if (pool.v[v].track == track && sounding(v)) return mod_lfo_phase(v);
    return -1;
}

/* the held notes in arrival order (count), *sounding_note: the note playing, -1 none. Read without a lock from the main
 * loop, as audio_hits: a torn read shows one odd frame */
static int audio_arp(int track, uint8_t held[ARP_HELD], int *sounding_note)
{
    const q_arp *a = &arp[track];
    int n = a->nh;
    for (int i = 0; i < n; i++) held[i] = a->h[i].note;
    *sounding_note = a->snd ? a->sn : -1;
    return n;
}
