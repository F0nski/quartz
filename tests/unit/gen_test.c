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

static void loop_lengths(void)
{
    q_pattern *pt = setup(M_KIT);
    assert(gen_loop(pt) == 32);
    pt->src = SRC_MARBLES; pt->marbles.len = 7;
    assert(gen_loop(pt) == 7);
}

/* MARBLES */

static q_pattern *msetup(int model, int mode)
{
    project_defaults(&P);
    P.sound[0].model = model;
    q_pattern *pt = &P.pat[0][0];
    pt->src = SRC_MARBLES;
    pt->marbles = (q_marbles){ 2, 5, mode, 64, 0, 0, 64, 64, 0, 60, 2, { 0, 0 } };
    gen_reset(&G, &R);
    return pt;
}

/* bit i: KIT_NOTE + i in the step's notes */
static int kit_pads(const q_step *st)
{
    int m = 0;
    if (st->flags & SF_ON)
        for (int s = 0; s < 4; s++)
            if (st->note[s] >= KIT_NOTE && st->note[s] < KIT_NOTE + 3) m |= 1 << (st->note[s] - KIT_NOTE);
    return m;
}

/* DEJA VU 63 and 64 are the frozen centre */
static void marbles_dejavu_mid_loops(void)
{
    for (int dv = 63; dv <= 64; dv++) {
        for (int mode = 0; mode < 7; mode++) {
            int pads[40];
            q_pattern *pt = msetup(M_KIT, mode);
            pt->marbles.dejavu = dv;
            pt->marbles.bias = 40;
            for (int k = 0; k < 40; k++) { q_step st; int acc; played(pt, k, &st, &acc); pads[k] = kit_pads(&st); }
            /* CLUSTERS, DRUMS, DIVIDER and MARKOV also follow their own patterns or history: only the Bernoulli
               and three-state models loop on LEN alone */
            if (mode == 0 || mode == 3 || mode == 5)
                for (int k = 5; k < 40; k++) assert(pads[k] == pads[k - 5]);
        }
        /* T OUT t1: X still draws on every step, so the played flag and the note loop together. Several fresh
           loops: at least one must mix played and silent steps. */
        int mixed = 0;
        for (int run = 0; run < 8; run++) {
            int on[400], notes[400], plays = 0;
            q_pattern *pt = msetup(8, 0);
            pt->marbles.tout = 1; pt->marbles.dejavu = dv; pt->marbles.bias = 64;
            pt->marbles.range = 4; pt->marbles.spread = 100;
            for (int k = 0; k < 400; k++) {
                q_step st; int acc;
                on[k] = played(pt, k, &st, &acc);
                notes[k] = on[k] ? st.note[0] : -1;
                plays += !!on[k];
            }
            mixed += plays > 0 && plays < 400;
            for (int k = 5; k < 400; k++) assert(on[k] == on[k - 5] && notes[k] == notes[k - 5]);
        }
        assert(mixed > 0);
    }
}

static void marbles_dejavu0_varies(void)
{
    q_pattern *pt = msetup(8, 0);
    pt->marbles.range = 4; pt->marbles.spread = 100;
    int notes[64], differ = 0;
    for (int k = 0; k < 64; k++) { q_step st; int acc; played(pt, k, &st, &acc); notes[k] = st.note[0]; }
    for (int l = 1; l < 12; l++) differ += memcmp(notes + 5 * l, notes + 5 * (l - 1), 5 * sizeof *notes) != 0;
    assert(differ >= 2);
}

static void marbles_notes_in_allowed(void)
{
    q_pattern *pt = msetup(8, 0);
    pt->marbles.notes[0] = 0x91;             /* C, E, G */
    pt->marbles.notes[1] = 0xF0;             /* bits above 11: ignored */
    pt->marbles.spread = 120;
    for (int k = 0; k < 200; k++) {
        q_step st; int acc;
        played(pt, k, &st, &acc);
        int n = st.note[0], c = n % 12;
        assert(c == 0 || c == 4 || c == 7);
        /* NOTE and NOTE + RANGE octaves are both C: the quantizer stays within */
        assert(n >= 60 && n <= 60 + 2 * 12);
    }
}

static void quantize_steps_keeps_strongest(void)
{
    project_defaults(&P);
    P.mode = 1; P.tonic = 0;
    q_pattern *pt = &P.pat[0][0];
    pt->marbles.notes[0] = pt->marbles.notes[1] = 0;
    uint16_t a = gen_allowed(&P, pt);
    assert(a == SCALE_MASK[1] && a == 0xAB5);
    int seen = 0;
    for (int n = 0; n < 128; n++) {
        assert(gen_quantize(n, a, 0, 127) % 12 == 0);
        seen |= 1 << (gen_quantize(n, a, 0, 0) % 12);
    }
    assert(seen == 0xAB5);
    P.tonic = 2;                                /* D major: the scale from D, the strongest note D */
    a = gen_allowed(&P, pt);
    assert(a == (((0xAB5 << 2) | (0xAB5 >> 10)) & 0xFFF));
    for (int n = 0; n < 128; n++) assert(gen_quantize(n, a, 2, 127) % 12 == 2);
    /* nearest, the lower one on a tie */
    assert(gen_quantize(61, 0x005, 0, 0) == 60);   /* C, D: 61 is between */
    assert(gen_quantize(62, 0x001, 0, 0) == 60);
    assert(gen_quantize(66, 0x001, 0, 0) == 60);
    assert(gen_quantize(67, 0x001, 0, 0) == 72);
}

static void quantize_top_clamps(void)
{
    assert(gen_quantize(120 + 48, 1 << 4, 0, 0) == 124);
    q_pattern *pt = msetup(8, 0);
    pt->marbles.note = 120; pt->marbles.range = 4; pt->marbles.spread = 127;
    pt->marbles.notes[0] = 1 << 4;            /* E only */
    for (int k = 0; k < 200; k++) {
        q_step st; int acc;
        played(pt, k, &st, &acc);
        assert(st.note[0] <= 127 && st.note[0] % 12 == 4);
    }
}

/* note 0 is no note (plan_notes skips it): the quantizer never returns it */
static void quantize_never_zero(void)
{
    assert(gen_quantize(0, 0x001, 0, 0) == 12);
    assert(gen_quantize(0, 0xFFF, 0, 0) == 1);
    assert(gen_quantize(-30, 0x001, 0, 0) == 12);
    for (int n = -12; n < 128; n++) assert(gen_quantize(n, 0xAB5, 0, 127) > 0);
}

static void marbles_t2_every_step(void)
{
    for (int mode = 0; mode < 7; mode++) {
        q_pattern *pt = msetup(M_KIT, mode);
        pt->marbles.bias = 30;
        for (int k = 0; k < 64; k++) {
            q_step st; int acc;
            assert(played(pt, k, &st, &acc) && acc == 0);
            assert(kit_pads(&st) & 2);
            assert(st.vel == 100 && st.len == 4 && st.prob == 20);
            for (int s = 0; s < 4; s++) assert(!st.note[s] || (st.note[s] >= KIT_NOTE && st.note[s] < KIT_NOTE + 3));
        }
    }
}

/* T OUT 3: a synth plays on t3's triggers, those that put pad 3 on a KIT from the same state */
static void marbles_tout3_on_t3(void)
{
    int ons = 0;
    for (int mode = 0; mode < 7; mode++) {
        q_pattern *pt = msetup(M_KIT, mode);
        pt->marbles.tout = 3;
        q_gen gk = G;
        uint32_t rk = R;
        for (int k = 0; k < 64; k++) {
            q_step sk, ss;
            P.sound[0].model = M_KIT;
            gen_step(&gk, &P, 0, pt, k, &rk, &sk);
            P.sound[0].model = 8;
            gen_step(&G, &P, 0, pt, k, &R, &ss);
            assert(!!(ss.flags & SF_ON) == !!(kit_pads(&sk) & 4));
            assert(!(ss.flags & SF_ON) || (ss.note[0] && !ss.note[1]));
            ons += !!(ss.flags & SF_ON);
        }
    }
    assert(ons > 0 && ons < 7 * 64);
}

static void marbles_drums_pattern(void)
{
    static const uint8_t PAT0[8] = { 1, 0, 0, 0, 2, 0, 0, 0 };
    q_pattern *pt = msetup(M_KIT, 2);
    for (int k = 0; k < 64; k++) {
        q_step st; int acc;
        played(pt, k, &st, &acc);
        int m = kit_pads(&st);
        assert(((m & 1) | (m >> 1 & 2)) == PAT0[k % 8]);
    }
}

static void marbles_jitter_bounds(void)
{
    q_pattern *pt = msetup(8, 0);
    pt->marbles.jitter = 127;
    int moved = 0;
    for (int k = 0; k < 200; k++) {
        q_step st; int acc;
        played(pt, k, &st, &acc);
        assert(st.micro >= -11 && st.micro <= 11);
        moved += st.micro != 0;
    }
    assert(moved > 0);
    pt->marbles.jitter = 0;
    for (int k = 0; k < 200; k++) { q_step st; int acc; played(pt, k, &st, &acc); assert(st.micro == 0); }
}

static void marbles_divider_ratchets(void)
{
    q_pattern *pt = msetup(M_KIT, 4);
    pt->marbles.bias = 0;                     /* { 8, 1 } on t1, { 1, 8 } on t3 */
    int t3 = 0, last = -1;
    for (int k = 0; k < 64; k++) {
        q_step st; int acc;
        played(pt, k, &st, &acc);
        int m = kit_pads(&st);
        assert((m & 1) && STEP_RATCHET(&st) == 4);
        if (m & 4) { t3++; assert(k % 8 == 0); if (last >= 0) assert(k - last == 8); last = k; }
    }
    assert(t3 == 8);
}

int main(void)
{
    corner_follows_node();
    dens0_silent();
    interpolates();
    synth_voice_note();
    chaos_perturbs_at_loop_start();
    loop_lengths();
    marbles_dejavu_mid_loops();
    marbles_dejavu0_varies();
    marbles_notes_in_allowed();
    quantize_steps_keeps_strongest();
    quantize_top_clamps();
    quantize_never_zero();
    marbles_t2_every_step();
    marbles_drums_pattern();
    marbles_tout3_on_t3();
    marbles_jitter_bounds();
    marbles_divider_ratchets();
    puts("gen ok");
    return 0;
}
