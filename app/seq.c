/* No libc headers: the device builds the app's C freestanding, so the compiler's builtins stand in. */
#include "seq.h"

void project_defaults(q_project *p)
{
    __builtin_memset(p, 0, sizeof *p);
    p->version = Q_VERSION;
    p->tempo = 120;
    p->swing = 50;
    p->mode = 1;                    /* MAJOR */
    p->quant = 2;
    for (int i = 0; i < Q_SONG; i++) p->song[i].reps = 1;   /* 1..16: what the load's clamp would make of 0 */
    for (int i = 0; i < Q_TRACKS; i++) {
        q_sound *s = &p->sound[i];
        s->model = 8; s->poly = 1; s->level = 100;
        s->harm = s->timb = s->morph = s->decay = 64;
        for (int n = 0; n < Q_PATTERNS; n++) {
            q_pattern *pt = &p->pat[i][n];
            pt->len = 16; pt->div = DIV_16; pt->path = PATH_FWD;
            pt->xlen = pt->ylen = 4; pt->ydiv = DIV_4;
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

void seq_start(q_seq *s)
{
    uint64_t now = s->now;
    __builtin_memset(s, 0, sizeof *s);
    s->now = now;
    s->running = 1;
    for (int t = 0; t < Q_TRACKS; t++) {
        s->t[t].cur = -1;
        s->t[t].next_i = now;
    }
}

void seq_stop(q_seq *s, q_event *out, int *n)
{
    *n = 0;
    for (int t = 0; t < Q_TRACKS; t++) {
        if (s->t[t].playing)
            out[(*n)++] = (q_event){ 0, (uint8_t)t, s->t[t].note, 0, 0 };
        s->t[t].playing = 0;
    }
    s->running = 0;
}

int seq_pos(const q_seq *s, int track)
{
    if (!s->running) return -1;
    return s->t[track].cur < 0 ? 0 : s->t[track].cur;
}

static void emit(q_event *out, int *n, int max, uint64_t at, uint64_t now, int t, uint8_t note, uint8_t vel, int on)
{
    if (*n < max) out[(*n)++] = (q_event){ (uint32_t)(at - now), (uint8_t)t, note, vel, (uint8_t)on };
}

int seq_block(q_seq *s, const q_project *p, uint32_t nframes, q_event *out, int max)
{
    int n = 0;
    uint64_t end = s->now + nframes;
    if (!s->running) return 0;

    for (int t = 0; t < Q_TRACKS; t++) {
        q_seq_track *k = &s->t[t];
        const q_pattern *pt = &p->pat[t][p->pattern[t]];

        /* the step in progress keeps its duration: carry the fractional start of the next step into the new
           denominator, rounded up like a start frame (r * den' < 2^32 for tempo <= 240) */
        if (p->tempo != k->tempo || pt->div != k->div) {
            uint32_t num, den;
            seq_step_ratio(p->tempo, pt->div, &num, &den);
            if (k->den) {
                uint32_t r = k->next_r * den;
                k->next_r = r / k->den + (r % k->den > 0);
            }
            k->tempo = p->tempo;
            k->div = pt->div;
            k->num = num;
            k->den = den;
        }

        for (;;) {
            uint64_t start = k->next_i + (k->next_r > 0);
            if (k->playing && k->off_at < end) {
                emit(out, &n, max, k->off_at, s->now, t, k->note, 0, 0);
                k->playing = 0;
            } else if (start < end) {
                k->cur = k->cur + 1 >= pt->len ? 0 : k->cur + 1;
                const q_step *st = &pt->steps[k->cur];
                if (st->flags & SF_ON) {
                    /* half a step on, over 2 x den so that an odd num stays exact */
                    uint32_t r = 2 * k->next_r + k->num;
                    emit(out, &n, max, start, s->now, t, st->note[0], st->vel, 1);
                    k->playing = 1;
                    k->note = st->note[0];
                    k->off_at = k->next_i + r / (2 * k->den) + (r % (2 * k->den) > 0);
                }
                k->next_r += k->num;
                k->next_i += k->next_r / k->den;
                k->next_r %= k->den;
            } else {
                break;
            }
        }
    }

    /* insertion sort, stable: a track's note-off stays before its next note-on */
    for (int i = 1; i < n; i++) {
        q_event e = out[i];
        int j = i;
        for (; j > 0 && out[j - 1].offset > e.offset; j--) out[j] = out[j - 1];
        out[j] = e;
    }
    s->now = end;
    return n;
}
