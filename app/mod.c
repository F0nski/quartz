/* Per-voice ADSR and LFO. mod_voice_block(v) touches only voice v's state (it runs on either core). */
#include "project.h"
#include "mod.h"
#include "dsp/pq.h"
#include "fastmath.h"            /* libm is slow on the device: 128 voice blocks per audio block call these */

#define MOD_BLK 12                                   /* frames per Plaits block */
enum { E_IDLE, E_ATK, E_DEC, E_REL };

typedef struct {
    q_vparam p; int tempo, stage, acc;
    float env, astep;                                /* astep: the attack's slope per block */
    float ph, inc;                                   /* LFO phase 0..1 and its advance per block */
    float lv;                                        /* the LFO's value, softened (LFO_GLIDE) */
    uint32_t cyc, notes;                             /* S&H: cycle number (FREE: global, TRIG: own) and note counter */
    float ph2, inc2, lv2;                            /* LFO 2's, the same */
    uint32_t cyc2;
} mvoice;
static mvoice MV[PQ_VOICES];
/* the LFO's jumps (SAW's return, SQR's edges, S&H's steps) glided over ~2 ms: a one-pole of 1 - exp(-12 / Q_SR / 2 ms)
 * a block. A jump on LEVEL or CUTOFF clicks */
#define LFO_GLIDE 0.127f
#define NRATE 76                                     /* q_sound.lrate 0..75 */
static float RPH[NRATE], RHZ[NRATE - LRATE_SYNC];    /* the FREE LFOs' phase per RATE since mod_restart; free rates' Hz */
static uint32_t RCYC[NRATE];                         /* ... and their cycle count */

/* beats per cycle of the synced rates */
static const float PERIOD[LRATE_SYNC] = { 32, 16, 8, 4, 2, 1, 2.f / 3, 0.5f, 1.f / 3, 0.25f, 1.f / 6, 0.125f };

/* 0.001 * 10000^(x/127) s */
static float env_time(int x) { return 0.001f * fm_powf(10000.f, x / 127.f); }
static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

static float hash_rnd(uint32_t x)                    /* -1..1 */
{
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x * (2.f / 4294967295.f) - 1.f;
}

/* a wave at phase ph; S&H's value from its cycle (and, TRIG, the voice and its notes) */
static float lfo_wave(int shape, int trig, float ph, uint32_t cyc, uint32_t notes, int v)
{
    switch (shape) {
    case LS_SIN: return fm_sinf(6.2831853f * ph);
    case LS_TRI: return ph < 0.25f ? 4 * ph : ph < 0.75f ? 2 - 4 * ph : 4 * ph - 4;
    case LS_SAW: return 2 * ph - 1;
    case LS_SQR: return ph < 0.5f ? 1.f : -1.f;
    default: return hash_rnd(cyc + (trig ? (v + 1) * 0x9E3779B1U + notes * 0x85EBCA6BU : 0));
    }
}

static float lfo_value(const mvoice *s, int v) { return lfo_wave(s->p.lshape, s->p.ltrig, s->ph, s->cyc, s->notes, v); }
/* (another seed for S&H: two LFOs on S&H draw apart) */
static float lfo2_value(const mvoice *s, int v)
{
    return lfo_wave(s->p.l2shape, s->p.l2trig, s->ph2, s->cyc2 + 0x68E31DA4U, s->notes, v);
}

/* an LFO's value l at depth -64..63 onto dest: added to the other LFO's, a gain multiplied */
static void lfo_apply(pq_mod *m, int dest, int depth, float l, float *semi, float *res)
{
    float d = clampf(depth / 63.f, -1, 1), ld = d * l;
    switch (dest) {
    case LD_HARM: m->harm += ld; break;
    case LD_TIMB: m->timb += ld; break;
    case LD_MORPH: m->morph += ld; break;
    case LD_PITCH: m->pitch += 12 * ld; break;
    case LD_CUT: *semi += 60 * ld; break;
    case LD_RES: *res = clampf(*res + ld, 0, 1); break;
    default: m->gain *= 1 - (d < 0 ? -d : d) * (1 - l) / 2; break;
    }
}

void mod_init(void)
{
    __builtin_memset(MV, 0, sizeof MV);
    for (int v = 0; v < PQ_VOICES; v++)
        MV[v].p = (q_vparam){ 0, 64, 127, 20, FT_LP, 127, 0, 0, LS_SIN, 0, 5, 0, LD_CUT, LS_SIN, 0, 5, 0, LD_PITCH };
    for (int i = 0; i < NRATE - LRATE_SYNC; i++) RHZ[i] = 0.05f * __builtin_powf(600.f, i / 63.f);
    mod_restart();
}

void mod_set(int v, const q_vparam *p, int tempo)
{
    mvoice *s = &MV[v];
    s->p = *p; s->tempo = tempo;
    float per_frame = p->lrate < LRATE_SYNC ? tempo / 60.f / Q_SR / PERIOD[p->lrate] : RHZ[p->lrate - LRATE_SYNC] / Q_SR;
    s->inc = per_frame * MOD_BLK;
    if (!p->ltrig) { s->ph = RPH[p->lrate]; s->cyc = RCYC[p->lrate]; }
    per_frame = p->l2rate < LRATE_SYNC ? tempo / 60.f / Q_SR / PERIOD[p->l2rate] : RHZ[p->l2rate - LRATE_SYNC] / Q_SR;
    s->inc2 = per_frame * MOD_BLK;
    if (!p->l2trig) { s->ph2 = RPH[p->l2rate]; s->cyc2 = RCYC[p->l2rate]; }
}

/* a slot freed without a note-off (fade, steal across tracks) keeps a stale level: its attack starts from 0 */
void mod_slot_reset(int v) { MV[v].env = 0; MV[v].stage = E_IDLE; }

void mod_note_on(int v, int accent)
{
    mvoice *s = &MV[v];
    s->acc = accent; s->stage = E_ATK; s->notes++;
    s->astep = (1 - s->env) * (MOD_BLK / Q_SR) / env_time(s->p.atk);
    if (s->p.ltrig) { s->ph = 0; s->cyc = 0; }
    if (s->p.l2trig) { s->ph2 = 0; s->cyc2 = 0; }
}

/* static: clang places a static function where it is first used, here the screen's code, after the render (app.h
 * APP_LATE); used by audio_lfo_phase (audio.c), itself called by the screen; unity build only */
static float mod_lfo_phase(int v, int lfo2) { return lfo2 ? MV[v].ph2 : MV[v].ph; }
void mod_note_off(int v) { if (MV[v].stage != E_IDLE) MV[v].stage = E_REL; }

void mod_clock(int tempo, uint32_t n)
{
    for (int i = 0; i < NRATE; i++) {
        RPH[i] += n * (i < LRATE_SYNC ? tempo / 60.f / Q_SR / PERIOD[i] : RHZ[i - LRATE_SYNC] / Q_SR);
        while (RPH[i] >= 1) { RPH[i] -= 1; RCYC[i]++; }
    }
}

void mod_restart(void) { __builtin_memset(RPH, 0, sizeof RPH); __builtin_memset(RCYC, 0, sizeof RCYC); }

void mod_voice_block(int v, pq_mod *m)
{
    mvoice *s = &MV[v];
    const q_vparam *p = &s->p;
    const float dt = MOD_BLK / Q_SR;
    float sus = p->sus / 127.f;
    switch (s->stage) {
    case E_ATK:
        s->env += s->astep;
        if (s->env >= 1) { s->env = 1; s->stage = E_DEC; }
        break;
    case E_DEC:                                      /* 1 % of the gap left after DEC */
        s->env = sus + (s->env - sus) * fm_expf(-4.6051702f * dt / env_time(p->dec));
        break;
    case E_REL:                                      /* 0.001 left after REL */
        s->env *= fm_expf(-6.9077553f * dt / env_time(p->rel));
        if (s->env < 0.001f) { s->env = 0; s->stage = E_IDLE; }
        break;
    }

    s->lv += (lfo_value(s, v) - s->lv) * LFO_GLIDE;
    if (p->l2depth)                                  /* (DEPTH 0: LFO 2 off, its wave not computed) */
        s->lv2 += (lfo2_value(s, v) - s->lv2) * LFO_GLIDE;
    float semi = 15.5f + p->cutoff * (119.5f / 127) + s->env * (p->envamt / 63.f) * 96 * (s->acc ? 1.5f : 1);
    float res = p->res / 127.f;
    m->env = s->env; m->type = p->ftype;
    m->pitch = m->harm = m->timb = m->morph = 0; m->gain = 1;
    lfo_apply(m, p->ldest, p->ldepth, s->lv, &semi, &res);
    if (p->l2depth)
        lfo_apply(m, p->l2dest, p->l2depth, s->lv2, &semi, &res);
    m->cutoff = clampf(440.f * fm_expf((semi - 69) * (0.69314718f / 12)) / Q_SR, 0.0001f, 0.45f);
    m->res = 0.5f * fm_powf(60.f, res);

    s->ph += s->inc;                                 /* TRIG: own phase; FREE: mod_set re-derives it */
    if (s->ph >= 1) { s->ph -= 1; s->cyc++; }
    s->ph2 += s->inc2;
    if (s->ph2 >= 1) { s->ph2 -= 1; s->cyc2++; }
}

/* Out of line, as seq.c's mul64: inlined 64-bit multiplies among loads corrupt a register (base/bsp/docs/CPU.md) */
static __attribute__((noinline)) uint64_t mul64u(uint64_t a, uint64_t b) { return a * b; }

/* ---- arpeggiator: per track, over the track's events; offsets are frames of the block ---- */

void arp_reset(q_arp *a) { __builtin_memset(a, 0, sizeof *a); a->rng = 1; }
int arp_sounding(const q_arp *a) { return a->snd; }
void arp_unlatch(q_arp *a) { a->unlatch = 1; }

static int held_off(q_arp *a, int note);

void arp_input(q_arp *a, const q_event *e)
{
    a->track = e->track;
    if (a->nin == ARP_IN) {
        if (e->on) return;
        int j = ARP_IN - 1;                          /* a note-off takes the room of the newest note-on */
        while (j >= 0 && !a->in[j].on) j--;
        if (j < 0) { held_off(a, e->note); return; }   /* all note-offs: the held note ends now, not at its date */
        for (; j + 1 < ARP_IN; j++) a->in[j] = a->in[j + 1];
        a->nin--;
    }
    a->in[a->nin++] = *e;
}

static void held_drop(q_arp *a, int j)
{
    for (a->nh--; j < a->nh; j++) a->h[j] = a->h[j + 1];
    if (!a->nh) a->run = 0;                          /* the sounding note still ends at its gate */
}

static int held_off(q_arp *a, int note)              /* a latched note outlives its key; 0: not held */
{
    for (int j = 0; j < a->nh; j++) if (a->h[j].note == note) { if (!a->h[j].latched) held_drop(a, j); return 1; }
    return 0;
}

static void held_on(q_arp *a, const q_event *e, int mode)
{
    int j = 0;
    while (j < a->nh && a->h[j].note != e->note) j++;
    if (j == a->nh) {
        if (j == ARP_HELD) return;
        if (!j) { a->run = mode != AM_OFF; a->i = e->offset; a->r = 0; a->rd = 1; a->idx = 0; }   /* the first step falls here */
        a->nh++;
    }
    a->h[j] = (q_arpn){ e->note, e->vel, e->pat, e->step, (uint8_t)(e->flags & EV_ACCENT),
                        (uint8_t)(mode != AM_OFF && (e->flags & EV_LATCH)) };
}

static void arp_note_off(q_arp *a, uint32_t t, q_event **o)
{
    **o = (q_event){ t, (uint8_t)a->track, a->sn, a->sv, 0, a->sp, a->ss, 0, 0 };
    (*o)++; a->snd = 0;
}

/* the step due at frame t: note, then the clock's advance */
static void arp_step(q_arp *a, const q_arpp *p, uint32_t num, uint32_t den, uint32_t t, q_event **o)
{
    uint8_t ord[ARP_HELD], ln[ARP_HELD * 4], lj[ARP_HELD * 4];
    int L = 0;
    for (int j = 0; j < a->nh; j++) {                /* by pitch, or by arrival for ORDER */
        int k = j;
        if (p->mode != AM_ORDER) while (k > 0 && a->h[ord[k - 1]].note > a->h[j].note) { ord[k] = ord[k - 1]; k--; }
        ord[k] = (uint8_t)j;
    }
    for (int oc = 0; oc < p->oct; oc++)
        for (int m = 0; m < a->nh; m++) {
            int nt = a->h[ord[m]].note + 12 * oc;
            if (nt <= 127) { ln[L] = (uint8_t)nt; lj[L++] = ord[m]; }
        }
    uint32_t P = p->mode == AM_UPDN && L > 1 ? 2 * L - 2 : L, pos = a->idx % P;
    if (p->mode == AM_DOWN) pos = L - 1 - pos;
    else if (p->mode == AM_UPDN && pos >= (uint32_t)L) pos = 2 * L - 2 - pos;
    else if (p->mode == AM_RAND) {
        a->rng ^= a->rng << 13; a->rng ^= a->rng >> 17; a->rng ^= a->rng << 5;
        pos = a->rng % L;
    }
    a->idx = a->idx % P + 1;
    if (a->snd) arp_note_off(a, t, o);
    const q_arpn *h = &a->h[lj[pos]];
    **o = (q_event){ t, (uint8_t)a->track, ln[pos], h->vel, 1, h->pat, h->step, h->flags, 0 };
    (*o)++;
    a->snd = 1; a->sn = ln[pos]; a->sv = h->vel; a->sp = h->pat; a->ss = h->step;
    uint32_t g = (uint32_t)(mul64u(num, p->gate) / mul64u(100, den));
    a->soff = t + (g ? g : 1);

    a->r = (uint32_t)(mul64u(a->r, den) / a->rd); a->rd = den;      /* a tempo change applies from this advance */
    a->i += num / den; a->r += num % den;
    if (a->r >= den) { a->r -= den; a->i++; }
}

int arp_block(q_arp *a, const q_arpp *p, uint32_t num, uint32_t den, uint32_t n, q_event *out)
{
    q_event *o = out;
    int k = 0;
    if (p->mode != a->mode) {
        if (a->snd) arp_note_off(a, 0, &o);
        if (p->mode == AM_OFF) {                     /* the held keys sound directly; latched ones have no key */
            for (int j = a->nh - 1; j >= 0; j--) if (a->h[j].latched) held_drop(a, j);
            for (int j = 0; j < a->nh; j++)
                *o++ = (q_event){ 0, (uint8_t)a->track, a->h[j].note, a->h[j].vel, 1, a->h[j].pat, a->h[j].step, a->h[j].flags, 0 };
            a->run = 0;                              /* the keys stay tracked, for the way back */
        } else if (a->mode == AM_OFF) {
            for (int j = 0; j < a->nh; j++)
                *o++ = (q_event){ 0, (uint8_t)a->track, a->h[j].note, a->h[j].vel, 0, a->h[j].pat, a->h[j].step, 0, 0 };
            a->run = a->nh > 0; a->i = a->r = 0; a->rd = 1; a->idx = 0;
        }
        a->mode = p->mode;
    }
    if (a->unlatch) {
        for (int j = a->nh - 1; j >= 0; j--) if (a->h[j].latched) held_drop(a, j);
        a->unlatch = 0;
    }
    for (;;) {                                       /* by date: the note-off, then the inputs, then the step */
        uint32_t t = n;
        if (k < a->nin && a->in[k].offset < t) t = a->in[k].offset;
        if (a->snd && a->soff < t) t = a->soff;
        if (a->run && a->i < t) t = a->i;
        if (t >= n) break;
        if (a->snd && a->soff == t) arp_note_off(a, t, &o);
        else if (k < a->nin && a->in[k].offset == t) {
            const q_event *e = &a->in[k++];
            if (p->mode == AM_OFF) *o++ = *e;
            if (!e->on) {
                /* a note sounded directly while MODE was OFF but not tracked (a 9th key) has no arp note: its off passes */
                if (!held_off(a, e->note) && p->mode != AM_OFF) *o++ = *e;
            } else {
                if (e->flags & EV_LEGATO) held_off(a, e->from);
                held_on(a, e, p->mode);
            }
        } else arp_step(a, p, num, den, t, &o);
    }
    a->nin = 0;
    if (a->run) a->i -= n;
    if (a->snd) a->soff -= n;
    return (int)(o - out);
}
