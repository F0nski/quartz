#include <math.h>
#include <string.h>
#include "seq.h"

void project_defaults(q_project *p)
{
    memset(p, 0, sizeof *p);
    p->version = 1;
    p->tempo = 120;
    for (int i = 0; i < Q_TRACKS; i++) {
        q_sound *s = &p->sound[i];
        s->model = 8; s->poly = 1; s->level = 100;
        s->harm = s->timb = s->morph = s->decay = 64;
        q_pattern *pt = &p->pat[i];
        pt->len = 16; pt->div = DIV_16;
        for (int k = 0; k < Q_STEPS; k++) { pt->steps[k].note = 60; pt->steps[k].vel = 100; }
    }
}

double seq_step_frames(int tempo, int div)
{
    static const double beats[DIV_N] = { 1. / 8, 1. / 6, 1. / 4, 1. / 3, 1. / 2, 2. / 3, 1, 2, 4 };
    return (double)Q_SR * 60 / tempo * beats[div];
}

void seq_start(q_seq *s)
{
    uint64_t now = s->now;
    memset(s, 0, sizeof *s);
    s->now = now;
    s->running = 1;
    for (int t = 0; t < Q_TRACKS; t++) {
        s->t[t].cur = -1;
        s->t[t].next_start = now;
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
        __typeof__(s->t[0]) *k = &s->t[t];
        const q_pattern *pt = &p->pat[t];

        /* the step in progress keeps its duration: re-anchor on the start of the next one */
        if (p->tempo != k->tempo || pt->div != k->div) {
            k->anchor_f = (double)k->next_start;
            k->anchor_n = k->n;
            k->tempo = p->tempo;
            k->div = pt->div;
        }
        double spf = seq_step_frames(k->tempo, k->div);

        for (;;) {
            if (k->playing && k->off_at < end) {
                emit(out, &n, max, k->off_at, s->now, t, k->note, 0, 0);
                k->playing = 0;
            } else if (k->next_start < end) {
                uint64_t start = k->next_start;
                k->cur = k->cur + 1 >= pt->len ? 0 : k->cur + 1;
                const q_step *st = &pt->steps[k->cur];
                if (st->on) {
                    emit(out, &n, max, start, s->now, t, st->note, st->vel, 1);
                    k->playing = 1;
                    k->note = st->note;
                    k->off_at = (uint64_t)ceil(k->anchor_f + (double)(k->n - k->anchor_n + 0.5) * spf);
                }
                k->n++;
                k->next_start = (uint64_t)ceil(k->anchor_f + (double)(k->n - k->anchor_n) * spf);
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
