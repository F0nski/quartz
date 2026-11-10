/* The step 1 interface: three domains (EDIT, SEQ, GLO) and their pages, the keys, the lights and a sober screen.
 * Part of the unity build (app.c): gfx.c is in scope. The render reads the project each block: byte fields are
 * written as they are, a model change goes through audio_track_model. */
#include "ui.h"
#include "audio.h"
#include "dsp/pq.h"

#define MSG_MS 1500u
#define HEAD_H 30
#define BAND_H 83                    /* the scene, 240 x 166, in two bands */
#define FOOT_Y 196
#define PINK RGB(255, 47, 142)       /* notes playing only */

enum { D_EDIT, D_SEQ, D_GLO, ND };
enum { P_ENGINE, P_SOUND, P_STEPS, P_TEMPO, P_CPU, P_XY };
/* what PRESET sets on a held step; the black keys 0..7 pick RATCHET .. MICRO, 8 is CLEAR */
enum { A_VEL, A_RATCHET, A_PROB, A_COND, A_SLIDE, A_ACCENT, A_NOTE, A_LEN, A_MICRO, A_N };
#define COND_N 40                    /* NONE, FIRST, !FIRST, PRE, !PRE, then A:B for B = 2..8, A = 1..B */

static const uint8_t DOMAIN_PAGES[ND][2] = {{P_ENGINE, P_SOUND}, {P_STEPS, P_XY}, {P_TEMPO, P_CPU}};
static const uint8_t DOMAIN_N[ND] = {2, 1, 2};      /* SEQ: 2 pages while PATH is XY (domain_n) */
static const uint8_t DOMAIN_BTN[ND] = {B_EDIT, B_SEQ, B_GLO};
static const char *const DOMAIN_NAME[ND] = {"EDIT", "SEQ", "GLO"};

typedef struct { const char *name, *label[4]; int16_t min[4], max[4]; } q_page;
static const q_page PAGE[] = {
    {"ENGINE", {"HARM", "TIMB", "MORPH", "DECAY"}, {0, 0, 0, 0}, {127, 127, 127, 127}},
    {"SOUND", {"MODEL", "TUNE", "GLIDE", "POLY"}, {0, -24, 0, 1}, {23, 24, 127, 4}},
    {"STEPS", {"LEN", "DIV", "PATH"}, {1, 0, 0}, {Q_STEPS, DIV_N - 1, PATH_N - 1}},
    {"TEMPO", {"TEMPO"}, {40}, {240}},
    {"CPU", {0}, {0}, {0}},
    {"XY", {"X LEN", "X DIV", "Y LEN", "Y DIV"}, {1, 0, 1, 0}, {4, DIV_N - 1, 4, DIV_N - 1}},
};

/* Plaits' registration order (pq.h) */
static const char *const MODEL_NAME[24] = {
    "VA VCF", "PHASE DIST", "6-OP A", "6-OP B", "6-OP C", "WAVE TERRAIN", "STRING MACH", "CHIPTUNE",
    "VA", "WAVESHAPE", "2-OP FM", "GRAIN FORM", "ADDITIVE", "WAVETABLE", "CHORDS", "SPEECH",
    "SWARM", "NOISE", "PARTICLE", "STRING", "MODAL", "BASS DRUM", "SNARE", "HI-HAT"};
static const char *const DIV_NAME[DIV_N] = {"1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2", "1/1"};
static const char *const PATH_NAME[PATH_N] = {"FWD", "REV", "PING-PONG", "SNAKE", "COLUMNS", "SPIRAL", "RANDOM", "XY"};
static const char *const ATTR_NAME[A_N] = {"VEL", "RATCHET", "PROB", "COND", "SLIDE", "ACCENT", "NOTE", "LEN", "MICRO"};
static const uint8_t ATTR_ROW[A_N] = {A_NOTE, A_VEL, A_LEN, A_MICRO, A_PROB, A_COND, A_RATCHET, A_SLIDE, A_ACCENT};
static const char *const COND_NAME[5] = {"NONE", "FIRST", "!FIRST", "PRE", "!PRE"};
static const uint8_t WHITE[16] = PLAT_WHITE_KEYS;
static const uint8_t BLACK[11] = PLAT_BLACK_KEYS;
static const uint16_t KCOL[4] = {RGB(58, 109, 255), RGB(34, 213, 142), RGB(240, 240, 250), RGB(255, 122, 46)};

/* what is on the screen, part by part: a part is drawn again when its view changes */
typedef struct {
    struct { int8_t track, domain, page, n, msg, pat, playing; } head;   /* pat: P1..P8 asked; playing: the one playing when it differs, else 0 */
    struct { int8_t pid; uint8_t col, hide; int16_t v[4]; } foot;           /* col: the knobs shown in colour, the rest in grey (a lock page) */
    struct {
        int8_t pid, spage, cursor, pos, len, path, held, attr, showpath, xlen, ylen;
        uint16_t locks;                         /* the page's steps that have p-locks */
        q_step hst;                             /* the held step */
        int16_t v[4];
        uint8_t model;
        uint16_t tempo;
        q_step st[16];
        int32_t cpu[6];                         /* the CPU page's numbers as shown; core 2 -1: OFF */
    } scene;
} q_view;

static struct {
    q_project *p;
    int domain, page[ND], track, octave, spage, rec, cursor, dirty;
    int held, held_key, edited, attr, lpage, nwrote, seqdown;   /* held: the step held 1..64, 0 none; lpage: the held step's p-lock page;
                                                                   nwrote: notes of the chord written since the hold began */
    uint8_t last_note[Q_TRACKS];               /* a step switched on by hand takes it */
    int8_t key_note[NKEYS], key_track[NKEYS];  /* what each held key sounds, for its note-off; -1 none */
    uint32_t btn, keys, msg_until, locks_until, path_until;
    q_view shown;
    int full;                                  /* the next frame draws everything */
} ui;

static q_pattern *pat(void) { return &ui.p->pat[ui.track][ui.p->pattern[ui.track]]; }
static int domain_n(int d) { return d == D_SEQ && pat()->path == PATH_XY ? 2 : DOMAIN_N[d]; }
static int page_id(void) { return DOMAIN_PAGES[ui.domain][ui.page[ui.domain]]; }

static int knob_get(int pid, int k)
{
    const q_sound *s = &ui.p->sound[ui.track];
    const q_pattern *t = &ui.p->pat[ui.track][ui.p->pattern[ui.track]];
    switch (pid * 4 + k) {
    case P_ENGINE * 4: return s->harm;
    case P_ENGINE * 4 + 1: return s->timb;
    case P_ENGINE * 4 + 2: return s->morph;
    case P_ENGINE * 4 + 3: return s->decay;
    case P_SOUND * 4: return s->model;
    case P_SOUND * 4 + 1: return s->tune;
    case P_SOUND * 4 + 2: return s->glide;
    case P_SOUND * 4 + 3: return s->poly;
    case P_STEPS * 4: return t->len;
    case P_STEPS * 4 + 1: return t->div;
    case P_STEPS * 4 + 2: return t->path;
    case P_TEMPO * 4: return ui.p->tempo;
    case P_XY * 4: return t->xlen;
    case P_XY * 4 + 1: return t->div;
    case P_XY * 4 + 2: return t->ylen;
    case P_XY * 4 + 3: return t->ydiv;
    }
    return 0;
}

static void knob_set(int pid, int k, int v)
{
    q_sound *s = &ui.p->sound[ui.track];
    q_pattern *t = &ui.p->pat[ui.track][ui.p->pattern[ui.track]];
    switch (pid * 4 + k) {
    case P_ENGINE * 4: s->harm = (uint8_t)v; break;
    case P_ENGINE * 4 + 1: s->timb = (uint8_t)v; break;
    case P_ENGINE * 4 + 2: s->morph = (uint8_t)v; break;
    case P_ENGINE * 4 + 3: s->decay = (uint8_t)v; break;
    case P_SOUND * 4: s->model = (uint8_t)v; audio_track_model(ui.track); break;
    case P_SOUND * 4 + 1: s->tune = (int8_t)v; break;
    case P_SOUND * 4 + 2: s->glide = (uint8_t)v; break;
    case P_SOUND * 4 + 3: s->poly = (uint8_t)v; break;
    case P_STEPS * 4:
        t->len = (uint8_t)v;
        if (ui.cursor > v)              /* step entry past the end: back to step 1 */
            ui.cursor = 1;
        break;
    case P_STEPS * 4 + 1: t->div = (uint8_t)v; break;
    case P_STEPS * 4 + 2:
        t->path = (uint8_t)v;
        ui.path_until = plat_ms() + MSG_MS;     /* the path is drawn while it is being chosen */
        break;
    case P_TEMPO * 4: ui.p->tempo = (uint16_t)v; break;
    case P_XY * 4: t->xlen = (uint8_t)v; break;
    case P_XY * 4 + 1: t->div = (uint8_t)v; break;
    case P_XY * 4 + 2: t->ylen = (uint8_t)v; break;
    case P_XY * 4 + 3: t->ydiv = (uint8_t)v; break;
    }
    ui.dirty = 1;
}

static void knob_turn(int pid, int k, int d)
{
    const q_page *pg = &PAGE[pid];
    int v;
    if (!d || !pg->label[k])
        return;
    v = clamp(knob_get(pid, k) + d, pg->min[k], pg->max[k]);
    if (v != knob_get(pid, k))
        knob_set(pid, k, v);
}

/* ------------------------------------------------------------------ input --- */
static void button(int b)
{
    int d;
    for (d = 0; d < ND; d++)
        if (b == DOMAIN_BTN[d]) {
            if (ui.domain != d)
                ui.held = 0;
            ui.domain = d;
        }
    switch (b) {
    case B_PLAY:
        audio_play(!audio_playing());
        ui.rec = 0;
        break;
    case B_REC:
        if (!audio_playing()) {
            ui.rec = !ui.rec;
            ui.cursor = 1;
        }
        break;
    case B_SAVE:
        if (audio_playing())            /* a flash write stalls the render */
            ui.msg_until = plat_ms() + MSG_MS;
        else if (store_save(ui.p) == 0)
            ui.dirty = 0;
        break;
    case B_OCTDN:
    case B_OCTUP:
        d = b == B_OCTUP ? 1 : -1;
        if (ui.domain == D_SEQ)
            ui.spage = clamp(ui.spage + d, 0, Q_STEPS / 16 - 1);
        else
            ui.octave = clamp(ui.octave + d, -2, 2);
        break;
    }
}

static int cond_value(int i)
{
    int b = 2;
    if (i < 5)
        return i;
    for (i -= 5; i >= b; b++)
        i -= b;
    return (i + 1) | b << 4;
}

static int cond_index(int c)
{
    int i;
    for (i = 0; i < COND_N - 1 && cond_value(i) != c; i++) {}
    return i;
}

/* the fields before the flag: the render never reads half a step */
static void step_on(q_step *s)
{
    __asm__ volatile("" ::: "memory");
    s->flags |= SF_ON;
}

static void step_clear(q_pattern *t, int n)
{
    q_step *s = &t->steps[n];
    s->flags &= (uint8_t)~SF_ON;
    __asm__ volatile("" ::: "memory");
    __builtin_memset(s, 0, sizeof *s);
    s->note[0] = 60;
    s->vel = 100;
    s->len = 4;
    s->prob = 20;
    lock_clear_step(t, n);
}

/* PRESET on the held step: the chosen attribute */
static void attr_turn(q_step *s, int d)
{
    int i, lo = 127, hi = 0;
    switch (ui.attr) {
    case A_VEL: s->vel = (uint8_t)clamp(s->vel + d, 1, 127); break;
    case A_RATCHET: s->flags = (uint8_t)((s->flags & ~0x18) | (clamp(STEP_RATCHET(s) + d, 1, 4) - 1) << 3); break;
    case A_PROB: s->prob = (uint8_t)clamp(s->prob + d, 0, 20); break;
    case A_COND: s->cond = (uint8_t)cond_value(clamp(cond_index(s->cond) + d, 0, COND_N - 1)); break;
    case A_NOTE:                        /* the whole chord, kept in 1..127 */
        for (i = 0; i < 4; i++)
            if (i == 0 || s->note[i]) {
                lo = lo < s->note[i] ? lo : s->note[i];
                hi = hi > s->note[i] ? hi : s->note[i];
            }
        d = clamp(d, 1 - lo, 127 - hi);
        for (i = 0; i < 4; i++)
            if (i == 0 || s->note[i])
                s->note[i] = (uint8_t)(s->note[i] + d);
        break;
    case A_LEN: s->len = (uint8_t)clamp(s->len + d, 1, 64); break;
    case A_MICRO: s->micro = (int8_t)clamp(s->micro + d, -11, 11); break;
    }
}

/* the q_sound field the held step's lock page gives knob k, -1 none */
static int lock_param(int k) { return ui.lpage ? (k == 1 || k == 2 ? LK_TUNE + k - 1 : -1) : k; }

static int lock_value(const q_pattern *t, int param, int *locked)
{
    int i = lock_find(t, ui.held - 1, param), k = param < LK_TUNE ? param : param - LK_TUNE + 1;
    *locked = i >= 0;
    if (i < 0)
        return knob_get(ui.lpage ? P_SOUND : P_ENGINE, k);
    return param == LK_TUNE ? (int8_t)t->locks[i].value : t->locks[i].value;
}

static void lock_turn(int k, int d)
{
    q_pattern *t = pat();
    int pid = ui.lpage ? P_SOUND : P_ENGINE, param = lock_param(k), locked, v;
    if (!d || param < 0)
        return;
    v = clamp(lock_value(t, param, &locked) + d, PAGE[pid].min[k], PAGE[pid].max[k]);
    ui.edited = 1;                      /* a knob turned on a held step is an edit, the lock fitting or not */
    if (lock_set(t, ui.held - 1, param, v)) {
        ui.locks_until = plat_ms() + MSG_MS;
        return;
    }
    step_on(&t->steps[ui.held - 1]);
    ui.dirty = 1;
}

static void pattern_set(int n)
{
    ui.p->pattern[ui.track] = (uint8_t)n;
    if (ui.cursor > pat()->len)
        ui.cursor = 1;
    ui.dirty = 1;
}

/* a black key while a step is held */
static void black_key(int b, q_step *s, q_pattern *t)
{
    if (b >= 11)
        return;
    if (b == 8) {
        step_clear(t, ui.held - 1);
    } else if (b < 8) {
        if (b == 3 || b == 4)           /* SLIDE, ACCENT: toggles */
            s->flags ^= b == 3 ? SF_SLIDE : SF_ACCENT;
        else
            ui.attr = b + 1;
    }
}

static void key_down(int k)
{
    q_pattern *t = pat();
    q_step *s = 0;
    int note = 53 + k + 12 * ui.octave, w, b;
    for (w = 0; w < 16 && WHITE[w] != k; w++) {}
    if (ui.domain == D_SEQ) {           /* the white keys are the page's 16 steps */
        if (w < 16 && ui.seqdown) {     /* SEQ held: the white keys 1..8 are the patterns */
            if (w < Q_PATTERNS)
                pattern_set(w);
            return;
        }
        if (w < 16 && !ui.held) {
            ui.held = ui.spage * 16 + w + 1;
            ui.held_key = k;
            ui.edited = ui.lpage = ui.nwrote = 0;
            ui.attr = A_VEL;
            return;
        }
        if (!ui.held)
            return;
        ui.edited = ui.dirty = 1;
        s = &t->steps[ui.held - 1];
        if (w == 16) {
            for (b = 0; b < 11 && BLACK[b] != k; b++) {}
            black_key(b, s, t);
            return;
        }
    }
    audio_live_note(ui.track, note, 1);
    ui.key_note[k] = (int8_t)note;
    ui.key_track[k] = (int8_t)ui.track;
    ui.last_note[ui.track] = (uint8_t)note;
    if (s) {                            /* the first note replaces the chord, the next ones fill it up to POLY */
        if (!ui.nwrote)
            __builtin_memset(s->note, 0, sizeof s->note);
        if (ui.nwrote < ui.p->sound[ui.track].poly)
            s->note[ui.nwrote++] = (uint8_t)note;
        step_on(s);
    } else if (ui.rec) {                /* step entry: the note at the cursor, then the next step */
        s = &t->steps[ui.cursor - 1];
        s->note[0] = (uint8_t)note;
        s->vel = 100;
        step_on(s);
        ui.cursor = ui.cursor % t->len + 1;
        ui.dirty = 1;
    }
}

static void key_up(int k)
{
    if (ui.held && k == ui.held_key) {  /* nothing edited: the step toggles */
        q_step *s = &pat()->steps[ui.held - 1];
        if (!ui.edited) {
            if (!(s->flags & SF_ON)) {
                s->note[0] = ui.last_note[ui.track];
                s->vel = 100;
            }
            __asm__ volatile("" ::: "memory");   /* note and vel before on */
            s->flags ^= SF_ON;
            ui.dirty = 1;
        }
        ui.held = 0;
    }
    if (ui.key_note[k] < 0)
        return;
    audio_live_note(ui.key_track[k], ui.key_note[k], 0);
    ui.key_note[k] = -1;
}

void ui_input(void)
{
    uint32_t btn = plat_buttons(), keys = plat_keys();
    int i, e, d, n;
    ui.seqdown = btn >> B_SEQ & 1u;
    for (i = 0; i < NB; i++)
        if ((btn & ~ui.btn) >> i & 1u)
            button(i);
    for (i = 0; i < NKEYS; i++) {
        if ((keys & ~ui.keys) >> i & 1u)
            key_down(i);
        else if ((ui.keys & ~keys) >> i & 1u)
            key_up(i);
    }
    ui.btn = btn;
    ui.keys = keys;
    d = ui.domain;                      /* after the buttons: SELECT turns the page of the domain just picked */
    n = domain_n(d);
    if ((e = plat_enc(EN_ALGO)) != 0) {
        ui.track = clamp(ui.track + e, 0, Q_TRACKS - 1);
        ui.held = 0;
        if (ui.cursor > ui.p->pat[ui.track][ui.p->pattern[ui.track]].len)       /* LEN is per track: same rule as when it shrinks */
            ui.cursor = 1;
    }
    if ((e = plat_enc(EN_SELECT)) != 0) {
        if (ui.held)                    /* the held step's p-lock page */
            ui.lpage = ((ui.lpage + e) % 2 + 2) % 2;
        else
            ui.page[d] = ((ui.page[d] + e) % n + n) % n;
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {
        if (ui.held) {
            attr_turn(&pat()->steps[ui.held - 1], e);
            ui.edited = ui.dirty = 1;
        } else if (ui.domain == D_SEQ) {
            pattern_set(clamp(ui.p->pattern[ui.track] + e, 0, Q_PATTERNS - 1));
        } else if (ui.domain == D_EDIT) {
            knob_turn(P_SOUND, 0, e);
        }
    }
    if (ui.page[D_SEQ] >= domain_n(D_SEQ))      /* another pattern or track, no XY page */
        ui.page[D_SEQ] = 0;
    for (i = 0; i < 4; i++) {
        e = plat_enc(EN_K1 + i);
        if (ui.held)
            lock_turn(i, e);
        else
            knob_turn(page_id(), i, e);
    }
}

static void lights(void)
{
    const q_pattern *t = &ui.p->pat[ui.track][ui.p->pattern[ui.track]];
    uint32_t btn = 1u << DOMAIN_BTN[ui.domain], keys = 0;
    int i, pos = audio_pos(ui.track);
    if (audio_playing())
        btn |= 1u << B_PLAY;
    if (ui.rec)
        btn |= 1u << B_REC;
    if (ui.domain == D_SEQ && ui.seqdown)       /* SEQ held: the pattern asked */
        keys = 1u << WHITE[ui.p->pattern[ui.track]];
    else if (ui.domain == D_SEQ)
        for (i = 0; i < 16; i++)
            if (t->steps[ui.spage * 16 + i].flags & SF_ON || ui.spage * 16 + i == pos)
                keys |= 1u << WHITE[i];
    plat_leds(btn, keys);
}

/* ----------------------------------------------------------------- screen --- */
static void int_text(int v, char *b)
{
    char t[12];
    int n = 0;
    unsigned u = (unsigned)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) != 0);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}

static void note_name(int note, char *b)
{
    static const char *const N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const char *s = N[note % 12];
    while (*s)
        *b++ = *s++;
    int_text(note / 12 - 1, b);
}

static void knob_text(int pid, int k, int v, char *b)
{
    if ((pid == P_STEPS && k > 0) || (pid == P_XY && k & 1)) {
        const char *s = pid == P_STEPS && k == 2 ? PATH_NAME[v] : DIV_NAME[v];
        while ((*b++ = *s++) != 0) {}
        return;
    }
    if (pid == P_SOUND && k == 1 && v > 0)
        *b++ = '+';
    int_text(v, b);
}

static void attr_text(const q_step *s, int a, char *b)
{
    int i;
    switch (a) {
    case A_VEL: int_text(s->vel, b); break;
    case A_RATCHET: int_text(STEP_RATCHET(s), b); break;
    case A_PROB:
        int_text(s->prob * 5, b);
        while (*b)
            b++;
        *b++ = '%', *b = 0;
        break;
    case A_COND:
        if (s->cond < 5) {
            const char *n = COND_NAME[s->cond];
            while ((*b++ = *n++) != 0) {}
        } else {
            int_text(s->cond & 15, b);
            while (*b)
                b++;
            *b++ = ':';
            int_text(s->cond >> 4, b);
        }
        break;
    case A_SLIDE:
    case A_ACCENT: {
        const char *n = s->flags & (a == A_SLIDE ? SF_SLIDE : SF_ACCENT) ? "ON" : "OFF";
        while ((*b++ = *n++) != 0) {}
        break;
    }
    case A_NOTE:
        for (i = 0; i < 4; i++)
            if (i == 0 || s->note[i]) {
                if (i)
                    *b++ = ' ';
                note_name(s->note[i], b);
                while (*b)
                    b++;
            }
        break;
    case A_LEN: int_text(s->len, b); break;
    case A_MICRO:
        if (s->micro > 0)
            *b++ = '+';
        int_text(s->micro, b);
        break;
    }
}

static void view_now(q_view *v)
{
    const q_pattern *t = &ui.p->pat[ui.track][ui.p->pattern[ui.track]];
    int pid = page_id(), k, fpid = ui.held ? (ui.lpage ? P_SOUND : P_ENGINE) : pid, locked;
    __builtin_memset(v, 0, sizeof *v);  /* the padding too: views are compared whole */
    v->head.track = (int8_t)ui.track;
    v->head.domain = (int8_t)ui.domain;
    v->head.page = (int8_t)ui.page[ui.domain];
    v->head.n = (int8_t)domain_n(ui.domain);
    v->head.msg = plat_ms() < ui.msg_until ? 1 : plat_ms() < ui.locks_until ? 2 : 0;
    v->head.pat = (int8_t)(ui.p->pattern[ui.track] + 1);
    if (audio_playing() && audio_pattern(ui.track) != ui.p->pattern[ui.track])
        v->head.playing = (int8_t)(audio_pattern(ui.track) + 1);
    v->foot.pid = (int8_t)fpid;
    v->scene.pid = (int8_t)pid;
    v->foot.col = 15;
    for (k = 0; k < 4; k++)
        if (PAGE[fpid].label[k])
            v->foot.v[k] = (int16_t)knob_get(fpid, k);
    if (ui.held)                        /* the lock page: the locked values in colour, the track's in grey */
        for (v->foot.col = 0, k = 0; k < 4; k++) {
            if (lock_param(k) < 0) {
                v->foot.hide |= (uint8_t)(1u << k);
                continue;
            }
            v->foot.v[k] = (int16_t)lock_value(t, lock_param(k), &locked);
            v->foot.col |= (uint8_t)(locked << k);
        }
    if (pid == P_ENGINE || pid == P_SOUND) {
        __builtin_memcpy(v->scene.v, v->foot.v, sizeof v->scene.v);
        v->scene.model = ui.p->sound[ui.track].model;
    } else if (pid == P_STEPS || pid == P_XY) {
        v->scene.spage = (int8_t)ui.spage;
        v->scene.path = (int8_t)t->path;
        v->scene.showpath = pid == P_STEPS && plat_ms() < ui.path_until;
        v->scene.xlen = (int8_t)(pid == P_XY ? t->xlen : 4);
        v->scene.ylen = (int8_t)(pid == P_XY ? t->ylen : 4);
        v->scene.held = (int8_t)ui.held;
        v->scene.attr = (int8_t)ui.attr;
        if (ui.held)
            v->scene.hst = t->steps[ui.held - 1];
        for (k = 0; k < t->nlocks; k++)
            if (t->locks[k].step / 16 == ui.spage)
                v->scene.locks |= (uint16_t)(1u << (t->locks[k].step % 16));
        v->scene.cursor = (int8_t)(ui.rec ? ui.cursor : 0);
        v->scene.pos = (int8_t)audio_pos(ui.track);
        v->scene.len = (int8_t)t->len;
        __builtin_memcpy(v->scene.st, &t->steps[ui.spage * 16], sizeof v->scene.st);
    } else if (pid == P_TEMPO) {
        v->scene.tempo = ui.p->tempo;
    } else {
        q_load l = audio_load();
        v->scene.cpu[0] = (int32_t)l.core1_pct;
        v->scene.cpu[1] = l.cpu2 ? (int32_t)l.core2_pct : -1;
        v->scene.cpu[2] = (int32_t)l.xruns;
        v->scene.cpu[3] = (int32_t)l.active;
        v->scene.cpu[4] = (int32_t)pq_voice_bytes();
        v->scene.cpu[5] = (int32_t)l.cpu2_stack;
    }
}

static void draw_head(const q_view *v)
{
    char t[8];
    int32_t x;
    cv_begin(240, HEAD_H, C_BLACK);
    cv_rect(6, 5, 20, 20, C_HI);
    int_text(v->head.track + 1, t);
    cv_text(6 + (20 - text_w(&FONT_B, t)) / 2, 7, &FONT_B, t, C_BLACK);
    x = cv_text(34, 7, &FONT_B, DOMAIN_NAME[v->head.domain], C_WHITE);
    if (v->head.msg)
        cv_text(x + 10, 7, &FONT_B, v->head.msg == 1 ? "STOP TO SAVE" : "LOCKS FULL", KCOL[3]);
    else if (v->head.domain == D_SEQ) {         /* the pattern asked, after the one playing when it differs */
        x += 10;
        if (v->head.playing) {
            t[0] = 'P', t[1] = (char)('0' + v->head.playing), t[2] = 0;
            x = cv_text(x, 7, &FONT_B, t, C_GRAY) + 4;
            x = cv_text(x, 7, &FONT_B, ">", C_GRAY) + 4;
        }
        t[0] = 'P', t[1] = (char)('0' + v->head.pat), t[2] = 0;
        cv_text(x, 7, &FONT_B, t, C_AMB);
    } else
        cv_text(x + 10, 7, &FONT_B, PAGE[v->foot.pid].name, C_AMB);
    t[0] = (char)('1' + v->head.page);
    t[1] = '/';
    t[2] = (char)('0' + v->head.n);
    t[3] = 0;
    cv_text(234 - text_w(&FONT_S, t), 7, &FONT_S, t, C_GRAY);
    cv_rect(0, HEAD_H - 1, 240, 1, C_LINE);
    cv_blit(0, 0);
}

/* the whole 166-pixel scene drawn at y offset oy: each band clips its own rows */
static void draw_scene(const q_view *v, int32_t oy)
{
    char t[16];
    int k, i;
    switch (v->scene.pid) {
    case P_ENGINE:
    case P_SOUND:
        cv_text(10, oy + 8, &FONT_M, MODEL_NAME[v->scene.model], C_WHITE);
        for (k = 0; k < 4; k++) {
            const q_page *pg = &PAGE[v->scene.pid];
            int32_t x = 60 * k + 12, h = 108 * (v->scene.v[k] - pg->min[k]) / (pg->max[k] - pg->min[k]);
            cv_rect(x, oy + 48, 36, 108, C_LINE);
            cv_rect(x, oy + 156 - h, 36, h, KCOL[k]);
        }
        break;
    case P_STEPS:
    case P_XY:
        if (v->scene.held) {            /* the held step's attributes, the one PRESET sets on K4's colour */
            for (i = 0; i < A_N; i++) {
                int a = ATTR_ROW[i];
                int32_t y = oy + 2 + 18 * i;
                uint16_t c = a == v->scene.attr ? C_WHITE : C_GRAY;
                if (a == v->scene.attr)
                    cv_rect(0, y - 1, 240, 18, KCOL[3]);
                cv_text(10, y + 2, &FONT_S, ATTR_NAME[a], c);
                attr_text(&v->scene.hst, a, t);
                cv_text(230 - text_w(&FONT_S, t), y + 2, &FONT_S, t, a == v->scene.attr ? C_WHITE : C_HI);
            }
            break;
        }
        t[0] = 0;
        int_text(v->scene.spage * 16 + 1, t);
        for (i = 0; t[i]; i++) {}
        t[i++] = '-';
        int_text(v->scene.spage * 16 + 16, t + i);
        cv_text(8, oy + 2, &FONT_XS, t, C_GRAY);
        for (i = 0; i < 16; i++) {          /* 4 x 4, steps 1-4 on the first row */
            int s = v->scene.spage * 16 + i;
            int32_t x = 6 + 58 * (i % 4), y = oy + 18 + 37 * (i / 4);
            const q_step *st = &v->scene.st[i];
            uint16_t c = s == v->scene.pos ? PINK : s >= v->scene.len || i % 4 >= v->scene.xlen || i / 4 >= v->scene.ylen ? C_LINE
                         : st->flags & SF_ON ? C_HI : C_DIM;
            if (s == v->scene.cursor - 1) {
                cv_rect(x, y, 54, 33, C_WHITE);
                cv_rect(x + 2, y + 2, 50, 29, c);
            } else {
                cv_rect(x, y, 54, 33, c);
            }
            if (st->flags & SF_ON) {
                note_name(st->note[0], t);
                cv_text(x + (54 - text_w(&FONT_T, t)) / 2, y + 20, &FONT_T, t, C_BLACK);
            }
            if (v->scene.locks >> i & 1u)
                cv_rect(x + 44, y + 4, 6, 6, KCOL[3]);
        }
        if (v->scene.showpath && v->scene.path != PATH_RANDOM && v->scene.path != PATH_XY) {
            int m = v->scene.path == PATH_SNAKE ? 1 : v->scene.path == PATH_COLUMNS ? 2 : v->scene.path == PATH_SPIRAL ? 3 : 0;
            int first = PATHS[m][v->scene.path == PATH_REV ? 15 : 0];
            for (i = 0; i < 15; i++) {
                int a = PATHS[m][i], b = PATHS[m][i + 1];
                int32_t x0 = 33 + 58 * (a % 4), y0 = oy + 34 + 37 * (a / 4), x1 = 33 + 58 * (b % 4), y1 = oy + 34 + 37 * (b / 4);
                cv_line(x0, y0, x1, y1, C_WHITE);
                cv_line(x0 + 1, y0, x1 + 1, y1, C_WHITE);
                cv_line(x0, y0 + 1, x1, y1 + 1, C_WHITE);
            }
            cv_rect(33 + 58 * (first % 4) - 4, oy + 34 + 37 * (first / 4) - 4, 9, 9, KCOL[3]);
        }
        break;
    case P_TEMPO:
        int_text(v->scene.tempo, t);
        cv_text((240 - text_w(&FONT_L, t)) / 2, oy + 52, &FONT_L, t, C_WHITE);
        cv_text((240 - text_w(&FONT_XS, "BPM")) / 2, oy + 90, &FONT_XS, "BPM", C_GRAY);
        break;
    case P_CPU: {
        static const char *const ROW[6] = {"CORE 1", "CORE 2", "LOST BLOCKS", "VOICES", "BYTES/VOICE", "CORE 2 STACK"};
        for (i = 0; i < 6; i++) {
            int32_t y = oy + 6 + 24 * i;
            cv_text(16, y, &FONT_S, ROW[i], C_GRAY);
            if (i == 1 && v->scene.cpu[1] < 0) {
                t[0] = 'O', t[1] = 'F', t[2] = 'F', t[3] = 0;
            } else {
                int_text(v->scene.cpu[i], t);
                for (k = 0; t[k]; k++) {}
                if (i < 2)
                    t[k++] = '%', t[k] = 0;
            }
            cv_text(224 - text_w(&FONT_S, t), y, &FONT_S, t, C_WHITE);
        }
        break;
    }
    }
}

static void draw_foot(const q_view *v)
{
    const q_page *pg = &PAGE[v->foot.pid];
    char t[12];
    int k;
    cv_begin(240, 240 - FOOT_Y, C_BLACK);
    cv_rect(0, 0, 240, 1, C_LINE);
    for (k = 0; k < 4; k++) {
        int32_t x = 60 * k + 6;
        uint16_t c = v->foot.col >> k & 1u ? KCOL[k] : C_GRAY;
        if (!pg->label[k] || v->foot.hide >> k & 1u)
            continue;
        cv_text(x, 5, &FONT_XS, pg->label[k], c);
        knob_text(v->foot.pid, k, v->foot.v[k], t);
        cv_text(x, 20, &FONT_B, t, c);
        cv_rect(x, 39, 48, 2, C_LINE);
        cv_rect(x, 39, 48 * (v->foot.v[k] - pg->min[k]) / (pg->max[k] - pg->min[k]), 2, c);
    }
    cv_blit(0, FOOT_Y);
}

/* fm1-base calls it again when it resumes the app (a refused update): the held notes get their note-offs, the
 * unsaved edits stay unsaved, and the keys and buttons still down do not count as pressed again */
void ui_init(q_project *p)
{
    int k, dirty = ui.dirty;
    uint32_t btn = ui.btn, keys = ui.keys;
    ui.held = 0;                        /* (a hold's release must not toggle its step) */
    if (ui.p)
        for (k = 0; k < NKEYS; k++)
            key_up(k);
    __builtin_memset(&ui, 0, sizeof ui);
    __builtin_memset(ui.key_note, -1, sizeof ui.key_note);
    __builtin_memset(ui.last_note, 60, sizeof ui.last_note);
    ui.p = p;
    ui.cursor = 1;
    ui.full = 1;
    ui.dirty = dirty;
    ui.btn = btn;
    ui.keys = keys;
}

void ui_frame(void)
{
    q_view v;
    int b;
    ui_input();
    lights();
    view_now(&v);
    if (ui.full || __builtin_memcmp(&v.head, &ui.shown.head, sizeof v.head))
        draw_head(&v);
    if (ui.full || __builtin_memcmp(&v.scene, &ui.shown.scene, sizeof v.scene))
        for (b = 0; b < 2; b++) {
            cv_begin(240, BAND_H, C_BLACK);
            draw_scene(&v, -BAND_H * b);
            cv_blit(0, HEAD_H + BAND_H * b);
        }
    if (ui.full || __builtin_memcmp(&v.foot, &ui.shown.foot, sizeof v.foot))
        draw_foot(&v);
    ui.shown = v;
    ui.full = 0;
}

#ifdef PLAT_HOST
int ui_expect(const char *what, int *got)
{
    const q_sound *s = &ui.p->sound[ui.track];
    const q_pattern *t = &ui.p->pat[ui.track][ui.p->pattern[ui.track]];
    static const char *const F[] = {"stepon", "stepnote", "stepnote2", "stepvel", "steplen", "stepprob", "stepcond",
                                    "stepratchet", "stepslide", "stepaccent", "stepmicro", "lockharm"};
    const char *u = strchr(what, '_');
    int f = 12, n = u ? atoi(u + 1) : 0;
    while (u && f-- > 0 && !(!strncmp(F[f], what, (size_t)(u - what)) && !F[f][u - what])) {}
    if (u && f >= 0) {
        const q_step *st = &t->steps[n - 1];
        if (n < 1 || n > Q_STEPS)
            return 0;
        switch (f) {
        case 0: *got = st->flags & SF_ON; break;
        case 1: *got = st->note[0]; break;
        case 2: *got = st->note[1]; break;
        case 3: *got = st->vel; break;
        case 4: *got = st->len; break;
        case 5: *got = st->prob; break;
        case 6: *got = st->cond; break;
        case 7: *got = STEP_RATCHET(st); break;
        case 8: *got = st->flags & SF_SLIDE ? 1 : 0; break;
        case 9: *got = st->flags & SF_ACCENT ? 1 : 0; break;
        case 10: *got = st->micro; break;
        default: {
            int i = lock_find(t, n - 1, LK_HARM);
            *got = i < 0 ? -1 : t->locks[i].value;
        }
        }
    } else if (!strcmp(what, "domain")) *got = ui.domain;
    else if (!strcmp(what, "page")) *got = ui.page[ui.domain] + 1;
    else if (!strcmp(what, "track")) *got = ui.track + 1;
    else if (!strcmp(what, "playing")) *got = audio_playing();
    else if (!strcmp(what, "rec")) *got = ui.rec;
    else if (!strcmp(what, "cursor")) *got = ui.cursor;
    else if (!strcmp(what, "model")) *got = s->model;
    else if (!strcmp(what, "poly")) *got = s->poly;
    else if (!strcmp(what, "tune")) *got = s->tune;
    else if (!strcmp(what, "harm")) *got = s->harm;
    else if (!strcmp(what, "tempo")) *got = ui.p->tempo;
    else if (!strcmp(what, "len")) *got = t->len;
    else if (!strcmp(what, "div")) *got = t->div;
    else if (!strcmp(what, "pos")) *got = audio_pos(ui.track);
    else if (!strcmp(what, "held")) *got = ui.held;
    else if (!strcmp(what, "attr")) *got = ui.attr;
    else if (!strcmp(what, "path")) *got = t->path;
    else if (!strcmp(what, "xlen")) *got = t->xlen;
    else if (!strcmp(what, "ylen")) *got = t->ylen;
    else if (!strcmp(what, "ydiv")) *got = t->ydiv;
    else if (!strcmp(what, "pattern")) *got = ui.p->pattern[ui.track] + 1;
    else if (!strcmp(what, "playpat")) *got = audio_pattern(ui.track) + 1;
    else if (!strcmp(what, "locks")) *got = t->nlocks;
    else if (!strcmp(what, "msg_locks_full")) *got = plat_ms() < ui.locks_until;
    else if (!strcmp(what, "dirty")) *got = ui.dirty;
    else if (!strcmp(what, "msg_stop_to_save")) *got = plat_ms() < ui.msg_until;
    else if (!strcmp(what, "active")) *got = (int)audio_load().active;
    else
        return 0;
    return 1;
}
#endif
