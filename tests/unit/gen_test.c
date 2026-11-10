#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "gen.c"
#include "seq.c"

static q_project P;
static q_gen G;
static uint32_t R = 12345;

static q_pattern *setup(int model)
{
    project_defaults(&P);
    P.sound[0].model = model;
    q_pattern *pt = &P.pat[0][0];
    pt->src = SRC_GRIDS;
    pt->grids = (q_grids){ 0, 0, 0, { 127, 127, 127 }, 1, 60 };
    memset(&G, 0, sizeof G);
    return pt;
}

static int mix(int a, int b, int bal) { return (a * (255 - bal) + b * bal) >> 8; }

/* the step k's flags & SF_ON; its notes in *st, its accented slots in *acc */
static int played(const q_pattern *pt, int k, q_step *st, int *acc)
{
    memset(st, 0, sizeof *st);
    *acc = gen_step(&G, &P, 0, pt, k, &R, st);
    return st->flags & SF_ON;
}

static void corner_follows_node(void)
{
    q_pattern *pt = setup(M_KIT);
    for (int k = 0; k < 32; k++) {
        q_step st; int acc;
        int on = played(pt, k, &st, &acc);
        int want_on = 0, want_acc = 0, slot = 0;
        for (int i = 0; i < 3; i++) {
            int o = i * 32 + k;
            int lvl = mix(mix(node_10[o], node_15[o], 0), mix(node_8[o], node_7[o], 0), 0);
            if (lvl > 0) {
                want_on = 1;
                assert(st.note[slot] == KIT_NOTE + i);
                if (lvl > 192) want_acc |= 1 << slot;
                slot++;
            }
        }
        assert(!!on == want_on);
        assert(acc == want_acc);
        if (on) { assert(st.vel == 95 && st.len == 4 && st.micro == 0 && STEP_RATCHET(&st) == 1); }
        if (slot < 4) assert(st.note[slot] == 0);
    }
}

static void dens0_silent(void)
{
    q_pattern *pt = setup(M_KIT);
    pt->grids.dens[0] = pt->grids.dens[1] = pt->grids.dens[2] = 0;
    for (int k = 0; k < 64; k++) {
        q_step st; int acc;
        assert(!played(pt, k, &st, &acc) && acc == 0);
    }
}

/* x = 16: u8 = 32, cell 0, bal = 128; y = 0 */
static void interpolates(void)
{
    q_pattern *pt = setup(M_KIT);
    pt->grids.x = 16;
    int seen = 0;
    for (int k = 0; k < 32; k++) {
        int o = k;                                    /* instrument 0 */
        if (node_10[o] == node_15[o]) continue;
        int lvl = mix(mix(node_10[o], node_15[o], 128), mix(node_8[o], node_7[o], 128), 0);
        seen++;
        /* a level > 255 - u8(dens): the density just under, then at, the threshold */
        for (int d = 0; d < 128; d++) {
            pt->grids.dens[0] = d;
            q_step st; int acc;
            played(pt, k, &st, &acc);
            int u = d * 255 / 127;
            int want = lvl > 255 - u;
            int got = 0;
            for (int s = 0; s < 4; s++) got |= st.note[s] == KIT_NOTE && (st.flags & SF_ON);
            /* voices 1, 2 at dens 127 may play too; only voice 0 (note KIT_NOTE) is checked */
            assert(got == want);
        }
    }
    assert(seen > 0);
}

static void synth_voice_note(void)
{
    q_pattern *pt = setup(8);
    pt->grids.voice = 2; pt->grids.note = 67;
    for (int k = 0; k < 32; k++) {
        q_step st; int acc;
        int on = played(pt, k, &st, &acc);
        int lvl = mix(mix(node_10[32 + k], node_15[32 + k], 0), mix(node_8[32 + k], node_7[32 + k], 0), 0);
        assert(!!on == (lvl > 0));
        if (on) {
            assert(st.note[0] == 67 && !st.note[1] && !st.note[2] && !st.note[3]);
            assert(acc == (lvl > 192 ? 1 : 0));
        }
    }
}

static void chaos_perturbs_at_loop_start(void)
{
    q_pattern *pt = setup(M_KIT);
    pt->grids.chaos = 127;
    int changed_at_zero = 0;
    for (int k = 0; k < 96; k++) {
        uint8_t before[3]; memcpy(before, G.perturb, 3);
        q_step st; int acc;
        played(pt, k, &st, &acc);
        if (k % 32) assert(!memcmp(before, G.perturb, 3));
        else if (memcmp(before, G.perturb, 3)) changed_at_zero++;
        for (int i = 0; i < 3; i++) assert(G.perturb[i] < 64);   /* byte * (255 >> 2) >> 8 */
    }
    assert(changed_at_zero >= 2);
}

static void loop_len(void)
{
    q_pattern *pt = setup(M_KIT);
    assert(gen_loop(pt) == 32);
    pt->src = SRC_MARBLES; pt->marbles.len = 7;
    assert(gen_loop(pt) == 7);
}

int main(void)
{
    corner_follows_node();
    dens0_silent();
    interpolates();
    synth_voice_note();
    chaos_perturbs_at_loop_start();
    loop_len();
    puts("gen ok");
    return 0;
}
