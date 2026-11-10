/* No libc headers: the device builds the app's C freestanding, so the compiler's builtins stand in. */
#include "voices.h"

void voices_init(q_voices *p)
{
    __builtin_memset(p, 0, sizeof *p);
    for (int i = 0; i < Q_VOICES; i++) p->v[i].track = p->v[i].model = -1;
}

/* oldest slot in the given state; track >= 0 restricts it to that track's slots, model >= 0 to that engine's */
static int oldest(const q_voices *p, int state, int track, int model)
{
    int best = -1;
    for (int i = 0; i < Q_VOICES; i++)
        if (p->v[i].state == state && (track < 0 || p->v[i].track == track) && (model < 0 || p->v[i].model == model) &&
            (best < 0 || p->v[i].age < p->v[best].age)) best = i;
    return best;
}

/* the first free slot from slot `first` on, holding `model` if >= 0, else none of the models in `avoid` */
static int first_free(const q_voices *p, int first, int model, uint32_t avoid)
{
    for (int k = 0; k < Q_VOICES; k++) {
        int i = (first + k) % Q_VOICES, m = p->v[i].model;
        if (p->v[i].state == V_FREE && (model >= 0 ? m == model : m < 0 || !(avoid >> m & 1u))) return i;
    }
    return -1;
}

int voices_pick(const q_voices *p, int track, int poly, int model, uint32_t wanted)
{
    int own = 0, load[2] = {0, 0}, slot;
    if (poly < 1) poly = 1;
    if (poly > 4) poly = 4;
    for (int i = 0; i < Q_VOICES; i++) {
        if (p->v[i].state == V_FREE || p->v[i].state == V_PREP) continue;
        load[i / 3]++;
        if (p->v[i].track != track || p->v[i].model != model) continue;
        own++;
    }
    if (own >= poly) {
        slot = oldest(p, V_RELEASED, track, model);     /* covers mono retrigger */
        if (slot < 0) slot = oldest(p, V_ON, track, model);
    } else {
        int first = load[1] < load[0] ? 3 : 0;      /* least loaded core first */
        slot = first_free(p, first, model, 0);
        if (slot < 0) slot = first_free(p, first, -1, wanted);  /* not an engine prepared for another track */
        if (slot < 0) slot = first_free(p, first, -1, 0);
        if (slot < 0) slot = oldest(p, V_RELEASED, -1, model);
        if (slot < 0) slot = oldest(p, V_RELEASED, -1, -1);
        if (slot < 0) slot = oldest(p, V_ON, -1, -1);
    }
    return slot;
}

int voices_note_on(q_voices *p, int track, int note, int poly, int model, uint32_t wanted)
{
    int slot = voices_pick(p, track, poly, model, wanted);
    q_vslot *s = &p->v[slot];
    s->state = V_ON; s->track = (int8_t)track; s->note = (uint8_t)note; s->model = (int8_t)model; s->age = ++p->clock;
    return slot;
}

int voices_note_off(q_voices *p, int track, int note)
{
    int best = -1;      /* the oldest match when the note is held twice */
    for (int i = 0; i < Q_VOICES; i++) {
        q_vslot *s = &p->v[i];
        if (s->state == V_ON && s->track == track && s->note == note && (best < 0 || s->age < p->v[best].age)) best = i;
    }
    if (best >= 0) p->v[best].state = V_RELEASED;
    return best;
}

int voices_retarget(q_voices *p, int track, int from, int to)
{
    int best = -1;      /* the oldest match, as voices_note_off */
    for (int i = 0; i < Q_VOICES; i++) {
        q_vslot *s = &p->v[i];
        if (s->state == V_ON && s->track == track && s->note == from && (best < 0 || s->age < p->v[best].age)) best = i;
    }
    if (best >= 0) p->v[best].note = (uint8_t)to;
    return best;
}

void voices_free(q_voices *p, int slot)
{
    p->v[slot].state = V_FREE;
    p->v[slot].track = -1;
}

void voices_release_track(q_voices *p, int track)
{
    for (int i = 0; i < Q_VOICES; i++)
        if (p->v[i].track == track) voices_free(p, i);
}
