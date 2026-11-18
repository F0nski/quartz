#include <assert.h>
#include <stdio.h>
#include "voices.c"

static q_voices V;

static void mono_retriggers_same_slot(void)
{
    voices_init(&V);
    int a = voices_note_on(&V, 0, 60, 1, 0, 0, 0);
    int b = voices_note_on(&V, 0, 64, 1, 0, 0, 0);
    assert(a == b);
    assert(V.v[a].note == 64 && V.v[a].state == V_ON);
}

static void poly_uses_own_slots(void)
{
    voices_init(&V);
    int s[4];
    for (int i = 0; i < 3; i++) s[i] = voices_note_on(&V, 0, 60 + i, 3, 0, 0, 0);
    assert(s[0] != s[1] && s[1] != s[2] && s[0] != s[2]);
    s[3] = voices_note_on(&V, 0, 70, 3, 0, 0, 0);
    assert(s[3] == s[0]);
    assert(V.v[s[3]].note == 70);
}

static void balances_cores(void)
{
    voices_init(&V);
    int n[2] = {0, 0};
    for (int t = 0; t < 4; t++) n[voices_note_on(&V, t, 60, 1, 0, 0, 0) / 3]++;
    for (int t = 0; t < 2; t++) n[voices_note_on(&V, t, 62, 2, 0, 0, 0) / 3]++;
    assert(n[0] == 3 && n[1] == 3);
}

static void steals_global_oldest(void)
{
    voices_init(&V);
    for (int t = 0; t < 4; t++) voices_note_on(&V, t, 60, 1, 0, 0, 0);
    voices_note_on(&V, 0, 62, 2, 0, 0, 0);
    voices_note_on(&V, 1, 62, 2, 0, 0, 0);
    int oldest = 0;
    for (int i = 1; i < Q_VOICES; i++) if (V.v[i].age < V.v[oldest].age) oldest = i;
    int s = voices_note_on(&V, 5, 72, 1, 0, 0, 0);
    assert(s == oldest && V.v[s].track == 5);
}

static void stolen_note_off_ignored(void)
{
    voices_init(&V);
    int s0 = voices_note_on(&V, 0, 60, 1, 0, 0, 0);
    for (int t = 2; t < 7; t++) voices_note_on(&V, t, 50, 1, 0, 0, 0); /* fill the pool */
    int s1 = voices_note_on(&V, 1, 60, 1, 0, 0, 0);
    assert(s1 == s0);
    assert(voices_note_off(&V, 0, 60, 0) == -1);
    assert(V.v[s1].state == V_ON);
}

static void released_slot_reused_first(void)
{
    voices_init(&V);
    int s[Q_VOICES];
    for (int t = 0; t < Q_VOICES; t++) s[t] = voices_note_on(&V, t, 60, 1, 0, 0, 0);
    assert(voices_note_off(&V, 3, 60, 0) == s[3]);
    assert(V.v[s[3]].state == V_RELEASED);
    assert(voices_note_on(&V, 7, 61, 1, 0, 0, 0) == s[3]);
}

static void own_released_slot_stolen_first(void)
{
    voices_init(&V);
    int a = voices_note_on(&V, 0, 60, 2, 0, 0, 0);       /* oldest, still on */
    int b = voices_note_on(&V, 0, 62, 2, 0, 0, 0);
    assert(voices_note_off(&V, 0, 62, 0) == b);
    int c = voices_note_on(&V, 0, 64, 2, 0, 0, 0);       /* at its limit: takes its released slot, not the older V_ON one */
    assert(c == b && V.v[a].state == V_ON && V.v[a].note == 60);
}

static void duplicate_note_off_releases_oldest(void)
{
    voices_init(&V);
    int a = voices_note_on(&V, 0, 60, 2, 0, 0, 0);
    int b = voices_note_on(&V, 0, 60, 2, 0, 0, 0);
    assert(a != b);
    assert(voices_note_off(&V, 0, 60, 0) == a);
    assert(V.v[a].state == V_RELEASED && V.v[b].state == V_ON);
    assert(voices_note_off(&V, 0, 60, 0) == b);
    assert(voices_note_off(&V, 0, 60, 0) == -1);
}

static void poly_is_clamped(void)
{
    voices_init(&V);
    int a = voices_note_on(&V, 0, 60, 0, 0, 0, 0);       /* poly 0 acts as 1: retriggers, no v[-1] */
    assert(a >= 0 && voices_note_on(&V, 0, 61, -3, 0, 0, 0) == a);
    voices_init(&V);
    int s[5];
    for (int i = 0; i < 5; i++) s[i] = voices_note_on(&V, 0, 60 + i, 9, 0, 0, 0);    /* poly 9 acts as 4 */
    assert(s[4] == s[0]);
}

static void free_and_release_track(void)
{
    voices_init(&V);
    int a = voices_note_on(&V, 0, 60, 1, 0, 0, 0), b = voices_note_on(&V, 1, 60, 2, 0, 0, 0), c = voices_note_on(&V, 1, 62, 2, 0, 0, 0);
    voices_free(&V, a);
    assert(V.v[a].state == V_FREE && V.v[a].track == -1);
    assert(voices_note_off(&V, 0, 60, 0) == -1);
    voices_note_off(&V, 1, 62, 0);                 /* a released slot is freed with its track too */
    voices_release_track(&V, 1);
    assert(V.v[b].state == V_FREE && V.v[c].state == V_FREE && V.v[b].track == -1 && V.v[c].track == -1);
    int d = voices_note_on(&V, 2, 50, 1, 0, 0, 0);
    voices_release_track(&V, 1);                /* other tracks are untouched */
    assert(V.v[d].state == V_ON && V.v[d].track == 2);
}

static void free_slot_with_model_preferred(void)
{
    voices_init(&V);
    assert(V.v[0].model == -1 && V.v[5].model == -1);   /* unknown until the renderer sets it */
    V.v[4].model = 20;
    assert(voices_note_on(&V, 0, 60, 1, 20, 0, 0) == 4);      /* over slot 0, the first of the least loaded core */
    V.v[1].model = 20;
    V.v[2].model = 20;
    assert(voices_note_on(&V, 1, 60, 1, 20, 0, 0) == 1);      /* matching on both cores: the least loaded first */
    assert(voices_note_on(&V, 2, 60, 1, 13, 0, 0) == 0);      /* none matching: as before */
    /* the released fallback: the matching one over the oldest */
    voices_init(&V);
    int s[Q_VOICES];
    for (int t = 0; t < Q_VOICES; t++) s[t] = voices_note_on(&V, t, 60, 1, 0, 0, 0);
    V.v[s[3]].model = 13;
    voices_note_off(&V, 1, 60, 0);                 /* the oldest released */
    voices_note_off(&V, 3, 60, 0);
    assert(voices_note_on(&V, 7, 61, 1, 13, 0, 0) == s[3]);
    assert(voices_note_on(&V, 8, 61, 1, 13, 0, 0) == s[1]);
}

static void prep_never_allocated(void)
{
    voices_init(&V);
    V.v[2].state = V_PREP;
    V.v[2].model = 20;
    for (int i = 0; i < 20; i++) assert(voices_note_on(&V, i % 7, 60 + i, 1 + i % 3, 20, 0, 0) != 2);
    assert(V.v[2].state == V_PREP);
    int n[2] = {0, 0};                          /* not rendered: not a core's load */
    voices_init(&V);
    V.v[0].state = V_PREP;
    for (int t = 0; t < 3; t++) n[voices_note_on(&V, t, 60, 1, 0, 0, 0) / 3]++;
    assert(n[0] == 2 && n[1] == 1);             /* counted, core 1 would get the third */
}

static void unwanted_free_slot_before_another_tracks(void)
{
    voices_init(&V);
    V.v[0].model = 13;                          /* prepared for another track */
    V.v[1].model = 8;                           /* no track wants it */
    assert(voices_note_on(&V, 0, 60, 1, 20, 1u << 13, 0) == 1);
    V.v[3].model = 13;
    V.v[4].model = -1;                          /* unknown: as good as unwanted */
    assert(voices_note_on(&V, 1, 60, 1, 20, 1u << 13, 0) == 4);   /* core 2, the least loaded */
    for (int v = 0; v < Q_VOICES; v++) if (V.v[v].state == V_FREE) V.v[v].model = 13;
    assert(V.v[voices_note_on(&V, 2, 60, 1, 20, 1u << 13, 0)].model == 20);   /* only wanted ones left: one of them */
}

static void own_counts_only_the_tracks_model(void)
{
    voices_init(&V);
    int a = voices_note_on(&V, 0, 60, 1, 8, 0, 0);
    assert(V.v[a].model == 8);                  /* the engine the slot will hold */
    V.v[a].state = V_RELEASED;                  /* a model change: fading on the old engine */
    V.v[3].model = 20;
    int b = voices_note_on(&V, 0, 62, 1, 20, 0, 0);
    assert(b == 3 && V.v[a].state == V_RELEASED);   /* not a mono retrigger of the fading slot */
    assert(voices_note_on(&V, 0, 64, 1, 20, 0, 0) == b);   /* on its model: retriggered */
}

static void retarget_takes_the_from_note(void)
{
    voices_init(&V);
    int a = voices_note_on(&V, 0, 60, 3, 0, 0, 0);
    int b = voices_note_on(&V, 0, 64, 3, 0, 0, 0);
    int c = voices_note_on(&V, 0, 67, 3, 0, 0, 0);     /* newer, held */
    voices_note_on(&V, 1, 64, 1, 0, 0, 0);             /* another track's, same note */
    assert(voices_retarget(&V, 0, 64, 72) == b);
    assert(V.v[b].note == 72 && V.v[b].state == V_ON && V.v[a].note == 60 && V.v[c].note == 67);
    assert(voices_note_off(&V, 0, 72, 0) == b);
}

static void retarget_none_without_on(void)
{
    voices_init(&V);
    assert(voices_retarget(&V, 0, 60, 62) == -1);
    int a = voices_note_on(&V, 0, 60, 2, 0, 0, 0);
    voices_note_off(&V, 0, 60, 0);                     /* released: not V_ON */
    voices_note_on(&V, 1, 60, 1, 0, 0, 0);
    int b = voices_note_on(&V, 0, 64, 2, 0, 0, 0);     /* the track's, another note */
    assert(voices_retarget(&V, 0, 60, 62) == -1 && V.v[a].note == 60 && V.v[b].note == 64);
}

static void keyboard_has_its_own_poly(void)
{
    voices_init(&V);
    int k = voices_note_on(&V, 0, 72, 1, 0, 0, 1);      /* the keyboard's, held */
    int a = voices_note_on(&V, 0, 60, 1, 0, 0, 0);
    int b = voices_note_on(&V, 0, 62, 1, 0, 0, 0);      /* the sequencer's mono retrigger */
    assert(k != a && a == b && V.v[k].state == V_ON && V.v[k].note == 72);
    assert(voices_note_on(&V, 0, 74, 1, 0, 0, 1) == k);   /* the keyboard's own mono retrigger */
    voices_note_on(&V, 0, 64, 2, 0, 0, 0);
    assert(voices_note_off(&V, 0, 74, 0) == -1 && V.v[k].state == V_ON);   /* a step's note-off, same note */
    assert(voices_retarget(&V, 0, 74, 76) == -1);       /* a step's legato */
    assert(voices_note_off(&V, 0, 74, 1) == k);
}

/* no slot free nor released: a note takes its own track's oldest note (a KIT's other engine) before another track's */
static void steals_own_track_first(void)
{
    voices_init(&V);
    for (int t = 1; t < 3; t++)                          /* the chords first: older than the KIT's hits */
        for (int k = 0; k < 2; k++) voices_note_on(&V, t, 60 + k, 2, 8, 0, 0);
    int bd = voices_note_on(&V, 0, 36, 1, 21, 0, 0);    /* track 0, a KIT: BD then SN */
    int sn = voices_note_on(&V, 0, 37, 1, 22, 0, 0);
    assert(sn != bd);
    for (int v = 0; v < Q_VOICES; v++) assert(V.v[v].state == V_ON);
    assert(voices_pick(&V, 0, 1, 23, 0, 0) == bd);       /* HH: the KIT's oldest, not track 1's older chord note */
    voices_note_off(&V, 1, 61, 0);                       /* a released tail goes first, whatever its track */
    int tail = -1;
    for (int v = 0; v < Q_VOICES; v++) if (V.v[v].state == V_RELEASED) tail = v;
    assert(voices_pick(&V, 0, 1, 23, 0, 0) == tail);
    assert(voices_pick(&V, 3, 1, 8, 0, 0) == tail);
    voices_note_on(&V, 3, 50, 1, 8, 0, 0);               /* no own note, nothing released: the global oldest */
    int oldest = 0;
    for (int v = 1; v < Q_VOICES; v++) if (V.v[v].age < V.v[oldest].age) oldest = v;
    assert(voices_pick(&V, 4, 1, 8, 0, 0) == oldest);
}

int main(void)
{
    steals_own_track_first();
    mono_retriggers_same_slot();
    poly_uses_own_slots();
    balances_cores();
    steals_global_oldest();
    stolen_note_off_ignored();
    released_slot_reused_first();
    own_released_slot_stolen_first();
    duplicate_note_off_releases_oldest();
    poly_is_clamped();
    free_and_release_track();
    free_slot_with_model_preferred();
    prep_never_allocated();
    unwanted_free_slot_before_another_tracks();
    own_counts_only_the_tracks_model();
    retarget_takes_the_from_note();
    retarget_none_without_on();
    keyboard_has_its_own_poly();
    puts("voices ok");
    return 0;
}
