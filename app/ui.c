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
#define PINK RGB(255, 70, 170)       /* notes playing only */

enum { D_EDIT, D_SEQ, D_GLO, ND };
enum { P_ENGINE, P_SOUND, P_STEPS, P_TEMPO, P_CPU };

static const uint8_t DOMAIN_PAGES[ND][2] = {{P_ENGINE, P_SOUND}, {P_STEPS}, {P_TEMPO, P_CPU}};
static const uint8_t DOMAIN_N[ND] = {2, 1, 2};
static const uint8_t DOMAIN_BTN[ND] = {B_EDIT, B_SEQ, B_GLO};
static const char *const DOMAIN_NAME[ND] = {"EDIT", "SEQ", "GLO"};

typedef struct { const char *name, *label[4]; int16_t min[4], max[4]; } q_page;
static const q_page PAGE[] = {
    {"ENGINE", {"HARM", "TIMB", "MORPH", "DECAY"}, {0, 0, 0, 0}, {127, 127, 127, 127}},
    {"SOUND", {"MODEL", "TUNE", "GLIDE", "POLY"}, {0, -24, 0, 1}, {23, 24, 127, 4}},
    {"STEPS", {"LEN", "DIV"}, {1, 0}, {Q_STEPS, DIV_N - 1}},
    {"TEMPO", {"TEMPO"}, {40}, {240}},
    {"CPU", {0}, {0}, {0}},
};

/* Plaits' registration order (pq.h) */
static const char *const MODEL_NAME[24] = {
    "VA VCF", "PHASE DIST", "6-OP A", "6-OP B", "6-OP C", "WAVE TERRAIN", "STRING MACH", "CHIPTUNE",
    "VA", "WAVESHAPE", "2-OP FM", "GRAIN FORM", "ADDITIVE", "WAVETABLE", "CHORDS", "SPEECH",
    "SWARM", "NOISE", "PARTICLE", "STRING", "MODAL", "BASS DRUM", "SNARE", "HI-HAT"};
static const char *const DIV_NAME[DIV_N] = {"1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2", "1/1"};
static const uint8_t WHITE[16] = PLAT_WHITE_KEYS;
static const uint16_t KCOL[4] = {RGB(70, 140, 255), RGB(80, 220, 120), C_WHITE, RGB(255, 150, 40)};

/* what is on the screen, part by part: a part is drawn again when its view changes */
typedef struct {
    struct { int8_t track, domain, page, msg; } head;
    struct { int8_t pid; int16_t v[4]; } foot;
    struct {
        int8_t pid, spage, cursor, pos, len;
        int16_t v[4];
        uint8_t model;
        uint16_t tempo;
        q_step st[16];
        q_load load;
    } scene;
} q_view;

static struct {
    q_project *p;
    int domain, page[ND], track, octave, spage, rec, cursor, dirty;
    uint8_t last_note[Q_TRACKS];               /* a step switched on by hand takes it */
    int8_t key_note[NKEYS], key_track[NKEYS];  /* what each held key sounds, for its note-off; -1 none */
    uint32_t btn, keys, msg_until;
    q_view shown;
    int full;                                  /* the next frame draws everything */
} ui;

static int page_id(void) { return DOMAIN_PAGES[ui.domain][ui.page[ui.domain]]; }

static int knob_get(int pid, int k)
{
    const q_sound *s = &ui.p->sound[ui.track];
    const q_pattern *t = &ui.p->pat[ui.track];
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
    case P_TEMPO * 4: return ui.p->tempo;
    }
    return 0;
}

static void knob_set(int pid, int k, int v)
{
    q_sound *s = &ui.p->sound[ui.track];
    q_pattern *t = &ui.p->pat[ui.track];
    switch (pid * 4 + k) {
    case P_ENGINE * 4: s->harm = (uint8_t)v; break;
    case P_ENGINE * 4 + 1: s->timb = (uint8_t)v; break;
    case P_ENGINE * 4 + 2: s->morph = (uint8_t)v; break;
    case P_ENGINE * 4 + 3: s->decay = (uint8_t)v; break;
    case P_SOUND * 4: s->model = (uint8_t)v; audio_track_model(ui.track); break;
    case P_SOUND * 4 + 1: s->tune = (int8_t)v; break;
    case P_SOUND * 4 + 2: s->glide = (uint8_t)v; break;
    case P_SOUND * 4 + 3: s->poly = (uint8_t)v; break;
    case P_STEPS * 4: t->len = (uint8_t)v; break;
    case P_STEPS * 4 + 1: t->div = (uint8_t)v; break;
    case P_TEMPO * 4: ui.p->tempo = (uint16_t)v; break;
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
        if (b == DOMAIN_BTN[d])
            ui.domain = d;
    switch (b) {
    case B_PLAY:
        audio_play(!audio_playing());   /* audio_play(1) while playing would leave its notes on */
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

static void key_down(int k)
{
    q_pattern *t = &ui.p->pat[ui.track];
    int note = 53 + k + 12 * ui.octave, w;
    if (ui.domain == D_SEQ) {           /* the white keys are the page's 16 steps */
        for (w = 0; w < 16; w++)
            if (WHITE[w] == k) {
                q_step *s = &t->steps[ui.spage * 16 + w];
                if (!s->on) {
                    s->note = ui.last_note[ui.track];
                    s->vel = 100;
                }
                s->on = !s->on;
                ui.dirty = 1;
            }
        return;
    }
    audio_live_note(ui.track, note, 1);
    ui.key_note[k] = (int8_t)note;
    ui.key_track[k] = (int8_t)ui.track;
    ui.last_note[ui.track] = (uint8_t)note;
    if (ui.rec) {                       /* step entry: the note at the cursor, then the next step */
        t->steps[ui.cursor - 1] = (q_step){1, (uint8_t)note, 100, 0};
        ui.cursor = ui.cursor % t->len + 1;
        ui.dirty = 1;
    }
}

static void key_up(int k)
{
    if (ui.key_note[k] < 0)
        return;
    audio_live_note(ui.key_track[k], ui.key_note[k], 0);
    ui.key_note[k] = -1;
}

void ui_input(void)
{
    uint32_t btn = plat_buttons(), keys = plat_keys();
    int i, e, d = ui.domain, n = DOMAIN_N[d];
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
    if ((e = plat_enc(EN_ALGO)) != 0)
        ui.track = clamp(ui.track + e, 0, Q_TRACKS - 1);
    if ((e = plat_enc(EN_SELECT)) != 0)
        ui.page[d] = ((ui.page[d] + e) % n + n) % n;
    if ((e = plat_enc(EN_PRESET)) != 0 && ui.domain == D_EDIT)
        knob_turn(P_SOUND, 0, e);
    for (i = 0; i < 4; i++)
        knob_turn(page_id(), i, plat_enc(EN_K1 + i));
}

static void lights(void)
{
    const q_pattern *t = &ui.p->pat[ui.track];
    uint32_t btn = 1u << DOMAIN_BTN[ui.domain], keys = 0;
    int i, pos = audio_pos(ui.track);
    if (audio_playing())
        btn |= 1u << B_PLAY;
    if (ui.rec)
        btn |= 1u << B_REC;
    if (ui.domain == D_SEQ)
        for (i = 0; i < 16; i++)
            if (t->steps[ui.spage * 16 + i].on || ui.spage * 16 + i == pos)
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
    if (pid == P_STEPS && k == 1) {
        const char *s = DIV_NAME[v];
        while ((*b++ = *s++) != 0) {}
        return;
    }
    if (pid == P_SOUND && k == 1 && v > 0)
        *b++ = '+';
    int_text(v, b);
}

static void view_now(q_view *v)
{
    const q_pattern *t = &ui.p->pat[ui.track];
    int pid = page_id(), k;
    __builtin_memset(v, 0, sizeof *v);  /* the padding too: views are compared whole */
    v->head.track = (int8_t)ui.track;
    v->head.domain = (int8_t)ui.domain;
    v->head.page = (int8_t)ui.page[ui.domain];
    v->head.msg = plat_ms() < ui.msg_until;
    v->foot.pid = v->scene.pid = (int8_t)pid;
    for (k = 0; k < 4; k++)
        if (PAGE[pid].label[k])
            v->foot.v[k] = (int16_t)knob_get(pid, k);
    if (pid == P_ENGINE || pid == P_SOUND) {
        __builtin_memcpy(v->scene.v, v->foot.v, sizeof v->scene.v);
        v->scene.model = ui.p->sound[ui.track].model;
    } else if (pid == P_STEPS) {
        v->scene.spage = (int8_t)ui.spage;
        v->scene.cursor = (int8_t)(ui.rec ? ui.cursor : 0);
        v->scene.pos = (int8_t)audio_pos(ui.track);
        v->scene.len = (int8_t)t->len;
        __builtin_memcpy(v->scene.st, &t->steps[ui.spage * 16], sizeof v->scene.st);
    } else if (pid == P_TEMPO) {
        v->scene.tempo = ui.p->tempo;
    } else {
        v->scene.load = audio_load();
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
        cv_text(x + 10, 7, &FONT_B, "STOP TO SAVE", KCOL[3]);
    else
        cv_text(x + 10, 7, &FONT_B, PAGE[v->foot.pid].name, C_AMB);
    t[0] = (char)('1' + v->head.page);
    t[1] = '/';
    t[2] = (char)('0' + DOMAIN_N[v->head.domain]);
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
        t[0] = 0;
        int_text(v->scene.spage * 16 + 1, t);
        for (i = 0; t[i]; i++) {}
        t[i++] = '-';
        int_text(v->scene.spage * 16 + 16, t + i);
        cv_text(8, oy + 2, &FONT_XS, t, C_GRAY);
        for (i = 0; i < 16; i++) {
            int s = v->scene.spage * 16 + i;
            int32_t x = 5 + 29 * (i % 8), y = oy + (i < 8 ? 22 : 89);
            const q_step *st = &v->scene.st[i];
            uint16_t c = s == v->scene.pos ? PINK : s >= v->scene.len ? C_LINE : st->on ? C_HI : C_DIM;
            if (s == v->scene.cursor - 1) {
                cv_rect(x, y, 26, 54, C_WHITE);
                cv_rect(x + 2, y + 2, 22, 50, c);
            } else {
                cv_rect(x, y, 26, 54, c);
            }
            if (st->on) {
                note_name(st->note, t);
                cv_text(x + (26 - text_w(&FONT_T, t)) / 2, y + 38, &FONT_T, t, C_BLACK);
            }
        }
        break;
    case P_TEMPO:
        int_text(v->scene.tempo, t);
        cv_text((240 - text_w(&FONT_L, t)) / 2, oy + 52, &FONT_L, t, C_WHITE);
        cv_text((240 - text_w(&FONT_XS, "BPM")) / 2, oy + 90, &FONT_XS, "BPM", C_GRAY);
        break;
    case P_CPU: {
        static const char *const ROW[5] = {"CORE 1", "CORE 2", "LOST BLOCKS", "VOICES", "BYTES/VOICE"};
        const q_load *l = &v->scene.load;
        int val[5] = {(int)l->core1_pct, (int)l->core2_pct, (int)l->xruns, (int)l->active, (int)pq_voice_bytes()};
        for (i = 0; i < 5; i++) {
            int32_t y = oy + 14 + 28 * i;
            cv_text(16, y, &FONT_S, ROW[i], C_GRAY);
            if (i == 1 && !l->cpu2) {
                t[0] = 'O', t[1] = 'F', t[2] = 'F', t[3] = 0;
            } else {
                int_text(val[i], t);
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
        if (!pg->label[k])
            continue;
        cv_text(x, 5, &FONT_XS, pg->label[k], KCOL[k]);
        knob_text(v->foot.pid, k, v->foot.v[k], t);
        cv_text(x, 20, &FONT_B, t, KCOL[k]);
        cv_rect(x, 39, 48, 2, C_LINE);
        cv_rect(x, 39, 48 * (v->foot.v[k] - pg->min[k]) / (pg->max[k] - pg->min[k]), 2, KCOL[k]);
    }
    cv_blit(0, FOOT_Y);
}

void ui_init(q_project *p)
{
    __builtin_memset(&ui, 0, sizeof ui);
    __builtin_memset(ui.key_note, -1, sizeof ui.key_note);
    __builtin_memset(ui.last_note, 60, sizeof ui.last_note);
    ui.p = p;
    ui.cursor = 1;
    ui.full = 1;
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
    const q_pattern *t = &ui.p->pat[ui.track];
    if (!strncmp(what, "stepon_", 7) || !strncmp(what, "stepnote_", 9)) {
        int n = atoi(strchr(what, '_') + 1);
        if (n < 1 || n > Q_STEPS)
            return 0;
        *got = what[4] == 'o' ? t->steps[n - 1].on : t->steps[n - 1].note;
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
    else if (!strcmp(what, "dirty")) *got = ui.dirty;
    else if (!strcmp(what, "msg_stop_to_save")) *got = plat_ms() < ui.msg_until;
    else if (!strcmp(what, "active")) *got = (int)audio_load().active;
    else
        return 0;
    return 1;
}
#endif
