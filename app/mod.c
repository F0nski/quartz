/* Per-voice ADSR and LFO. mod_voice_block(v) touches only voice v's state (it runs on either core). */
#include "project.h"
#include "mod.h"
#include "dsp/pq.h"

#define MOD_BLK 12                                   /* frames per Plaits block */
enum { E_IDLE, E_ATK, E_DEC, E_REL };

typedef struct {
    q_vparam p; int tempo, stage, acc;
    float env, astep;                                /* astep: the attack's slope per block */
    float ph, inc;                                   /* LFO phase 0..1 and its advance per block */
    uint32_t cyc, notes;                             /* S&H: cycle number (FREE: global, TRIG: own) and note counter */
} mvoice;
static mvoice MV[PQ_VOICES];
#define NRATE 76                                     /* q_sound.lrate 0..75 */
static float RPH[NRATE], RHZ[NRATE - LRATE_SYNC];    /* the FREE LFOs' phase per RATE since mod_restart; free rates' Hz */
static uint32_t RCYC[NRATE];                         /* ... and their cycle count */

/* beats per cycle of the synced rates */
static const float PERIOD[LRATE_SYNC] = { 32, 16, 8, 4, 2, 1, 2.f / 3, 0.5f, 1.f / 3, 0.25f, 1.f / 6, 0.125f };

/* 0.001 * 10000^(x/127) s */
static float env_time(int x) { return 0.001f * __builtin_powf(10000.f, x / 127.f); }
static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

static float hash_rnd(uint32_t x)                    /* -1..1 */
{
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x * (2.f / 4294967295.f) - 1.f;
}

static float lfo_value(const mvoice *s, int v)
{
    float ph = s->ph;
    switch (s->p.lshape) {
    case LS_SIN: return __builtin_sinf(6.2831853f * ph);
    case LS_TRI: return ph < 0.25f ? 4 * ph : ph < 0.75f ? 2 - 4 * ph : 4 * ph - 4;
    case LS_SAW: return 2 * ph - 1;
    case LS_SQR: return ph < 0.5f ? 1.f : -1.f;
    default: return hash_rnd(s->cyc + (s->p.ltrig ? (v + 1) * 0x9E3779B1U + s->notes * 0x85EBCA6BU : 0));
    }
}

void mod_init(void)
{
    __builtin_memset(MV, 0, sizeof MV);
    for (int v = 0; v < PQ_VOICES; v++) MV[v].p = (q_vparam){ 0, 64, 127, 20, FT_LP, 127, 0, 0, LS_SIN, 0, 5, 0, LD_CUT };
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
}

void mod_note_on(int v, int accent)
{
    mvoice *s = &MV[v];
    s->acc = accent; s->stage = E_ATK; s->notes++;
    s->astep = s->p.atk ? (1 - s->env) * (MOD_BLK / Q_SR) / env_time(s->p.atk) : 1.f;
    if (s->p.ltrig) { s->ph = 0; s->cyc = 0; }
}

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
        s->env = sus + (s->env - sus) * __builtin_expf(-4.6051702f * dt / env_time(p->dec));
        break;
    case E_REL:                                      /* 0.001 left after REL */
        s->env *= __builtin_expf(-6.9077553f * dt / env_time(p->rel));
        if (s->env < 0.001f) { s->env = 0; s->stage = E_IDLE; }
        break;
    }

    float l = lfo_value(s, v), d = clampf(p->ldepth / 63.f, -1, 1), ld = d * l;
    float semi = 15.5f + p->cutoff * (119.5f / 127) + s->env * (p->envamt / 63.f) * 96 * (s->acc ? 1.5f : 1);
    float res = p->res / 127.f;
    m->env = s->env; m->type = p->ftype;
    m->pitch = m->harm = m->timb = m->morph = 0; m->gain = 1;
    switch (p->ldest) {
    case LD_HARM: m->harm = ld; break;
    case LD_TIMB: m->timb = ld; break;
    case LD_MORPH: m->morph = ld; break;
    case LD_PITCH: m->pitch = 12 * ld; break;
    case LD_CUT: semi += 60 * ld; break;
    case LD_RES: res = clampf(res + ld, 0, 1); break;
    default: m->gain = 1 - (d < 0 ? -d : d) * (1 - l) / 2; break;
    }
    m->cutoff = clampf(440.f * __builtin_expf((semi - 69) * (0.69314718f / 12)) / Q_SR, 0.0001f, 0.45f);
    m->res = 0.5f * __builtin_powf(60.f, res);

    s->ph += s->inc;                                 /* TRIG: own phase; FREE: mod_set re-derives it */
    if (s->ph >= 1) { s->ph -= 1; s->cyc++; }
}
