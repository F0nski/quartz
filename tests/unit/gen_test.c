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
    pt->grids = (q_grids){ 0, 0, 0, { 127, 127, 127 }, 1, 60, { 0, 1, 2 } };
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

/* a KIT's parts play their PAD; two parts on one pad play it once, accented when either is */
static void grids_kit_pads(void)
{
    q_pattern *pt = setup(M_KIT);
    pt->grids.pad[0] = pt->grids.pad[1] = 6;
    pt->grids.pad[2] = 2;
    for (int k = 0; k < 32; k++) {
        q_step st; int acc;
        int on = played(pt, k, &st, &acc);
        int want_on = 0, want_acc = 0, slot = 0, seven = -1;
        for (int i = 0; i < 3; i++) {
            int lvl = node_10[i * 32 + k];
            if (lvl <= 0) continue;
            want_on = 1;
            if (i < 2 && seven >= 0) {          /* part 2 on part 1's pad: no second note */
                if (lvl > 192) want_acc |= 1 << seven;
                continue;
            }
            assert(st.note[slot] == KIT_NOTE + (i < 2 ? 6 : 2));
            if (i < 2) seven = slot;
            if (lvl > 192) want_acc |= 1 << slot;
            slot++;
        }
        assert(!!on == want_on);
        assert(acc == want_acc);
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
    pt->src = SRC_LIFE;
    assert(gen_loop(pt) == 16);
    pt->src = SRC_LANES;
    assert(gen_loop(pt) == 16);
    pt->src = SRC_CHORDS;
    assert(gen_loop(pt) == 16);
    pt->src = SRC_TONNETZ;
    assert(gen_loop(pt) == 16);
    pt->src = SRC_ORBITS;
    assert(gen_loop(pt) == 16);
    pt->src = SRC_MIMIC;
    assert(gen_loop(pt) == 16);
}

/* MARBLES */

static q_pattern *msetup(int model, int mode)
{
    project_defaults(&P);
    P.sound[0].model = model;
    q_pattern *pt = &P.pat[0][0];
    pt->src = SRC_MARBLES;
    pt->marbles = (q_marbles){ 2, 5, mode, 64, 0, 0, 64, 64, 0, 60, 2, { 0, 0 }, 2 };
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

/* GATE: a synth note lasts 1 << gate eighths of a step; a KIT's stay at 4 */
static void marbles_gate_sets_len(void)
{
    static const uint8_t LEN[6] = { 1, 2, 4, 8, 16, 32 };
    for (int g = 0; g < 6; g++) {
        q_pattern *pt = msetup(8, T_COMPLEMENTARY_BERNOULLI);
        pt->marbles.gate = (uint8_t)g;
        for (int k = 0; k < 16; k++) {
            q_step st; int acc;
            if (played(pt, k, &st, &acc)) assert(st.len == LEN[g]);
        }
    }
    q_pattern *pt = msetup(M_KIT, T_COMPLEMENTARY_BERNOULLI);
    pt->marbles.gate = 5;
    q_step st; int acc;
    assert(played(pt, 0, &st, &acc) && st.len == 4);
    project_defaults(&P);
    assert(P.pat[1][3].marbles.gate == 2);
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

/* the screen's levels are the ones grids_step uses: without CHAOS a voice plays a step iff its level clears DENS */
static void grids_level_matches_step(void)
{
    static q_project P2;
    q_gen g;
    uint32_t rng = 1;
    project_defaults(&P2);
    P2.sound[0].model = M_KIT;
    q_pattern *pt = &P2.pat[0][0];
    pt->src = SRC_GRIDS;
    pt->grids = (q_grids){ 40, 90, 0, { 70, 90, 110 }, 1, 60, { 0, 1, 2 } };
    gen_reset(&g, &rng);
    for (int k = 0; k < 32; k++) {
        q_step st;
        memset(&st, 0, sizeof st);
        gen_step(&g, &P2, 0, pt, k, &rng, &st);
        for (int v = 0; v < 3; v++) {
            int plays = 0;
            for (int n = 0; n < 4; n++) plays |= st.note[n] == KIT_NOTE + v;
            assert(plays == (gen_grids_level(&pt->grids, k, v) > 255 - pt->grids.dens[v] * 255 / 127));
        }
    }
}

/* LIFE */

static q_pattern *life_setup(int model)
{
    project_defaults(&P);
    P.sound[0].model = model;
    q_pattern *pt = &P.pat[0][0];
    pt->src = SRC_LIFE;
    memset(&G, 0, sizeof G);
    return pt;
}

static int cell(const uint16_t g[LIFE_H], int c, int r) { return g[(r + LIFE_H) % LIFE_H] >> ((c + LIFE_W) % LIFE_W) & 1; }
static void set_cell(uint16_t g[LIFE_H], int c, int r) { g[r] |= (uint16_t)(1u << c); }
static int alive(const uint16_t g[LIFE_H]) { int n = 0; for (int r = 0; r < LIFE_H; r++) n += __builtin_popcount(g[r]); return n; }

/* the 16 steps of a tour: notes and accents per step */
static void life_tour(const q_pattern *pt, q_step st[16], int acc[16])
{
    for (int k = 0; k < 16; k++) acc[k] = gen_step(&G, &P, 0, pt, k, &R, &st[k]);
}

static int tour_notes(const q_pattern *pt)
{
    q_step st[16]; int acc[16], n = 0;
    life_tour(pt, st, acc);
    for (int k = 0; k < 16; k++) if (st[k].flags & SF_ON) for (int i = 0; i < 4; i++) n += st[k].note[i] != 0;
    return n;
}

static void life_blinker(void)
{
    uint16_t a[LIFE_H] = {0}, b[LIFE_H], c[LIFE_H];
    for (int x = 4; x <= 6; x++) set_cell(a, x, 3);
    life_next(a, b);
    assert(alive(b) == 3 && cell(b, 5, 2) && cell(b, 5, 3) && cell(b, 5, 4));
    life_next(b, c);
    assert(!memcmp(a, c, sizeof a));
}

static void life_glider_wraps(void)
{
    uint16_t g[LIFE_H] = {0}, n[LIFE_H], g0[LIFE_H];
    /* a glider going right and down the rows' indices, near the top right corner */
    set_cell(g, 14, 7); set_cell(g, 15, 6); set_cell(g, 13, 5); set_cell(g, 14, 5); set_cell(g, 15, 5);
    memcpy(g0, g, sizeof g);
    for (int i = 0; i < 64; i++) { life_next(g, n); memcpy(g, n, sizeof g); assert(alive(g) == 5); }
    assert(!memcmp(g, g0, sizeof g));
}

static void life_block_still(void)
{
    uint16_t g[LIFE_H] = {0}, n[LIFE_H];
    set_cell(g, 0, 0); set_cell(g, 1, 0); set_cell(g, 0, 1); set_cell(g, 1, 1);
    life_next(g, n);
    assert(!memcmp(g, n, sizeof g));
}

static void life_seed_repeats(void)
{
    q_pattern *pt = life_setup(M_KIT);
    q_step st;
    pt->life.seed = 17; pt->life.dens = 70;
    gen_step(&G, &P, 0, pt, 0, &R, &st);
    uint16_t a[LIFE_H]; memcpy(a, G.life.grid, sizeof a);
    assert(alive(a) > 0);
    memset(&G, 0, sizeof G);
    gen_step(&G, &P, 0, pt, 0, &R, &st);
    assert(!memcmp(a, G.life.grid, sizeof a));
    pt->life.seed = 18; memset(&G, 0, sizeof G);
    gen_step(&G, &P, 0, pt, 0, &R, &st);
    assert(memcmp(a, G.life.grid, sizeof a));
    pt->life.dens = 0; memset(&G, 0, sizeof G);
    assert(tour_notes(pt) == 0 && alive(G.life.grid) == 0);
    pt->life.dens = 127; memset(&G, 0, sizeof G);
    gen_step(&G, &P, 0, pt, 0, &R, &st);
    assert(alive(G.life.grid) >= 128 * 40 / 100 && alive(G.life.grid) <= 128 * 70 / 100);
}

static void life_gen_every_n_tours(void)
{
    q_pattern *pt = life_setup(M_KIT);
    pt->life.gen = 2; pt->life.mut = 0;
    q_step st;
    gen_step(&G, &P, 0, pt, 0, &R, &st);            /* seeded */
    memset(G.life.grid, 0, sizeof G.life.grid);
    for (int x = 4; x <= 6; x++) set_cell(G.life.grid, x, 3);
    uint16_t a[LIFE_H]; memcpy(a, G.life.grid, sizeof a);
    for (int k = 1; k < 16; k++) gen_step(&G, &P, 0, pt, k, &R, &st);
    for (int t = 2; t <= 4; t++)                     /* tours 2..4: nothing */
        for (int k = 0; k < 16; k++) { gen_step(&G, &P, 0, pt, k, &R, &st); assert(G.life.gens == 0 && !memcmp(a, G.life.grid, sizeof a)); }
    gen_step(&G, &P, 0, pt, 0, &R, &st);            /* tour 5's step 0 */
    assert(G.life.gens == 1 && cell(G.life.grid, 5, 2) && !cell(G.life.grid, 4, 3));
}

static void life_auto_reseeds_dead(void)
{
    for (int autos = 0; autos <= 1; autos++) {
        q_pattern *pt = life_setup(M_KIT);
        pt->life.gen = 0; pt->life.autos = (uint8_t)autos; pt->life.mut = 0;
        q_step st;
        gen_step(&G, &P, 0, pt, 0, &R, &st);
        memset(G.life.grid, 0, sizeof G.life.grid);
        for (int k = 1; k < 16; k++) gen_step(&G, &P, 0, pt, k, &R, &st);
        gen_step(&G, &P, 0, pt, 0, &R, &st);
        if (autos) { assert(G.life.reseeds == 1 && G.life.stuck == 0 && alive(G.life.grid) > 0); }
        else {
            assert(G.life.reseeds == 0 && G.life.stuck == 1);
            assert(tour_notes(pt) == 0);
        }
    }
}

/* DENS 0 draws an empty grid: AUTO has nothing to draw, so it does not reseed (the seed would flash every generation) */
static void life_auto_idle_at_dens0(void)
{
    q_pattern *pt = life_setup(M_KIT);
    pt->life.gen = 0; pt->life.autos = 1; pt->life.mut = 0; pt->life.dens = 0;
    for (int t = 0; t < 4; t++) tour_notes(pt);
    assert(G.life.reseeds == 0 && alive(G.life.grid) == 0);
}

static void life_frozen_kept_without_auto(void)
{
    for (int autos = 0; autos <= 1; autos++) {
        q_pattern *pt = life_setup(M_KIT);
        pt->life.gen = 0; pt->life.autos = (uint8_t)autos; pt->life.mut = 0;
        q_step st;
        gen_step(&G, &P, 0, pt, 0, &R, &st);
        memset(G.life.grid, 0, sizeof G.life.grid);
        set_cell(G.life.grid, 0, 0); set_cell(G.life.grid, 1, 0); set_cell(G.life.grid, 0, 1); set_cell(G.life.grid, 1, 1);
        for (int k = 1; k < 16; k++) gen_step(&G, &P, 0, pt, k, &R, &st);
        if (autos) { gen_step(&G, &P, 0, pt, 0, &R, &st); assert(G.life.reseeds == 1); }
        else {
            for (int t = 0; t < 3; t++) assert(tour_notes(pt) == 4);   /* columns 0 and 1, rows 0 and 1, every tour */
            assert(G.life.stuck == 1 && G.life.reseeds == 0);
        }
    }
}

static void life_mut_never_stuck(void)
{
    q_pattern *pt = life_setup(M_KIT);
    pt->life.gen = 0; pt->life.autos = 1; pt->life.mut = 127;
    q_step st;
    gen_step(&G, &P, 0, pt, 0, &R, &st);
    memset(G.life.grid, 0, sizeof G.life.grid);
    set_cell(G.life.grid, 0, 0); set_cell(G.life.grid, 1, 0); set_cell(G.life.grid, 0, 1); set_cell(G.life.grid, 1, 1);
    for (int k = 1; k < 16; k++) gen_step(&G, &P, 0, pt, k, &R, &st);
    for (int g = 0; g < 8; g++) {
        gen_step(&G, &P, 0, pt, 0, &R, &st);
        assert(G.life.reseeds == 0 && G.life.stuck == 0);
        assert(alive(G.life.mut) >= 1 && alive(G.life.mut) <= 6);
        for (int k = 1; k < 16; k++) gen_step(&G, &P, 0, pt, k, &R, &st);
    }
}

static void life_seed_change_next_tour(void)
{
    q_pattern *pt = life_setup(M_KIT);
    pt->life.gen = 3;                                 /* no generation in the way */
    q_step st;
    for (int k = 0; k < 5; k++) gen_step(&G, &P, 0, pt, k, &R, &st);
    uint16_t a[LIFE_H]; memcpy(a, G.life.grid, sizeof a);
    pt->life.seed = 3;
    gen_step(&G, &P, 0, pt, 5, &R, &st);
    pt->life.seed = 9;
    for (int k = 6; k < 16; k++) { gen_step(&G, &P, 0, pt, k, &R, &st); assert(!memcmp(a, G.life.grid, sizeof a)); }
    gen_step(&G, &P, 0, pt, 0, &R, &st);
    assert(G.life.reseeds == 1);
    q_life_run want; memset(&want, 0, sizeof want);
    gen_life_seed(&want, 9, pt->life.dens);
    assert(!memcmp(want.grid, G.life.grid, sizeof want.grid));
}

static void life_rows_poly(void)
{
    q_life l = { 2, 48, 0, 0, 70, 1, 1, 0 };
    uint16_t g[LIFE_H] = {0};
    uint8_t rows[4];
    set_cell(g, 3, 1); set_cell(g, 3, 2); set_cell(g, 3, 5); set_cell(g, 3, 7);
    assert(gen_life_rows(&l, 0, g, 3, rows) == 2 && rows[0] == 1 && rows[1] == 2);
    l.poly = 4;
    assert(gen_life_rows(&l, 1, g, 3, rows) == 3 && rows[0] == 1 && rows[1] == 2 && rows[2] == 5);
    l.map = 1;
    assert(gen_life_rows(&l, 1, g, 3, rows) == 3 && rows[0] == 1 && rows[1] == 5 && rows[2] == 7);
    l.poly = 2;
    assert(gen_life_rows(&l, 1, g, 3, rows) == 2 && rows[0] == 1 && rows[1] == 5);
    assert(gen_life_rows(&l, 0, g, 4, rows) == 0);
}

static void life_notes_synth(void)
{
    q_pattern *pt = life_setup(8);
    uint16_t sc = gen_scale(&P);                      /* C major */
    assert(gen_life_note(48, sc, 0, 0) == 48 && gen_life_note(48, sc, 0, 1) == 50 && gen_life_note(48, sc, 0, 2) == 52);
    assert(gen_life_note(49, sc, 0, 0) == 48);
    /* through gen_step: column 0 with rows 0 and 2 living */
    pt->life.gen = 3; pt->life.poly = 4; pt->life.note = 48;
    q_step st;
    gen_step(&G, &P, 0, pt, 0, &R, &st);
    memset(G.life.grid, 0, sizeof G.life.grid); memset(G.life.born, 0, sizeof G.life.born);
    set_cell(G.life.grid, 1, 0); set_cell(G.life.grid, 1, 2);
    int acc = gen_step(&G, &P, 0, pt, 1, &R, &st);
    assert((st.flags & SF_ON) && st.note[0] == 48 && st.note[1] == 52 && st.note[2] == 0 && acc == 0);
    assert(st.vel == 95 && st.len == 4 && st.prob == 20);
}

static void life_degrees_clamp(void)
{
    life_setup(8);
    uint16_t sc = gen_scale(&P);
    for (int r = 0; r < LIFE_H; r++) { int n = gen_life_note(127, sc, 0, r); assert(n >= 1 && n <= 127); }
}

/* near 127 the rows clamp to one note: it plays once, its accent kept if any of its cells is born */
static void life_no_repeat_at_top(void)
{
    q_pattern *pt = life_setup(8);
    pt->life.gen = 3; pt->life.poly = 4; pt->life.note = 127;
    q_step st;
    gen_step(&G, &P, 0, pt, 0, &R, &st);
    memset(G.life.grid, 0, sizeof G.life.grid); memset(G.life.born, 0, sizeof G.life.born);
    set_cell(G.life.grid, 1, 0); set_cell(G.life.grid, 1, 1); set_cell(G.life.grid, 1, 2);
    set_cell(G.life.born, 1, 2);
    int acc = gen_step(&G, &P, 0, pt, 1, &R, &st);
    assert(st.note[0] && st.note[1] == 0 && acc == 1);
}

static void life_notes_kit(void)
{
    q_pattern *pt = life_setup(M_KIT);
    pt->life.gen = 3; pt->life.poly = 4;
    q_step st;
    gen_step(&G, &P, 0, pt, 0, &R, &st);
    memset(G.life.grid, 0, sizeof G.life.grid);
    set_cell(G.life.grid, 2, 4); set_cell(G.life.grid, 2, 7);
    gen_step(&G, &P, 0, pt, 2, &R, &st);
    assert(st.note[0] == KIT_NOTE + 4 && st.note[1] == KIT_NOTE + 7);
    pt->life.map = 1;
    gen_step(&G, &P, 0, pt, 2, &R, &st);
    assert(st.note[0] == KIT_NOTE + 1 && st.note[1] == KIT_NOTE + 2 && st.note[2] == 0);
}

static void life_born_accented(void)
{
    q_pattern *pt = life_setup(M_KIT);
    pt->life.gen = 0; pt->life.poly = 4; pt->life.autos = 0;
    q_step st[16]; int acc[16];
    life_tour(pt, st, acc);                           /* the seed: every cell is born */
    for (int k = 0; k < 16; k++) {
        int n = 0; while (n < 4 && st[k].note[n]) n++;
        assert(acc[k] == (1 << n) - 1);
    }
    q_step s1;
    gen_step(&G, &P, 0, pt, 0, &R, &s1);              /* a generation, then a still block with no newborn */
    memset(G.life.grid, 0, sizeof G.life.grid); memset(G.life.born, 0, sizeof G.life.born);
    set_cell(G.life.grid, 1, 0); set_cell(G.life.grid, 1, 1);
    assert(gen_step(&G, &P, 0, pt, 1, &R, &s1) == 0 && s1.note[0]);
}

/* the sequencer resets the generator on a pattern change (seq.c): the new pattern's seed, not the old grid */
static void life_pattern_switch_reseeds(void)
{
    q_pattern *pt = life_setup(M_KIT);
    pt->life.seed = 1;
    for (int t = 0; t < 3; t++) tour_notes(pt);
    q_pattern *p2 = &P.pat[0][1];
    p2->src = SRC_LIFE; p2->life.seed = 2;
    gen_reset(&G, &R);
    q_step st;
    gen_step(&G, &P, 0, p2, 0, &R, &st);
    q_life_run want; memset(&want, 0, sizeof want);
    gen_life_seed(&want, 2, p2->life.dens);
    assert(!memcmp(want.grid, G.life.grid, sizeof want.grid));
}

/* LANES */

static q_pattern *lanes_setup(int model)
{
    project_defaults(&P);
    P.sound[0].model = model;
    q_pattern *pt = &P.pat[0][0];
    pt->src = SRC_LANES;
    memset(&G, 0, sizeof G);
    return pt;
}

/* the step n played (n counts from the reset, k is the rank in the tour) */
static int lanes_play(const q_pattern *pt, int n, q_step *st) { return gen_step(&G, &P, 0, pt, n % 16, &R, st); }

static void lanes_defaults(void)
{
    const q_pattern *pt = lanes_setup(8);
    const q_lanes *l = &pt->lanes;
    static const uint8_t len[L_N] = { 7, 5, 3, 4, 6 };
    static const uint8_t v[L_N][7] = { { 1, 0, 1, 1, 0, 1, 0 }, { 0, 4, 2, 5, 3 }, { 2, 2, 3 }, { 1, 3, 1, 2 }, { 3, 1, 2, 1, 3, 2 } };
    assert(l->note == 48 && sizeof(q_lanes) == 38);
    for (int i = 0; i < L_N; i++) {
        assert(lanes_len(l, i) == len[i]);
        for (int c = 0; c < 16; c++) assert(lanes_get(l, i, c) == (c < len[i] ? v[i][c] : i == L_OCT ? 2 : 0));
    }
}

static void lanes_heads(void)
{
    const q_pattern *pt = lanes_setup(8);
    static q_step a[840];
    for (int n = 0; n < 840; n++) lanes_play(pt, n, &a[n]);
    for (int n = 0; n < 420; n++)
        assert(a[n].flags == a[n + 420].flags && a[n].note[0] == a[n + 420].note[0] && a[n].len == a[n + 420].len && a[n].vel == a[n + 420].vel);
    for (int m = 1; m < 420; m++) {
        int same = 1;
        for (int n = 0; n < 420 && same; n++)
            same = a[n].flags == a[n + m].flags && a[n].note[0] == a[n + m].note[0] && a[n].len == a[n + m].len && a[n].vel == a[n + m].vel;
        assert(!same);
    }
}

static void lanes_no_reset_at_wrap(void)
{
    q_pattern *pt = lanes_setup(8);
    q_lanes *l = &pt->lanes;
    lanes_set_len(l, L_TRIG, 16); l->trig = 0xFFFF;
    lanes_set_len(l, L_OCT, 1); lanes_set_len(l, L_NOTE, 5);
    q_step st[17];
    for (int n = 0; n < 17; n++) lanes_play(pt, n, &st[n]);
    assert(st[16].note[0] == st[1].note[0] && st[16].note[0] != st[0].note[0]);   /* NOTE cell 1, not 0 */
}

static void lanes_reset(void)
{
    q_pattern *pt = lanes_setup(8);
    q_step a, b;
    lanes_play(pt, 0, &a);
    for (int n = 1; n < 7; n++) lanes_play(pt, n, &b);
    gen_reset(&G, &R);
    lanes_play(pt, 0, &b);
    assert(G.lanes_n == 1 && b.flags == a.flags && b.note[0] == a.note[0]);
}

static void lanes_len_shrink(void)
{
    q_pattern *pt = lanes_setup(8);
    q_lanes *l = &pt->lanes;
    lanes_set_len(l, L_TRIG, 1); lanes_set(l, L_TRIG, 0, 1); lanes_set_len(l, L_OCT, 1);
    lanes_set_len(l, L_NOTE, 16);
    for (int c = 0; c < 16; c++) lanes_set(l, L_NOTE, c, c);
    q_step st;
    for (int n = 0; n < 9; n++) lanes_play(pt, n, &st);
    lanes_set_len(l, L_NOTE, 4);
    lanes_play(pt, 9, &st);
    assert(st.note[0] == gen_life_note(48, gen_scale(&P), 0, 1));                /* 9 mod 4 = cell 1 */
}

static void lanes_trig_off(void)
{
    q_pattern *pt = lanes_setup(8);
    pt->lanes.trig = 0;
    q_step st;
    for (int n = 0; n < 16; n++) { lanes_play(pt, n, &st); assert(!(st.flags & SF_ON)); }
}

/* the note of one step with NOTE deg and OCT oct, every lane one cell long */
static int lanes_one(q_pattern *pt, int deg, int oct, int dur, int vel, q_step *st)
{
    q_lanes *l = &pt->lanes;
    for (int i = 0; i < L_N; i++) lanes_set_len(l, i, 1);
    lanes_set(l, L_TRIG, 0, 1); lanes_set(l, L_NOTE, 0, deg); lanes_set(l, L_OCT, 0, oct);
    lanes_set(l, L_DUR, 0, dur); lanes_set(l, L_VEL, 0, vel);
    memset(&G, 0, sizeof G);
    int acc = lanes_play(pt, 0, st);
    assert(st->flags & SF_ON);
    return acc;
}

static void lanes_notes_synth(void)
{
    q_pattern *pt = lanes_setup(8);                     /* C major, NOTE 48 */
    q_step st;
    static const int deg[4] = { 0, 1, 2, 7 }, want[4] = { 48, 50, 52, 60 };
    for (int i = 0; i < 4; i++) { lanes_one(pt, deg[i], 2, 2, 2, &st); assert(st.note[0] == want[i]); }
    lanes_one(pt, 0, 0, 2, 2, &st); assert(st.note[0] == 24);
    lanes_one(pt, 0, 4, 2, 2, &st); assert(st.note[0] == 72);
}

static void lanes_note_clamp(void)
{
    q_pattern *pt = lanes_setup(8);
    q_step st;
    pt->lanes.note = 127;
    lanes_one(pt, 15, 4, 2, 2, &st); assert(st.note[0] >= 1 && st.note[0] <= 127);
    pt->lanes.note = 1;
    lanes_one(pt, 0, 0, 2, 2, &st); assert(st.note[0] >= 1 && st.note[0] <= 127);
}

static void lanes_kit(void)
{
    q_pattern *pt = lanes_setup(M_KIT);
    q_step st;
    lanes_one(pt, 4, 0, 2, 2, &st); assert(st.note[0] == KIT_NOTE + 4);
    lanes_one(pt, 15, 4, 2, 2, &st); assert(st.note[0] == KIT_NOTE + 15);
}

static void lanes_dur_vel(void)
{
    q_pattern *pt = lanes_setup(8);
    q_step st;
    static const int len[5] = { 2, 4, 8, 16, 32 }, vel[3] = { 40, 70, 100 };
    for (int d = 0; d < 5; d++) { lanes_one(pt, 0, 2, d, 2, &st); assert(st.len == len[d]); }
    for (int v = 0; v < 3; v++) { assert(lanes_one(pt, 0, 2, 2, v, &st) == 0); assert(st.vel == vel[v]); }
    assert(lanes_one(pt, 0, 2, 2, 3, &st) == 1 && st.vel == 100);
}

static void lanes_degree(void)
{
    lanes_setup(8);
    uint16_t sc = gen_scale(&P);
    assert(gen_lanes_degree(48, sc, 0, 48) == 0);
    assert(gen_lanes_degree(48, sc, 0, 52) == 2);
    assert(gen_lanes_degree(48, sc, 0, 61) == 7);       /* C#4: C4 and D4 tie, the lower */
    assert(gen_lanes_degree(48, sc, 0, 30) == 0);
    assert(gen_lanes_degree(48, sc, 0, 100) == 15);
}

static void lanes_pattern_switch(void)
{
    q_pattern *pt = lanes_setup(8);
    q_step st, want;
    for (int n = 0; n < 16; n++) lanes_play(pt, n, &st);
    q_pattern *p2 = &P.pat[0][1];
    p2->src = SRC_LANES;
    for (int i = 0; i < L_N; i++) lanes_set_len(&p2->lanes, i, 3);
    lanes_set(&p2->lanes, L_TRIG, 0, 1); lanes_set(&p2->lanes, L_NOTE, 0, 9);
    gen_reset(&G, &R);
    lanes_play(p2, 0, &st);
    memset(&G, 0, sizeof G);
    lanes_play(p2, 0, &want);
    assert(st.flags & SF_ON && st.note[0] == want.note[0] && st.note[0] == gen_life_note(48, gen_scale(&P), 0, 9));
}

/* CHORDS */

static q_pattern *chords_setup(int model)
{
    project_defaults(&P);
    P.sound[0].model = model;
    P.sound[0].poly = 4;
    q_pattern *pt = &P.pat[0][0];
    pt->src = SRC_CHORDS;
    memset(&G, 0, sizeof G);
    return pt;
}
static int chords_strike(const q_pattern *pt, int n, q_step *st) { return gen_step(&G, &P, 0, pt, n % 16, &R, st); }
static int nnotes(const q_step *st) { int n = 0; for (int i = 0; i < 4; i++) n += st->note[i] != 0; return n; }
/* the chord of bar b's first strike, rate 0, from a reset: its notes in st */
static void chords_bar(const q_pattern *pt, int bars, q_step *st)
{
    memset(&G, 0, sizeof G);
    q_step s;
    for (int n = 0; n < 16 * bars; n++) chords_strike(pt, n, &s);
    chords_strike(pt, 16 * bars, st);
}
static int same(const q_step *st, int a, int b, int c, int d)
{
    return st->note[0] == a && st->note[1] == b && st->note[2] == c && st->note[3] == d;
}

static void chords_defaults(void)
{
    const q_pattern *pt = chords_setup(8);
    const q_chords *c = &pt->chords;
    assert(sizeof(q_chords) == 13);
    assert(c->note == 48 && c->len == 4 && c->rate == 2 && c->strum == 0 && c->voice == 1);
    assert(c->ch[0] == CH_MAKE(0, CS_TRIAD) && c->ch[1] == CH_MAKE(5, CS_TRIAD) && c->ch[2] == CH_MAKE(3, CS_TRIAD) && c->ch[3] == CH_MAKE(4, CS_TRIAD));
}

static void chords_one_per_bar(void)
{
    q_pattern *pt = chords_setup(8);
    pt->chords.rate = 0; pt->chords.voice = 0;
    static const int root[8] = { 48, 57, 53, 55, 48, 57, 53, 55 };
    memset(&G, 0, sizeof G);
    for (int n = 0; n < 16 * 8; n++) {
        q_step st;
        chords_strike(pt, n, &st);
        assert(!!(st.flags & SF_ON) == (n % 16 == 0));
        if (n % 16 == 0) assert(st.note[0] == root[n / 16]);
    }
    gen_reset(&G, &R);
    q_step st;
    chords_strike(pt, 0, &st);
    assert(st.note[0] == 48);
}

static void chords_sizes(void)
{
    q_pattern *pt = chords_setup(8);
    q_chords *c = &pt->chords;
    q_step st;
    c->rate = 0; c->voice = 0; c->len = 1;
    c->ch[0] = CH_MAKE(0, CS_TRIAD); chords_bar(pt, 0, &st); assert(same(&st, 48, 52, 55, 0));
    c->ch[0] = CH_MAKE(5, CS_TRIAD); chords_bar(pt, 0, &st); assert(same(&st, 57, 60, 64, 0));
    c->ch[0] = CH_MAKE(4, CS_SEVENTH); chords_bar(pt, 0, &st); assert(same(&st, 55, 59, 62, 65));
    c->ch[0] = CH_MAKE(0, CS_SUS2); chords_bar(pt, 0, &st); assert(same(&st, 48, 50, 55, 0));
    c->ch[0] = CH_MAKE(0, CS_SUS4); chords_bar(pt, 0, &st); assert(same(&st, 48, 53, 55, 0));
}

static void chords_poly_cut(void)
{
    q_pattern *pt = chords_setup(8);
    q_chords *c = &pt->chords;
    q_step st;
    c->rate = 0; c->voice = 0; c->len = 1;
    P.sound[0].poly = 2; c->ch[0] = CH_MAKE(0, CS_TRIAD); chords_bar(pt, 0, &st); assert(same(&st, 48, 52, 0, 0));
    P.sound[0].poly = 3; c->ch[0] = CH_MAKE(4, CS_SEVENTH); chords_bar(pt, 0, &st); assert(same(&st, 55, 59, 65, 0));
}

static void chords_smooth(void)
{
    q_pattern *pt = chords_setup(8);
    q_chords *c = &pt->chords;
    q_step st;
    P.sound[0].poly = 3; c->rate = 0; c->voice = 1; c->len = 2;
    c->ch[0] = CH_MAKE(0, CS_TRIAD); c->ch[1] = CH_MAKE(3, CS_TRIAD);
    chords_bar(pt, 0, &st); assert(same(&st, 48, 52, 55, 0));       /* the first after the reset: root position */
    chords_bar(pt, 1, &st); assert(same(&st, 48, 53, 57, 0));       /* IV near I: C F A */
}

static void chords_smooth_counts(void)
{
    q_pattern *pt = chords_setup(8);
    q_chords *c = &pt->chords;
    c->rate = 0; c->voice = 1; c->len = 3;
    c->ch[0] = CH_MAKE(0, CS_TRIAD); c->ch[1] = CH_MAKE(4, CS_SEVENTH); c->ch[2] = CH_MAKE(5, CS_TRIAD);
    memset(&G, 0, sizeof G);
    for (int n = 0; n < 16 * 6; n++) {
        q_step st;
        if (n == 16 * 2) P.sound[0].poly = 1;
        chords_strike(pt, n, &st);
        if (!(st.flags & SF_ON)) continue;
        assert(nnotes(&st) >= 1 && nnotes(&st) <= P.sound[0].poly);
        for (int i = 0; i < nnotes(&st); i++) assert(st.note[i] >= 1 && st.note[i] <= 127);
    }
}

static void chords_smooth_stable(void)
{
    q_pattern *pt = chords_setup(8);
    pt->chords.rate = 0;
    static q_step a[12];
    memset(&G, 0, sizeof G);
    for (int n = 0; n < 16 * 12; n++) { q_step st; chords_strike(pt, n, &st); if (n % 16 == 0) a[n / 16] = st; }
    for (int b = 4; b < 8; b++) assert(!memcmp(a[b].note, a[b + 4].note, 4));
}

static void chords_note_clamp(void)
{
    q_pattern *pt = chords_setup(8);
    q_chords *c = &pt->chords;
    c->rate = 0; c->note = 127;
    for (int i = 0; i < 4; i++) c->ch[i] = CH_MAKE(i * 2, CS_SEVENTH);
    for (int v = 0; v < 2; v++) {
        c->voice = (uint8_t)v;
        memset(&G, 0, sizeof G);
        for (int n = 0; n < 16 * 8; n++) {
            q_step st;
            chords_strike(pt, n, &st);
            for (int i = 0; i < nnotes(&st); i++) assert(st.note[i] >= 1 && st.note[i] <= 127);
        }
    }
}

/* SMOOTH stays near NOTE: a descending progression over many loops keeps its bass within a fifth of the root position's */
static void chords_smooth_no_drift(void)
{
    q_pattern *pt = chords_setup(8);
    q_chords *c = &pt->chords;
    static const uint8_t D[7] = { 0, 6, 5, 4, 3, 2, 1 };
    c->rate = 0; c->voice = 1; c->len = 7;
    for (int i = 0; i < 7; i++) c->ch[i] = CH_MAKE(D[i], CS_TRIAD);
    P.sound[0].poly = 3;
    static q_step a[7 * 6];
    memset(&G, 0, sizeof G);
    for (int n = 0; n < 16 * 7 * 6; n++) { q_step st; chords_strike(pt, n, &st); if (n % 16 == 0) a[n / 16] = st; }
    for (int b = 0; b < 7 * 6; b++) {
        int root = gen_life_note(48, gen_scale(&P), 0, D[b % 7]);
        assert(a[b].note[0] >= root - 7 && a[b].note[0] <= root + 7);
    }
    for (int b = 7; b < 14; b++) assert(!memcmp(a[b].note, a[b + 28].note, 4));
}

/* SOURCE switched to CHORDS mid-tour: its bars line up with the tour, the first strike on the next step 0 */
static void chords_source_mid_bar(void)
{
    q_pattern *pt = chords_setup(8);
    pt->chords.rate = 0; pt->chords.voice = 0;
    memset(&G, 0, sizeof G);
    q_step st;
    for (int k = 5; k < 16; k++) { gen_step(&G, &P, 0, pt, k, &R, &st); assert(!(st.flags & SF_ON)); }
    gen_step(&G, &P, 0, pt, 0, &R, &st);
    assert(st.flags & SF_ON && st.note[0] == 57);     /* the tour's start: bar 1 (vi), bar 0 was the partial one */
}

static void chords_rate_len(void)
{
    q_pattern *pt = chords_setup(8);
    static const int strikes[5] = { 1, 2, 4, 8, 16 }, len[5] = { 128, 64, 32, 16, 8 };
    for (int r = 0; r < 5; r++) {
        pt->chords.rate = (uint8_t)r;
        memset(&G, 0, sizeof G);
        int n = 0;
        for (int k = 0; k < 16; k++) {
            q_step st;
            int acc = chords_strike(pt, k, &st);
            if (!(st.flags & SF_ON)) continue;
            n++;
            assert(st.len == len[r] && st.vel == 100);
            assert(acc == (k == 0 ? 7 : 0));                 /* the bar's first strike: every note accented */
        }
        assert(n == strikes[r]);
    }
}

static void chords_strum_bits(void)
{
    q_pattern *pt = chords_setup(8);
    pt->chords.strum = 5;
    q_step st;
    memset(&G, 0, sizeof G);
    chords_strike(pt, 0, &st);
    assert(SF_STRUM(&st) == 5 && (st.flags & 31) == SF_ON);
}

static void chords_kit(void)
{
    q_pattern *pt = chords_setup(M_KIT);
    for (int n = 0; n < 16; n++) { q_step st; chords_strike(pt, n, &st); assert(!(st.flags & SF_ON)); }
}

static void chords_len_shrink(void)
{
    q_pattern *pt = chords_setup(8);
    q_chords *c = &pt->chords;
    c->rate = 0; c->voice = 0; c->len = 8;
    for (int i = 0; i < 8; i++) c->ch[i] = CH_MAKE(i % 7, CS_TRIAD);
    memset(&G, 0, sizeof G);
    q_step st;
    for (int n = 0; n < 16 * 6; n++) chords_strike(pt, n, &st);
    c->len = 3;
    chords_strike(pt, 16 * 6, &st);
    assert(st.note[0] == 48);                         /* 6 mod 3 = chord 0, I */
}

/* TONNETZ */

static q_pattern *tonnetz_setup(int model)
{
    project_defaults(&P);
    P.sound[0].model = model;
    P.sound[0].poly = 3;
    q_pattern *pt = &P.pat[0][0];
    pt->src = SRC_TONNETZ;
    memset(&G, 0, sizeof G);
    return pt;
}
static int has3(const uint8_t a[3], int x, int y, int z)
{
    int m = 0;
    for (int i = 0; i < 3; i++) m |= 1 << a[i];
    return m == (1 << x | 1 << y | 1 << z);
}
static int shared(q_tz a, q_tz b)
{
    uint8_t x[3], y[3];
    int n = 0;
    gen_tz_pcs(a, 0, x), gen_tz_pcs(b, 0, y);
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) n += x[i] == y[j];
    return n;
}
/* the roots of bars 0..nb-1 (rate 0, ROOT): the first note of each bar's strike, mod 12 */
static void tonnetz_roots(q_pattern *pt, int nb, int *root)
{
    memset(&G, 0, sizeof G);
    for (int n = 0; n < 16 * nb; n++) {
        q_step st;
        gen_step(&G, &P, 0, pt, n % 16, &R, &st);
        if (n % 16 == 0) { uint8_t pcs[3]; gen_tz_pcs(G.tonnetz.cur, G.tonnetz.base, pcs); root[n / 16] = pcs[0]; }
    }
}

static void tonnetz_defaults(void)
{
    const q_pattern *pt = tonnetz_setup(8);
    const q_tonnetz *t = &pt->tonnetz;
    assert(sizeof(q_tonnetz) == 10);
    assert(t->len == 4 && t->note == 48 && t->rate == 2 && t->strum == 0 && t->voice == 1 && t->chaos == 0 && t->scale == 1 && t->ret == 1);
    assert(TZ_OP(t, 0) == TZ_R && TZ_OP(t, 1) == TZ_L && TZ_OP(t, 2) == TZ_R && TZ_OP(t, 3) == TZ_L);
}

static void tonnetz_moves(void)
{
    q_tz c = { 0, 0, 0 }, s;
    uint8_t x[3];
    s = gen_tz_move(c, TZ_P); gen_tz_pcs(s, 0, x); assert(s.m == 1 && has3(x, 0, 3, 7) && x[0] == 0);
    s = gen_tz_move(c, TZ_L); gen_tz_pcs(s, 0, x); assert(s.m == 1 && has3(x, 4, 7, 11) && x[0] == 4);
    s = gen_tz_move(c, TZ_R); gen_tz_pcs(s, 0, x); assert(s.m == 1 && has3(x, 9, 0, 4) && x[0] == 9);
    q_tz am = gen_tz_move(c, TZ_R);
    s = gen_tz_move(am, TZ_P); gen_tz_pcs(s, 0, x); assert(s.m == 0 && has3(x, 9, 1, 4) && x[0] == 9);
    s = gen_tz_move(am, TZ_L); gen_tz_pcs(s, 0, x); assert(s.m == 0 && has3(x, 5, 9, 0) && x[0] == 5);
    s = gen_tz_move(am, TZ_R); gen_tz_pcs(s, 0, x); assert(s.m == 0 && has3(x, 0, 4, 7) && x[0] == 0);
    for (int o = TZ_P; o <= TZ_R; o++) {
        q_tz a = gen_tz_move(c, o), b = gen_tz_move(a, o);
        assert(shared(c, a) == 2 && b.i == c.i && b.j == c.j && b.m == c.m);
        assert(gen_tz_move(c, TZ_STAY).i == c.i);
    }
}

static void tonnetz_start(void)
{
    q_pattern *pt = tonnetz_setup(8);
    q_tz s; int base; uint8_t x[3];
    gen_tz_start(&P, &pt->tonnetz, &s, &base); gen_tz_pcs(s, base, x); assert(s.i == 0 && s.j == 0 && has3(x, 0, 4, 7) && x[0] == 0);
    pt->tonnetz.note = 50; gen_tz_start(&P, &pt->tonnetz, &s, &base); gen_tz_pcs(s, base, x); assert(s.m == 1 && has3(x, 2, 5, 9) && x[0] == 2);
    pt->tonnetz.note = 59; pt->tonnetz.scale = 0;    /* FREE: B diminished starts minor */
    gen_tz_start(&P, &pt->tonnetz, &s, &base); gen_tz_pcs(s, base, x); assert(s.m == 1 && has3(x, 11, 2, 6) && x[0] == 11);
    pt->tonnetz.scale = 1;                            /* IN SCALE: Bm (F#) is out, its first neighbour in the scale: L, G */
    gen_tz_start(&P, &pt->tonnetz, &s, &base); gen_tz_pcs(s, base, x); assert(has3(x, 7, 11, 2));
}

static void tonnetz_walk_free(void)
{
    q_pattern *pt = tonnetz_setup(8);
    pt->tonnetz.scale = 0; pt->tonnetz.ret = 0; pt->tonnetz.rate = 0; pt->tonnetz.voice = 0;
    int r[6];
    tonnetz_roots(pt, 6, r);
    assert(r[0] == 0 && r[1] == 9 && r[2] == 5 && r[3] == 2 && r[4] == 10 && r[5] == 7);
}

static void tonnetz_walk_return(void)
{
    q_pattern *pt = tonnetz_setup(8);
    pt->tonnetz.rate = 0; pt->tonnetz.voice = 0;
    int r[7];
    tonnetz_roots(pt, 7, r);
    assert(r[0] == 0 && r[1] == 9 && r[2] == 5 && r[3] == 2 && r[4] == 5 && r[5] == 0 && r[6] == 9);
}

static void tonnetz_in_scale(void)
{
    static const uint16_t SEQS[2] = { 0x55, (uint16_t)(TZ_L | TZ_R << 2 | TZ_P << 4 | TZ_L << 6) };
    q_pattern *pt = tonnetz_setup(8);
    uint16_t sc = gen_scale(&P);
    for (int q = 0; q < 2; q++) {
        pt->tonnetz.seq = SEQS[q]; pt->tonnetz.ret = 0;
        memset(&G, 0, sizeof G);
        for (int n = 0; n < 16 * 4 * 16; n++) {
            q_step st;
            gen_step(&G, &P, 0, pt, n % 16, &R, &st);
            for (int i = 0; i < 4 && st.note[i]; i++) assert(sc >> (st.note[i] % 12) & 1);
        }
    }
}

static void tonnetz_in_scale_stuck(void)
{
    q_pattern *pt = tonnetz_setup(8);
    P.mode = 9;                                       /* MAJ PENTA: C D E G A */
    uint16_t sc = gen_scale(&P);
    pt->tonnetz.note = 52;                            /* E: E G# B would start out of the scale and stay there */
    pt->tonnetz.seq = 0x55; pt->tonnetz.len = 1; pt->tonnetz.ret = 0;
    memset(&G, 0, sizeof G);
    for (int n = 0; n < 16 * 16; n++) {
        q_step st;
        gen_step(&G, &P, 0, pt, n % 16, &R, &st);
        for (int i = 0; i < 4 && st.note[i]; i++) assert(sc >> (st.note[i] % 12) & 1);
    }
}

static void tonnetz_chaos(void)
{
    q_pattern *pt = tonnetz_setup(8);
    pt->tonnetz.rate = 0; pt->tonnetz.scale = 0; pt->tonnetz.ret = 0;
    int a[20], b[20];
    tonnetz_roots(pt, 20, a);
    tonnetz_roots(pt, 20, b);
    for (int i = 0; i < 20; i++) assert(a[i] == b[i]);
    pt->tonnetz.chaos = 127;
    memset(&G, 0, sizeof G);
    int hits = 0;
    for (int n = 0; n < 16 * 41; n++) {
        q_step st;
        gen_step(&G, &P, 0, pt, n % 16, &R, &st);
        if (n % 16 == 0 && n) hits += G.tonnetz.chaos_hit;
    }
    assert(hits >= 30);
}

static void tonnetz_len_shrink(void)
{
    q_pattern *pt = tonnetz_setup(8);
    pt->tonnetz.len = 8; pt->tonnetz.rate = 0;
    memset(&G, 0, sizeof G);
    q_step st;
    for (int n = 0; n < 16 * 6; n++) gen_step(&G, &P, 0, pt, n % 16, &R, &st);
    pt->tonnetz.len = 3;
    for (int n = 16 * 6; n < 16 * 12; n++) { gen_step(&G, &P, 0, pt, n % 16, &R, &st); assert(G.tonnetz.applied < 3); }
}

static void tonnetz_long_walk(void)
{
    q_pattern *pt = tonnetz_setup(8);
    pt->tonnetz.scale = 0; pt->tonnetz.ret = 0; pt->tonnetz.chaos = 127; pt->tonnetz.rate = 4;
    memset(&G, 0, sizeof G);
    for (int n = 0; n < 16 * 4000; n++) {
        q_step st;
        gen_step(&G, &P, 0, pt, n % 16, &R, &st);
        for (int i = 0; i < 4 && st.note[i]; i++) assert(st.note[i] >= 1 && st.note[i] <= 127);
        assert(G.tonnetz.cur.i >= -2000 && G.tonnetz.cur.i <= 2000 && G.tonnetz.cur.j >= -2000 && G.tonnetz.cur.j <= 2000);
    }
}

/* a FREE walk far from the start: the lattice wraps (i by 12, j by 3: the same pitch classes), the chord stays */
static void tonnetz_wrap(void)
{
    q_pattern *pt = tonnetz_setup(8);
    pt->tonnetz.scale = 0; pt->tonnetz.ret = 0; pt->tonnetz.seq = TZ_STAY; pt->tonnetz.len = 1;
    memset(&G, 0, sizeof G);
    q_step st;
    gen_step(&G, &P, 0, pt, 0, &R, &st);
    G.tonnetz.cur = (q_tz){ 9500, -9400, 1 };
    uint8_t a[3], b[3];
    gen_tz_pcs(G.tonnetz.cur, G.tonnetz.base, a);
    for (int n = 1; n < 17; n++) gen_step(&G, &P, 0, pt, n % 16, &R, &st);
    gen_tz_pcs(G.tonnetz.cur, G.tonnetz.base, b);
    assert(G.tonnetz.cur.i > -100 && G.tonnetz.cur.i < 100 && G.tonnetz.cur.j > -100 && G.tonnetz.cur.j < 100);
    assert(a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && G.tonnetz.cur.m == 1);
}

static void tonnetz_play_like_chords(void)
{
    q_pattern *pt = tonnetz_setup(8);
    static const int strikes[5] = { 1, 2, 4, 8, 16 }, len[5] = { 128, 64, 32, 16, 8 };
    for (int r = 0; r < 5; r++) {
        pt->tonnetz.rate = (uint8_t)r; pt->tonnetz.strum = 3;
        memset(&G, 0, sizeof G);
        int n = 0;
        for (int k = 0; k < 16; k++) {
            q_step st;
            int acc = gen_step(&G, &P, 0, pt, k, &R, &st);
            if (!(st.flags & SF_ON)) continue;
            n++;
            assert(st.len == len[r] && st.vel == 100 && SF_STRUM(&st) == 3 && acc == (k == 0 ? 7 : 0));
        }
        assert(n == strikes[r]);
    }
    pt = tonnetz_setup(M_KIT);
    for (int k = 0; k < 16; k++) { q_step st; gen_step(&G, &P, 0, pt, k, &R, &st); assert(!(st.flags & SF_ON)); }
}

/* ORBITS */

static q_pattern *orbits_setup(int model)
{
    project_defaults(&P);
    P.sound[0].model = model;
    q_pattern *pt = &P.pat[0][0];
    pt->src = SRC_ORBITS;
    memset(&G, 0, sizeof G);
    return pt;
}

static void orbits_defaults(void)
{
    const q_orbits *o = &orbits_setup(8)->orbits;
    assert(sizeof(q_orbits) == 14);
    assert(o->note == 48 && o->moons == 3);
    static const uint8_t per[4] = { 8, 6, 5, 3 }, val[4] = { 0, 2, 4, 7 };
    for (int m = 0; m < 4; m++) assert(o->per[m] == per[m] && o->pha[m] == 0 && o->val[m] == val[m]);
}

static void orbits_passes(void)
{
    const q_orbits *o = &orbits_setup(8)->orbits;
    assert(gen_orbits_hits(o, 0) == 7 && gen_orbits_hits(o, 1) == 0 && gen_orbits_hits(o, 5) == 4);
    assert(gen_orbits_hits(o, 6) == 2 && gen_orbits_hits(o, 8) == 1 && gen_orbits_hits(o, 24) == 3 && gen_orbits_hits(o, 120) == 7);
    for (uint32_t n = 1; n < 120; n++) assert(gen_orbits_hits(o, n) != 7);
}

static void orbits_notes(void)
{
    q_pattern *pt = orbits_setup(8);
    q_step st;
    int acc = gen_step(&G, &P, 0, pt, 0, &R, &st);
    assert(st.flags & SF_ON && st.note[0] == 48 && st.note[1] == 52 && st.note[2] == 55 && st.note[3] == 0 && acc == 7);
    for (int k = 1; k < 5; k++) { gen_step(&G, &P, 0, pt, k, &R, &st); assert(!(st.flags & SF_ON)); }
    acc = gen_step(&G, &P, 0, pt, 5, &R, &st);
    assert(st.note[0] == 55 && st.note[1] == 0 && acc == 0 && st.len == 4 && st.vel == 100);
    pt = orbits_setup(M_KIT);
    gen_step(&G, &P, 0, pt, 0, &R, &st);
    assert(st.note[0] == KIT_NOTE && st.note[1] == KIT_NOTE + 2 && st.note[2] == KIT_NOTE + 4);
}

static void orbits_phase_mod(void)
{
    q_orbits *o = &orbits_setup(8)->orbits;
    o->pha[0] = 11;
    assert(gen_orbits_hits(o, 3) & 1);
    assert(!(gen_orbits_hits(o, 0) & 1));
}

static void orbits_period_change(void)
{
    q_pattern *pt = orbits_setup(8);
    q_step st;
    for (int k = 0; k < 5; k++) gen_step(&G, &P, 0, pt, k, &R, &st);
    pt->orbits.per[0] = 1;
    for (int k = 5; k < 8; k++) { gen_step(&G, &P, 0, pt, k, &R, &st); assert(st.flags & SF_ON && st.note[0] == 48); }
}

static void orbits_four(void)
{
    q_pattern *pt = orbits_setup(8);
    pt->orbits.moons = 4;
    for (int m = 0; m < 4; m++) pt->orbits.per[m] = 4;
    q_step st;
    int acc = gen_step(&G, &P, 0, pt, 0, &R, &st);
    assert(st.note[0] && st.note[1] && st.note[2] && st.note[3] && acc == 15);
}

static void orbits_moons(void)
{
    q_pattern *pt = orbits_setup(8);
    pt->orbits.moons = 1;
    assert(gen_orbits_hits(&pt->orbits, 0) == 1);
    q_step st;
    for (int n = 0; n < 17; n++) gen_step(&G, &P, 0, pt, n % 16, &R, &st);
    assert(G.orbits_n == 17);
}

/* MIMIC */

static q_pattern *mimic_setup(int model, const uint8_t *notes, const uint8_t *gaps, int count)
{
    project_defaults(&P);
    P.sound[0].model = model;
    q_pattern *pt = &P.pat[0][0];
    pt->src = SRC_MIMIC;
    if (notes) {
        pt->mimic.n = (uint8_t)count;
        for (int i = 0; i < count; i++) pt->mimic.ev[i][0] = notes[i], pt->mimic.ev[i][1] = gaps[i];
    }
    if (notes) pt->mimic.order = 1, pt->mimic.drift = 0;
    memset(&G, 0, sizeof G);
    return pt;
}

/* a 7-note phrase: C4 D4 E4 G4 E4 D4 C4 */
static const uint8_t M7_NOTE[7] = { 60, 62, 64, 67, 64, 62, 60 }, M7_GAP[7] = { 2, 1, 1, 2, 1, 1, 4 };

/* the notes step k plays; their number */
static int mplay(q_pattern *pt, int k, uint8_t out[4])
{
    q_step st;
    gen_step(&G, &P, 0, pt, k % 16, &R, &st);
    int nb = 0;
    if (st.flags & SF_ON)
        for (; nb < 4 && st.note[nb]; nb++) out[nb] = st.note[nb];
    return nb;
}

static void mimic_defaults(void)
{
    const q_mimic *m = &mimic_setup(8, 0, 0, 0)->mimic;
    assert(sizeof(q_mimic) == 69);
    assert(m->n == 0 && m->order == 1 && m->drift == 16 && m->loop == 0 && m->seed == 0);
}

static void mimic_in_order(void)
{
    static const uint8_t n[3] = { 60, 62, 64 }, g[3] = { 1, 2, 1 };
    q_pattern *pt = mimic_setup(8, n, g, 3);
    uint8_t o[4];
    assert(mplay(pt, 0, o) == 1 && o[0] == 60);
    assert(mplay(pt, 1, o) == 1 && o[0] == 62);
    assert(mplay(pt, 2, o) == 0);
    assert(mplay(pt, 3, o) == 1 && o[0] == 64);
    assert(mplay(pt, 4, o) == 1 && o[0] == 60);
    q_step st;
    gen_step(&G, &P, 0, pt, 5, &R, &st);
    assert(st.len == 4 && st.vel == 100 && st.prob == 20);
}

static void mimic_chord(void)
{
    static const uint8_t n[3] = { 60, 64, 67 }, g[3] = { 0, 0, 2 };
    q_pattern *pt = mimic_setup(8, n, g, 3);
    uint8_t o[4];
    assert(mplay(pt, 0, o) == 3 && o[0] == 60 && o[1] == 64 && o[2] == 67);
    assert(mplay(pt, 1, o) == 0);
    assert(mplay(pt, 2, o) == 3 && o[0] == 60 && o[1] == 64 && o[2] == 67);
}

/* a chord whose places hold the same note (or, on a kit, the same pad) plays it once */
static void mimic_chord_no_repeat(void)
{
    static const uint8_t n[3] = { 64, 60, 60 }, g[3] = { 0, 0, 1 };   /* after place 1, place 2 or 0: a note already on */
    q_pattern *pt = mimic_setup(8, n, g, 3);
    uint8_t o[4];
    for (int s = 0; s < 8; s++) {
        pt->mimic.seed = (uint8_t)s;
        memset(&G, 0, sizeof G);
        assert(mplay(pt, 0, o) == 2 && o[0] == 64 && o[1] == 60);
    }
    static const uint8_t k[2] = { 60, 76 }, kg[2] = { 0, 1 };
    static const uint8_t same[2] = { 60, 60 }, z[2] = { 0, 0 };   /* nothing but one note, all chord: no endless walk */
    pt = mimic_setup(8, same, z, 2);
    assert(mplay(pt, 0, o) == 1 && o[0] == 60);
    pt = mimic_setup(M_KIT, k, kg, 2);
    assert(mplay(pt, 0, o) == 1 && o[0] == KIT_NOTE + 8);
}

static void mimic_all_chord(void)
{
    static const uint8_t n[5] = { 60, 62, 64, 65, 67 }, g[5] = { 0 };
    q_pattern *pt = mimic_setup(8, n, g, 5);
    uint8_t o[4];
    for (int k = 0; k < 32; k++) assert(mplay(pt, k, o) <= 4);
    assert(G.mimic.n == 32);
}

static void mimic_order2(void)
{
    static const uint8_t n[4] = { 60, 62, 60, 64 }, g[4] = { 1, 1, 1, 1 };
    q_pattern *pt = mimic_setup(8, n, g, 4);
    pt->mimic.order = 2;
    uint8_t o[4];
    int a = 0, b = 0, seen62 = 0, seen64 = 0;
    for (int k = 0; k < 200; k++) {
        mplay(pt, k, o);
        if (a == 62 && b == 60) assert(o[0] == 64);
        a = b, b = o[0];
    }
    mimic_setup(8, n, g, 4);
    a = 0;
    for (int k = 0; k < 200; k++) {
        mplay(pt, k, o);
        if (a == 60) seen62 |= o[0] == 62, seen64 |= o[0] == 64;
        a = o[0];
    }
    assert(seen62 && seen64);
}

static void mimic_fallback(void)
{
    static const uint8_t n[4] = { 60, 62, 64, 65 }, g[4] = { 1, 1, 1, 1 };
    q_pattern *pt = mimic_setup(8, n, g, 4);
    pt->mimic.order = 2;
    uint8_t o[4];
    mplay(pt, 0, o);
    G.mimic.prev = 1, G.mimic.cur = 3;  /* after a jump: 62 then 65, a pair the phrase does not hold */
    assert(mplay(pt, 1, o) == 1 && o[0] == 60);
}

static void mimic_drift(void)
{
    q_pattern *pt = mimic_setup(8, M7_NOTE, M7_GAP, 7);
    pt->mimic.drift = 127;
    unsigned places = 0;
    int jumped = 0;
    uint8_t o[4];
    for (int k = 0; k < 400; k++) {
        if (mplay(pt, k, o)) {
            places |= 1u << G.mimic.last[0];
            jumped |= G.mimic.jumps & 1;
        }
    }
    assert(places == 0x7f && jumped);
}

static void mimic_run(q_pattern *pt, int steps, uint8_t *notes)
{
    uint8_t o[4];
    memset(&G, 0, sizeof G);
    for (int k = 0; k < steps; k++) notes[k] = mplay(pt, k, o) ? o[0] : 0;
}

static void mimic_loop(void)
{
    q_pattern *pt = mimic_setup(8, M7_NOTE, M7_GAP, 7);
    pt->mimic.drift = 64; pt->mimic.loop = 8;
    uint8_t a[24];
    mimic_run(pt, 24, a);
    assert(!memcmp(a, a + 8, 8) && !memcmp(a, a + 16, 8));
}

static void mimic_seed(void)
{
    q_pattern *pt = mimic_setup(8, M7_NOTE, M7_GAP, 7);
    pt->mimic.drift = 64; pt->mimic.seed = 5;
    uint8_t a[64], b[64], c[64];
    mimic_run(pt, 64, a);
    mimic_run(pt, 64, b);
    assert(!memcmp(a, b, 64));
    pt->mimic.seed = 6;
    mimic_run(pt, 64, c);
    assert(memcmp(a, c, 64));
    pt->mimic.seed = 5;                 /* changed while playing: drawn again from the new SEED */
    uint8_t o[4];
    memset(&G, 0, sizeof G);
    for (int k = 0; k < 10; k++) mplay(pt, k, o);
    assert(G.mimic.seed == 5);
    pt->mimic.seed = 9;
    mplay(pt, 10, o);
    assert(G.mimic.seed == 9);
}

static void mimic_silent(void)
{
    q_pattern *pt = mimic_setup(8, M7_NOTE, M7_GAP, 7);
    uint8_t o[4];
    G.mimic.listen = 1;
    for (int k = 0; k < 16; k++) assert(mplay(pt, k, o) == 0);
    G.mimic.listen = 0;
    pt->mimic.n = 0;
    for (int k = 0; k < 16; k++) assert(mplay(pt, k, o) == 0);
}

static void mimic_listen_kept_on_reset(void)
{
    mimic_setup(8, 0, 0, 0);
    G.mimic.listen = 1;
    gen_reset(&G, &R);
    assert(G.mimic.listen == 1);
}

static void mimic_kit(void)
{
    static const uint8_t n[2] = { 60, 30 }, g[2] = { 1, 1 };
    q_pattern *pt = mimic_setup(M_KIT, n, g, 2);
    uint8_t o[4];
    assert(mplay(pt, 0, o) == 1 && o[0] == KIT_NOTE + 8);
    assert(mplay(pt, 1, o) == 1 && o[0] == KIT_NOTE + 10);
}

static void mimic_shrunk(void)
{
    q_pattern *pt = mimic_setup(8, M7_NOTE, M7_GAP, 7);
    pt->mimic.drift = 0;
    uint8_t o[4];
    for (int k = 0; k < 40 && G.mimic.cur != 6; k++) mplay(pt, k, o);
    assert(G.mimic.cur == 6);
    pt->mimic.n = 3;
    for (int k = 40; k < 60; k++)
        for (int i = 0, nb = mplay(pt, k, o); i < nb; i++) assert(o[i] == 60 || o[i] == 62 || o[i] == 64);
}

static void mimic_gap(void)
{
    assert(gen_mimic_gap(250, 125, 0) == 2);
    assert(gen_mimic_gap(60, 125, 0) == 0);
    assert(gen_mimic_gap(63, 125, 0) == 1);
    assert(gen_mimic_gap(5000, 125, 0) == 7);
    assert(gen_mimic_gap(0, 125, 1) == 1);
    assert(gen_mimic_gap(4000000000u, 1000, 0) == 7);
}

static void mimic_learn(void)
{
    q_mimic m = { 0 };
    gen_mimic_learn(&m, 60, 0);
    gen_mimic_learn(&m, 62, 2);
    assert(m.n == 2 && m.ev[0][0] == 60 && m.ev[0][1] == 2 && m.ev[1][0] == 62 && m.ev[1][1] == 1);
    m.n = 0;
    for (int i = 0; i < 33; i++) gen_mimic_learn(&m, 40 + i, 1);
    assert(m.n == 32 && m.ev[0][0] == 41 && m.ev[31][0] == 72);
}

/* MARBLES' new draw (PRESET): the deja vu loops drawn again */
static void marbles_draw_new_loops(void)
{
    msetup(8, 0);
    gen_reset(&G, &R);
    q_dejavu t = G.t, x = G.x;
    gen_marbles_draw(&G, &R);
    assert(memcmp(t.loop, G.t.loop, sizeof t.loop) && memcmp(x.loop, G.x.loop, sizeof x.loop));
}

static const uint8_t CIPHER_REF[32][34] = {
    {0, 232, 8, 184, 16, 136, 24, 88, 32, 36, 232, 204, 176, 148, 120, 92, 64, 36, 8, 32, 176, 120, 120, 176, 32, 12, 208, 204, 144, 12, 24, 204, 128, 108},   /* CLASSIC */
    {0, 44, 90, 6, 52, 96, 14, 59, 104, 21, 66, 111, 0, 0, 0, 128, 128, 128, 0, 1, 128, 129, 128, 2, 0, 0, 130, 130, 128, 1, 2, 3, 128, 129},   /* SIERPIN */
    {0, 88, 220, 202, 184, 166, 148, 130, 112, 94, 76, 58, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 228, 122, 16, 166, 60, 210, 104, 254, 148, 42, 0, 0},   /* FORTY2 */
    {240, 246, 252, 35, 9, 111, 118, 92, 35, 43, 111, 118, 124, 102, 77, 111, 118, 92, 102, 107, 111, 119, 124, 170, 137, 239, 221, 220, 170, 170, 239, 255, 252, 230},   /* LACE */
    {0, 0, 18, 27, 0, 0, 54, 63, 0, 81, 90, 0, 0, 117, 126, 0, 144, 153, 0, 0, 180, 189, 0, 0, 216, 0, 0, 243, 252, 0, 0, 0, 64, 82},   /* KNOT */
    {0, 232, 212, 193, 172, 149, 130, 108, 88, 67, 46, 0, 0, 0, 0, 1, 0, 2, 2, 0, 0, 3, 0, 4, 0, 1, 4, 1, 4, 6, 2, 5, 0, 8},   /* GEAR */
    {248, 253, 250, 255, 253, 249, 254, 251, 250, 255, 251, 255, 252, 253, 255, 255, 253, 255, 254, 255, 255, 255, 255, 51, 120, 189, 3, 71, 142, 211, 22, 91, 163, 231},   /* BEACON */
    {7, 87, 175, 7, 95, 13, 14, 15, 8, 9, 11, 11, 13, 13, 14, 15, 26, 27, 27, 27, 31, 31, 31, 62, 124, 174, 236, 31, 93, 159, 205, 15, 78, 126},   /* MELODY */
    {0, 151, 46, 183, 92, 215, 110, 247, 152, 0, 32, 32, 0, 32, 0, 1, 32, 1, 2, 34, 0, 32, 34, 3, 32, 33, 2, 32, 4, 4, 36, 5, 32, 33},   /* DRIFT */
    {0, 0, 18, 27, 0, 0, 54, 63, 0, 81, 90, 0, 0, 117, 126, 0, 144, 153, 0, 0, 180, 189, 0, 0, 216, 0, 0, 243, 252, 0, 0, 0, 64, 82},   /* TIDE */
    {158, 168, 178, 252, 222, 168, 188, 188, 232, 232, 178, 188, 178, 252, 242, 188, 188, 188, 252, 252, 239, 113, 243, 53, 183, 249, 133, 253, 73, 193, 3, 133, 219, 93},   /* MOTOR */
    {0, 66, 0, 66, 0, 2, 0, 154, 0, 82, 0, 242, 0, 194, 0, 74, 0, 98, 0, 162, 0, 162, 0, 250, 24, 95, 90, 41, 156, 195, 222, 85, 32, 23},   /* STORM */
    {31, 223, 159, 95, 63, 255, 191, 127, 95, 31, 223, 191, 32, 33, 34, 35, 32, 33, 34, 35, 32, 41, 34, 44, 40, 32, 42, 49, 49, 33, 35, 53, 33, 33},   /* MESH */
    {241, 1, 17, 42, 57, 75, 99, 117, 137, 154, 173, 193, 213, 232, 240, 240, 240, 242, 242, 241, 240, 244, 240, 245, 240, 242, 246, 240, 240, 241, 248, 248, 240, 243},   /* DIG */
    {20, 185, 70, 20, 153, 66, 60, 233, 94, 44, 249, 66, 20, 233, 199, 116, 169, 195, 108, 185, 207, 28, 169, 195, 52, 153, 199, 52, 122, 195, 28, 110, 191, 236},   /* FLIP */
    {0, 157, 59, 216, 118, 150, 78, 6, 190, 117, 0, 0, 0, 0, 0, 137, 164, 190, 216, 242, 26, 78, 131, 183, 236, 176, 255, 78, 157, 235, 78, 183, 32, 137},   /* SWELL */
    {0, 216, 176, 217, 204, 56, 16, 232, 152, 139, 0, 0, 68, 95, 122, 0, 0, 203, 230, 0, 0, 0, 82, 109, 0, 0, 0, 217, 244, 0, 0, 69, 96, 123},   /* STAIRS 1 */
    {0, 154, 148, 184, 188, 222, 220, 0, 0, 148, 148, 188, 252, 254, 254, 0, 0, 250, 250, 252, 252, 254, 254, 113, 0, 37, 190, 78, 224, 107, 246, 124, 0, 121},   /* STAIRS 2 */
    {0, 11, 0, 0, 44, 0, 0, 77, 88, 0, 110, 121, 0, 143, 154, 0, 0, 187, 0, 0, 220, 0, 0, 253, 0, 38, 60, 0, 104, 126, 0, 170, 0, 0},   /* LADDER */
    {0, 192, 128, 106, 56, 192, 128, 162, 112, 62, 128, 64, 168, 118, 128, 64, 224, 174, 124, 64, 0, 230, 180, 64, 0, 192, 236, 186, 0, 192, 36, 242, 0, 192},   /* TERRACE */
    {16, 122, 122, 144, 14, 68, 144, 34, 142, 16, 182, 88, 16, 202, 162, 144, 217, 108, 144, 239, 182, 16, 133, 128, 16, 155, 202, 144, 49, 148, 144, 71, 100, 16},   /* WEAVE */
    {0, 126, 124, 158, 248, 254, 60, 126, 208, 238, 236, 126, 120, 158, 236, 238, 32, 126, 220, 190, 248, 126, 124, 126, 240, 238, 28, 74, 248, 166, 212, 130, 64, 254},   /* CHIME */
    {192, 17, 98, 123, 218, 56, 151, 245, 96, 204, 56, 164, 82, 203, 69, 190, 56, 55, 190, 69, 204, 83, 163, 55, 204, 96, 245, 150, 56, 218, 124, 96, 16, 191},   /* SPIRAL */
    {0, 64, 144, 210, 32, 114, 180, 4, 80, 146, 228, 38, 248, 80, 160, 250, 80, 161, 248, 77, 164, 248, 78, 163, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},   /* COIL */
    {252, 61, 127, 175, 255, 31, 86, 143, 202, 251, 59, 127, 173, 255, 30, 95, 152, 201, 250, 59, 127, 173, 255, 31, 94, 151, 202, 251, 61, 127, 175, 255, 52, 93},   /* CHAOS */
    {160, 108, 57, 198, 147, 95, 108, 49, 198, 138, 95, 108, 57, 245, 130, 95, 108, 32, 245, 138, 95, 19, 56, 229, 146, 94, 11, 48, 229, 169, 94, 3, 56, 244},   /* SPARK */
    {0, 48, 100, 152, 204, 252, 48, 68, 152, 168, 252, 48, 100, 84, 136, 252, 48, 0, 84, 168, 252, 204, 184, 89, 58, 118, 23, 80, 25, 45, 54, 175, 56, 36},   /* FLINT */
    {128, 225, 96, 210, 68, 177, 38, 164, 24, 144, 10, 137, 4, 128, 0, 128, 0, 2, 0, 0, 5, 2, 3, 0, 9, 2, 3, 0, 12, 12, 14, 0, 2, 17},   /* QUAD */
    {21, 108, 18, 105, 15, 102, 6, 138, 93, 48, 3, 135, 0, 0, 0, 0, 0, 0, 3, 48, 93, 138, 6, 102, 15, 105, 18, 108, 21, 78, 36, 171, 129, 87},   /* MODULO */
    {40, 33, 58, 51, 12, 5, 30, 23, 96, 121, 114, 74, 69, 92, 87, 174, 185, 176, 139, 130, 157, 148, 236, 229, 242, 203, 192, 217, 214, 47, 36, 61, 11, 2},   /* XOR */
    {77, 77, 69, 69, 77, 77, 68, 68, 108, 108, 100, 103, 111, 111, 103, 103, 14, 14, 6, 6, 14, 14, 129, 171, 169, 163, 73, 66, 64, 106, 96, 106, 11, 1},   /* FRACTAL */
    {0, 132, 70, 70, 132, 40, 234, 234, 40, 236, 174, 174, 236, 208, 146, 146, 208, 227, 104, 227, 104, 131, 16, 131, 16, 67, 216, 67, 216, 35, 192, 35, 192, 30},   /* GLITCH */
};

/* CIPHER: the firmware's bytes are the search's (docs/superpowers: the bank's script), formula by formula */
static void cipher_matches_search(void)
{
    for (int f = 0; f < CIPHER_N; f++) {
        q_cipher c;
        gen_cipher_preset(&c, f);
        for (uint32_t n = 0; n < 34; n++) {
            if (gen_cipher_byte(&c, n) != CIPHER_REF[f][n]) { printf("%s n %u\n", CIPHER_DEF[f].name, n); assert(0); }
        }
    }
}

/* the extremes of A, B, C and of t: no shift past 31, no division by 0 (UBSan's shift check and the trap) */
static void cipher_extremes_safe(void)
{
    static const uint8_t X[2] = {0, 15};
    for (int f = 0; f < CIPHER_N; f++)
        for (int i = 0; i < 8; i++) {
            q_cipher c = { (uint8_t)f, X[i & 1], X[i >> 1 & 1], X[i >> 2], 64, 0, 0, 1, 48, 2 };
            uint32_t sum = 0;
            for (uint32_t n = 0; n < 300; n++) sum += gen_cipher_byte(&c, n);      /* t 0..299 */
            c.start = 127; c.speed = 255;
            for (uint32_t n = 0; n < 4; n++) sum += gen_cipher_byte(&c, n);
            for (uint32_t n = 16843000u; n < 16843010u; n++) sum += gen_cipher_byte(&c, n);   /* t past 2^32: it wraps */
            (void)sum;
        }
}

/* each formula at its advised values: alive (enough distinct bytes, enough rhythm levels) */
static void cipher_alive(void)
{
    for (int f = 0; f < CIPHER_N; f++) {
        q_cipher c;
        gen_cipher_preset(&c, f);
        int seen[256] = {0}, lv[8] = {0}, d = 0, l = 0;
        for (uint32_t n = 0; n < 32; n++) { int v = gen_cipher_byte(&c, n); d += !seen[v]++; l += !lv[v >> 5]++; }
        assert(d >= 6 && l >= 4);
    }
}

static q_pattern *cipher_setup(int model)
{
    project_defaults(&P);
    P.sound[0].model = model;
    q_pattern *pt = &P.pat[0][0];
    pt->src = SRC_CIPHER;
    memset(&G, 0, sizeof G);
    return pt;
}

/* the byte decides the step: DENS against bits 5-7, the accent bit 4, the note bits 0-3 (a KIT: the pad) */
static void cipher_decode(void)
{
    q_pattern *pt = cipher_setup(8);
    pt->cipher.dens = 0;
    for (int k = 0; k < 64; k++) { q_step st; int acc; assert(!played(pt, k, &st, &acc)); }
    pt = cipher_setup(8);
    pt->cipher.dens = 127;
    uint16_t sc = gen_scale(&P);
    int nsc = __builtin_popcount(sc);
    for (uint32_t k = 0; k < 64; k++) {
        q_step st; int acc, v = gen_cipher_byte(&pt->cipher, k % pt->cipher.len);
        assert(played(pt, (int)k, &st, &acc));
        assert(st.note[0] == gen_life_note(pt->cipher.note, sc, P.tonic, (v & 15) * pt->cipher.range * nsc / 16));
        assert(!st.note[1] && st.len == 4 && st.vel == 100 && !!acc == !!(v & 16));
    }
    pt = cipher_setup(M_KIT);
    pt->cipher.dens = 127;
    for (uint32_t k = 0; k < 32; k++) {
        q_step st; int acc;
        played(pt, (int)k, &st, &acc);
        assert(st.note[0] == KIT_NOTE + (gen_cipher_byte(&pt->cipher, k % pt->cipher.len) & 15));
    }
    pt = cipher_setup(8);                     /* DENS in between: the rhythm level against round(DENS x 8 / 127) */
    pt->cipher.dens = 48;
    for (uint32_t k = 0; k < 64; k++) {
        q_step st; int acc;
        assert(!!played(pt, (int)k, &st, &acc) == ((gen_cipher_byte(&pt->cipher, k % 16) >> 5) < 3));
    }
}

/* the loop: LEN 16 comes back on step 17; FREE runs on; gen_loop: LEN, 16 in FREE */
static void cipher_loop(void)
{
    q_pattern *pt = cipher_setup(8);
    pt->cipher.dens = 127;
    q_step a, b; int acc;
    played(pt, 0, &a, &acc);
    for (int k = 1; k < 16; k++) played(pt, k, &b, &acc);
    played(pt, 16, &b, &acc);
    assert(a.note[0] == b.note[0] && G.cipher_n == 1);
    assert(gen_loop(pt) == 16);
    pt->cipher.len = 40;
    assert(gen_loop(pt) == 40);
    pt = cipher_setup(8);
    pt->cipher.len = 0;
    for (int k = 0; k < 64; k++) played(pt, k, &b, &acc);
    assert(G.cipher_n == 64 && gen_loop(pt) == 16);
}

/* LEN changed while playing: the formula's step follows the sequencer's (k), so step 0 stays on the loop's start */
static void cipher_len_change_keeps_loop(void)
{
    q_pattern *pt = cipher_setup(8);
    pt->cipher.dens = 127;
    pt->cipher.len = 64;
    q_step st, first; int acc;
    played(pt, 0, &first, &acc);
    for (int k = 1; k <= 40; k++) played(pt, k, &st, &acc);
    pt->cipher.len = 16;                      /* the sequencer wraps: the next step is k 0 */
    played(pt, 0, &st, &acc);
    assert(st.note[0] == first.note[0] && G.cipher_n == 1);
    pt->cipher.len = 0;                       /* FREE for 20 steps, then LEN 12 at k 4 */
    for (int k = 0; k < 20; k++) played(pt, k % 16, &st, &acc);
    pt->cipher.len = 12;
    played(pt, 4, &st, &acc);
    assert(st.note[0] == gen_life_note(pt->cipher.note, gen_scale(&P), P.tonic,
                                       (gen_cipher_byte(&pt->cipher, 4) & 15) * pt->cipher.range * __builtin_popcount(gen_scale(&P)) / 16));
    assert(G.cipher_n == 5);
}

/* np, the numbers a formula uses: one it has not changes no byte; one it has changes some (over stretches of t) */
static void cipher_np_matches(void)
{
    for (int f = 0; f < CIPHER_N; f++) {
        for (int k = 0; k < 3; k++) {
            q_cipher c, d;
            gen_cipher_preset(&c, f);
            d = c;
            uint8_t *x = k == 0 ? &d.a : k == 1 ? &d.b : &d.c;
            int differs = 0;
            for (int st = 0; st < 128 && !differs; st += 9) {   /* several stretches of t: one may not show a number */
                c.start = d.start = (uint8_t)st;
                for (int v = 0; v < 16 && !differs; v++) {
                    *x = (uint8_t)v;
                    for (uint32_t n = 0; n < 64; n++) differs |= gen_cipher_byte(&c, n) != gen_cipher_byte(&d, n);
                }
            }
            if (differs != (k < CIPHER_DEF[f].np)) { fprintf(stderr, "%s param %d\n", CIPHER_DEF[f].name, k); assert(0); }
        }
    }
}

int main(void)
{
    cipher_matches_search();
    cipher_extremes_safe();
    cipher_alive();
    cipher_decode();
    cipher_loop();
    cipher_len_change_keeps_loop();
    cipher_np_matches();
    marbles_gate_sets_len();
    grids_level_matches_step();
    corner_follows_node();
    grids_kit_pads();
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
    life_blinker();
    life_glider_wraps();
    life_block_still();
    life_seed_repeats();
    life_gen_every_n_tours();
    life_auto_reseeds_dead();
    life_auto_idle_at_dens0();
    life_frozen_kept_without_auto();
    life_mut_never_stuck();
    life_seed_change_next_tour();
    life_rows_poly();
    life_notes_synth();
    life_degrees_clamp();
    life_no_repeat_at_top();
    life_notes_kit();
    life_born_accented();
    life_pattern_switch_reseeds();
    lanes_defaults();
    lanes_heads();
    lanes_no_reset_at_wrap();
    lanes_reset();
    lanes_len_shrink();
    lanes_trig_off();
    lanes_notes_synth();
    lanes_note_clamp();
    lanes_kit();
    lanes_dur_vel();
    lanes_degree();
    lanes_pattern_switch();
    chords_defaults();
    chords_one_per_bar();
    chords_sizes();
    chords_poly_cut();
    chords_smooth();
    chords_smooth_counts();
    chords_smooth_stable();
    chords_smooth_no_drift();
    chords_source_mid_bar();
    chords_note_clamp();
    chords_rate_len();
    chords_strum_bits();
    chords_kit();
    chords_len_shrink();
    tonnetz_defaults();
    tonnetz_moves();
    tonnetz_start();
    tonnetz_walk_free();
    tonnetz_walk_return();
    tonnetz_in_scale();
    tonnetz_in_scale_stuck();
    tonnetz_chaos();
    tonnetz_len_shrink();
    tonnetz_long_walk();
    tonnetz_wrap();
    tonnetz_play_like_chords();
    orbits_defaults();
    orbits_passes();
    orbits_notes();
    orbits_phase_mod();
    orbits_period_change();
    orbits_four();
    orbits_moons();
    mimic_defaults();
    mimic_in_order();
    mimic_chord();
    mimic_chord_no_repeat();
    mimic_all_chord();
    mimic_order2();
    mimic_fallback();
    mimic_drift();
    mimic_loop();
    mimic_seed();
    mimic_silent();
    mimic_listen_kept_on_reset();
    mimic_kit();
    mimic_shrunk();
    mimic_gap();
    mimic_learn();
    marbles_draw_new_loops();
    puts("gen ok");
    return 0;
}
