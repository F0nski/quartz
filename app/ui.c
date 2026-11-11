/* The step 1 interface: three domains (EDIT, SEQ, GLO) and their pages, the keys, the lights and a sober screen.
 * Part of the unity build (app.c): gfx.c is in scope. The render reads the project each block: byte fields are
 * written as they are, a model change goes through audio_track_model. */
#include "ui.h"
#include "audio.h"
#include "dsp/pq.h"
#include "gen.h"
#include "scene.h"
#include "plateau.h"

#define MSG_MS 1500u
#define HEAD_H 30
#define BAND_H 83                    /* the scene, 240 x 166, in two bands */
#define FOOT_Y 196
#define PINK RGB(255, 47, 142)       /* notes playing only */

enum { D_EDIT, D_SEQ, D_GLO, D_HOME, D_ENV, D_LFO, D_ARP, D_FX, ND };
enum { P_ENGINE, P_SOUND, P_STEPS, P_TEMPO, P_SCALE, P_CPU, P_XY, P_TRACKS, P_SONG, P_GRIDS, P_MAP, P_DENS, P_MARBLES, P_T,
       P_X, P_NOTES, P_PAD, P_KIT, P_ADSR, P_FILTER, P_LFO, P_ARP, P_INSERT, P_MIX, P_GFX };
/* what PRESET sets on a held step; the black keys 0..7 pick RATCHET .. MICRO, 8 is CLEAR */
enum { A_VEL, A_RATCHET, A_PROB, A_COND, A_SLIDE, A_ACCENT, A_NOTE, A_LEN, A_MICRO, A_N };
#define COND_N 40                    /* NONE, FIRST, !FIRST, PRE, !PRE, then A:B for B = 2..8, A = 1..B */

static const uint8_t DOMAIN_PAGES[ND][4] = {{P_ENGINE, P_SOUND}, {P_STEPS, P_XY}, {P_TEMPO, P_SCALE, P_GFX, P_CPU}, {P_TRACKS, P_SONG},
                                                {P_ADSR, P_FILTER}, {P_LFO}, {P_ARP}, {P_INSERT, P_MIX}};
static const uint8_t DOMAIN_N[ND] = {2, 1, 4, 2, 2, 1, 1, 2};      /* SEQ: 2 pages while PATH is XY (pages) */
static const uint8_t GEN_PAGES[2][4] = {{P_GRIDS, P_MAP, P_DENS}, {P_MARBLES, P_T, P_X, P_NOTES}};
static const uint8_t KIT_PAGES[4] = {P_PAD, P_KIT};
static const uint8_t DOMAIN_BTN[ND] = {B_EDIT, B_SEQ, B_GLO, B_HOME, B_ENV, B_LFO, B_ARP, B_FX};
static const char *const DOMAIN_NAME[ND] = {"EDIT", "SEQ", "GLO", "HOME", "ENV", "LFO", "ARP", "FX"};

typedef struct { const char *name, *label[4]; int16_t min[4], max[4]; } q_page;
static const q_page PAGE[] = {
    {"ENGINE", {"HARM", "TIMB", "MORPH", "DECAY"}, {0, 0, 0, 0}, {127, 127, 127, 127}},
    {"SOUND", {"MODEL", "TUNE", "GLIDE", "POLY"}, {0, -24, 0, 1}, {M_KIT, 24, 127, 4}},
    {"STEPS", {"LEN", "DIV", "PATH", "SOURCE"}, {1, 0, 0, 0}, {Q_STEPS, DIV_N - 1, PATH_N - 1, SRC_N - 1}},
    {"TEMPO", {"TEMPO", "SWING", "COMP", "DUCK"}, {40, 50, 0, 0}, {240, 75, 127, 127}},
    {"SCALE", {"TONIC", "MODE", "QUANT", "CLICK"}, {0, 0, 0, 0}, {11, 10, 2, 3}},
    {"CPU", {0}, {0}, {0}},
    {"XY", {"X LEN", "X DIV", "Y LEN", "Y DIV"}, {1, 0, 1, 0}, {4, DIV_N - 1, 4, DIV_N - 1}},
    {"TRACKS", {0}, {0}, {0}},
    {"SONG", {"ENTRY", "SCENE", "REPEAT", "MODE"}, {1, 1, 1, 0}, {Q_SONG, Q_SCENES, 16, 1}},
    {"GRIDS", {"DIV", "VOICE", "NOTE", "SOURCE"}, {0, 1, 1, 0}, {DIV_N - 1, 3, 127, SRC_N - 1}},
    {"MAP", {"X", "Y", "CHAOS"}, {0, 0, 0}, {127, 127, 127}},
    {"DENS", {"DENS 1", "DENS 2", "DENS 3"}, {0, 0, 0}, {127, 127, 127}},
    {"MARBLES", {"DIV", "T OUT", "LEN", "SOURCE"}, {0, 1, 1, 0}, {DIV_N - 1, 3, 16, SRC_N - 1}},
    {"T", {"MODE", "BIAS", "JITTER", "DEJA VU"}, {0, 0, 0, 0}, {6, 127, 127, 127}},
    {"X", {"SPREAD", "X BIAS", "STEPS"}, {0, 0, 0}, {127, 127, 127}},
    {"NOTES", {"NOTE", "RANGE"}, {1, 1}, {127, 4}},
    {"PAD", {"TUNE", "HARM", "TIMB", "DECAY"}, {-24, 0, 0, 0}, {24, 127, 127, 127}},
    {"KIT", {"MODEL", "ENGINE", "LEVEL"}, {0, 0, 0}, {M_KIT, 2, 127}},
    {"ADSR", {"ATTACK", "DECAY", "SUSTAIN", "RELEASE"}, {0, 0, 0, 0}, {127, 127, 127, 127}},
    {"FILTER", {"TYPE", "CUTOFF", "RES", "ENV AMT"}, {0, 0, 0, -64}, {2, 127, 127, 63}},
    {"LFO", {"SHAPE", "RATE", "DEPTH", "DEST"}, {0, 0, -64, 0}, {4, LRATE_SYNC + 63, 63, 6}},
    {"ARP", {"MODE", "OCT", "DIV", "GATE"}, {0, 1, 0, 5}, {AM_ORDER, 4, DIV_N - 1, 100}},
    {"INSERT", {"", "", "", "MIX"}, {0, 0, 0, 0}, {127, 127, 127, 127}},      /* its name and knobs: INS_NAME, INS_LABEL */
    {"MIX", {"DELAY", "REVERB", "PAN", "LEVEL"}, {0, 0, -64, 0}, {127, 127, 63, 127}},
    {"FX", {"DLY TIME", "DLY FB", "REV SIZE", "REV TONE"}, {0, 0, 0, 0}, {DIV_N - 1, 127, 127, 127}},
};

/* Plaits' registration order (pq.h) */
static const char *const MODEL_NAME[M_KIT + 1] = {
    "VA VCF", "PHASE DIST", "6-OP A", "6-OP B", "6-OP C", "WAVE TERRAIN", "STRING MACH", "CHIPTUNE",
    "VA", "WAVESHAPE", "2-OP FM", "GRAIN FORM", "ADDITIVE", "WAVETABLE", "CHORDS", "SPEECH",
    "SWARM", "NOISE", "PARTICLE", "STRING", "MODAL", "BASS DRUM", "SNARE", "HI-HAT", "KIT"};
static const char *const ENGINE_NAME[3] = {"BD", "SN", "HH"};
static const char *const DIV_NAME[DIV_N] = {"1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2", "1/1"};
static const char *const FTYPE_NAME[3] = {"LP", "HP", "BP"};
static const char *const LSHAPE_NAME[5] = {"SIN", "TRI", "SAW", "SQR", "S&H"};
static const char *const LRATE_NAME[LRATE_SYNC] = {"8 BAR", "4 BAR", "2 BAR", "1 BAR", "1/2", "1/4", "1/4T", "1/8", "1/8T",
                                                   "1/16", "1/16T", "1/32"};
static const char *const LDEST_NAME[7] = {"HARM", "TIMB", "MORPH", "PITCH", "CUTOFF", "RES", "LEVEL"};
/* the insert's page name and its 3 knobs by type: "" a knob with no role (K4 is always MIX) */
static const char *const INS_NAME[IT_N] = {"INSERT OFF", "OVERDRIVE", "WAVEFOLD", "BITCRUSH", "CHORUS", "PHASER", "RING MOD", "COMB"};
static const char *const INS_LABEL[IT_N][3] = {{"", "", ""}, {"DRIVE", "TONE", ""}, {"FOLD", "BIAS", "TONE"}, {"BITS", "RATE", ""},
                                                {"RATE", "DEPTH", ""}, {"RATE", "DEPTH", "FEEDBK"}, {"FREQ", "", ""}, {"NOTE", "FEEDBK", "DAMP"}};
static const char *const AMODE_NAME[6] = {"OFF", "UP", "DOWN", "UP-DN", "RAND", "ORDER"};
static const char *const SRC_NAME[SRC_N] = {"STEP", "GRIDS", "MARBLES"};
/* Marbles' T models, its TGeneratorModel order (gen.c) */
static const char *const T_MODE_NAME[7] = {"C.BERN", "CLUSTERS", "DRUMS", "I.BERN", "DIVIDER", "3 STATES", "MARKOV"};
static const char *const PATH_NAME[PATH_N] = {"FWD", "REV", "PING-PONG", "SNAKE", "COLUMNS", "SPIRAL", "RANDOM", "XY"};
static const char *const TONIC_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const MODE_NAME[11] = {"CHROM", "MAJOR", "MINOR", "DORIAN", "PHRYG", "LYDIAN", "MIXO", "LOCRIAN",
                                          "HARM MIN", "PENTA", "MIN PENTA"};
static const char *const QUANT_NAME[3] = {"OFF", "50 %", "100 %"};
static const char *const CLICK_NAME[4] = {"OFF", "METRO", "COUNT", "BOTH"};
static const char *const ATTR_NAME[A_N] = {"VEL", "RATCHET", "PROB", "COND", "SLIDE", "ACCENT", "NOTE", "LEN", "MICRO"};
static const uint8_t ATTR_ROW[A_N] = {A_NOTE, A_VEL, A_LEN, A_MICRO, A_PROB, A_COND, A_RATCHET, A_SLIDE, A_ACCENT};
static const char *const COND_NAME[5] = {"NONE", "FIRST", "!FIRST", "PRE", "!PRE"};
static const uint8_t WHITE[16] = PLAT_WHITE_KEYS;
static const uint8_t BLACK[11] = PLAT_BLACK_KEYS;
static const uint16_t KCOL[4] = {RGB(58, 109, 255), RGB(34, 213, 142), RGB(240, 240, 250), RGB(255, 122, 46)};

typedef struct { uint8_t model, pat, playing, mute; int8_t pos, len; } q_trow;   /* pat, playing as in head */

/* what is on the screen, part by part: a part is drawn again when its view changes */
typedef struct {
    struct { int8_t track, domain, page, n, msg, pat, playing, scene, pad, ltrig, itype, punch; } head;   /* pat: P1..P8 asked; playing: the one playing when it differs, else 0;
                                                                                   pad: a KIT's in EDIT 1..16, else 0;
                                                                                   ltrig: LFO's FREE 1 / TRIG 2, else 0; itype: the insert's, in the page name */
    struct { int8_t pid; uint8_t col, hide, itype; int16_t v[4]; } foot;           /* col: the knobs shown in colour, the rest in grey (a lock page);
                                                       itype: the INSERT page's insert, else 0 */
    struct {
        int8_t pid, spage, cursor, pos, len, path, held, attr, showpath, xlen, ylen, src;   /* a generator: pos its rank, len its loop */
        uint16_t locks;                         /* the page's steps that have p-locks */
        uint16_t notes;                         /* NOTES: the allowed pitch classes */
        q_step hst;                             /* the held step */
        int16_t v[4];
        uint8_t model, pad;                     /* pad: KIT, the pad its knobs set */
        uint8_t eng[Q_PADS];                    /* KIT: the pads' engines */
        uint16_t tempo;
        q_step st[16];
        int32_t cpu[8];                         /* the CPU page's numbers as shown; core 2 -1: OFF */
        uint8_t anim;                           /* an animated scene, redrawn as it moves: 0 none, 1 the planet (v: its ENGINE
                                                   knobs), 2 the SEQ plateau */
        q_trow tr[Q_TRACKS];                    /* TRACKS */
        struct { int8_t n, cursor, first, playing; q_entry e[6]; } song;               /* SONG: the 6 entries shown from first */
    } scene;
} q_view;

static struct {
    q_project *p;
    int domain, page[ND], track, octave, spage, rec, cursor, dirty, entry;   /* entry: the song cursor 1..nsong, 0 none */
    int held, held_key, edited, attr, lpage, nwrote, seqdown, arpdown;   /* held: the step held 1..64, 0 none; lpage: the held step's p-lock page (LPAGE);
                                                                   nwrote: notes of the chord written since the hold began */
    int pad, padheld;                          /* a KIT: its last pad played 0..15; the held pad's key + 1, 0 none */
    int homedown, savedown, seldown, saveused, msg_scene;      /* saveused: a white key came while SAVE was held; msg_scene: 1..8 asked */
    int fxdown, msg_punch;                     /* msg_punch: the punch-in last pressed 1..16, shown until punch_until */
    uint16_t punch;                            /* the punch-ins held under FX, bit k = white key k + 1 */
    uint8_t last_note[Q_TRACKS];               /* a step switched on by hand takes it */
    int8_t key_note[NKEYS], key_track[NKEYS];  /* what each held key sounds, for its note-off; -1 none */
    uint32_t btn, keys, msg_until, locks_until, path_until, punch_until;
    q_view shown;
    int full;                                  /* the next frame draws everything */
    uint32_t scene_ms, fps_ms;                 /* the last scene drawn (every other 16-ms frame: 30 a second); the second being counted */
    int frames, fps, fps_mixed;                /* scene frames this second; the last whole second's; a frame without the scene */
} ui;

static q_pattern *pat(void) { return &ui.p->pat[ui.track][ui.p->pattern[ui.track]]; }
static int kit(void) { return ui.p->sound[ui.track].model == M_KIT; }

/* the domain's pages: their number, their ids in out (4 always written: a page index not yet brought back in range
   still names a page) */
static int pages(int d, uint8_t *out)
{
    const q_pattern *t = pat();
    if (d == D_SEQ && t->src != SRC_STEP) {
        __builtin_memcpy(out, GEN_PAGES[t->src - 1], 4);
        return t->src == SRC_GRIDS ? 3 : kit() ? 2 : 4;     /* a KIT has no X section */
    }
    if (d == D_EDIT && kit()) {
        __builtin_memcpy(out, KIT_PAGES, 4);
        return 2;
    }
    __builtin_memcpy(out, DOMAIN_PAGES[d], 4);
    return d == D_SEQ && t->path == PATH_XY ? 2 : DOMAIN_N[d];
}

static int domain_n(int d)
{
    uint8_t id[4];
    return pages(d, id);
}

static int page_id(void)
{
    uint8_t id[4];
    pages(ui.domain, id);
    return id[ui.page[ui.domain]];
}

/* the page K1-K4 set, a step held aside: a KIT's pad held in EDIT gives its PAD page */
static int knob_page(void) { return ui.domain == D_EDIT && ui.padheld && kit() ? P_PAD : page_id(); }

/* a KIT plays the generators' 3 voices on its pads: no voice, note nor T output to pick; no arp either */
static int knob_hidden(int pid, int k)
{
    return (kit() && ((pid == P_GRIDS && (k == 1 || k == 2)) || (pid == P_MARBLES && k == 1) || pid == P_ARP))   /* a KIT has no arp */
           || (pid == P_INSERT && k < 3 && !*INS_LABEL[ui.p->sound[ui.track].itype][k]);   /* no role: turns ignored */
}

static int knob_get(int pid, int k)
{
    const q_sound *s = &ui.p->sound[ui.track];
    const q_pattern *t = &ui.p->pat[ui.track][ui.p->pattern[ui.track]];
    const q_pad *pd = &ui.p->kit[ui.track][ui.pad];
    switch (pid * 4 + k) {
    case P_ENGINE * 4: return s->harm;
    case P_ENGINE * 4 + 1: return s->timb;
    case P_ENGINE * 4 + 2: return s->morph;
    case P_ENGINE * 4 + 3: return s->decay;
    case P_SOUND * 4:
    case P_KIT * 4: return s->model;
    case P_SOUND * 4 + 1: return s->tune;
    case P_SOUND * 4 + 2: return s->glide;
    case P_SOUND * 4 + 3: return s->poly;
    case P_STEPS * 4: return t->len;
    case P_STEPS * 4 + 1: return t->div;
    case P_STEPS * 4 + 2: return t->path;
    case P_STEPS * 4 + 3:
    case P_GRIDS * 4 + 3:
    case P_MARBLES * 4 + 3: return t->src;
    case P_GRIDS * 4:
    case P_MARBLES * 4: return t->div;
    case P_GRIDS * 4 + 1: return t->grids.voice;
    case P_GRIDS * 4 + 2: return t->grids.note;
    case P_MAP * 4: return t->grids.x;
    case P_MAP * 4 + 1: return t->grids.y;
    case P_MAP * 4 + 2: return t->grids.chaos;
    case P_DENS * 4:
    case P_DENS * 4 + 1:
    case P_DENS * 4 + 2: return t->grids.dens[k];
    case P_MARBLES * 4 + 1: return t->marbles.tout;
    case P_MARBLES * 4 + 2: return t->marbles.len;
    case P_T * 4: return t->marbles.mode;
    case P_T * 4 + 1: return t->marbles.bias;
    case P_T * 4 + 2: return t->marbles.jitter;
    case P_T * 4 + 3: return t->marbles.dejavu;
    case P_X * 4: return t->marbles.spread;
    case P_X * 4 + 1: return t->marbles.xbias;
    case P_X * 4 + 2: return t->marbles.steps;
    case P_NOTES * 4: return t->marbles.note;
    case P_NOTES * 4 + 1: return t->marbles.range;
    case P_PAD * 4: return pd->tune;
    case P_PAD * 4 + 1: return pd->harm;
    case P_PAD * 4 + 2: return pd->timb;
    case P_PAD * 4 + 3: return pd->decay;
    case P_KIT * 4 + 1: return pd->engine;
    case P_KIT * 4 + 2: return pd->level;
    case P_ADSR * 4: return s->atk;
    case P_ADSR * 4 + 1: return s->dec;
    case P_ADSR * 4 + 2: return s->sus;
    case P_ADSR * 4 + 3: return s->rel;
    case P_FILTER * 4: return s->ftype;
    case P_FILTER * 4 + 1: return s->cutoff;
    case P_FILTER * 4 + 2: return s->res;
    case P_FILTER * 4 + 3: return s->envamt;
    case P_LFO * 4: return s->lshape;
    case P_LFO * 4 + 1: return s->lrate;
    case P_LFO * 4 + 2: return s->ldepth;
    case P_LFO * 4 + 3: return s->ldest;
    case P_ARP * 4: return s->amode;
    case P_ARP * 4 + 1: return s->aoct;
    case P_ARP * 4 + 2: return s->adiv;
    case P_ARP * 4 + 3: return s->agate;
    case P_INSERT * 4: return s->i1;
    case P_INSERT * 4 + 1: return s->i2;
    case P_INSERT * 4 + 2: return s->i3;
    case P_INSERT * 4 + 3: return s->imix;
    case P_MIX * 4: return s->dsend;
    case P_MIX * 4 + 1: return s->rsend;
    case P_MIX * 4 + 2: return s->pan;
    case P_MIX * 4 + 3: return s->level;
    case P_GFX * 4: return ui.p->dtime;
    case P_GFX * 4 + 1: return ui.p->dfb;
    case P_GFX * 4 + 2: return ui.p->rsize;
    case P_GFX * 4 + 3: return ui.p->rtone;
    case P_TEMPO * 4: return ui.p->tempo;
    case P_TEMPO * 4 + 1: return ui.p->swing;
    case P_TEMPO * 4 + 2: return ui.p->comp;
    case P_TEMPO * 4 + 3: return ui.p->duck;
    case P_SCALE * 4: return ui.p->tonic;
    case P_SCALE * 4 + 1: return ui.p->mode;
    case P_SCALE * 4 + 2: return ui.p->quant;
    case P_SCALE * 4 + 3: return ui.p->click;
    case P_XY * 4: return t->xlen;
    case P_XY * 4 + 1: return t->div;
    case P_XY * 4 + 2: return t->ylen;
    case P_XY * 4 + 3: return t->ydiv;
    case P_SONG * 4: return ui.entry;
    case P_SONG * 4 + 1: return ui.entry ? ui.p->song[ui.entry - 1].scene + 1 : 1;
    case P_SONG * 4 + 2: return ui.entry ? ui.p->song[ui.entry - 1].reps : 1;
    case P_SONG * 4 + 3: return ui.p->song_on;
    }
    return 0;
}

static void knob_set(int pid, int k, int v)
{
    q_sound *s = &ui.p->sound[ui.track];
    q_pattern *t = &ui.p->pat[ui.track][ui.p->pattern[ui.track]];
    q_pad *pd = &ui.p->kit[ui.track][ui.pad];
    switch (pid * 4 + k) {
    case P_ENGINE * 4: s->harm = (uint8_t)v; break;
    case P_ENGINE * 4 + 1: s->timb = (uint8_t)v; break;
    case P_ENGINE * 4 + 2: s->morph = (uint8_t)v; break;
    case P_ENGINE * 4 + 3: s->decay = (uint8_t)v; break;
    case P_SOUND * 4:
    case P_KIT * 4: s->model = (uint8_t)v; audio_track_model(ui.track); break;
    case P_SOUND * 4 + 1: s->tune = (int8_t)v; break;
    case P_SOUND * 4 + 2: s->glide = (uint8_t)v; break;
    case P_SOUND * 4 + 3: s->poly = (uint8_t)v; break;
    case P_STEPS * 4:
        t->len = (uint8_t)v;
        if (ui.cursor > v)              /* step entry past the end: back to step 1 */
            ui.cursor = 1;
        break;
    case P_STEPS * 4 + 1:
    case P_GRIDS * 4:
    case P_MARBLES * 4: t->div = (uint8_t)v; break;
    case P_STEPS * 4 + 2:
        t->path = (uint8_t)v;
        ui.path_until = plat_ms() + MSG_MS;     /* the path is drawn while it is being chosen */
        break;
    case P_STEPS * 4 + 3:               /* the steps and the generators' settings stay */
    case P_GRIDS * 4 + 3:
    case P_MARBLES * 4 + 3: t->src = (uint8_t)v; break;
    case P_GRIDS * 4 + 1: t->grids.voice = (uint8_t)v; break;
    case P_GRIDS * 4 + 2: t->grids.note = (uint8_t)v; break;
    case P_MAP * 4: t->grids.x = (uint8_t)v; break;
    case P_MAP * 4 + 1: t->grids.y = (uint8_t)v; break;
    case P_MAP * 4 + 2: t->grids.chaos = (uint8_t)v; break;
    case P_DENS * 4:
    case P_DENS * 4 + 1:
    case P_DENS * 4 + 2: t->grids.dens[k] = (uint8_t)v; break;
    case P_MARBLES * 4 + 1: t->marbles.tout = (uint8_t)v; break;
    case P_MARBLES * 4 + 2: t->marbles.len = (uint8_t)v; break;
    case P_T * 4: t->marbles.mode = (uint8_t)v; break;
    case P_T * 4 + 1: t->marbles.bias = (uint8_t)v; break;
    case P_T * 4 + 2: t->marbles.jitter = (uint8_t)v; break;
    case P_T * 4 + 3: t->marbles.dejavu = (uint8_t)v; break;
    case P_X * 4: t->marbles.spread = (uint8_t)v; break;
    case P_X * 4 + 1: t->marbles.xbias = (uint8_t)v; break;
    case P_X * 4 + 2: t->marbles.steps = (uint8_t)v; break;
    case P_NOTES * 4: t->marbles.note = (uint8_t)v; break;
    case P_NOTES * 4 + 1: t->marbles.range = (uint8_t)v; break;
    case P_PAD * 4: pd->tune = (int8_t)v; break;
    case P_PAD * 4 + 1: pd->harm = (uint8_t)v; break;
    case P_PAD * 4 + 2: pd->timb = (uint8_t)v; break;
    case P_PAD * 4 + 3: pd->decay = (uint8_t)v; break;
    case P_KIT * 4 + 1: pd->engine = (uint8_t)v; break;     /* the render takes it at the pad's next hit */
    case P_KIT * 4 + 2: pd->level = (uint8_t)v; break;
    case P_ADSR * 4: s->atk = (uint8_t)v; break;
    case P_ADSR * 4 + 1: s->dec = (uint8_t)v; break;
    case P_ADSR * 4 + 2: s->sus = (uint8_t)v; break;
    case P_ADSR * 4 + 3: s->rel = (uint8_t)v; break;
    case P_FILTER * 4: s->ftype = (uint8_t)v; break;
    case P_FILTER * 4 + 1: s->cutoff = (uint8_t)v; break;
    case P_FILTER * 4 + 2: s->res = (uint8_t)v; break;
    case P_FILTER * 4 + 3: s->envamt = (int8_t)v; break;
    case P_LFO * 4: s->lshape = (uint8_t)v; break;
    case P_LFO * 4 + 1: s->lrate = (uint8_t)v; break;
    case P_LFO * 4 + 2: s->ldepth = (int8_t)v; break;
    case P_LFO * 4 + 3: s->ldest = (uint8_t)v; break;
    case P_ARP * 4: s->amode = (uint8_t)v; break;
    case P_ARP * 4 + 1: s->aoct = (uint8_t)v; break;
    case P_ARP * 4 + 2: s->adiv = (uint8_t)v; break;
    case P_ARP * 4 + 3: s->agate = (uint8_t)v; break;
    case P_INSERT * 4: s->i1 = (uint8_t)v; break;
    case P_INSERT * 4 + 1: s->i2 = (uint8_t)v; break;
    case P_INSERT * 4 + 2: s->i3 = (uint8_t)v; break;
    case P_INSERT * 4 + 3: s->imix = (uint8_t)v; break;
    case P_MIX * 4: s->dsend = (uint8_t)v; break;
    case P_MIX * 4 + 1: s->rsend = (uint8_t)v; break;
    case P_MIX * 4 + 2: s->pan = (int8_t)v; break;
    case P_MIX * 4 + 3: s->level = (uint8_t)v; break;
    case P_GFX * 4: ui.p->dtime = (uint8_t)v; break;
    case P_GFX * 4 + 1: ui.p->dfb = (uint8_t)v; break;
    case P_GFX * 4 + 2: ui.p->rsize = (uint8_t)v; break;
    case P_GFX * 4 + 3: ui.p->rtone = (uint8_t)v; break;
    case P_TEMPO * 4: ui.p->tempo = (uint16_t)v; break;
    case P_TEMPO * 4 + 1: ui.p->swing = (uint8_t)v; break;
    case P_TEMPO * 4 + 2: ui.p->comp = (uint8_t)v; break;
    case P_TEMPO * 4 + 3: ui.p->duck = (uint8_t)v; break;
    case P_SCALE * 4: ui.p->tonic = (uint8_t)v; break;
    case P_SCALE * 4 + 1: ui.p->mode = (uint8_t)v; break;
    case P_SCALE * 4 + 2: ui.p->quant = (uint8_t)v; break;
    case P_SCALE * 4 + 3: ui.p->click = (uint8_t)v; break;
    case P_XY * 4: t->xlen = (uint8_t)v; break;
    case P_XY * 4 + 1: t->div = (uint8_t)v; break;
    case P_XY * 4 + 2: t->ylen = (uint8_t)v; break;
    case P_XY * 4 + 3: t->ydiv = (uint8_t)v; break;
    case P_SONG * 4: ui.entry = v; return;
    case P_SONG * 4 + 1: ui.p->song[ui.entry - 1].scene = (uint8_t)(v - 1); break;
    case P_SONG * 4 + 2: ui.p->song[ui.entry - 1].reps = (uint8_t)v; break;
    case P_SONG * 4 + 3: ui.p->song_on = (uint8_t)v; break;
    }
    ui.dirty = 1;
}

static void knob_turn(int pid, int k, int d)
{
    const q_page *pg = &PAGE[pid];
    int v;
    if (!d || !pg->label[k] || knob_hidden(pid, k))
        return;
    if (pid == P_SONG && k < 3 && !ui.entry)    /* no entries: nothing to edit */
        return;
    v = clamp(knob_get(pid, k) + d, pg->min[k], pid == P_SONG && !k ? ui.p->nsong : pg->max[k]);
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
        d = !audio_playing();
        if (!d)                         /* stopping disarms REC */
            ui.rec = 0;
        audio_rec(ui.rec);              /* armed before the start: the count-in */
        audio_play(d);
        ui.msg_scene = 0;
        break;
    case B_REC:
        ui.rec = !ui.rec;
        audio_rec(ui.rec);
        ui.cursor = 1;
        break;
    case B_SAVE:                        /* saves at the release, unless a white key came meanwhile */
        ui.saveused = 0;
        break;
    case B_SEL:                         /* the SONG page: clears the entry (SEL with HOME held is for the scenes) */
        if (ui.domain == D_HOME && page_id() == P_SONG && !ui.homedown && ui.entry) {
            q_project *p = ui.p;
            int i;
            for (i = ui.entry; i < p->nsong; i++)
                p->song[i - 1] = p->song[i];
            p->song[--p->nsong] = (q_entry){0, 1};
            ui.entry = ui.entry > p->nsong ? p->nsong : ui.entry;
            ui.dirty = 1;
        }
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
    case A_NOTE:                        /* the whole chord, kept in 1..127; a KIT's pads stay */
        if (kit())
            break;
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

/* the held step's p-lock pages: a synth track's 8, a KIT's ADSR, FILTER, LFO, INSERT and MIX (ui.lpage counts in the track's own
   list); LOCK_OF: the LK_* each knob locks, -1 for the ones that do not (MODEL, POLY) */
static const uint8_t LPAGE[8] = {P_ENGINE, P_SOUND, P_ADSR, P_FILTER, P_LFO, P_INSERT, P_MIX, P_ARP};
static const int8_t LOCK_OF[8][4] = {
    {LK_HARM, LK_TIMB, LK_MORPH, LK_DECAY}, {-1, LK_TUNE, LK_GLIDE, -1}, {LK_ATK, LK_DEC, LK_SUS, LK_REL},
    {LK_FTYPE, LK_CUT, LK_RES, LK_ENVAMT}, {LK_SHAPE, LK_RATE, LK_DEPTH, LK_DEST}, {LK_I1, LK_I2, LK_I3, LK_IMIX},
    {LK_DSEND, LK_RSEND, LK_PAN, LK_LEVEL}, {LK_AMODE, LK_AOCT, LK_ADIV, LK_AGATE}};
static int lpage_n(void) { return kit() ? 5 : 8; }
static int lpage_row(void) { return ui.lpage + (kit() ? 2 : 0); }
static int lock_pid(void) { return LPAGE[lpage_row()]; }
static int lock_param(int k) { return LOCK_OF[lpage_row()][k]; }

static int lock_value(const q_pattern *t, int k, int *locked)
{
    int param = lock_param(k), i = lock_find(t, ui.held - 1, param);
    *locked = i >= 0;
    if (i < 0)
        return knob_get(lock_pid(), k);
    return param == LK_TUNE || param == LK_ENVAMT || param == LK_DEPTH || param == LK_PAN ? (int8_t)t->locks[i].value : t->locks[i].value;
}

static void lock_turn(int k, int d)
{
    q_pattern *t = pat();
    int pid = lock_pid(), param = lock_param(k), locked, v;
    if (!d || param < 0 || knob_hidden(pid, k))
        return;
    v = clamp(lock_value(t, k, &locked) + d, PAGE[pid].min[k], PAGE[pid].max[k]);
    ui.edited = 1;                      /* a knob turned on a held step is an edit, the lock fitting or not */
    if (lock_set(t, ui.held - 1, param, v)) {
        ui.locks_until = plat_ms() + MSG_MS;
        return;
    }
    step_on(&t->steps[ui.held - 1]);
    ui.dirty = 1;
}

/* a note the render recorded: it lands on its step, whatever pattern is shown now */
static void rec_write(const q_rec *r)
{
    q_step *s = &ui.p->pat[r->track][r->pat].steps[r->step];
    int i, poly = ui.p->sound[r->track].model == M_KIT ? 4 : ui.p->sound[r->track].poly;   /* a KIT: pads added */
    if (ui.p->pat[r->track][r->pat].src != SRC_STEP)    /* a generator's: played, not written */
        return;
    if (!(s->flags & SF_ON)) {
        __builtin_memset(s, 0, sizeof *s);
        s->note[0] = r->note;
        s->vel = r->vel;
        s->len = r->len;
        s->micro = r->micro;
        s->prob = 20;
        step_on(s);
    } else if (poly == 1) {
        s->note[0] = r->note;
        s->vel = r->vel;
        s->len = r->len;
        s->micro = r->micro;
    } else {
        for (i = 0; i < poly && s->note[i] != r->note; i++) {}
        if (i < poly)
            return;
        for (i = 1; i < poly && s->note[i]; i++) {}
        if (i < poly)
            s->note[i] = r->note;
    }
    ui.dirty = 1;
}

/* overdub: a lockable knob turned also locks the step playing, when it is on and of the pattern shown */
static void rec_lock(int pid, int k)
{
    q_pattern *t = pat();
    int pos = audio_pos(ui.track), param = -1, r;
    for (r = 0; r < 8; r++)
        if (LPAGE[r] == pid)
            param = LOCK_OF[r][k];
    if (param < 0 || knob_hidden(pid, k) || pos < 0 || t->src != SRC_STEP || audio_pattern(ui.track) != ui.p->pattern[ui.track] || !(t->steps[pos].flags & SF_ON))
        return;
    if (lock_set(t, pos, param, (uint8_t)knob_get(pid, k)))
        ui.locks_until = plat_ms() + MSG_MS;
}

static void pattern_set(int n)
{
    ui.p->pattern[ui.track] = (uint8_t)n;
    ui.held = 0;                        /* the hold was another pattern's step: its release toggles nothing */
    if (ui.cursor > pat()->len)
        ui.cursor = 1;
    ui.dirty = 1;
}

/* a KIT's pad in or out of the held step's notes, 4 at most: the last one out switches the step off */
static void pad_toggle(q_step *s, int note)
{
    int i, n = 0;
    if (!(s->flags & SF_ON))
        __builtin_memset(s->note, 0, sizeof s->note);
    else
        for (n = 1; n < 4 && s->note[n]; n++) {}
    for (i = 0; i < n && s->note[i] != note; i++) {}
    if (i == n) {
        if (n < 4)
            s->note[n] = (uint8_t)note;
        step_on(s);
    } else if (n == 1) {
        s->flags &= (uint8_t)~SF_ON;
    } else {
        for (; i < 3; i++)
            s->note[i] = s->note[i + 1];
        s->note[3] = 0;
    }
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

static void save_release(void)
{
    if (ui.saveused)
        return;
    if (audio_playing())                /* a flash write stalls the render */
        ui.msg_until = plat_ms() + MSG_MS;
    else if (store_save(ui.p) == 0)
        ui.dirty = 0;
}

/* a new pattern or mask for the project: the cursor stays inside the pattern */
static void scene_apply(const q_scene *sc)
{
    int t;
    for (t = 0; t < Q_TRACKS; t++)
        ui.p->pattern[t] = sc->pat[t];
    ui.p->mute = sc->mute;
    if (ui.cursor > pat()->len)
        ui.cursor = 1;
}

/* HOME held and a white key: 0..3 mute the track, 8..15 the scene 1..8 */
static void home_key(int w)
{
    q_scene *sc = &ui.p->scene[w < 8 ? 0 : w - 8];
    int t;
    if (w < Q_TRACKS) {
        ui.p->mute ^= (uint8_t)(1u << w);
        ui.dirty = 1;
    } else if (w >= 8 && (ui.seldown || !sc->used)) {
        for (t = 0; t < Q_TRACKS; t++)
            sc->pat[t] = ui.p->pattern[t];
        sc->mute = ui.p->mute;
        sc->used = 1;
        ui.dirty = 1;
    } else if (w >= 8 && !audio_playing()) {
        scene_apply(sc);
        ui.dirty = 1;
    } else if (w >= 8) {                /* the mutes and patterns follow when the scene has started */
        audio_scene(w - 8);
        ui.msg_scene = w - 7;
    }
}

/* NOTES: a pitch class in or out of the allowed notes, the last one kept. Both bytes come from the new mask: between
   the two writes the render may see a mix for a block, never a 0 it was not meant to (0 is the scale) */
static void notes_toggle(int c)
{
    q_pattern *t = pat();
    int m = gen_allowed(ui.p, t) ^ 1 << c;
    if (!m)
        return;
    t->marbles.notes[1] = (uint8_t)(m >> 8);
    t->marbles.notes[0] = (uint8_t)m;
    ui.dirty = 1;
}

static const char *const PUNCH_NAME[16] = {"LOOP 1/8", "LOOP 1/12", "LOOP 1/16", "LOOP 1/32", "TAPE STOP", "LOW-PASS",
    "HIGH-PASS", "SWEEP", "BITCRUSH", "DOWNSAMPLE", "GATE 1/16", "AUTO-PAN", "DELAY THROW", "FREEZE", "STUTTER",
    "STUT GATE"};

/* overdub: the punch-ins held join the step playing's, empty steps too (as rec_lock otherwise) */
static void punch_rec(void)
{
    q_pattern *t = pat();
    int pos = audio_pos(ui.track), h, i, v;
    if (!ui.punch || !ui.rec || !audio_playing() || pos < 0 || t->src != SRC_STEP ||
        audio_pattern(ui.track) != ui.p->pattern[ui.track])
        return;
    for (h = 0; h < 2; h++) {
        int bits = ui.punch >> 8 * h & 0xFF;
        i = lock_find(t, pos, LK_PUNCH_LO + h);
        v = (i >= 0 ? t->locks[i].value : 0) | bits;
        if (!bits || (i >= 0 && v == t->locks[i].value))
            continue;
        if (lock_set(t, pos, LK_PUNCH_LO + h, v))
            ui.locks_until = plat_ms() + MSG_MS;
        else
            ui.dirty = 1;
    }
}

static void key_down(int k)
{
    q_pattern *t = pat();
    q_step *s = 0;
    int note = 53 + k + 12 * ui.octave, w, b;
    for (w = 0; w < 16 && WHITE[w] != k; w++) {}
    if (w < 16 && ui.homedown) {
        home_key(w);
        return;
    }
    if (ui.fxdown) {                    /* FX held: the white keys are the punch-ins, the black ones nothing */
        if (w < 16) {
            ui.punch |= (uint16_t)(1u << w);
            audio_punch(ui.punch);
            ui.msg_punch = w + 1;
            ui.punch_until = plat_ms() + MSG_MS;
        }
        return;
    }
    if (w < 16 && ui.savedown) {        /* SAVE held: the white keys 1..8 add the scene to the song */
        ui.saveused = 1;
        if (w < Q_SCENES && ui.p->nsong < Q_SONG) {
            ui.p->song[ui.p->nsong++] = (q_entry){(uint8_t)w, 1};
            ui.entry = ui.p->nsong;
            ui.dirty = 1;
        }
        return;
    }
    if (ui.domain == D_SEQ && t->src != SRC_STEP && !ui.seqdown) {     /* a generator: the keys play the track */
        if (page_id() == P_NOTES) {
            notes_toggle(note % 12);
            return;
        }
    } else if (ui.domain == D_SEQ) {    /* the white keys are the page's 16 steps */
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
    if (kit()) {                        /* the white keys are the pads */
        if (w == 16)
            return;
        note = KIT_NOTE + w;
        ui.pad = w;
        if (!s)
            ui.padheld = k + 1;
    }
    /* written to the held step: not overdubbed too; ARP held: the arp keeps the note after its key is released */
    audio_live_note(ui.track, note, s ? 2 : ui.arpdown && !kit() ? 3 : 1);
    ui.key_note[k] = (int8_t)note;
    ui.key_track[k] = (int8_t)ui.track;
    ui.last_note[ui.track] = (uint8_t)note;
    if (s && kit()) {
        pad_toggle(s, note);
    } else if (s) {                     /* the first note replaces the chord, the next ones fill it up to POLY */
        if (!ui.nwrote)
            __builtin_memset(s->note, 0, sizeof s->note);
        if (ui.nwrote < ui.p->sound[ui.track].poly)
            s->note[ui.nwrote++] = (uint8_t)note;
        step_on(s);
    } else if (ui.rec && !audio_playing() && t->src == SRC_STEP) {    /* step entry: the note at the cursor, then the next step; playing, the render records */
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
    int w;
    for (w = 0; w < 16 && WHITE[w] != k; w++) {}
    if (w < 16 && ui.punch >> w & 1) {
        ui.punch &= (uint16_t)~(1u << w);
        audio_punch(ui.punch);
    }
    if (ui.held && k == ui.held_key) {  /* nothing edited: the step toggles */
        q_step *s = &pat()->steps[ui.held - 1];
        if (!ui.edited) {
            if (!(s->flags & SF_ON)) {
                int n = ui.last_note[ui.track];
                if (kit()) {            /* one pad: the last one played, pad 1 when none */
                    __builtin_memset(s->note, 0, sizeof s->note);
                    n = n >= KIT_NOTE && n < KIT_NOTE + Q_PADS ? n : KIT_NOTE;
                }
                s->note[0] = (uint8_t)n;
                s->vel = 100;
            }
            __asm__ volatile("" ::: "memory");   /* note and vel before on */
            s->flags ^= SF_ON;
            ui.dirty = 1;
        }
        ui.held = 0;
    }
    if (ui.padheld == k + 1)
        ui.padheld = 0;
    if (ui.key_note[k] < 0)
        return;
    audio_live_note(ui.key_track[k], ui.key_note[k], 0);
    ui.key_note[k] = -1;
}

void ui_input(void)
{
    uint32_t btn = plat_buttons(), keys = plat_keys();
    int i, e, d, n, kpid;
    q_rec rec;
    if ((n = audio_scene_started()) >= 0) {     /* first of the frame: before any other write to the mute mask */
        scene_apply(&ui.p->scene[n]);
        ui.msg_scene = 0;
    }
    while (audio_take_rec(&rec))
        rec_write(&rec);
    ui.seqdown = btn >> B_SEQ & 1u;
    ui.homedown = btn >> B_HOME & 1u;
    ui.savedown = btn >> B_SAVE & 1u;
    ui.seldown = btn >> B_SEL & 1u;
    ui.arpdown = btn >> B_ARP & 1u;
    ui.fxdown = btn >> B_FX & 1u;
    if (!ui.fxdown && ui.punch) {       /* FX let go: every punch-in with it */
        ui.punch = 0;
        audio_punch(0);
    }
    for (i = 0; i < NB; i++)
        if ((btn & ~ui.btn) >> i & 1u)
            button(i);
    if ((ui.btn & ~btn) >> B_SAVE & 1u)
        save_release();
    for (i = 0; i < NKEYS; i++) {
        if ((keys & ~ui.keys) >> i & 1u)
            key_down(i);
        else if ((ui.keys & ~keys) >> i & 1u)
            key_up(i);
    }
    ui.btn = btn;
    ui.keys = keys;
    punch_rec();
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
            ui.lpage = ((ui.lpage + e) % lpage_n() + lpage_n()) % lpage_n();
        else
            ui.page[d] = ((ui.page[d] + e) % n + n) % n;
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {
        if (ui.held) {
            attr_turn(&pat()->steps[ui.held - 1], e);
            ui.edited = ui.dirty = 1;
        } else if (ui.domain == D_SEQ) {
            pattern_set(clamp(ui.p->pattern[ui.track] + e, 0, Q_PATTERNS - 1));
        } else if (ui.domain == D_EDIT && ui.padheld && kit()) {
            knob_turn(P_KIT, 1, e);     /* the held pad's engine */
        } else if (ui.domain == D_EDIT) {
            knob_turn(P_SOUND, 0, e);
        } else if (ui.domain == D_HOME && page_id() == P_SONG) {
            knob_turn(P_SONG, 0, e);
        } else if (ui.domain == D_FX && ui.page[D_FX] == 0) {   /* the insert's type */
            ui.p->sound[ui.track].itype = (uint8_t)clamp(ui.p->sound[ui.track].itype + e, 0, IT_N - 1);
            ui.dirty = 1;
        } else if (ui.domain == D_LFO) {        /* FREE / TRIG */
            ui.p->sound[ui.track].ltrig = (uint8_t)clamp(ui.p->sound[ui.track].ltrig + e, 0, 1);
            ui.dirty = 1;
        }
    }
    if (ui.page[D_SEQ] >= domain_n(D_SEQ))      /* another pattern or track, no XY page */
        ui.page[D_SEQ] = 0;
    kpid = knob_page();
    for (i = 0; i < 4; i++) {
        e = plat_enc(EN_K1 + i);
        if (ui.held) {
            lock_turn(i, e);
        } else {
            knob_turn(kpid, i, e);
            if (e && ui.rec && audio_playing())
                rec_lock(kpid, i);
        }
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
    else if (ui.domain == D_HOME) {             /* the tracks playing, the scenes memorised */
        for (i = 0; i < Q_TRACKS; i++)
            keys |= (uint32_t)(!(ui.p->mute >> i & 1u)) << WHITE[i];
        for (i = 0; i < Q_SCENES; i++)
            keys |= (uint32_t)ui.p->scene[i].used << WHITE[8 + i];
    } else if (ui.domain == D_SEQ && t->src == SRC_STEP)     /* a generator's keys play: no steps to show */
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

/* a rate or frequency in Hz, 2 significant digits (0.05 .. 44000) */
static void hz_text(float f, char *b)
{
    int dec = f < 0.1f ? 3 : f < 1.f ? 2 : f < 10.f ? 1 : 0, p = 1, n, fr, j, r = 1;
    for (j = 0; j < dec; j++)
        p *= 10;
    for (j = 100; f >= (float)j; j *= 10)    /* 100 and over: the digits after the first 2 are 0 */
        r *= 10;
    n = (int)(f * (float)p / (float)r + 0.5f) * r;
    int_text(n / p, b);
    while (*b)
        b++;
    fr = n % p;
    if (!fr)
        return;
    *b++ = '.';
    for (p /= 10; p; p /= 10) {         /* the fraction's digits, the trailing zeros dropped */
        *b++ = (char)('0' + fr / p);
        fr %= p;
        if (!fr)
            break;
    }
    *b = 0;
}

/* the engines with their own envelope: the ADSR shapes only their filter (6-OP, STRING, MODAL, the drums, the KIT) */
static int own_env(int m) { return (m >= 2 && m <= 4) || m >= 19; }

static void knob_text(int pid, int k, int v, char *b)
{
    if (pid >= P_ADSR) {
        const char *s = 0;
        switch (pid * 4 + k) {
        case P_FILTER * 4: s = FTYPE_NAME[v]; break;
        case P_LFO * 4: s = LSHAPE_NAME[v]; break;
        case P_LFO * 4 + 1:
            if (v >= LRATE_SYNC)
                return hz_text(0.05f * __builtin_powf(600.f, (float)(v - LRATE_SYNC) / 63.f), b);
            s = LRATE_NAME[v];
            break;
        case P_LFO * 4 + 3: s = LDEST_NAME[v]; break;
        case P_ARP * 4: s = AMODE_NAME[v]; break;
        case P_ARP * 4 + 2: s = DIV_NAME[v]; break;
        case P_ARP * 4 + 3:
            int_text(v, b);
            while (*b)
                b++;
            *b++ = ' ', *b++ = '%', *b = 0;
            return;
        }
        if (s) {
            while ((*b++ = *s++) != 0) {}
            return;
        }
    }
    if (pid == P_INSERT && k < 3) {             /* the knob's meaning depends on the insert's type */
        int it = ui.p->sound[ui.track].itype;
        if (it == IT_CRUSH && !k) {
            int_text(16 - (14 * v + 63) / 127, b);
            return;
        }
        if ((it == IT_CRUSH && k == 1) || ((it == IT_CHORUS || it == IT_PHASER) && !k) || (it == IT_RING && !k)) {
            hz_text(it == IT_CRUSH ? 44100.f * __builtin_powf(500.f / 44100.f, (float)v / 127.f)
                    : it == IT_RING ? 20.f * __builtin_powf(250.f, (float)v / 127.f)
                                    : 0.05f * __builtin_powf(100.f, (float)v / 127.f), b);
            return;
        }
        if (it == IT_COMB && !k) {
            note_name(v < 29 ? 29 : v, b);      /* the comb's lowest note is F1 */
            return;
        }
    }
    if (pid == P_GFX && !k) {
        const char *s = DIV_NAME[v];
        while ((*b++ = *s++) != 0) {}
        return;
    }
    if (pid == P_SONG && k == 3) {
        const char *s = v ? "SONG" : "PATTERN";
        while ((*b++ = *s++) != 0) {}
        return;
    }
    if (pid == P_SCALE) {
        const char *s = k == 0 ? TONIC_NAME[v] : k == 1 ? MODE_NAME[v] : k == 2 ? QUANT_NAME[v] : CLICK_NAME[v];
        while ((*b++ = *s++) != 0) {}
        return;
    }
    if ((k == 3 && (pid == P_STEPS || pid == P_GRIDS || pid == P_MARBLES)) || (pid == P_T && !k)) {
        const char *s = pid == P_T ? T_MODE_NAME[v] : SRC_NAME[v];
        while ((*b++ = *s++) != 0) {}
        return;
    }
    if ((pid == P_GRIDS && k == 2) || (pid == P_NOTES && !k)) {
        note_name(v, b);
        return;
    }
    if ((pid == P_STEPS && k > 0) || (pid == P_XY && k & 1) || ((pid == P_GRIDS || pid == P_MARBLES) && !k)) {
        const char *s = pid == P_STEPS && k == 2 ? PATH_NAME[v] : DIV_NAME[v];
        while ((*b++ = *s++) != 0) {}
        return;
    }
    if (pid == P_KIT && k == 1) {
        const char *s = ENGINE_NAME[v];
        while ((*b++ = *s++) != 0) {}
        return;
    }
    if (((pid == P_SOUND && k == 1) || (pid == P_PAD && !k)) && v > 0)
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
    int pid = page_id(), k, fpid = ui.held ? lock_pid() : knob_page(), locked;
    __builtin_memset(v, 0, sizeof *v);  /* the padding too: views are compared whole */
    v->head.track = (int8_t)ui.track;
    v->head.domain = (int8_t)ui.domain;
    v->head.page = (int8_t)ui.page[ui.domain];
    v->head.n = (int8_t)domain_n(ui.domain);
    v->head.msg = plat_ms() < ui.msg_until ? 1 : plat_ms() < ui.locks_until ? 2 : 0;
    v->head.pat = (int8_t)(ui.p->pattern[ui.track] + 1);
    v->head.scene = (int8_t)ui.msg_scene;
    v->head.punch = (int8_t)(plat_ms() < ui.punch_until ? ui.msg_punch : 0);
    if (audio_playing() && audio_pattern(ui.track) != ui.p->pattern[ui.track])
        v->head.playing = (int8_t)(audio_pattern(ui.track) + 1);
    if (ui.domain == D_EDIT && kit())
        v->head.pad = (int8_t)(ui.pad + 1);
    v->foot.pid = (int8_t)fpid;
    v->scene.pid = (int8_t)pid;
    if (pid == P_LFO)
        v->head.ltrig = (int8_t)(ui.p->sound[ui.track].ltrig + 1);
    v->foot.col = 15;
    if (fpid == P_INSERT)
        v->foot.itype = v->head.itype = ui.p->sound[ui.track].itype;
    for (k = 0; k < 4; k++) {
        if (PAGE[fpid].label[k])
            v->foot.v[k] = (int16_t)knob_get(fpid, k);
        if (knob_hidden(fpid, k))
            v->foot.hide |= (uint8_t)(1u << k);
    }
    if (ui.held)                   /* the lock page: the locked values in colour, the track's in grey */
        for (v->foot.col = 0, k = 0; k < 4; k++) {
            if (lock_param(k) < 0 || knob_hidden(fpid, k)) {
                v->foot.hide |= (uint8_t)(1u << k);
                continue;
            }
            v->foot.v[k] = (int16_t)lock_value(t, k, &locked);
            v->foot.col |= (uint8_t)(locked << k);
        }
    if (pid == P_ENGINE || pid == P_SOUND || pid == P_SCALE || pid == P_PAD || pid >= P_ADSR) {
        __builtin_memcpy(v->scene.v, v->foot.v, sizeof v->scene.v);
        v->scene.model = pid == P_PAD ? KIT_MODEL + ui.p->kit[ui.track][ui.pad].engine : ui.p->sound[ui.track].model;
        if ((pid == P_ENGINE || pid == P_SOUND) && scene_has(v->scene.model)) {
            v->scene.anim = 1;
            if (pid == P_SOUND)         /* the scene shows the sound's ENGINE knobs on both pages */
                for (k = 0; k < 4; k++)
                    v->scene.v[k] = (int16_t)knob_get(P_ENGINE, k);
        }
    } else if (pid == P_KIT) {
        v->scene.pad = (uint8_t)ui.pad;
        for (k = 0; k < Q_PADS; k++)
            v->scene.eng[k] = ui.p->kit[ui.track][k].engine;
    } else if (pid == P_STEPS || pid == P_XY) {
        v->scene.anim = 2;
        v->scene.spage = (int8_t)ui.spage;
        v->scene.path = (int8_t)t->path;
        v->scene.showpath = pid == P_STEPS && plat_ms() < ui.path_until;
        v->scene.xlen = (int8_t)(pid == P_XY ? t->xlen : 4);
        v->scene.ylen = (int8_t)(pid == P_XY ? t->ylen : 4);
        v->scene.held = (int8_t)ui.held;
        v->scene.model = ui.p->sound[ui.track].model;
        v->scene.attr = (int8_t)ui.attr;
        if (ui.held)
            v->scene.hst = t->steps[ui.held - 1];
        for (k = 0; k < t->nlocks; k++)
            if (t->locks[k].step / 16 == ui.spage)
                v->scene.locks |= (uint16_t)(1u << (t->locks[k].step % 16));
        v->scene.cursor = (int8_t)(ui.rec && !audio_playing() ? ui.cursor : 0);
        v->scene.pos = (int8_t)audio_pos(ui.track);
        v->scene.len = (int8_t)t->len;
        __builtin_memcpy(v->scene.st, &t->steps[ui.spage * 16], sizeof v->scene.st);
    } else if (pid >= P_GRIDS) {        /* a generator: its rank, when the pattern shown is the one playing */
        v->scene.src = (int8_t)t->src;
        v->scene.len = (int8_t)gen_loop(t);
        v->scene.pos = (int8_t)(audio_pattern(ui.track) == ui.p->pattern[ui.track] ? audio_pos(ui.track) : -1);
        if (pid == P_NOTES)
            v->scene.notes = gen_allowed(ui.p, t);
    } else if (pid == P_TRACKS) {
        for (k = 0; k < Q_TRACKS; k++) {
            v->scene.tr[k].model = ui.p->sound[k].model;
            v->scene.tr[k].pat = (uint8_t)(ui.p->pattern[k] + 1);
            if (audio_playing() && audio_pattern(k) != ui.p->pattern[k])
                v->scene.tr[k].playing = (uint8_t)(audio_pattern(k) + 1);
            v->scene.tr[k].mute = ui.p->mute >> k & 1u;
            const q_pattern *pt = &ui.p->pat[k][ui.p->pattern[k]];
            v->scene.tr[k].pos = (int8_t)audio_pos(k);
            v->scene.tr[k].len = (int8_t)(pt->src == SRC_STEP ? pt->len : gen_loop(pt));
        }
    } else if (pid == P_SONG) {
        int first = ui.entry > 3 ? ui.entry - 3 : 1;     /* the cursor on the 3rd row at most */
        if (first + 5 > ui.p->nsong)
            first = ui.p->nsong - 5 > 1 ? ui.p->nsong - 5 : 1;
        v->scene.song.n = (int8_t)ui.p->nsong;
        v->scene.song.cursor = (int8_t)ui.entry;
        v->scene.song.first = (int8_t)first;
        v->scene.song.playing = (int8_t)(audio_song_entry() + 1);
        for (k = 0; k < 6 && first + k <= ui.p->nsong; k++)
            v->scene.song.e[k] = ui.p->song[first + k - 1];
        if (!ui.entry)
            v->foot.hide = 7;
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
        v->scene.cpu[6] = (int32_t)l.fx_pct;
        v->scene.cpu[7] = ui.fps;
    }
}

/* the plateau's view of the SEQ page: its cells, texts and marks, from what the page shows */
static void plateau_view(const q_view *v, q_plateau *p)
{
    int first = v->scene.spage * 16, i, k;
    __builtin_memset(p, 0, sizeof *p);
    for (i = 0; i < 16; i++) {
        const q_step *st = &v->scene.st[i];
        q_pcell *c = &p->c[i];
        c->on = st->flags & SF_ON;
        c->vel = st->vel;
        c->len = st->len;
        c->extra = STEP_RATCHET(st) > 1 || st->prob < 20 || st->cond || st->flags & (SF_SLIDE | SF_ACCENT) || st->micro ||
                   (v->scene.locks >> i & 1u);
        if (v->scene.model == M_KIT)        /* its first pad */
            int_text(st->note[0] - KIT_NOTE + 1, c->name);
        else
            for (k = 0; TONIC_NAME[st->note[0] % 12][k]; k++)
                c->name[k] = TONIC_NAME[st->note[0] % 12][k];
        if (first + i < v->scene.len && i % 4 < v->scene.xlen && i / 4 < v->scene.ylen)
            p->live |= (uint16_t)(1u << i);
    }
    p->pos = (int8_t)(v->scene.pos >= first && v->scene.pos < first + 16 ? v->scene.pos - first : -1);
    p->cursor = (int8_t)(v->scene.cursor - 1 >= first && v->scene.cursor - 1 < first + 16 ? v->scene.cursor - 1 - first : -1);
    p->end = (int8_t)(v->scene.len - 1 >= first && v->scene.len - 1 < first + 15 ? v->scene.len - 1 - first : -1);
    p->held = (int8_t)(v->scene.held - 1 >= first && v->scene.held - 1 < first + 16 ? v->scene.held - 1 - first : -1);
    if (v->scene.showpath && v->scene.path != PATH_RANDOM && v->scene.path != PATH_XY) {
        int m = v->scene.path == PATH_SNAKE ? 1 : v->scene.path == PATH_COLUMNS ? 2 : v->scene.path == PATH_SPIRAL ? 3 : 0;
        p->npath = 16;
        for (i = 0; i < 16; i++)
            p->path[i] = PATHS[m][v->scene.path == PATH_REV ? 15 - i : i];
    }
    if (p->pos >= 0) {
        const q_step *st = &v->scene.st[p->pos];
        __builtin_memcpy(p->top_l, "STEP ", 5);
        p->top_l[5] = (char)('0' + (v->scene.pos + 1) / 10);
        p->top_l[6] = (char)('0' + (v->scene.pos + 1) % 10);
        if (st->flags & SF_ON)
            note_name(st->note[0], p->top_r);
        else
            p->top_r[0] = p->top_r[1] = '-';
    } else {
        int_text(first + 1, p->top_l);
        for (k = 0; p->top_l[k]; k++) {}
        p->top_l[k++] = '-';
        int_text(first + 16, p->top_l + k);
    }
    if (p->held >= 0) {
        for (i = 0; i < A_N; i++) {
            int a = ATTR_ROW[i];
            for (k = 0; ATTR_NAME[a][k]; k++)
                p->rows[i][0][k] = ATTR_NAME[a][k];
            attr_text(&v->scene.hst, a, p->rows[i][1]);
            if (a == v->scene.attr)
                p->attr = (int8_t)i;
        }
        if (v->scene.model == M_KIT)
            int_text(v->scene.hst.note[0] - KIT_NOTE + 1, p->held_name);
        else
            note_name(v->scene.hst.note[0], p->held_name);
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
    else if (v->head.punch)
        cv_text(x + 10, 7, &FONT_B, PUNCH_NAME[v->head.punch - 1], C_AMB);
    else if (v->head.domain == D_HOME && v->head.scene) {       /* the scene asked, until it starts */
        t[0] = (char)('0' + v->head.scene), t[1] = 0;
        x = cv_text(x + 10, 7, &FONT_B, "SCENE", C_AMB);
        cv_text(x + 4, 7, &FONT_B, t, C_AMB);
    } else if (v->head.domain == D_SEQ) {         /* the pattern asked, after the one playing when it differs */
        x += 10;
        if (v->head.playing) {
            t[0] = 'P', t[1] = (char)('0' + v->head.playing), t[2] = 0;
            x = cv_text(x, 7, &FONT_B, t, C_GRAY) + 4;
            x = cv_text(x, 7, &FONT_B, ">", C_GRAY) + 4;
        }
        t[0] = 'P', t[1] = (char)('0' + v->head.pat), t[2] = 0;
        cv_text(x, 7, &FONT_B, t, C_AMB);
    } else {
        x = cv_text(x + 10, 7, &FONT_B, v->foot.pid == P_INSERT ? INS_NAME[v->head.itype] : PAGE[v->foot.pid].name, C_AMB);
        if (v->head.ltrig)
            x = cv_text(x + 8, 7, &FONT_B, v->head.ltrig == 2 ? "TRIG" : "FREE", C_AMB);
        if (v->head.pad) {              /* a KIT: the pad its knobs set */
            if (v->foot.pid != P_PAD)
                x = cv_text(x + 8, 7, &FONT_B, "PAD", C_AMB);
            int_text(v->head.pad, t);
            cv_text(x + 6, 7, &FONT_B, t, C_AMB);
        }
    }
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
    case P_PAD:
    case P_ADSR:
    case P_FILTER:
    case P_LFO:
    case P_ARP:
    case P_INSERT:
    case P_MIX:
    case P_GFX:
        if (v->scene.pid != P_GFX)
            cv_text(10, oy + 8, &FONT_M, MODEL_NAME[v->scene.model], C_WHITE);
        if (v->scene.pid == P_ARP && v->scene.model == M_KIT) {
            cv_text(10, oy + 60, &FONT_M, "NO ARP ON KIT", C_GRAY);
            break;
        }
        if (v->scene.pid == P_ADSR && own_env(v->scene.model))      /* its own envelope shapes the level */
            cv_text(10, oy + 34, &FONT_S, "FILTER ONLY", C_AMB);
        for (k = 0; k < 4; k++) {
            const q_page *pg = &PAGE[v->scene.pid];
            int32_t x = 60 * k + 12, h = 108 * (v->scene.v[k] - pg->min[k]) / (pg->max[k] - pg->min[k]);
            cv_rect(x, oy + 48, 36, 108, C_LINE);
            cv_rect(x, oy + 156 - h, 36, h, KCOL[k]);
        }
        break;
    case P_KIT:                         /* the 16 pads, 4 x 4 as the steps, the one the knobs set lit */
        for (i = 0; i < Q_PADS; i++) {
            int32_t x = 6 + 58 * (i % 4), y = oy + 18 + 37 * (i / 4);
            const char *n = ENGINE_NAME[v->scene.eng[i]];
            cv_rect(x, y, 54, 33, i == v->scene.pad ? C_HI : C_LINE);
            cv_text(x + (54 - text_w(&FONT_T, n)) / 2, y + 20, &FONT_T, n, i == v->scene.pad ? C_BLACK : C_GRAY);
        }
        break;
    case P_GRIDS:
    case P_MAP:
    case P_DENS:
    case P_MARBLES:
    case P_T:
    case P_X:
    case P_NOTES:
        cv_text(10, oy + 8, &FONT_M, SRC_NAME[v->scene.src], C_WHITE);
        t[0] = '-', t[1] = 0;
        if (v->scene.pos >= 0)
            int_text(v->scene.pos + 1, t);
        for (i = 0; t[i]; i++) {}
        t[i++] = '/';
        int_text(v->scene.len, t + i);
        if (v->scene.pid != P_NOTES) {
            cv_text((240 - text_w(&FONT_L, t)) / 2, oy + 52, &FONT_L, t, C_WHITE);
            cv_rect(10, oy + 120, 220, 4, C_LINE);
            if (v->scene.pos >= 0)
                cv_rect(10, oy + 120, 220 * (v->scene.pos + 1) / v->scene.len, 4, PINK);
            break;
        }
        cv_text(230 - text_w(&FONT_S, t), oy + 12, &FONT_S, t, C_GRAY);
        for (i = 0; i < 12; i++) {      /* an octave's keys: the sharps above the naturals */
            int sharp = 0x54A >> i & 1, n = i < 5 ? i / 2 : (i + 1) / 2 - sharp, on = v->scene.notes >> i & 1;
            int32_t x = 9 + 32 * n + 16 * sharp, y = oy + (sharp ? 50 : 104);
            cv_rect(x, y, 30, 48, on ? C_HI : C_LINE);
            cv_text(x + (30 - text_w(&FONT_S, TONIC_NAME[i])) / 2, y + 16, &FONT_S, TONIC_NAME[i], on ? C_BLACK : C_GRAY);
        }
        break;
    case P_TRACKS:
        for (i = 0; i < Q_TRACKS; i++) {
            const q_trow *r = &v->scene.tr[i];
            int32_t y = oy + 4 + 40 * i, x = 150;
            uint16_t c = r->mute ? C_GRAY : C_WHITE;
            cv_rect(10, y + 2, 20, 20, r->mute ? C_DIM : C_HI);
            int_text(i + 1, t);
            cv_text(10 + (20 - text_w(&FONT_B, t)) / 2, y + 4, &FONT_B, t, C_BLACK);
            cv_text(38, y + 6, &FONT_S, MODEL_NAME[r->model], c);
            if (r->playing) {
                t[0] = 'P', t[1] = (char)('0' + r->playing), t[2] = 0;
                x = cv_text(x, y + 6, &FONT_S, t, C_GRAY) + 4;
                x = cv_text(x, y + 6, &FONT_S, ">", C_GRAY) + 4;
            }
            t[0] = 'P', t[1] = (char)('0' + r->pat), t[2] = 0;
            cv_text(x, y + 6, &FONT_S, t, C_AMB);
            if (r->mute)
                cv_text(194, y + 6, &FONT_S, "MUTE", KCOL[3]);
            cv_rect(10, y + 28, 220, 4, C_LINE);
            if (r->pos >= 0)
                cv_rect(10, y + 28, 220 * (r->pos % r->len + 1) / r->len, 4, PINK);
        }
        break;
    case P_SONG:
        if (!v->scene.song.n)
            cv_text(16, oy + 8, &FONT_S, "EMPTY", C_GRAY);
        for (i = 0; i < 6 && v->scene.song.first + i <= v->scene.song.n; i++) {
            int e = v->scene.song.first + i;
            int32_t y = oy + 4 + 26 * i;
            uint16_t c = e == v->scene.song.playing ? PINK : C_WHITE;
            if (e == v->scene.song.cursor)
                cv_rect(0, y - 2, 240, 24, C_LINE);
            int_text(e, t);
            cv_text(10, y + 3, &FONT_S, t, C_GRAY);
            t[0] = 'S', t[1] = (char)('1' + v->scene.song.e[i].scene), t[2] = 0;
            cv_text(60, y + 3, &FONT_S, t, c);
            t[0] = 'X';
            int_text(v->scene.song.e[i].reps, t + 1);
            cv_text(230 - text_w(&FONT_S, t), y + 3, &FONT_S, t, c);
        }
        break;
    case P_TEMPO:
        int_text(v->scene.tempo, t);
        cv_text((240 - text_w(&FONT_L, t)) / 2, oy + 52, &FONT_L, t, C_WHITE);
        cv_text((240 - text_w(&FONT_XS, "BPM")) / 2, oy + 90, &FONT_XS, "BPM", C_GRAY);
        break;
    case P_SCALE:
        cv_text((240 - text_w(&FONT_L, TONIC_NAME[v->scene.v[0]])) / 2, oy + 40, &FONT_L, TONIC_NAME[v->scene.v[0]], C_WHITE);
        cv_text((240 - text_w(&FONT_M, MODE_NAME[v->scene.v[1]])) / 2, oy + 100, &FONT_M, MODE_NAME[v->scene.v[1]], C_GRAY);
        break;
    case P_CPU: {
        static const char *const ROW[8] = {"CORE 1", "CORE 2", "LOST BLOCKS", "VOICES", "BYTES/VOICE", "CORE 2 STACK", "EFFECTS",
                                           "SCENE FPS"};
        for (i = 0; i < 8; i++) {
            int32_t y = oy + 4 + 20 * i;
            cv_text(16, y, &FONT_S, ROW[i], C_GRAY);
            if (i == 1 && v->scene.cpu[1] < 0) {
                t[0] = 'O', t[1] = 'F', t[2] = 'F', t[3] = 0;
            } else {
                int_text(v->scene.cpu[i], t);
                for (k = 0; t[k]; k++) {}
                if (i < 2 || i == 6)
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
        const char *lb = v->foot.pid == P_INSERT && k < 3 ? INS_LABEL[v->foot.itype][k] : pg->label[k];
        if (!lb || v->foot.hide >> k & 1u)
            continue;
        cv_text(x, 5, &FONT_XS, lb, c);
        knob_text(v->foot.pid, k, v->foot.v[k], t);
        if (text_w(&FONT_B, t) <= 54)
            cv_text(x, 20, &FONT_B, t, c);
        else                            /* a long name keeps to its column */
            cv_text(x, 24, text_w(&FONT_S, t) <= 54 ? &FONT_S : &FONT_XS, t, c);
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
    if (v.scene.anim) {
        uint32_t now = plat_ms();
        if (ui.full || now - ui.scene_ms >= 30u || __builtin_memcmp(&v.scene, &ui.shown.scene, sizeof v.scene)) {
            /* shown anew (another scene, track, pattern or page of steps): the notes before do not hit */
            int fresh = ui.full || ui.shown.scene.anim != v.scene.anim || ui.shown.head.track != v.head.track ||
                        ui.shown.head.pat != v.head.pat || ui.shown.scene.spage != v.scene.spage;
            ui.scene_ms = now;
            ui.frames++;
            if (v.scene.anim == 1) {
                if (fresh)
                    scene_enter();
                scene_frame(v.scene.v, audio_hits(ui.track), now);
            } else {
                static q_plateau pv;
                if (fresh)
                    plateau_enter();
                plateau_view(&v, &pv);
                plateau_frame(&pv, audio_hits(ui.track), now);
            }
            for (b = 0; b < 2; b++) {
                cv_begin(240, BAND_H, C_BLACK);
                if (v.scene.anim == 1)
                    scene_band(-BAND_H * b);
                else
                    plateau_band(-BAND_H * b);
                cv_blit(0, HEAD_H + BAND_H * b);
            }
        }
    } else if (ui.full || __builtin_memcmp(&v.scene, &ui.shown.scene, sizeof v.scene))
        for (b = 0; b < 2; b++) {
            cv_begin(240, BAND_H, C_BLACK);
            draw_scene(&v, -BAND_H * b);
            cv_blit(0, HEAD_H + BAND_H * b);
        }
    if (ui.full || __builtin_memcmp(&v.foot, &ui.shown.foot, sizeof v.foot))
        draw_foot(&v);
    ui.fps_mixed |= !v.scene.anim;
    if (plat_ms() - ui.fps_ms >= 1000u) {      /* SCENE FPS: a second spent wholly on a scene */
        if (!ui.fps_mixed)
            ui.fps = ui.frames;
        ui.fps_ms = plat_ms();
        ui.frames = ui.fps_mixed = 0;
    }
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
    int f = 12, n = u ? atoi(u + 1) : 0, m = u ? atoi(strrchr(what, '_') + 1) : 0;   /* m: the index after the last '_' */
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
    } else if (!strncmp(what, "lock_", 5)) {     /* lock_P_N: LK_* P's lock on step N, -1 none */
        int i = n >= 0 && n <= LK_PUNCH_HI && m >= 1 && m <= Q_STEPS ? lock_find(t, m - 1, n) : -1;
        *got = i < 0 ? -1 : (int8_t)t->locks[i].value;
    } else if (!strncmp(what, "scene_used_", 11)) *got = m >= 1 && m <= Q_SCENES ? ui.p->scene[m - 1].used : 0;
    else if (!strncmp(what, "song_scene_", 11)) *got = m >= 1 && m <= Q_SONG ? ui.p->song[m - 1].scene + 1 : 0;
    else if (!strncmp(what, "song_reps_", 10)) *got = m >= 1 && m <= Q_SONG ? ui.p->song[m - 1].reps : 0;
    else if (!strcmp(what, "song_n")) *got = ui.p->nsong;
    else if (!strcmp(what, "song_on")) *got = ui.p->song_on;
    else if (!strcmp(what, "entry")) *got = ui.entry;
    else if (!strcmp(what, "playentry")) *got = audio_song_entry() + 1;
    else if (!strcmp(what, "msg_scene")) *got = ui.msg_scene;
    else if (!strcmp(what, "mute")) *got = ui.p->mute;
    else if (!strcmp(what, "domain")) *got = ui.domain;
    else if (!strcmp(what, "page")) *got = ui.page[ui.domain] + 1;
    else if (!strcmp(what, "track")) *got = ui.track + 1;
    else if (!strcmp(what, "playing")) *got = audio_playing();
    else if (!strcmp(what, "rec")) *got = ui.rec;
    else if (!strcmp(what, "cursor")) *got = ui.cursor;
    else if (!strcmp(what, "model")) *got = s->model;
    else if (!strcmp(what, "poly")) *got = s->poly;
    else if (!strcmp(what, "tune")) *got = s->tune;
    else if (!strcmp(what, "harm")) *got = s->harm;
    else if (!strcmp(what, "atk")) *got = s->atk;
    else if (!strcmp(what, "dec")) *got = s->dec;
    else if (!strcmp(what, "sus")) *got = s->sus;
    else if (!strcmp(what, "rel")) *got = s->rel;
    else if (!strcmp(what, "ftype")) *got = s->ftype;
    else if (!strcmp(what, "cutoff")) *got = s->cutoff;
    else if (!strcmp(what, "res")) *got = s->res;
    else if (!strcmp(what, "envamt")) *got = s->envamt;
    else if (!strcmp(what, "lshape")) *got = s->lshape;
    else if (!strcmp(what, "ltrig")) *got = s->ltrig;
    else if (!strcmp(what, "lrate")) *got = s->lrate;
    else if (!strcmp(what, "ldepth")) *got = s->ldepth;
    else if (!strcmp(what, "ldest")) *got = s->ldest;
    else if (!strcmp(what, "amode")) *got = s->amode;
    else if (!strcmp(what, "aoct")) *got = s->aoct;
    else if (!strcmp(what, "adiv")) *got = s->adiv;
    else if (!strcmp(what, "agate")) *got = s->agate;
    else if (!strcmp(what, "lpage")) *got = ui.lpage + 1;
    else if (!strcmp(what, "itype")) *got = s->itype;
    else if (!strcmp(what, "i1")) *got = s->i1;
    else if (!strcmp(what, "i2")) *got = s->i2;
    else if (!strcmp(what, "i3")) *got = s->i3;
    else if (!strcmp(what, "imix")) *got = s->imix;
    else if (!strcmp(what, "dsend")) *got = s->dsend;
    else if (!strcmp(what, "rsend")) *got = s->rsend;
    else if (!strcmp(what, "pan")) *got = s->pan;
    else if (!strcmp(what, "level")) *got = s->level;
    else if (!strcmp(what, "dtime")) *got = ui.p->dtime;
    else if (!strcmp(what, "scene3d")) *got = ui.shown.scene.anim == 1;
    else if (!strcmp(what, "anim")) *got = ui.shown.scene.anim;
    else if (!strcmp(what, "hits")) *got = (int)audio_hits(ui.track);
    else if (!strcmp(what, "fps")) *got = ui.fps;
    else if (!strcmp(what, "dfb")) *got = ui.p->dfb;
    else if (!strcmp(what, "rsize")) *got = ui.p->rsize;
    else if (!strcmp(what, "rtone")) *got = ui.p->rtone;
    else if (!strcmp(what, "comp")) *got = ui.p->comp;
    else if (!strcmp(what, "duck")) *got = ui.p->duck;
    else if (!strcmp(what, "tempo")) *got = ui.p->tempo;
    else if (!strcmp(what, "swing")) *got = ui.p->swing;
    else if (!strcmp(what, "tonic")) *got = ui.p->tonic;
    else if (!strcmp(what, "mode")) *got = ui.p->mode;
    else if (!strcmp(what, "quant")) *got = ui.p->quant;
    else if (!strcmp(what, "click")) *got = ui.p->click;
    else if (!strcmp(what, "overdub")) *got = ui.rec && audio_playing();
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
    else if (!strcmp(what, "punch")) *got = ui.punch;
    else if (!strcmp(what, "msg_locks_full")) *got = plat_ms() < ui.locks_until;
    else if (!strcmp(what, "dirty")) *got = ui.dirty;
    else if (!strcmp(what, "msg_stop_to_save")) *got = plat_ms() < ui.msg_until;
    else if (!strcmp(what, "active")) *got = (int)audio_load().active;
    else if (!strcmp(what, "src")) *got = t->src;
    else if (!strcmp(what, "gx")) *got = t->grids.x;
    else if (!strcmp(what, "gdens1")) *got = t->grids.dens[0];
    else if (!strcmp(what, "mlen")) *got = t->marbles.len;
    else if (!strcmp(what, "mnotes")) *got = t->marbles.notes[0] | t->marbles.notes[1] << 8;
    else if (!strcmp(what, "mdejavu")) *got = t->marbles.dejavu;
    else if (!strcmp(what, "mmode")) *got = t->marbles.mode;
    else if (!strcmp(what, "mtout")) *got = t->marbles.tout;
    else if (!strcmp(what, "gvoice")) *got = t->grids.voice;
    else if (!strcmp(what, "gnote")) *got = t->grids.note;
    else if (!strcmp(what, "mnote")) *got = t->marbles.note;
    else if (!strncmp(what, "trlen_", 6) && m >= 1 && m <= Q_TRACKS) *got = ui.shown.scene.tr[m - 1].len;
    else if (!strcmp(what, "pad")) *got = ui.pad + 1;
    else if (!strncmp(what, "pad", 3) && u && m >= 1 && m <= Q_PADS) {
        const q_pad *pd = &ui.p->kit[ui.track][m - 1];
        if (!strncmp(what, "padtune_", 8)) *got = pd->tune;
        else if (!strncmp(what, "padharm_", 8)) *got = pd->harm;
        else if (!strncmp(what, "padengine_", 10)) *got = pd->engine;
        else if (!strncmp(what, "padlevel_", 9)) *got = pd->level;
        else
            return 0;
    } else if (!strncmp(what, "stepnotes_", 10) && m >= 1 && m <= Q_STEPS) {
        const q_step *st = &t->steps[m - 1];
        for (*got = 1; *got < 4 && st->note[*got]; ++*got) {}
    }
    else
        return 0;
    return 1;
}
#endif
