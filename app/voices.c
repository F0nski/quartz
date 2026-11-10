#include <string.h>
#include "voices.h"

void voices_init(q_voices *p)
{
    memset(p, 0, sizeof *p);
    for (int i = 0; i < Q_VOICES; i++) p->v[i].track = -1;
}

static int oldest(const q_voices *p, int state)
{
    int best = -1;
    for (int i = 0; i < Q_VOICES; i++)
        if (p->v[i].state == state && (best < 0 || p->v[i].age < p->v[best].age)) best = i;
    return best;
}

int voices_note_on(q_voices *p, int track, int note, int poly)
{
    int own = 0, mine = -1, load[2] = {0, 0}, slot = -1;
    for (int i = 0; i < Q_VOICES; i++) {
        if (p->v[i].state == V_FREE) continue;
        load[i / 3]++;
        if (p->v[i].track != track) continue;
        own++;
        if (mine < 0 || p->v[i].age < p->v[mine].age) mine = i;
    }
    if (own >= poly) {
        slot = mine;                /* covers mono retrigger */
    } else {
        int first = load[1] < load[0] ? 3 : 0;      /* least loaded core first */
        for (int k = 0; k < Q_VOICES && slot < 0; k++) {
            int i = (first + k) % Q_VOICES;
            if (p->v[i].state == V_FREE) slot = i;
        }
        if (slot < 0) slot = oldest(p, V_RELEASED);
        if (slot < 0) slot = oldest(p, V_ON);
    }
    q_vslot *s = &p->v[slot];
    s->state = V_ON; s->track = (int8_t)track; s->note = (uint8_t)note; s->age = ++p->clock;
    return slot;
}

int voices_note_off(q_voices *p, int track, int note)
{
    for (int i = 0; i < Q_VOICES; i++) {
        q_vslot *s = &p->v[i];
        if (s->state == V_ON && s->track == track && s->note == note) {
            s->state = V_RELEASED;
            return i;
        }
    }
    return -1;
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
