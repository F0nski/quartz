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

int main(void)
{
    mono_retriggers_same_slot();
    poly_uses_own_slots();
    balances_cores();
    steals_global_oldest();
    stolen_note_off_ignored();
    released_slot_reused_first();
    puts("voices ok");
    return 0;
}
