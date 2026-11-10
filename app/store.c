/* The project in flash: the q_project as is, one store object. Part of the unity build (app.c). */
#include "audio.h"
_Static_assert(sizeof(q_project) <= PLAT_STORE_MAX, "q_project must fit one store object");

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* a valid-CRC object can still hold out-of-range fields: the render indexes and divides with them */
static void clamp_project(q_project *p)
{
    p->tempo = (uint16_t)clampi(p->tempo, 40, 240);
    for (int i = 0; i < Q_TRACKS; i++) {
        q_sound *s = &p->sound[i];
        s->model = (uint8_t)clampi(s->model, 0, 23);
        s->poly = (uint8_t)clampi(s->poly, 1, 4);
        s->tune = (int8_t)clampi(s->tune, -24, 24);
        s->glide = (uint8_t)clampi(s->glide, 0, 127);
        s->level = (uint8_t)clampi(s->level, 0, 127);
        s->harm = (uint8_t)clampi(s->harm, 0, 127);
        s->timb = (uint8_t)clampi(s->timb, 0, 127);
        s->morph = (uint8_t)clampi(s->morph, 0, 127);
        s->decay = (uint8_t)clampi(s->decay, 0, 127);
        q_pattern *q = &p->pat[i];
        q->len = (uint8_t)clampi(q->len, 1, Q_STEPS);
        q->div = (uint8_t)clampi(q->div, 0, DIV_N - 1);
        for (int k = 0; k < Q_STEPS; k++) {
            q->steps[k].on = q->steps[k].on != 0;
            q->steps[k].note = (uint8_t)clampi(q->steps[k].note, 0, 127);
            q->steps[k].vel = (uint8_t)clampi(q->steps[k].vel, 0, 127);
        }
    }
}

int store_load(q_project *p)
{
    if (plat_store_size(OBJ_PROJECT) == (int)sizeof *p) {   /* (plat_store_load cuts a longer object to fit) */
        q_project t;
        if (plat_store_load(OBJ_PROJECT, &t, sizeof t) >= 0 && t.version == Q_VERSION) {
            clamp_project(&t);
            *p = t;
            return 0;
        }
    }
    project_defaults(p);
    return -1;
}

/* a flash write stalls the render: not while playing */
int store_save(const q_project *p)
{
    if (audio_playing()) return -1;
    return plat_store_save(OBJ_PROJECT, p, sizeof *p);
}
