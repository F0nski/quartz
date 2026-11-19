/* No libc headers: the device builds the app's C freestanding, so the compiler's builtins stand in. */
#include "seq.h"

/* the default kit: BD SN CH OH RIM CLAP TOM x3 / TOM, the synthetic BD SN CH, CONGA COWBELL SHAKER RIDE */
static const q_pad KIT_DEFAULT[Q_PADS] = {
    { PAD_BD, -12, 72, 50, 70, 127 }, { PAD_SN, 7, 50, 60, 50, 100 }, { PAD_HH, 0, 50, 127, 20, 90 },
    { PAD_HH, 0, 50, 127, 90, 90 }, { PAD_SN, 19, 100, 100, 10, 90 }, { PAD_SN, 2, 110, 30, 40, 100 },
    { PAD_BD, -5, 10, 40, 60, 100 }, { PAD_BD, 0, 10, 40, 55, 100 }, { PAD_BD, 5, 10, 40, 50, 100 },
    { PAD_BD, -10, 90, 80, 40, 110, 127 }, { PAD_SN, 10, 80, 40, 70, 100, 127 }, { PAD_HH, 7, 30, 127, 15, 90, 127 },
    { PAD_BD, 12, 5, 60, 40, 100 }, { PAD_HH, 19, 10, 90, 15, 80 }, { PAD_HH, 0, 60, 10, 25, 80 },
    { PAD_HH, -5, 80, 50, 110, 80 },
};

/* a fresh LANES pattern plays at once: 7, 5, 3, 4, 6 cells (OCT 2 is no shift) */
static const uint8_t LANES_LEN[L_N] = { 7, 5, 3, 4, 6 };
static const uint8_t LANES_V[L_N][16] = {
    { 1, 0, 1, 1, 0, 1, 0 }, { 0, 4, 2, 5, 3 }, { 2, 2, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 }, { 1, 3, 1, 2 }, { 3, 1, 2, 1, 3, 2 } };

void project_defaults(q_project *p)
{
    __builtin_memset(p, 0, sizeof *p);
    p->version = Q_VERSION;
    p->tempo = 120;
    p->swing = 50;
    p->mode = 1;                    /* MAJOR */
    p->quant = 2;
    p->dtime = DIV_8; p->dfb = 40; p->rsize = 80; p->rtone = 64;   /* comp, duck: 0 */
    for (int i = 0; i < Q_SONG; i++) p->song[i].reps = 1;   /* 1..16: what the load's clamp would make of 0 */
    for (int i = 0; i < Q_TRACKS; i++) {
        q_sound *s = &p->sound[i];
        s->model = 8; s->poly = 1; s->level = 100;
        s->harm = s->timb = s->morph = s->decay = 64;
        s->dec = 64; s->sus = 127; s->rel = 20; s->cutoff = 127;    /* the rest of the new fields: 0 (LP, SIN, FREE, OFF...) */
        s->lrate = 5; s->ldest = LD_CUT; s->aoct = 1; s->adiv = DIV_16; s->agate = 50;
        s->i1 = s->i2 = s->i3 = 64; s->imix = 127;                  /* itype, dsend, rsend, pan: 0 (no insert, no send, centre) */
        for (int k = 0; k < Q_PADS; k++) p->kit[i][k] = KIT_DEFAULT[k];
        for (int n = 0; n < Q_PATTERNS; n++) {
            q_pattern *pt = &p->pat[i][n];
            pt->len = 16; pt->div = DIV_16; pt->path = PATH_FWD;
            pt->xlen = pt->ylen = 4; pt->ydiv = DIV_4;
            pt->grids = (q_grids){ 64, 64, 0, { 64, 64, 64 }, 1, 60, { 0, 1, 2 } };
            pt->marbles = (q_marbles){ 1, 8, 0, 64, 0, 0, 64, 64, 0, 48, 2, { 0, 0 }, 2 };
            pt->life = (q_life){ 2, 48, 0, 0, 70, 1, 1, 0 };
            pt->orbits = (q_orbits){ 48, 3, { 8, 6, 5, 3 }, { 0, 0, 0, 0 }, { 0, 2, 4, 7 } };
            pt->cipher = (q_cipher){ 0, 0, 0, 0, 96, 16, 0, 1, 48, 2 };
            gen_cipher_preset(&pt->cipher, 0);
            pt->mimic = (q_mimic){ 0, 1, 16, 0, 0, { { 0 } } };   /* no phrase: silent until LISTEN */
            pt->tonnetz = (q_tonnetz){ 48, 4, 2, 0, 1, 0, 1, 1, (uint16_t)(TZ_R | TZ_L << 2 | TZ_R << 4 | TZ_L << 6) };
            pt->chords = (q_chords){ 48, 4, 2, 0, 1, { CH_MAKE(0, CS_TRIAD), CH_MAKE(5, CS_TRIAD), CH_MAKE(3, CS_TRIAD), CH_MAKE(4, CS_TRIAD) } };
            pt->lanes.note = 48;
            for (int l = 0; l < L_N; l++) {
                lanes_set_len(&pt->lanes, l, LANES_LEN[l]);
                for (int c = 0; c < 16; c++) lanes_set(&pt->lanes, l, c, LANES_V[l][c]);
            }
            for (int k = 0; k < Q_STEPS; k++) {
                q_step *st = &pt->steps[k];
                st->note[0] = 60; st->vel = 100; st->len = 4; st->prob = 20;
            }
        }
    }
}

int lock_find(const q_pattern *pt, int step, int param)
{
    for (int i = 0; i < pt->nlocks; i++)
        if (pt->locks[i].step == step && pt->locks[i].param == param) return i;
    return -1;
}

/* The render reads nlocks and the entries concurrently: an entry is complete before nlocks counts it. */
int lock_set(q_pattern *pt, int step, int param, int value)
{
    int i = lock_find(pt, step, param);
    if (i >= 0) { pt->locks[i].value = (uint8_t)value; return 0; }
    if (pt->nlocks >= Q_LOCKS) return -1;
    pt->locks[pt->nlocks] = (q_lock){ (uint8_t)step, (uint8_t)param, (uint8_t)value };
    __atomic_thread_fence(__ATOMIC_RELEASE);
    pt->nlocks++;
    return 0;
}

/* Swap the last entry in, then shrink: the render may read a lock twice for a moment, never half of one. */
void lock_clear_step(q_pattern *pt, int step)
{
    for (int i = 0; i < pt->nlocks;) {
        if (pt->locks[i].step != step) { i++; continue; }
        pt->locks[i] = pt->locks[pt->nlocks - 1];
        __atomic_thread_fence(__ATOMIC_RELEASE);
        pt->nlocks--;
    }
}

/* frames per step = 750000/17 Hz x 60 / tempo x beats = 45000000 x beats_num / (17 x tempo x beats_den) */
void seq_step_ratio(int tempo, int div, uint32_t *num, uint32_t *den)
{
    static const uint8_t bnum[DIV_N] = { 1, 1, 1, 1, 1, 2, 1, 2, 4 }, bden[DIV_N] = { 8, 6, 4, 3, 2, 3, 1, 1, 1 };
    *num = 45000000u * bnum[div];
    *den = 17u * (uint32_t)tempo * bden[div];
}

/* The step at c keeps its start: carry the fractional start into the new denominator, rounded up like a start
   frame (r * den' < 2^32 for tempo <= 240). */
static void clock_set(q_clock *c, int tempo, int div)
{
    uint32_t num, den;
    if (tempo == c->tempo && div == c->div) return;
    seq_step_ratio(tempo, div, &num, &den);
    if (c->den) {
        uint32_t r = c->next_r * den;
        c->next_r = r / c->den + (r % c->den > 0);
    }
    c->tempo = tempo;
    c->div = div;
    c->num = num;
    c->den = den;
}

static void clock_next(q_clock *c)
{
    c->next_r += c->num;
    c->next_i += c->next_r / c->den;
    c->next_r %= c->den;
}

/* Out of line: inlined 64-bit multiplies among 16-bit loads now and then corrupt a register (base/bsp/docs/CPU.md) */
static __attribute__((noinline)) int64_t mul64(int64_t a, int64_t b) { return a * b; }

/* ceil(c's start + spf x f / 4800) in frames: f in 4800ths of a step keeps micro (200 per 24th), swing (96 per %)
   and ratchets (4800 / r) whole */
static int64_t date(const q_clock *c, int64_t f)
{
    int64_t x = mul64(c->next_r, 4800) + mul64(c->num, f), d = mul64(4800, c->den);
    return (int64_t)c->next_i + (x >= 0 ? (x + d - 1) / d : -(-x / d));
}

/* the step at c is planned half a step ahead (whole: a step ahead): its start minus half a step falls before end. A
   MARBLES step is planned a whole step ahead, so that its scene's marble falls for a step before the note sounds */
static int due(const q_clock *c, uint64_t end, int whole)
{
    return mul64((int64_t)c->next_i - (int64_t)end, 2 * (int64_t)c->den) + 2 * (int64_t)c->next_r < (int64_t)c->num * (whole ? 2 : 1);
}

/* a's start <= b's, exactly */
static int not_after(const q_clock *a, const q_clock *b)
{
    return mul64(mul64((int64_t)a->next_i - (int64_t)b->next_i, a->den), b->den) + mul64(a->next_r, b->den)
        <= mul64(b->next_r, a->den);
}

static void queue_push(q_queue *q, uint64_t at, uint32_t gate, q_event e)
{
    int i = q->n++;
    for (; i > 0 && (q->e[i - 1].at > at || (q->e[i - 1].at == at && q->e[i - 1].e.on > e.on)); i--)
        q->e[i] = q->e[i - 1];
    q->e[i] = (q_queued){ at, gate, e };
}

static q_queued queue_take(q_queue *q, int i)
{
    q_queued e = q->e[i];
    for (q->n--; i < q->n; i++) q->e[i] = q->e[i + 1];
    return e;
}

/* A queued note-on also holds a place for its note-off: n + ons never passes SEQ_QUEUE, so a note-off always
   finds room and a note-on that would not is dropped (0). */
static int queue_push_on(q_queue *q, uint64_t at, uint32_t gate, q_event e)
{
    if (q->n + q->ons + 2 > SEQ_QUEUE) return 0;
    queue_push(q, at, gate, e);
    q->ons++;
    return 1;
}

#define TIED UINT32_MAX     /* q_queued.gate of a tied note-on */
#define HELD UINT64_MAX     /* q_queued.at of a tied note's note-off: it leaves at a legato, an untie or seq_stop */

/* the pending tie's note-on if still queued, -1 if it has begun: ties leave in the order planned, so the last */
static int tied_on(const q_queue *q)
{
    int t = -1;
    for (int i = 0; i < q->n; i++)
        if (q->e[i].e.on && q->e[i].gate == TIED) t = i;
    return t;
}

/* the date of the last queued tied or legato note-on, 0 none */
static int64_t tie_date(const q_queue *q)
{
    int64_t d = 0;
    for (int i = 0; i < q->n; i++)
        if (q->e[i].e.on && (q->e[i].gate == TIED || (q->e[i].e.flags & EV_LEGATO))) d = (int64_t)q->e[i].at;
    return d;
}

/* the step at the track's x plays no note to replace the tied one: that one ends at the step's grid start */
static void untie(q_seq *s, int t)
{
    q_queue *q = &s->q[t];
    int64_t at = date(&s->t[t].x, 0);
    int i = tied_on(q);
    s->t[t].tie = 0;
    if (i >= 0) {
        q->e[i].gate = at > (int64_t)q->e[i].at ? (uint32_t)(at - (int64_t)q->e[i].at) : 0;
        return;
    }
    for (i = 0; q->e[i].at != HELD; i++) {}
    queue_push(q, (uint64_t)at, 0, queue_take(q, i).e);
}

/* A scene starts the track over at bar b: step 0 of the scene's pattern, both clocks at b. The note-ons queued for b
   or later go (their note-offs are not queued yet); a tie the kept note-ons leave pending ends at b, unless a kept
   legato note-on ends it, so no later step looks for a held note-off that is gone. */
static void reanchor(q_seq *s, const q_project *p, int t, const q_clock *b)
{
    const q_scene *sc = &p->scene[s->launch];
    q_seq_track *tr = &s->t[t];
    q_queue *q = &s->q[t];
    uint64_t at = (uint64_t)date(b, 0);
    int tied = -1, legato = -1, held = -1;
    for (int i = 0; i < q->n;) {
        const q_queued *e = &q->e[i];
        if (e->e.on && e->at >= at) {
            queue_take(q, i);
            q->ons--;
            continue;
        }
        if (e->e.on && e->gate == TIED) tied = i;
        if (e->e.on && (e->e.flags & EV_LEGATO)) legato = i;
        if (e->at == HELD) held = i;
        i++;
    }
    if (tied >= 0 && tied >= legato) q->e[tied].gate = (uint32_t)(at - q->e[tied].at);
    else if (legato < 0 && held >= 0) queue_push(q, at, 0, queue_take(q, held).e);
    if (tr->planned) {                  /* that step does not begin */
        tr->h[1] = tr->h[0];
        tr->h[0].step = -1;
    }
    tr->x = *b;
    tr->pos = 0;
    tr->planned = tr->pp = tr->loops = tr->tie = tr->anchor = 0;
    tr->k = tr->row = -1;
    tr->pat = tr->want = sc->pat[t];
    tr->path = p->pat[t][tr->pat].path;
    gen_reset(&s->g[t], &s->rng);
    s->mute = (s->mute & ~(1 << t)) | (sc->mute & 1 << t);
}

/* a launch pending: entry its song entry (-1 by hand), bar -1 the next bar, fixed at the next block */
static void pend(q_seq *s, int scene, int entry, int bar)
{
    s->launch = scene;
    s->lentry = entry;
    s->bar = bar;
    for (int t = 0; t < Q_TRACKS; t++) s->t[t].anchor = 1;
}

void seq_start(q_seq *s, const q_project *p, int count_in)
{
    uint64_t now = s->now;
    q_clock a = { now, 0, 0, 0, 0, 0 };
    uint32_t gcount[Q_TRACKS];          /* the scenes' readers keep their place in the notes across a restart */
    __builtin_memcpy(gcount, s->gcount, sizeof gcount);
    __builtin_memset(s, 0, sizeof *s);
    __builtin_memcpy(s->gcount, gcount, sizeof gcount);
    s->now = now;
    s->running = 1;
    s->rng = (uint32_t)now | 1;         /* xorshift: never 0 */
    s->count = 4 * count_in;
    s->beat = a;
    s->mute = s->seen_mute = p->mute;
    s->launch = s->entry = -1;
    clock_set(&a, p->tempo, DIV_1);     /* a bar's den divides every div's: the anchor carries exactly */
    if (count_in) clock_next(&a);
    for (int t = 0; t < Q_TRACKS; t++) {
        q_seq_track *tr = &s->t[t];
        tr->x = a;
        tr->k = tr->row = -1;
        tr->pat = tr->want = tr->seen = p->pattern[t];
        tr->path = p->pat[t][tr->pat].path;
        tr->h[0].step = tr->h[1].step = -1;
        gen_reset(&s->g[t], &s->rng);
    }
    if (p->song_on && p->nsong) pend(s, p->song[0].scene, 0, count_in);
}

void seq_click(q_seq *s, int on) { s->click = on; }

void seq_launch(q_seq *s, int scene) { pend(s, scene, -1, -1); }

int seq_scene_started(q_seq *s)
{
    int n = s->started - 1;
    s->started = 0;
    return n;
}

int seq_song_entry(const q_seq *s) { return s->running ? s->entry : -1; }

void seq_stop(q_seq *s, q_event *out, int *n)
{
    *n = 0;
    for (int t = 0; t <= Q_TRACKS; t++) {
        for (int i = 0; i < s->q[t].n; i++)
            if (!s->q[t].e[i].e.on) {
                out[*n] = s->q[t].e[i].e;
                out[(*n)++].offset = 0;
            }
        s->q[t].n = s->q[t].ons = 0;
    }
    s->running = 0;
}

/* the last step planned whose grid start the block's start has reached, 0 none */
static const q_planned *current(const q_seq *s, int track)
{
    const q_seq_track *tr = &s->t[track];
    if (!s->running || tr->h[1].step < 0) return 0;
    if ((int64_t)s->now >= date(&tr->h[1].c, 0)) return &tr->h[1];
    return tr->h[0].step < 0 ? 0 : &tr->h[0];
}

float seq_bar_frames(const q_seq *s)
{
    if (!s->running) return -1.0f;
    if (!s->beat.den) return s->count ? -1.0f : 0.0f;   /* before the first block: its first beat starts now */
    int64_t next = date(&s->beat, 0);
    int beat = next > (int64_t)s->now ? s->beats - 1 : s->beats;   /* the beat in progress */
    if (beat < s->count) return -1.0f;
    float fpb = (float)s->beat.num / (float)s->beat.den;
    float in = next > (int64_t)s->now ? fpb - (float)(next - (int64_t)s->now) : 0.0f;
    return (float)(beat % 4) * fpb + (in > 0.0f ? in : 0.0f);
}

int seq_pos(const q_seq *s, int track)
{
    const q_planned *c = current(s, track);
    return c ? c->step : -1;
}

int seq_pattern(const q_seq *s, int track)
{
    const q_planned *c = current(s, track);
    return c ? c->pat : s->t[track].pat;
}

static int64_t swung(const q_planned *h, const q_project *p)
{
    return date(&h->c, h->rank & 1 ? 96 * (p->swing - 50) : 0);
}

/* The next step is planned before the block's start passes halfway to it, except exactly halfway between two
   unswung starts: that one block gets the current step at micro 11. */
int seq_locate(const q_seq *s, const q_project *p, int track, int *micro, int *pat)
{
    const q_seq_track *tr = &s->t[track];
    const q_planned *c = current(s, track);
    int64_t now = (int64_t)s->now;
    if (!c) return -1;
    if (c == &tr->h[0] && now - swung(c, p) >= swung(&tr->h[1], p) - now) c = &tr->h[1];
    int64_t d = mul64(mul64(now - swung(c, p), 24), c->c.den), num = c->c.num;
    int m = (int)((2 * d + (d < 0 ? -num : num)) / (2 * num));
    *micro = m < -11 ? -11 : m > 11 ? 11 : m;
    *pat = c->pat;
    return c->step;
}

int seq_quantize(int micro, int quant) { return quant == 0 ? micro : quant == 1 ? micro / 2 : 0; }

static const uint8_t PATHS[4][16] = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
    { 0, 1, 2, 3, 7, 6, 5, 4, 8, 9, 10, 11, 15, 14, 13, 12 },      /* SNAKE */
    { 0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15 },      /* COLUMNS */
    { 0, 1, 2, 3, 7, 11, 15, 14, 13, 12, 8, 4, 5, 6, 10, 9 },      /* SPIRAL */
};

static const uint8_t DIV96[DIV_N] = { 3, 4, 6, 8, 12, 16, 24, 48, 96 };   /* a step of each DIV_*, in 96ths of a bar */

static uint32_t seq_rand(q_seq *s)
{
    s->rng ^= s->rng << 13;
    s->rng ^= s->rng >> 17;
    s->rng ^= s->rng << 5;
    return s->rng;
}

/* a pass of the pattern, in steps */
static int loop_len(const q_seq_track *tr, const q_pattern *pt, int src)
{
    if (src != SRC_STEP) return gen_loop(pt);
    return tr->path == PATH_XY ? pt->xlen : pt->len;
}

/* the step the track's rank plays */
static int step_at(q_seq *s, q_seq_track *tr, const q_pattern *pt)
{
    int len = pt->len, k = tr->k, m = 0;
    switch (tr->path) {
    case PATH_XY: return 4 * tr->row + k;
    case PATH_REV: k = len - 1 - k; break;
    case PATH_PINGPONG:
        k = len > 1 ? tr->pp % (2 * len - 2) : 0;
        if (k >= len) k = 2 * len - 2 - k;
        break;
    case PATH_RANDOM: k = (int)(seq_rand(s) % (uint32_t)len); break;
    case PATH_SNAKE: m = 1; break;
    case PATH_COLUMNS: m = 2; break;
    case PATH_SPIRAL: m = 3; break;
    }
    return 16 * (k / 16) + PATHS[m][k % 16];
}

/* The step's condition, then its probability: whether it plays. A step with a condition of its own or a
   probability sets PRE. */
static int decide(q_seq *s, q_seq_track *tr, const q_step *st)
{
    int c = st->cond, a = c & 15, b = c >> 4, own = 1, ok;
    if (c == CD_PRE || c == CD_NPRE) {
        ok = tr->pre == (c == CD_PRE);
        own = 0;
    } else if (c == CD_FIRST || c == CD_NFIRST) {
        ok = (tr->loops == 0) == (c == CD_FIRST);
    } else if (b >= 2 && b <= 8 && a >= 1 && a <= b) {
        ok = tr->loops % b == a - 1;
    } else {                            /* NONE, or a byte out of range: no condition */
        ok = 1;
        own = st->prob < 20;
    }
    if (ok && st->prob < 20) ok = st->prob && seq_rand(s) % 20 < st->prob;
    if (own) tr->pre = ok;
    return ok;
}

/* A tie pending from the step before: the first note-on queued is its legato. A slide ties the first note of the
   last repetition. A legato or tied note-on comes no earlier than the last one queued, so they leave the queue in
   the order planned (micro-timing can otherwise put a step's notes before the step before's) and a legato removes
   the note-off of the note it replaces. acc: the accented note slots (bit i: note[i]), on top of SF_ACCENT's all. */
static void plan_notes(q_seq *s, const q_project *p, int t, const q_step *st, int step, int acc, int gen)
{
    q_seq_track *tr = &s->t[t];
    q_queue *q = &s->q[t];
    int r = STEP_RATCHET(st), g = r > 1 && st->len > 8 ? 8 : st->len, hold = 0;
    int64_t f = 200 * st->micro + (tr->k & 1 ? 96 * (p->swing - 50) : 0), from = tie_date(q);
    int strum = gen ? 200 * SF_STRUM(st) : 0;   /* a generator's notes one after the other */
    if (st->flags & SF_ACCENT) acc = 15;
    for (int j = 0; j < r; j++) {
        for (int i = 0; i < 4; i++) {
            int64_t on = date(&tr->x, f + 4800 * j / r + strum * i), off = date(&tr->x, f + 4800 * j / r + 600 * g / r + strum * i);
            if (on < (int64_t)s->now) on = (int64_t)s->now;     /* dated before this block: at its start */
            if (!st->note[i]) continue;
            int vel = acc >> i & 1 ? st->vel + 32 : st->vel;
            if (vel > 127) vel = 127;
            int tie = (st->flags & SF_SLIDE) && j == r - 1 && !hold;
            int64_t a = (tr->tie || tie) && on < from ? from : on;
            if (j == 0 && p->pat[t][tr->pat].src != SRC_STEP) {      /* a generator's: for its scene, with its frame */
                uint32_t c = s->gcount[t] % 16u;
                s->gnote[t][c] = st->note[i];
                s->gat[t][c] = (uint32_t)a;
                s->gcount[t]++;
            }
            if (queue_push_on(q, (uint64_t)a, tie ? TIED : off > a ? (uint32_t)(off - a) : 0,
                    (q_event){ 0, (uint8_t)t, st->note[i], (uint8_t)vel, 1, (uint8_t)tr->pat, (uint8_t)step,
                               (uint8_t)((tr->tie ? EV_LEGATO : 0) | (acc >> i & 1 ? EV_ACCENT : 0) | (j ? EV_REPEAT : 0)), 0 })) {
                tr->tie = 0;
                hold |= tie;
            }
        }
        if (tr->tie) untie(s, t);       /* no note-on to replace it: before this step ties one of its own */
    }
    tr->tie = hold;
}

/* Plans the track's steps due before end. A step's duration is fixed when it begins (its start falls in a block),
   as in a plain grid: a tempo change while it is only planned reaches it. b: the bar a scene starts at, 0 none; the
   track starts over there once its next step would start at b or later. */
static void plan(q_seq *s, const q_project *p, int t, uint64_t end, const q_clock *b)
{
    q_seq_track *tr = &s->t[t];
    for (;;) {
        if (b && tr->anchor && not_after(b, &tr->x)) reanchor(s, p, t, b);
        const q_pattern *pt = &p->pat[t][tr->pat];
        int src = pt->src;              /* read once: the loop length and the step come from one source */
        int wrap = !tr->planned && tr->k + 1 >= loop_len(tr, pt, src);
        if (wrap) {
            pt = &p->pat[t][tr->want];
            src = pt->src;
        }
        clock_set(&tr->x, p->tempo, pt->div);
        if (tr->planned) {
            if (date(&tr->x, 0) >= (int64_t)end) return;
            clock_next(&tr->x);
            tr->planned = 0;
            continue;
        }
        if (!due(&tr->x, end, src == SRC_MARBLES)) return;
        if (wrap) {                     /* the pattern and its path are taken at the wrap */
            if (tr->want != tr->pat || pt->path != tr->path) tr->pp = 0;
            if (tr->want != tr->pat) gen_reset(&s->g[t], &s->rng), tr->src = src + 1;   /* (a new pattern: reset once) */
            tr->loops = tr->want != tr->pat ? 0 : tr->loops + 1;
            tr->pat = tr->want;
            tr->path = pt->path;
            tr->k = 0;
        } else {
            tr->k++;
        }
        /* XY's rows run on the bar from the anchor, so a tempo change keeps them on the X steps: every division is a
           whole number of 96ths of a bar. A row starting with the X step applies to it */
        tr->row = (int)(tr->pos / DIV96[pt->ydiv] % pt->ylen);
        tr->pos += DIV96[pt->div];
        int gen = src != SRC_STEP, step = gen ? tr->k : step_at(s, tr, pt), acc = 0;
        tr->pp++;
        tr->h[0] = tr->h[1];
        tr->h[1] = (q_planned){ step, tr->pat, tr->k, tr->x };
        /* a muted step still decides: it counts for PRE (and draws its probability); a generator still computes its
           step, so its state moves on. A generator's step has no condition nor probability. */
        q_step g;
        const q_step *st = &g;
        if (tr->src && tr->src != src + 1)   /* SOURCE changed while playing: the generator starts over (CHORDS, TONNETZ line up) */
            gen_reset(&s->g[t], &s->rng);
        tr->src = src + 1;
        if (gen) acc = gen_step(&s->g[t], p, t, pt, tr->k, &s->rng, &g);
        else st = &pt->steps[step];
        if ((st->flags & SF_ON) && (gen || decide(s, tr, st)) && !(s->mute >> t & 1))
            plan_notes(s, p, t, st, step, acc, gen);
        else if (tr->tie) untie(s, t);
        tr->planned = 1;
    }
}

int seq_gen_notes(const q_seq *s, int track, uint32_t *seen, uint8_t *notes, uint32_t *at, int max)
{
    uint32_t end = s->gcount[track], k = *seen;
    int n = 0;
    if (end - k > 16u)                  /* too far behind: the ring holds the last 16 */
        k = end - 16u;
    for (; k != end && n < max; k++, n++) {
        notes[n] = s->gnote[track][k % 16u];
        at[n] = s->gat[track][k % 16u];
    }
    *seen = k;
    return n;
}

static void emit(q_event *out, int *n, uint64_t now, const q_queued *q)
{
    out[*n] = q->e;
    out[(*n)++].offset = q->at < now ? 0 : (uint32_t)(q->at - now);
}

int seq_block(q_seq *s, const q_project *p, uint32_t nframes, q_event *out, int max)
{
    int n = 0;
    uint64_t end = s->now + nframes;
    q_clock bar = s->beat;
    if (!s->running) return 0;

    for (int t = 0; t < Q_TRACKS; t++)
        if (p->pattern[t] != s->t[t].seen) s->t[t].want = s->t[t].seen = p->pattern[t];
    if (p->mute != s->seen_mute) s->mute = s->seen_mute = p->mute;
    clock_set(&s->beat, p->tempo, DIV_4);
    if (s->launch >= 0) {               /* the launch's bar: not before the tracks start */
        if (s->bar < 0) s->bar = (s->beats + 3) / 4 > s->count / 4 ? (s->beats + 3) / 4 : s->count / 4;
        uint64_t r = s->beat.next_r + (uint64_t)mul64(s->beat.num, (uint32_t)(4 * s->bar - s->beats));
        bar = s->beat;
        uint64_t q = r / bar.den;
        bar.next_i += q;
        bar.next_r = (uint32_t)r - (uint32_t)q * bar.den;      /* r % den in 32 bits: no 64-bit multiply */
    }
    for (int64_t b; (b = date(&s->beat, 0)) < (int64_t)end; s->beats++, clock_next(&s->beat))
        if (s->click || s->beats < s->count)
            queue_push_on(&s->q[Q_CLICK_TRACK], (uint64_t)b, 0,
                    (q_event){ 0, Q_CLICK_TRACK, (uint8_t)(s->beats % 4 == 0), 100, 1, 0, 0, 0, 0 });
    for (int t = 0; t < Q_TRACKS; t++)
        plan(s, p, t, end, s->launch >= 0 && p->scene[s->launch].used ? &bar : 0);
    /* the launch has begun (every track has started over): reported, then the song's next entry. An entry lasts
       reps x its longest pattern, in whole bars. */
    if (s->launch >= 0 && date(&bar, 0) < (int64_t)end) {
        static const uint8_t u[DIV_N] = { 3, 4, 6, 8, 12, 16, 24, 48, 96 };    /* 24ths of a beat a step */
        const q_scene *sc = &p->scene[s->launch];
        if (sc->used) s->started = s->launch + 1;
        s->entry = s->lentry;
        s->launch = -1;
        if (s->entry >= 0 && p->song_on && p->nsong) {
            int m = 0, e = (s->entry + 1) % p->nsong;
            for (int t = 0; t < Q_TRACKS; t++) {
                const q_pattern *pt = &p->pat[t][sc->pat[t]];
                int n = (pt->src == SRC_STEP ? pt->len : gen_loop(pt)) * u[pt->div];
                if (n > m) m = n;
            }
            pend(s, p->song[e].scene, e, s->bar + p->song[s->entry].reps * ((m + 95) / 96));
        } else {
            s->entry = -1;
        }
    }

    /* the queues' heads by date, the lower track first at a date */
    for (;;) {
        q_queue *q = 0;
        for (int t = 0; t <= Q_TRACKS; t++)
            if (s->q[t].n && s->q[t].e[0].at < end && (!q || s->q[t].e[0].at < q->e[0].at)) q = &s->q[t];
        if (!q) break;
        q_queued e = q->e[0];
        int off = -1;                   /* a later note-off of the same note comes just before the note-on */
        for (int i = 1; e.e.on && off < 0 && i < q->n; i++)
            if (!q->e[i].e.on && q->e[i].e.note == e.e.note && q->e[i].at != HELD) off = i;
        if (n + 1 + (off >= 0) > max) break;
        if (off >= 0) {
            q_queued o = queue_take(q, off);
            o.at = e.at;
            emit(out, &n, s->now, &o);
        }
        queue_take(q, 0);
        if (e.e.on && (e.e.flags & EV_LEGATO)) {   /* it replaces the tied note, which has begun: no note-off for
                                                      that one */
            int i = 0;
            while (q->e[i].at != HELD) i++;
            e.e.from = queue_take(q, i).e.note;
        }
        emit(out, &n, s->now, &e);
        if (e.e.on) {
            q->ons--;
            if (e.e.track != Q_CLICK_TRACK)
                queue_push(q, e.gate == TIED ? HELD : e.at + e.gate, 0,
                        (q_event){ 0, e.e.track, e.e.note, 0, 0, e.e.pat, e.e.step, 0, 0 });
        }
    }
    s->now = end;
    return n;
}
