#include <assert.h>
#include <stdio.h>
#include "voices.c"

static q_voices V;

static void mono_retriggers_same_slot(void)
{
    voices_init(&V);
    int a = voices_note_on(&V, 0, 60, 1);
    int b = voices_note_on(&V, 0, 64, 1);
    assert(a == b);
    assert(V.v[a].note == 64 && V.v[a].state == V_ON);
}

static void poly_uses_own_slots(void)
{
    voices_init(&V);
    int s[4];
    for (int i = 0; i < 3; i++) s[i] = voices_note_on(&V, 0, 60 + i, 3);
    assert(s[0] != s[1] && s[1] != s[2] && s[0] != s[2]);
    s[3] = voices_note_on(&V, 0, 70, 3);
    assert(s[3] == s[0]);
    assert(V.v[s[3]].note == 70);
}

static void balances_cores(void)
{
    voices_init(&V);
    int n[2] = {0, 0};
    for (int t = 0; t < 4; t++) n[voices_note_on(&V, t, 60, 1) / 3]++;
    for (int t = 0; t < 2; t++) n[voices_note_on(&V, t, 62, 2) / 3]++;
    assert(n[0] == 3 && n[1] == 3);
}

static void steals_global_oldest(void)
{
    voices_init(&V);
    for (int t = 0; t < 4; t++) voices_note_on(&V, t, 60, 1);
    voices_note_on(&V, 0, 62, 2);
    voices_note_on(&V, 1, 62, 2);
    int oldest = 0;
    for (int i = 1; i < Q_VOICES; i++) if (V.v[i].age < V.v[oldest].age) oldest = i;
    int s = voices_note_on(&V, 5, 72, 1);
    assert(s == oldest && V.v[s].track == 5);
}

static void stolen_note_off_ignored(void)
{
    voices_init(&V);
    int s0 = voices_note_on(&V, 0, 60, 1);
    for (int t = 2; t < 7; t++) voices_note_on(&V, t, 50, 1); /* fill the pool */
    int s1 = voices_note_on(&V, 1, 60, 1);
    assert(s1 == s0);
    assert(voices_note_off(&V, 0, 60) == -1);
    assert(V.v[s1].state == V_ON);
}

static void released_slot_reused_first(void)
{
    voices_init(&V);
    int s[Q_VOICES];
    for (int t = 0; t < Q_VOICES; t++) s[t] = voices_note_on(&V, t, 60, 1);
    assert(voices_note_off(&V, 3, 60) == s[3]);
    assert(V.v[s[3]].state == V_RELEASED);
    assert(voices_note_on(&V, 7, 61, 1) == s[3]);
}

static void own_released_slot_stolen_first(void)
{
    voices_init(&V);
    int a = voices_note_on(&V, 0, 60, 2);       /* oldest, still on */
    int b = voices_note_on(&V, 0, 62, 2);
    assert(voices_note_off(&V, 0, 62) == b);
    int c = voices_note_on(&V, 0, 64, 2);       /* at its limit: takes its released slot, not the older V_ON one */
    assert(c == b && V.v[a].state == V_ON && V.v[a].note == 60);
}

static void duplicate_note_off_releases_oldest(void)
{
    voices_init(&V);
    int a = voices_note_on(&V, 0, 60, 2);
    int b = voices_note_on(&V, 0, 60, 2);
    assert(a != b);
    assert(voices_note_off(&V, 0, 60) == a);
    assert(V.v[a].state == V_RELEASED && V.v[b].state == V_ON);
    assert(voices_note_off(&V, 0, 60) == b);
    assert(voices_note_off(&V, 0, 60) == -1);
}

static void poly_is_clamped(void)
{
    voices_init(&V);
    int a = voices_note_on(&V, 0, 60, 0);       /* poly 0 acts as 1: retriggers, no v[-1] */
    assert(a >= 0 && voices_note_on(&V, 0, 61, -3) == a);
    voices_init(&V);
    int s[5];
    for (int i = 0; i < 5; i++) s[i] = voices_note_on(&V, 0, 60 + i, 9);    /* poly 9 acts as 4 */
    assert(s[4] == s[0]);
}

static void free_and_release_track(void)
{
    voices_init(&V);
    int a = voices_note_on(&V, 0, 60, 1), b = voices_note_on(&V, 1, 60, 2), c = voices_note_on(&V, 1, 62, 2);
    voices_free(&V, a);
    assert(V.v[a].state == V_FREE && V.v[a].track == -1);
    assert(voices_note_off(&V, 0, 60) == -1);
    voices_note_off(&V, 1, 62);                 /* a released slot is freed with its track too */
    voices_release_track(&V, 1);
    assert(V.v[b].state == V_FREE && V.v[c].state == V_FREE && V.v[b].track == -1 && V.v[c].track == -1);
    int d = voices_note_on(&V, 2, 50, 1);
    voices_release_track(&V, 1);                /* other tracks are untouched */
    assert(V.v[d].state == V_ON && V.v[d].track == 2);
}

int main(void)
{
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
    puts("voices ok");
    return 0;
}
