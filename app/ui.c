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
#define HOLD_MS 400u                 /* held longer: a long press, its release does nothing */

enum { D_EDIT, D_SEQ, D_GLO, D_HOME, D_ENV, D_LFO, D_ARP, D_FX, ND };
enum { P_ENGINE, P_SOUND, P_STEPS, P_TEMPO, P_SCALE, P_CPU, P_XY, P_TRACKS, P_SONG, P_GRIDS, P_MAP, P_DENS, P_MARBLES, P_T,
       P_X, P_NOTES, P_LIFE, P_LIFEK, P_SEED, P_EVOL, P_PAD, P_KIT, P_ADSR, P_FILTER, P_LFO, P_ARP, P_INSERT, P_MIX, P_GFX, P_KEYS,
       P_SN1, P_SN2, P_SN3, P_LANES, P_LENS, P_CHORDS, P_PROG, P_TONNETZ, P_WALK, P_PLAY, P_ORBITS, P_MOON, P_MOONK, P_CHORD, P_TZSTEP, P_MIMIC, P_LOOP, P_GRIDSK, P_CIPHER, P_CIPHERK, P_CPARAM, P_CTIME };            /* NOTES in SEQ: the cursor's step on K1-K4 */
/* what PRESET sets on a held step; the black keys pick them by family (FAMILY) */
/* REC: GRID the white keys are SEQ's steps, NOTES they write at a cursor, LIVE they record while playing, ARM the first
   note starts LIVE */
enum { R_OFF, R_GRID, R_NOTES, R_LIVE, R_ARM, R_LISTEN };   /* LISTEN: a MIMIC pattern learns from the keyboard */
enum { A_VEL, A_RATCHET, A_PROB, A_COND, A_SLIDE, A_ACCENT, A_NOTE, A_LEN, A_MICRO, A_N };
#define COND_N 40                    /* NONE, FIRST, !FIRST, PRE, !PRE, then A:B for B = 2..8, A = 1..B */

static const uint8_t DOMAIN_PAGES[ND][5] = {{P_ENGINE, P_SOUND}, {P_STEPS, P_XY}, {P_TEMPO, P_SCALE, P_KEYS, P_GFX, P_CPU}, {P_TRACKS, P_SONG},
                                                {P_ADSR, P_FILTER}, {P_LFO}, {P_ARP}, {P_MIX, P_INSERT}};
static const uint8_t DOMAIN_N[ND] = {2, 1, 5, 2, 2, 1, 1, 2};      /* SEQ: 2 pages while PATH is XY (pages) */
static const uint8_t GEN_PAGES[9][5] = {{P_GRIDS, P_MAP, P_DENS}, {P_MARBLES, P_T, P_X, P_NOTES}, {P_LIFE, P_SEED, P_EVOL},
                                        {P_LANES, P_LENS}, {P_CHORDS, P_PROG, P_CHORD},
                                        {P_TONNETZ, P_WALK, P_PLAY, P_TZSTEP}, {P_ORBITS, P_MOON}, {P_MIMIC, P_LOOP},
                                        {P_CIPHER, P_CPARAM, P_CTIME}};
static const uint8_t KIT_PAGES[5] = {P_PAD, P_KIT};
static const uint8_t NOTES_PAGES[5] = {P_SN1, P_SN2, P_SN3, P_STEPS, P_XY};
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
    {"TRACKS", {"LVL 1", "LVL 2", "LVL 3", "LVL 4"}, {0, 0, 0, 0}, {127, 127, 127, 127}},   /* the mixer: MIX LEVEL */
    {"SONG", {"ENTRY", "SCENE", "REPEAT", "MODE"}, {1, 1, 1, 0}, {Q_SONG, Q_SCENES, 16, 1}},
    {"ATLAS", {"DIV", "VOICE", "NOTE", "SOURCE"}, {0, 1, 1, 0}, {DIV_N - 1, 3, 127, SRC_N - 1}},
    {"MAP", {"X", "Y", "CHAOS"}, {0, 0, 0}, {127, 127, 127}},
    {"DENS", {"DENS 1", "DENS 2", "DENS 3"}, {0, 0, 0}, {127, 127, 127}},
    {"FLEET", {"DIV", "T OUT", "LEN", "SOURCE"}, {0, 1, 1, 0}, {DIV_N - 1, 3, 16, SRC_N - 1}},
    {"T", {"MODE", "BIAS", "JITTER", "DEJA VU"}, {0, 0, 0, 0}, {6, 127, 127, 127}},
    {"X", {"SPREAD", "X BIAS", "STEPS", "GATE"}, {0, 0, 0, 0}, {127, 127, 127, 5}},
    {"NOTES", {"NOTE", "RANGE"}, {1, 1}, {127, 4}},
    {"COLONY", {"DIV", "POLY", "NOTE", "SOURCE"}, {0, 1, 1, 0}, {DIV_N - 1, 4, 127, SRC_N - 1}},
    {"COLONY", {"DIV", "POLY", "MAP", "SOURCE"}, {0, 1, 0, 0}, {DIV_N - 1, 4, 1, SRC_N - 1}},     /* a KIT's: MAP for NOTE */
    {"SEED", {"SEED", "DENS", "AUTO"}, {0, 0, 0}, {127, 127, 1}},
    {"EVOL", {"GEN", "MUT"}, {0, 0}, {3, 127}},
    {"PAD", {"TUNE", "HARM", "TIMB", "DECAY"}, {-24, 0, 0, 0}, {24, 127, 127, 127}},
    {"KIT", {"MODEL", "ENGINE", "LEVEL", "CHAR"}, {0, 0, 0, 0}, {M_KIT, 2, 127, 127}},
    {"ADSR", {"ATTACK", "DECAY", "SUSTAIN", "RELEASE"}, {0, 0, 0, 0}, {127, 127, 127, 127}},
    {"FILTER", {"TYPE", "CUTOFF", "RES", "ENV AMT"}, {0, 0, 0, -64}, {2, 127, 127, 63}},
    {"LFO", {"SHAPE", "RATE", "DEPTH", "DEST"}, {0, 0, -64, 0}, {4, LRATE_SYNC + 63, 63, 6}},
    {"ARP", {"MODE", "OCT", "DIV", "GATE"}, {0, 1, 0, 5}, {AM_ORDER, 4, DIV_N - 1, 100}},
    {"INSERT", {"", "", "", "MIX"}, {0, 0, 0, 0}, {127, 127, 127, 127}},      /* its name and knobs: INS_NAME, INS_LABEL */
    {"MIX", {"DELAY", "REVERB", "PAN", "LEVEL"}, {0, 0, -64, 0}, {127, 127, 63, 127}},
    {"FX", {"DLY TIME", "DLY FB", "REV SIZE", "REV TONE"}, {0, 0, 0, 0}, {DIV_N - 1, 127, 127, 127}},
    {"KEYS", {"IN SCALE"}, {0}, {KEYS_ALL}},
    {"STEP", {"NOTE", "LEN", "VEL", "PROB"}, {1, 1, 1, 0}, {127, 64, 127, 20}},
    {"STEP", {"RATCHET", "COND", "MICRO"}, {1, 0, -11}, {4, 39, 11}},      /* COND: its index, COND_N - 1 at most */
    {"STEP", {"SLIDE", "ACCENT"}, {0, 0}, {1, 1}},
    {"LOOM", {"DIV", "NOTE", "TRIG LEN", "SOURCE"}, {0, 1, 1, 0}, {DIV_N - 1, 127, 16, SRC_N - 1}},
    {"LENS", {"NOTE LEN", "OCT LEN", "DUR LEN", "VEL LEN"}, {1, 1, 1, 1}, {16, 16, 16, 16}},
    {"CADENCE", {"DIV", "NOTE", "RATE", "SOURCE"}, {0, 1, 0, 0}, {DIV_N - 1, 127, 4, SRC_N - 1}},
    {"PROG", {"LEN", "STRUM", "VOICE"}, {1, 0, 0}, {8, 7, 1}},
    {"LATTICE", {"DIV", "NOTE", "RATE", "SOURCE"}, {0, 1, 0, 0}, {DIV_N - 1, 127, 4, SRC_N - 1}},
    {"WALK", {"LEN", "CHAOS", "SCALE", "RETURN"}, {1, 0, 0, 0}, {8, 127, 1, 1}},
    {"PLAY", {"STRUM", "VOICE"}, {0, 0}, {7, 1}},
    {"MOONS", {"DIV", "NOTE", "MOONS", "SOURCE"}, {0, 1, 1, 0}, {DIV_N - 1, 127, 4, SRC_N - 1}},
    {"MOON", {"PERIOD", "PHASE", "NOTE"}, {1, 0, 0}, {32, 31, 15}},
    {"MOON", {"PERIOD", "PHASE", "PAD"}, {1, 0, 0}, {32, 31, 15}},       /* a KIT's: PAD for NOTE */
    {"CHORD", {"DEGREE", "SIZE"}, {0, 0}, {6, CS_N - 1}},                 /* the chord under PRESET's cursor */
    {"STEP", {"OP"}, {0}, {3}},                                          /* the TONNETZ step under PRESET's cursor */
    {"IMPROV", {"DIV", "ORDER", "DRIFT", "SOURCE"}, {0, 1, 0, 0}, {DIV_N - 1, 2, 127, SRC_N - 1}},
    {"LOOP", {"LOOP"}, {0}, {16}},                                       /* 0 FREE */
    {"ATLAS", {"DIV", "VOICE", "PAD", "SOURCE"}, {0, 1, 0, 0}, {DIV_N - 1, 3, 15, SRC_N - 1}},   /* a KIT's: VOICE's PAD for NOTE */
    {"CIPHER", {"DIV", "FORMULA", "NOTE", "SOURCE"}, {0, 0, 1, 0}, {DIV_N - 1, CIPHER_N - 1, 127, SRC_N - 1}},
    {"CIPHER", {"DIV", "FORMULA", 0, "SOURCE"}, {0, 0, 0, 0}, {DIV_N - 1, CIPHER_N - 1, 0, SRC_N - 1}},   /* a KIT's: no NOTE */
    {"PARAM", {"A", "B", "C", "DENS"}, {0, 0, 0, 0}, {15, 15, 15, 127}},
    {"TIME", {"LEN", "START", "SPEED", "RANGE"}, {0, 0, 1, 1}, {64, 127, 255, 4}},     /* LEN 0 FREE */
};

/* Plaits' registration order (pq.h) */
static const char *const MODEL_NAME[M_KIT + 1] = {
    "SWEEP", "KINK", "HEX A", "HEX B", "HEX C", "BOWL", "ENSEMBLE", "ARCADE",
    "CREST", "PRISM", "MOON", "POLLEN", "FACETS", "PIPE", "CHORALE", "ORATOR",
    "CROWD", "STATIC", "GLITTER", "WIRE", "CHLADNI", "BASS DRUM", "SNARE", "HI-HAT", "KIT"};
/* the order PRESET and MODEL go through the models, by family (analog, FM, tables, spectral, chords, voice, noise,
   physical, drums); the project keeps Plaits' number */
static const uint8_t MODEL_ORDER[M_KIT + 1] = {8, 0, 1, 9, 10, 2, 3, 4, 13, 5, 12, 11, 14, 6, 7, 15, 16, 17, 18, 19, 20,
                                               21, 22, 23, 24};
static int model_rank(int m)
{
    int i;
    for (i = 0; i < M_KIT && MODEL_ORDER[i] != m; i++) {}
    return i;
}
static const char *const ENGINE_NAME[3] = {"BD", "SN", "HH"};
/* what the ENGINE knobs that pick (knob_zone) show: Plaits' chords (chord_bank.cc), m minor and M major as the
   scenes write them (scene_choir.c, scene_chip.c) */
static const char *const CHORD_NAME[11] = {"OCTAVE", "FIFTH", "SUS4", "m", "m7", "m9", "m11", "6/9", "M9", "M7", "M"};
/* ARCADE's arpeggios: Plaits' pattern p, direction p / 3, 1 << p % 3 octaves */
static const char *const ARP_NAME[12] = {"UP 1", "UP 2", "UP 4", "DOWN 1", "DOWN 2", "DOWN 4", "UP-DN 1", "UP-DN 2",
                                         "UP-DN 4", "RAND 1", "RAND 2", "RAND 4"};
static const char *const WORDS_NAME[6] = {"PHONEMES", "WORDS 1", "WORDS 2", "WORDS 3", "WORDS 4", "WORDS 5"};
/* HEX A-C's presets: the DX7 banks' names (syx_bank_0..2), cleaned; the brands and artists renamed */
static const char *const HEX_NAME[3][32] = {
    {"SOLID BAS", "DEEP LOW", "LEADTAPE", "ACID LINE", "BASS 3", "BILL BASS", "BASS 1", "ELEC BASS", "S.BAS 27.7",
     "RESONANT", "SYNBASS 2", "PRC SYN 1", "PRISMA 2", "ANALOG 4", "ANALOG A", "ANALOG 6", "BIG POLY", "INSERT 1",
     "SPIRAL", "TROTTER", "GASHAUS", "RING DING", "PAPAGAYO", "WINE GLAS", "AMYTAL", "VINTAGE 1", "WAVE V1",
     "WAVE V2", "VINTAGE 3", "VOCODER 2", "SEQUENCE", "BOUNCE 4"},
    {"E.PIANO 1", "TINES 1", "WINTER EP", "RS-EP C", "EP DELUXE", "CLAV EP", "SYN-CLAV", "CLAV", "PIANO 5",
     "GRAND 1", "CONCERT", "AC GUITAR", "SITAR", "KOTO", "HARPSI 1", "CLAV 3", "XYLO", "MARIMBA", "VIBE 1",
     "GLOCKEN", "BELL C", "BELLS", "TUB BELLS", "GONG 2", "KETTLE 6", "MID DRM 3", "ORI DRUM 1", "WOOD 6",
     "LATIN 4", "CIMBAL", "SYN DM 25", "BD-SNARE"},
    {"CLICK 124", "DRAWBAR", "E.ORGAN 3", "60S ORG", "OPTIC 28", "PIPES 1", "PIPES 3", "PIPES 2", "POLY 33",
     "SCORE", "ICE PAD 2", "WAVE PAD", "CLASSIC 2", "SOFT TCH", "PLANETS", "CIRRUS", "ENTRIX", "MAL POLY",
     "TEXTURE 6", "ETHER 5", "AIRY", "BORON A", "CINEMA 1", "STRINGS C", "STRINGS 3", "STRINGS 2", "STRINGS 7",
     "FULL STR", "SYN-ORCH", "BRASS 1", "BRASS 6 BC", "TRUMPET"}};
static const char *const DIV_NAME[DIV_N] = {"1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2", "1/1"};
static const char *const GATE_NAME[6] = {"1/8", "1/4", "1/2", "1", "2", "4"};   /* FLEET's GATE, in steps */
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
static const char *const SRC_NAME[SRC_N] = {"STEP", "ATLAS", "FLEET", "COLONY", "LOOM", "CADENCE", "LATTICE", "MOONS", "IMPROV", "CIPHER"};
/* Marbles' T models, its TGeneratorModel order (gen.c) */
static const char *const T_MODE_NAME[7] = {"C.BERN", "CLUSTERS", "DRUMS", "I.BERN", "DIVIDER", "3 STATES", "MARKOV"};
static const char *const PATH_NAME[PATH_N] = {"FWD", "REV", "PING-PONG", "SNAKE", "COLUMNS", "SPIRAL", "RANDOM", "XY"};
static const char *const TONIC_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const MODE_NAME[11] = {"CHROM", "MAJOR", "MINOR", "DORIAN", "PHRYG", "LYDIAN", "MIXO", "LOCRIAN",
                                          "HARM MIN", "PENTA", "MIN PENTA"};
static const char *const QUANT_NAME[3] = {"OFF", "50 %", "100 %"};
static const char *const CLICK_NAME[4] = {"OFF", "METRO", "COUNT", "BOTH"};
static const char *const KEYS_NAME[2] = {"OFF", "ALL"};
static const char *const ATTR_NAME[A_N] = {"VEL", "RATCHET", "PROB", "COND", "SLIDE", "ACCENT", "NOTE", "LEN", "MICRO"};
static const uint8_t ATTR_ROW[A_N] = {A_RATCHET, A_VEL, A_PROB, A_COND, A_NOTE, A_LEN, A_MICRO, A_SLIDE, A_ACCENT};
/* each black key's family of step functions, 0xFF none: a press picks the first, a press again the next (the empty ones
   are kept for the step components) */
#define X 0xFF
static const uint8_t FAMILY[11][2] = {{A_RATCHET, X}, {A_VEL, X}, {A_PROB, A_COND}, {A_NOTE, X}, {A_LEN, A_MICRO}, {A_SLIDE, X},
                                      {A_ACCENT, X}, {X, X}, {X, X}, {X, X}, {X, X}};
#undef X
static const char *const COND_NAME[5] = {"NONE", "FIRST", "!FIRST", "PRE", "!PRE"};
static const uint8_t WHITE[16] = PLAT_WHITE_KEYS;
static const uint8_t BLACK[11] = PLAT_BLACK_KEYS;
static const uint16_t KCOL[4] = {RGB(58, 109, 255), RGB(34, 213, 142), RGB(240, 240, 250), RGB(255, 122, 46)};

typedef struct { uint8_t model, pat, playing, mute, level; int8_t pos, len; } q_trow;
typedef struct { q_step st; uint8_t nlocks; q_lock lk[16]; } q_clip;   /* a step and its p-locks (lk[].step unused) */   /* pat, playing as in head */

/* what is on the screen, part by part: a part is drawn again when its view changes */
typedef struct {
    struct { int8_t track, domain, page, n, msg, pat, playing, scene, pad, ltrig, itype, punch, badge, bnum, model; uint8_t bpm; } head;   /* badge: REC's
                                                                                   state, 1 GRID .. 4 ARM (0 none or ARM's blink off); bnum: NOTES' step */   /* pat: P1..P8 asked; playing: the one playing when it differs, else 0;
                                                                                   pad: a KIT's in EDIT 1..16, else 0;
                                                                                   ltrig: LFO's FREE 1 / TRIG 2, else 0; itype: the insert's, in the page name */
    struct { int8_t pid; uint8_t col, hide, itype, model; int16_t v[4]; } foot;           /* col: the knobs shown in colour, the rest in grey (a lock page);
                                                       itype: the INSERT page's insert, else 0 */
    struct {
        int8_t pid, spage, cursor, pos, len, path, held, attr, showpath, xlen, ylen, src;   /* a generator: pos its rank, len its loop */
        uint16_t locks;                         /* the page's steps that have p-locks */
        uint16_t sel;                           /* the page's steps selected */
        uint16_t notes;                         /* NOTES: the allowed pitch classes */
        q_step hst;                             /* the held step */
        int16_t v[4];
        uint8_t model, pad;                     /* pad: KIT, the pad its knobs set */
        uint8_t eng[Q_PADS];                    /* KIT: the pads' engines */
        uint8_t chr[Q_PADS];                    /* KIT: the pads' CHAR */
        q_step st[16];
        int32_t cpu[8];                         /* the CPU page's numbers as shown; core 2 -1: OFF */
        uint8_t anim;                           /* an animated scene, redrawn as it moves: 0 none, 1 the sound's scene (v: its ENGINE
                                                   knobs), 2 the SEQ plateau, 3 MARBLES (v: SPREAD, X BIAS, DEJA VU), 4 GRIDS,
                                                   5 HOME's tracks (scene_home.c), 6 a sound page ADSR .. MIX (scene_sound.c),
                                                   7 TEMPO, SCALE, KEYS, FX, SONG (scene_glo.c), 8 LIFE (scene_life.c),
                                                   9 LANES (scene_lanes.c), 10 CHORDS (scene_chords.c),
                                                   11 TONNETZ (scene_tonnetz.c), 12 ORBITS (scene_orbits.c),
                                                   13 MIMIC (scene_mimic.c) */
        q_trow tr[Q_TRACKS];                    /* TRACKS */
        int8_t layer, lmode;                    /* a layer's map instead of the scene: its button + 1; REC in LIVE, SEL in NOTES */
        uint32_t lkeys, hot;                    /* its keys, and the keys pressed */
    } scene;
} q_view;

static struct {
    q_project *p;
    int domain, page[ND], track, octave, spage, rec, cursor, dirty, entry;   /* entry: the song cursor 1..nsong, 0 none */
    int held, held_key, edited, attr, lpage, nwrote, seqdown, arpdown;   /* held: the step held 1..64, 0 none; lpage: the held step's p-lock page (LPAGE);
                                                                   nwrote: notes of the chord written since the hold began */
    int pad, padheld;                          /* a KIT: its last pad played 0..15; the held pad's key + 1, 0 none */
    int homedown, savedown, seldown, msg_scene;   /* msg_scene: 1..8 asked */
    int fxdown, msg_punch;                     /* msg_punch: the punch-in last pressed 1..16, shown until punch_until */
    uint16_t punch;                            /* the punch-ins held under FX, bit k = white key k + 1 */
    uint8_t last_note[Q_TRACKS];               /* a step switched on by hand takes it */
    int8_t key_note[NKEYS], key_track[NKEYS];  /* what each held key sounds, for its note-off; -1 none */
    uint32_t btn, keys, msg_until, locks_until, path_until, punch_until;
    uint32_t wipe_until;                       /* SAVE held, black key 1 pressed once: a second press before it erases the project */
    uint32_t rng, rnd_until;                   /* RANDOM: its xorshift; its message shown until rnd_until */
    int rnd_kind;                              /* the message: 1 RANDOM PAGE, 2 RANDOM ALL, 3 RANDOM MODEL */
    uint32_t tap_ms[4], tap_until;             /* TAP TEMPO: the last taps' times (ntap of them); its message until tap_until */
    int ntap;
    uint32_t down_ms[NB], used;                /* when each button went down; used: bit b, a key or button came while b was held */
    q_clip clip;                               /* the grid's clipboard, common to the tracks: the last step cut or chord played */
    uint8_t preview[4];
    uint16_t sel;                              /* SEL held on the grid: the page's cells selected */
    int sel_ref;                               /* the selection's reference cell, the first pressed; -1 none */                        /* the held step's notes sounding at the stop, 0 none */
    int arm_key;
    int nchord;                                /* NOTES: notes written since all the keys were up */
    int erase_pos;                             /* LIVE, REC held: the last step erased, -1 none */
    uint8_t erase_notes[8];                    /* the notes of the keys held then, 0 none */
    int lane, cell, cell_key, cell_edited;     /* LANES: the lane edited (L_*); the cell held 1..16, 0 none, its key */
    int cell_lane;                             /* the lane of the cell last touched */
    int moon;                                  /* ORBITS: the moon MOON sets, 0..moons - 1 */
    uint32_t listen_ms;                        /* LISTEN: the last note learnt's time, its pattern; listen_n notes so far */
    int listen_track, listen_pat, listen_n;
    int chcur, tzcur;                          /* CHORDS, TONNETZ: PRESET's cursor, 0..len - 1 */
    int draws;                                 /* MARBLES: new draws asked (a probe) */
    int cell_flash;                            /* the cell last touched 1..16 (0 none) and when, for the scene */
    uint32_t cell_ms;
    int layer;                                 /* the layer whose map is shown: its button + 1, 0 none */                               /* ARM: the key that started the playing, played once step 1 is under way (key + 1) */
    q_view shown;
    int full;                                  /* the next frame draws everything */
    uint32_t scene_ms, fps_ms;                 /* the last scene drawn (every other 16-ms frame: 30 a second); the second being counted */
    uint32_t notes_seen;                       /* MARBLES: the track's notes already dropped (audio_notes) */
    int scene_key;                             /* the animated scene last drawn: anim, track, pattern, page of steps */
    int frames, fps, fps_mixed;                /* scene frames this second; the last whole second's; a frame without the scene */
} ui;

static q_pattern *pat(void) { return &ui.p->pat[ui.track][ui.p->pattern[ui.track]]; }
static int notes_on(void);
static int cond_index(int c);
static void step_on(q_step *s);
static void step_default(q_step *s);
static void attr_turn(q_step *s, int a, int d);
static const uint8_t SN_ATTR[3][4] = {{A_NOTE, A_LEN, A_VEL, A_PROB}, {A_RATCHET, A_COND, A_MICRO}, {A_SLIDE, A_ACCENT}};   /* P_SN1..3 */
static int kit(void) { return ui.p->sound[ui.track].model == M_KIT; }

/* the domain's pages: their number, their ids in out (5 always written: a page index not yet brought back in range
   still names a page) */
static int pages(int d, uint8_t *out)
{
    const q_pattern *t = pat();
    if (d == D_SEQ && t->src != SRC_STEP) {
        __builtin_memcpy(out, GEN_PAGES[t->src - 1], 5);
        if (t->src == SRC_LIFE && kit())
            out[0] = P_LIFEK;
        if (t->src == SRC_GRIDS && kit())
            out[0] = P_GRIDSK;
        if (t->src == SRC_CIPHER && kit())
            out[0] = P_CIPHERK;
        if (t->src == SRC_ORBITS && kit())
            out[1] = P_MOONK;
        return t->src == SRC_TONNETZ ? 4 : t->src == SRC_CHORDS ? 3 : t->src == SRC_ORBITS || t->src == SRC_LANES || t->src == SRC_MIMIC ? 2 : t->src != SRC_MARBLES ? 3 : kit() ? 2 : 4;   /* a KIT has no X section */
    }
    if (d == D_SEQ && notes_on()) {     /* the cursor's step first, then SEQ's own */
        __builtin_memcpy(out, NOTES_PAGES, 5);
        return t->path == PATH_XY ? 5 : 4;
    }
    if (d == D_EDIT && kit()) {
        __builtin_memcpy(out, KIT_PAGES, 5);
        return 2;
    }
    __builtin_memcpy(out, DOMAIN_PAGES[d], 5);
    return d == D_SEQ && t->path == PATH_XY ? 2 : DOMAIN_N[d];
}

static int domain_n(int d)
{
    uint8_t id[5];
    return pages(d, id);
}

static int page_id(void)
{
    uint8_t id[5];
    pages(ui.domain, id);
    return id[ui.page[ui.domain]];
}

/* the page K1-K4 set, a step held aside: a KIT's pad held in EDIT gives its PAD page */
static int knob_page(void) { return ui.domain == D_EDIT && ui.padheld && kit() ? P_PAD : page_id(); }

/* the engines with their own envelope: the ADSR shapes only their filter (6-OP, STRING, MODAL, the drums, the KIT) */
static int own_env(int m) { return (m >= 2 && m <= 4) || m >= 19; }

/* a KIT: no T output to pick; no arp either; an engine with its own envelope: no DECAY (the LPG it would set is bypassed) */
static int knob_hidden(int pid, int k)
{
    return (kit() && ((pid == P_MARBLES && k == 1) || pid == P_ARP))   /* a KIT has no arp */
           || (pid == P_INSERT && k < 3 && !*INS_LABEL[ui.p->sound[ui.track].itype][k])    /* no role: turns ignored */
           || (pid == P_SN1 && !k && kit())                                                 /* a KIT's pads stay */
           || (pid == P_ENGINE && k == 3 && own_env(ui.p->sound[ui.track].model))
           || (pid == P_CPARAM && k < 3 && k >= CIPHER_DEF[pat()->cipher.formula].np)   /* a number the formula has not */
           || (kit() && (pid == P_LANES || pid == P_LENS || pid == P_ORBITS) && k == 1);                       /* a KIT: no base note, no OCT */
}

static int knob_get(int pid, int k)
{
    const q_sound *s = &ui.p->sound[ui.track];
    const q_pattern *t = &ui.p->pat[ui.track][ui.p->pattern[ui.track]];
    const q_pad *pd = &ui.p->kit[ui.track][ui.pad];
    const q_step *st = &t->steps[ui.cursor - 1];
    switch (pid * 4 + k) {
    case P_SN1 * 4: return st->note[0];
    case P_SN1 * 4 + 1: return st->len;
    case P_SN1 * 4 + 2: return st->vel;
    case P_SN1 * 4 + 3: return st->prob;
    case P_SN2 * 4: return STEP_RATCHET(st);
    case P_SN2 * 4 + 1: return cond_index(st->cond);
    case P_SN2 * 4 + 2: return st->micro;
    case P_SN3 * 4: return st->flags & SF_SLIDE ? 1 : 0;
    case P_SN3 * 4 + 1: return st->flags & SF_ACCENT ? 1 : 0;
    case P_ENGINE * 4: return s->harm;
    case P_ENGINE * 4 + 1: return s->timb;
    case P_ENGINE * 4 + 2: return s->morph;
    case P_ENGINE * 4 + 3: return s->decay;
    case P_SOUND * 4:
    case P_KIT * 4: return model_rank(s->model);
    case P_SOUND * 4 + 1: return s->tune;
    case P_SOUND * 4 + 2: return s->glide;
    case P_SOUND * 4 + 3: return s->poly;
    case P_STEPS * 4: return t->len;
    case P_STEPS * 4 + 1: return t->div;
    case P_STEPS * 4 + 2: return t->path;
    case P_STEPS * 4 + 3:
    case P_GRIDS * 4 + 3:
    case P_GRIDSK * 4 + 3:
    case P_MARBLES * 4 + 3:
    case P_LIFE * 4 + 3:
    case P_LIFEK * 4 + 3:
    case P_LANES * 4 + 3:
    case P_CHORDS * 4 + 3:
    case P_TONNETZ * 4 + 3:
    case P_ORBITS * 4 + 3:
    case P_MIMIC * 4 + 3:
    case P_CIPHER * 4 + 3:
    case P_CIPHERK * 4 + 3: return t->src;
    case P_GRIDS * 4:
    case P_GRIDSK * 4:
    case P_MARBLES * 4:
    case P_LIFE * 4:
    case P_LIFEK * 4:
    case P_LANES * 4:
    case P_CHORDS * 4:
    case P_TONNETZ * 4:
    case P_ORBITS * 4:
    case P_MIMIC * 4:
    case P_CIPHER * 4:
    case P_CIPHERK * 4: return t->div;
    case P_CHORD * 4: return CH_DEG(t->chords.ch[ui.chcur]);
    case P_CHORD * 4 + 1: return CH_SIZE(t->chords.ch[ui.chcur]);
    case P_TZSTEP * 4: return TZ_OP(&t->tonnetz, ui.tzcur);
    case P_ORBITS * 4 + 1: return t->orbits.note;
    case P_ORBITS * 4 + 2: return t->orbits.moons;
    case P_MIMIC * 4 + 1: return t->mimic.order;
    case P_MIMIC * 4 + 2: return t->mimic.drift;
    case P_LOOP * 4: return t->mimic.loop;
    case P_CIPHER * 4 + 1:
    case P_CIPHERK * 4 + 1: return t->cipher.formula;
    case P_CIPHER * 4 + 2: return t->cipher.note;
    case P_CPARAM * 4: return t->cipher.a;
    case P_CPARAM * 4 + 1: return t->cipher.b;
    case P_CPARAM * 4 + 2: return t->cipher.c;
    case P_CPARAM * 4 + 3: return t->cipher.dens;
    case P_CTIME * 4: return t->cipher.len;
    case P_CTIME * 4 + 1: return t->cipher.start;
    case P_CTIME * 4 + 2: return t->cipher.speed;
    case P_CTIME * 4 + 3: return t->cipher.range;
    case P_MOON * 4:
    case P_MOONK * 4: return t->orbits.per[ui.moon];
    case P_MOON * 4 + 1:
    case P_MOONK * 4 + 1: return t->orbits.pha[ui.moon];
    case P_MOON * 4 + 2:
    case P_MOONK * 4 + 2: return t->orbits.val[ui.moon];
    case P_TONNETZ * 4 + 1: return t->tonnetz.note;
    case P_TONNETZ * 4 + 2: return t->tonnetz.rate;
    case P_WALK * 4: return t->tonnetz.len;
    case P_WALK * 4 + 1: return t->tonnetz.chaos;
    case P_WALK * 4 + 2: return t->tonnetz.scale;
    case P_WALK * 4 + 3: return t->tonnetz.ret;
    case P_PLAY * 4: return t->tonnetz.strum;
    case P_PLAY * 4 + 1: return t->tonnetz.voice;
    case P_CHORDS * 4 + 1: return t->chords.note;
    case P_CHORDS * 4 + 2: return t->chords.rate;
    case P_PROG * 4: return t->chords.len;
    case P_PROG * 4 + 1: return t->chords.strum;
    case P_PROG * 4 + 2: return t->chords.voice;
    case P_LANES * 4 + 1: return t->lanes.note;
    case P_LANES * 4 + 2: return lanes_len(&t->lanes, L_TRIG);
    case P_LENS * 4:
    case P_LENS * 4 + 1:
    case P_LENS * 4 + 2:
    case P_LENS * 4 + 3: return lanes_len(&t->lanes, 1 + k);
    case P_LIFE * 4 + 1:
    case P_LIFEK * 4 + 1: return t->life.poly;
    case P_LIFE * 4 + 2: return t->life.note;
    case P_LIFEK * 4 + 2: return t->life.map;
    case P_SEED * 4: return t->life.seed;
    case P_SEED * 4 + 1: return t->life.dens;
    case P_SEED * 4 + 2: return t->life.autos;
    case P_EVOL * 4: return t->life.gen;
    case P_EVOL * 4 + 1: return t->life.mut;
    case P_GRIDS * 4 + 1:
    case P_GRIDSK * 4 + 1: return t->grids.voice;
    case P_GRIDS * 4 + 2: return t->grids.note;
    case P_GRIDSK * 4 + 2: return t->grids.pad[t->grids.voice - 1];
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
    case P_X * 4 + 3: return t->marbles.gate;
    case P_NOTES * 4: return t->marbles.note;
    case P_NOTES * 4 + 1: return t->marbles.range;
    case P_PAD * 4: return pd->tune;
    case P_PAD * 4 + 1: return pd->harm;
    case P_PAD * 4 + 2: return pd->timb;
    case P_PAD * 4 + 3: return pd->decay;
    case P_KIT * 4 + 1: return pd->engine;
    case P_KIT * 4 + 2: return pd->level;
    case P_KIT * 4 + 3: return pd->chr;
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
    case P_KEYS * 4: return ui.p->keys;
    case P_XY * 4: return t->xlen;
    case P_XY * 4 + 1: return t->div;
    case P_XY * 4 + 2: return t->ylen;
    case P_XY * 4 + 3: return t->ydiv;
    case P_TRACKS * 4:
    case P_TRACKS * 4 + 1:
    case P_TRACKS * 4 + 2:
    case P_TRACKS * 4 + 3: return ui.p->sound[k].level;
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
    if (pid >= P_SN1 && pid <= P_SN3) {                 /* the cursor's step, made when it was empty */
        q_step *st = &t->steps[ui.cursor - 1];
        if (!(st->flags & SF_ON)) {
            step_default(st);
            lock_clear_step(t, ui.cursor - 1);
        }
        attr_turn(st, SN_ATTR[pid - P_SN1][k], v - knob_get(pid, k));
        step_on(st);
        ui.dirty = 1;
        return;
    }
    switch (pid * 4 + k) {
    case P_ENGINE * 4: s->harm = (uint8_t)v; break;
    case P_ENGINE * 4 + 1: s->timb = (uint8_t)v; break;
    case P_ENGINE * 4 + 2: s->morph = (uint8_t)v; break;
    case P_ENGINE * 4 + 3: s->decay = (uint8_t)v; break;
    case P_SOUND * 4:
    case P_KIT * 4: s->model = MODEL_ORDER[v]; audio_track_model(ui.track); break;
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
    case P_GRIDSK * 4:
    case P_MARBLES * 4:
    case P_LIFE * 4:
    case P_LIFEK * 4:
    case P_LANES * 4:
    case P_CHORDS * 4:
    case P_TONNETZ * 4:
    case P_ORBITS * 4:
    case P_MIMIC * 4:
    case P_CIPHER * 4:
    case P_CIPHERK * 4: t->div = (uint8_t)v; break;
    case P_CHORD * 4: t->chords.ch[ui.chcur] = CH_MAKE(v, CH_SIZE(t->chords.ch[ui.chcur])); break;
    case P_CHORD * 4 + 1: t->chords.ch[ui.chcur] = CH_MAKE(CH_DEG(t->chords.ch[ui.chcur]), v); break;
    case P_TZSTEP * 4: TZ_SET(&t->tonnetz, ui.tzcur, v); break;
    case P_ORBITS * 4 + 1: t->orbits.note = (uint8_t)v; break;
    case P_MIMIC * 4 + 1: t->mimic.order = (uint8_t)v; break;
    case P_MIMIC * 4 + 2: t->mimic.drift = (uint8_t)v; break;
    case P_LOOP * 4: t->mimic.loop = (uint8_t)v; break;
    case P_CIPHER * 4 + 1:
    case P_CIPHERK * 4 + 1: t->cipher.formula = (uint8_t)v; break;
    case P_CIPHER * 4 + 2: t->cipher.note = (uint8_t)v; break;
    case P_CPARAM * 4: t->cipher.a = (uint8_t)v; break;
    case P_CPARAM * 4 + 1: t->cipher.b = (uint8_t)v; break;
    case P_CPARAM * 4 + 2: t->cipher.c = (uint8_t)v; break;
    case P_CPARAM * 4 + 3: t->cipher.dens = (uint8_t)v; break;
    case P_CTIME * 4: t->cipher.len = (uint8_t)v; break;
    case P_CTIME * 4 + 1: t->cipher.start = (uint8_t)v; break;
    case P_CTIME * 4 + 2: t->cipher.speed = (uint8_t)v; break;
    case P_CTIME * 4 + 3: t->cipher.range = (uint8_t)v; break;
    case P_ORBITS * 4 + 2:
        t->orbits.moons = (uint8_t)v;
        if (ui.moon >= v) ui.moon = v - 1;   /* fewer moons: the one set among them */
        break;
    case P_MOON * 4:
    case P_MOONK * 4: t->orbits.per[ui.moon] = (uint8_t)v; break;
    case P_MOON * 4 + 1:
    case P_MOONK * 4 + 1: t->orbits.pha[ui.moon] = (uint8_t)v; break;
    case P_MOON * 4 + 2:
    case P_MOONK * 4 + 2: t->orbits.val[ui.moon] = (uint8_t)v; break;
    case P_TONNETZ * 4 + 1: t->tonnetz.note = (uint8_t)v; break;
    case P_TONNETZ * 4 + 2: t->tonnetz.rate = (uint8_t)v; break;
    case P_WALK * 4: t->tonnetz.len = (uint8_t)v; break;
    case P_WALK * 4 + 1: t->tonnetz.chaos = (uint8_t)v; break;
    case P_WALK * 4 + 2: t->tonnetz.scale = (uint8_t)v; break;
    case P_WALK * 4 + 3: t->tonnetz.ret = (uint8_t)v; break;
    case P_PLAY * 4: t->tonnetz.strum = (uint8_t)v; break;
    case P_PLAY * 4 + 1: t->tonnetz.voice = (uint8_t)v; break;
    case P_CHORDS * 4 + 1: t->chords.note = (uint8_t)v; break;
    case P_CHORDS * 4 + 2: t->chords.rate = (uint8_t)v; break;
    case P_PROG * 4: t->chords.len = (uint8_t)v; break;
    case P_PROG * 4 + 1: t->chords.strum = (uint8_t)v; break;
    case P_PROG * 4 + 2: t->chords.voice = (uint8_t)v; break;
    case P_LANES * 4 + 1: t->lanes.note = (uint8_t)v; break;
    case P_LANES * 4 + 2: lanes_set_len(&t->lanes, L_TRIG, v); break;
    case P_LENS * 4:
    case P_LENS * 4 + 1:
    case P_LENS * 4 + 2:
    case P_LENS * 4 + 3: lanes_set_len(&t->lanes, 1 + k, v); break;
    case P_LIFE * 4 + 1:
    case P_LIFEK * 4 + 1: t->life.poly = (uint8_t)v; break;
    case P_LIFE * 4 + 2: t->life.note = (uint8_t)v; break;
    case P_LIFEK * 4 + 2: t->life.map = (uint8_t)v; break;
    case P_SEED * 4: t->life.seed = (uint8_t)v; break;
    case P_SEED * 4 + 1: t->life.dens = (uint8_t)v; break;
    case P_SEED * 4 + 2: t->life.autos = (uint8_t)v; break;
    case P_EVOL * 4: t->life.gen = (uint8_t)v; break;
    case P_EVOL * 4 + 1: t->life.mut = (uint8_t)v; break;
    case P_STEPS * 4 + 2:
        t->path = (uint8_t)v;
        ui.path_until = plat_ms() + MSG_MS;     /* the path is drawn while it is being chosen */
        break;
    case P_STEPS * 4 + 3:               /* the steps and the generators' settings stay */
    case P_GRIDS * 4 + 3:
    case P_GRIDSK * 4 + 3:
    case P_MARBLES * 4 + 3:
    case P_LIFE * 4 + 3:
    case P_LIFEK * 4 + 3:
    case P_LANES * 4 + 3:
    case P_CHORDS * 4 + 3:
    case P_TONNETZ * 4 + 3:
    case P_ORBITS * 4 + 3:
    case P_MIMIC * 4 + 3:
    case P_CIPHER * 4 + 3:
    case P_CIPHERK * 4 + 3:
        t->src = (uint8_t)v;
        if ((v == SRC_CHORDS || v == SRC_TONNETZ || v == SRC_ORBITS || v == SRC_MIMIC) && !kit() && s->poly < 3)   /* chords, conjunctions: POLY 3 at least */
            s->poly = 3;
        break;
    case P_GRIDS * 4 + 1:
    case P_GRIDSK * 4 + 1: t->grids.voice = (uint8_t)v; break;
    case P_GRIDS * 4 + 2: t->grids.note = (uint8_t)v; break;
    case P_GRIDSK * 4 + 2: t->grids.pad[t->grids.voice - 1] = (uint8_t)v; break;
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
    case P_X * 4 + 3: t->marbles.gate = (uint8_t)v; break;
    case P_NOTES * 4: t->marbles.note = (uint8_t)v; break;
    case P_NOTES * 4 + 1: t->marbles.range = (uint8_t)v; break;
    case P_PAD * 4: pd->tune = (int8_t)v; break;
    case P_PAD * 4 + 1: pd->harm = (uint8_t)v; break;
    case P_PAD * 4 + 2: pd->timb = (uint8_t)v; break;
    case P_PAD * 4 + 3: pd->decay = (uint8_t)v; break;
    case P_KIT * 4 + 1: pd->engine = (uint8_t)v; break;     /* the render takes it at the pad's next hit */
    case P_KIT * 4 + 2: pd->level = (uint8_t)v; break;
    case P_KIT * 4 + 3: pd->chr = (uint8_t)v; break;
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
    case P_KEYS * 4: ui.p->keys = (uint8_t)v; break;
    case P_XY * 4: t->xlen = (uint8_t)v; break;
    case P_XY * 4 + 1: t->div = (uint8_t)v; break;
    case P_XY * 4 + 2: t->ylen = (uint8_t)v; break;
    case P_XY * 4 + 3: t->ydiv = (uint8_t)v; break;
    case P_TRACKS * 4:
    case P_TRACKS * 4 + 1:
    case P_TRACKS * 4 + 2:
    case P_TRACKS * 4 + 3: ui.p->sound[k].level = (uint8_t)v; break;
    case P_SONG * 4: ui.entry = v; return;
    case P_SONG * 4 + 1: ui.p->song[ui.entry - 1].scene = (uint8_t)(v - 1); break;
    case P_SONG * 4 + 2: ui.p->song[ui.entry - 1].reps = (uint8_t)v; break;
    case P_SONG * 4 + 3: ui.p->song_on = (uint8_t)v; break;
    }
    ui.dirty = 1;
}

/* the ENGINE knobs that pick (a preset, a chord, an arpeggio, a word bank): n choices from start, width wide each, as
   Plaits' quantizers cut them (six_op_engine.cc, chord_bank.cc, chiptune_engine.cc, speech_engine.cc); 0 a plain knob */
static int knob_zone(int model, int pid, int k, float *start, float *width)
{
    *start = 0.f;
    if (pid != P_ENGINE)
        return 0;
    if (!k && model >= 2 && model <= 4)                         /* HEX A-C: 32 presets */
        return *width = 127.f / 32.f / 1.02f, 32;
    if (!k && (model == 6 || model == 7 || model == 14))       /* ENSEMBLE, ARCADE, CHORALE: 11 chords */
        return *width = 127.f / 11.f / 1.02f, 11;
    if (k == 1 && model == 7)                                  /* ARCADE: 12 arpeggios */
        return *width = 127.f / 12.f, 12;
    if (!k && model == 15) {                                   /* ORATOR: the synths' blend up to 42, then 6 banks */
        *start = 127.f / 3.f;
        return *width = 127.f / 6.f / 1.65f, 6;
    }
    return 0;
}

static int knob_choice(int v, float start, float width, int n)
{
    int c = (int)__builtin_floorf((v - start) / width);
    return c < 0 ? 0 : c > n - 1 ? n - 1 : c;
}

/* v turned d detents: one choice a detent on a knob that picks, at its zone's middle; v + d on a plain one */
static int knob_step(int model, int pid, int k, int v, int d)
{
    float start, width;
    int n = knob_zone(model, pid, k, &start, &width), c;
    if (!n || (start > 0.f && v < start && v + d < start))   /* ORATOR's blend of synths stays plain */
        return v + d;
    if (start > 0.f && v < start)               /* up out of the blend: the detents left past 42 from the first bank */
        c = d - ((int)start - v) - 1;
    else if ((c = knob_choice(v, start, width, n) + d) < 0 && start > 0.f)
        return (int)start + c + 1;              /* down out of the banks: 42 (LPC), then the detents left */
    c = c < 0 ? 0 : c > n - 1 ? n - 1 : c;
    if (v >= start && c == knob_choice(v, start, width, n))   /* past an end: the value stays (127 does not drop) */
        return v;
    return (int)__builtin_roundf(start + (c + 0.5f) * width);
}

static void knob_turn(int pid, int k, int d)
{
    const q_page *pg = &PAGE[pid];
    int v;
    if (!d || !pg->label[k] || knob_hidden(pid, k))
        return;
    if (pid == P_SONG && k < 3 && !ui.entry)    /* no entries: nothing to edit */
        return;
    v = clamp(knob_step(ui.p->sound[ui.track].model, pid, k, knob_get(pid, k), d), pg->min[k],
              pid == P_SONG && !k ? ui.p->nsong : pg->max[k]);
    if (v != knob_get(pid, k))
        knob_set(pid, k, v);
}

/* ------------------------------------------------------------------ input --- */
static void rec_set(int r);
static void sel_clear(void);
static uint16_t sel_others(void);
static int layer_shown(void);

/* SEQ's white keys are its steps: REC on the grid, a STEP pattern */
static int grid_on(void) { return ui.domain == D_SEQ && ui.rec == R_GRID && pat()->src == SRC_STEP; }

/* LANES in SEQ: PRESET picks the lane, the grid's white keys are its cells */
static int lanes_on(void) { return ui.domain == D_SEQ && pat()->src == SRC_LANES; }
static const uint8_t LANE_MAX[L_N] = { 1, 15, 4, 4, 3 };
/* CHORDS in SEQ: the grid's white keys 1-8 are its chords */
static int chords_on(void) { return ui.domain == D_SEQ && pat()->src == SRC_CHORDS; }
/* TONNETZ in SEQ: the grid's white keys 1-8 are its steps */
static int tonnetz_on(void) { return ui.domain == D_SEQ && pat()->src == SRC_TONNETZ; }
/* the degree 0..6 whose note has note's pitch class, -1 out of the scale */
static int chord_degree(const q_chords *c, int note)
{
    for (int d = 0; d < 7; d++)
        if (gen_life_note(c->note, gen_scale(ui.p), ui.p->tonic, d) % 12 == note % 12)
            return d;
    return -1;
}

/* NOTES: the keys write the cursor's step (GRID out of SEQ is NOTES); in SEQ a STEP pattern, elsewhere stopped */
static int notes_on(void)
{
    return (ui.rec == R_NOTES || (ui.rec == R_GRID && ui.domain != D_SEQ)) && pat()->src == SRC_STEP &&
           (ui.domain == D_SEQ || !audio_playing());
}

/* a length by OCT: the next whole step up (d > 0) or down, 1 to 8 steps; up from a step or less it ties over the next
   (2 steps: a step made by hand lasts half of one) */
static int len_step(int len, int d)
{
    int up = len < 8 ? 16 : (len / 8 + 1) * 8;
    return d > 0 ? up > 64 ? 64 : up : len <= 8 ? len : ((len - 1) / 8) * 8;
}

static void button(int b)
{
    int d;
    for (d = 0; d < ND; d++)
        if (b == DOMAIN_BTN[d]) {
            if (ui.domain != d) {
                ui.held = 0;
                sel_clear();
                ui.nchord = 0;          /* the keys still down only sound */
            }
            ui.domain = d;
        }
    switch (b) {
    case B_PLAY:
        if (ui.btn >> B_REC & 1u) {     /* REC held: LIVE, at once while playing, else after the count-in or at the first note */
            if (audio_playing()) {
                rec_set(R_LIVE);
            } else if (ui.p->click & 2) {
                rec_set(R_LIVE);
                audio_play(1);
            } else {
                rec_set(R_ARM);
            }
        } else if (audio_playing()) {   /* the stop ends LIVE; GRID and NOTES stay */
            if (ui.rec == R_LIVE || ui.rec == R_ARM)
                rec_set(R_OFF);
            audio_play(0);
        } else {                        /* NOTES out of SEQ starts LIVE; armed before the start: the count-in */
            if (ui.rec == R_ARM || ((ui.rec == R_NOTES || ui.rec == R_GRID) && ui.domain != D_SEQ))
                rec_set(R_LIVE);
            audio_play(1);
        }
        ui.msg_scene = 0;
        break;
    case B_SEL:                         /* the SONG page: clears the entry (SEL with HOME held is for the scenes) */
        if (ui.domain == D_HOME && page_id() == P_SONG && !ui.homedown && ui.entry && !notes_on()) {   /* (NOTES: SEL is
                                                                                                    the cursor's) */
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
        if (ui.rec == R_LIVE && ui.btn >> B_REC & 1u) {    /* REC held in LIVE: OCT- erases (live_erase) */
        } else if (grid_on() && ui.held) {     /* the held step's length */
            q_step *s = &pat()->steps[ui.held - 1];
            s->len = (uint8_t)len_step(s->len, d);
            ui.edited = ui.dirty = 1;
        } else if (grid_on() && ui.sel_ref >= 0) {     /* the selection's: each its own */
            q_pattern *t = pat();
            uint16_t m = sel_others();
            int c, ref = ui.spage * 16 + ui.sel_ref;
            if (ref < t->len && t->steps[ref].flags & SF_ON)
                m |= (uint16_t)(1u << ui.sel_ref);
            for (c = 0; c < 16; c++)
                if (m >> c & 1u)
                    t->steps[ui.spage * 16 + c].len = (uint8_t)len_step(t->steps[ui.spage * 16 + c].len, d);
            ui.dirty = 1;
        } else if (grid_on()) {
            ui.spage = clamp(ui.spage + d, 0, Q_STEPS / 16 - 1);
        }
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

static void step_default(q_step *s)
{
    __builtin_memset(s, 0, sizeof *s);
    s->note[0] = 60;
    s->vel = 100;
    s->len = 4;
    s->prob = 20;
}

static void step_clear(q_pattern *t, int n)
{
    q_step *s = &t->steps[n];
    s->flags &= (uint8_t)~SF_ON;
    __asm__ volatile("" ::: "memory");
    step_default(s);
    lock_clear_step(t, n);
}

/* the step's attribute a turned by d (PRESET on a held step, NOTES' knobs); SLIDE and ACCENT: on to the right, off to the
   left */
static void attr_turn(q_step *s, int a, int d)
{
    int i, lo = 127, hi = 0;
    switch (a) {
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
    case A_SLIDE:
    case A_ACCENT:
        if (d)
            s->flags = (uint8_t)(d > 0 ? s->flags | (a == A_SLIDE ? SF_SLIDE : SF_ACCENT) : s->flags & ~(a == A_SLIDE ? SF_SLIDE : SF_ACCENT));
        break;
    }
}

/* the attribute's value from src to dst */
static void attr_copy(q_step *dst, const q_step *src, int attr)
{
    switch (attr) {
    case A_VEL: dst->vel = src->vel; break;
    case A_RATCHET: dst->flags = (uint8_t)((dst->flags & ~0x18) | (src->flags & 0x18)); break;
    case A_PROB: dst->prob = src->prob; break;
    case A_COND: dst->cond = src->cond; break;
    case A_SLIDE: dst->flags = (uint8_t)((dst->flags & ~SF_SLIDE) | (src->flags & SF_SLIDE)); break;
    case A_ACCENT: dst->flags = (uint8_t)((dst->flags & ~SF_ACCENT) | (src->flags & SF_ACCENT)); break;
    case A_NOTE:                        /* a KIT's pads stay, as attr_turn leaves them */
        if (!kit())
            __builtin_memcpy(dst->note, src->note, sizeof dst->note);
        break;
    case A_LEN: dst->len = src->len; break;
    case A_MICRO: dst->micro = src->micro; break;
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

static int lock_value(const q_pattern *t, int step, int k, int *locked)
{
    int param = lock_param(k), i = lock_find(t, step, param);
    *locked = i >= 0;
    if (i < 0)
        return knob_get(lock_pid(), k);
    return param == LK_TUNE || param == LK_ENVAMT || param == LK_DEPTH || param == LK_PAN ? (int8_t)t->locks[i].value : t->locks[i].value;
}

static void lock_turn(int step, int k, int d)
{
    q_pattern *t = pat();
    int pid = lock_pid(), param = lock_param(k), locked, v;
    if (!d || param < 0 || knob_hidden(pid, k))
        return;
    v = clamp(knob_step(ui.p->sound[ui.track].model, pid, k, lock_value(t, step, k, &locked), d), PAGE[pid].min[k],
              PAGE[pid].max[k]);
    ui.edited = 1;                      /* a knob turned on a held step is an edit, the lock fitting or not */
    if (lock_set(t, step, param, v)) {
        ui.locks_until = plat_ms() + MSG_MS;
        return;
    }
    step_on(&t->steps[step]);
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
    ui.held = ui.cell = 0;              /* the hold was another pattern's step or cell: its release toggles nothing */
    sel_clear();
    ui.nchord = 0;                      /* the keys still down only sound */
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

/* the clipboard: a chord played, at the defaults of a step made by hand */
static void clip_notes(const uint8_t *notes, int n)
{
    step_default(&ui.clip.st);
    __builtin_memcpy(ui.clip.st.note, notes, (uint32_t)n);
    ui.clip.nlocks = 0;
}

/* the step and its p-locks into the clipboard, the step emptied */
static void clip_cut(int step)
{
    q_pattern *t = pat();
    int i;
    ui.clip.st = t->steps[step];
    ui.clip.st.flags &= (uint8_t)~SF_ON;    /* pasted, the step lights once it is whole */
    ui.clip.nlocks = 0;
    for (i = 0; i < t->nlocks; i++)
        if (t->locks[i].step == step && ui.clip.nlocks < 16)
            ui.clip.lk[ui.clip.nlocks++] = t->locks[i];
    step_clear(t, step);
    ui.dirty = 1;
}

/* the clipboard onto an empty step: its notes up to POLY (a KIT: pad 1 when it has none), its p-locks while they fit */
static void clip_paste(int step)
{
    q_pattern *t = pat();
    q_step *s = &t->steps[step];
    int i, poly = kit() ? 4 : ui.p->sound[ui.track].poly;
    *s = ui.clip.st;
    for (i = poly; i < 4; i++)
        s->note[i] = 0;
    if (kit()) {                        /* its pads only */
        uint8_t pads[4] = {0};
        int m = 0;
        for (i = 0; i < 4 && (i == 0 || s->note[i]); i++)
            if (s->note[i] >= KIT_NOTE && s->note[i] < KIT_NOTE + Q_PADS)
                pads[m++] = s->note[i];
        if (!m)
            pads[0] = KIT_NOTE;
        __builtin_memcpy(s->note, pads, sizeof pads);
    }
    lock_clear_step(t, step);           /* an empty step may keep p-locks (a live erase, a KIT's last pad out) */
    for (i = 0; i < ui.clip.nlocks; i++)
        if (lock_set(t, step, ui.clip.lk[i].param, ui.clip.lk[i].value))
            ui.locks_until = plat_ms() + MSG_MS;
    step_on(s);
    ui.dirty = 1;
}

/* a black key on a step: SLIDE and ACCENT toggle, the others pick the function PRESET sets */
static void family_key(int b, q_step *s)
{
    const uint8_t *f = FAMILY[b];
    if (f[0] == A_SLIDE || f[0] == A_ACCENT)
        s->flags ^= f[0] == A_SLIDE ? SF_SLIDE : SF_ACCENT;
    else if (f[0] != 0xFF)
        ui.attr = f[0] == ui.attr && f[1] != 0xFF ? f[1] : f[0];
}

/* LISTEN's pattern's step in ms, its gaps' unit */
static uint32_t listen_step_ms(void) { return 2500u * DIV96[ui.p->pat[ui.listen_track][ui.listen_pat].div] / ui.p->tempo; }

/* the only writer of ui.rec: the recorder armed in LIVE alone; LISTEN tells the render, and its end closes the phrase
   (the last note's gap: the time to the end) */
static void rec_set(int r)
{
    if (ui.rec == R_LISTEN && r != R_LISTEN) {
        q_mimic *m = &ui.p->pat[ui.listen_track][ui.listen_pat].mimic;
        if (ui.listen_n) {
            m->ev[m->n - 1][1] = (uint8_t)gen_mimic_gap(plat_ms() - ui.listen_ms, listen_step_ms(), 1);
            ui.dirty = 1;
        }
        audio_mimic_listen(ui.listen_track, 0);
    } else if (r == R_LISTEN && ui.rec != R_LISTEN) {
        ui.listen_track = ui.track, ui.listen_pat = ui.p->pattern[ui.track], ui.listen_n = 0;
        audio_mimic_listen(ui.track, 1);
    }
    if (r == R_NOTES && ui.rec != R_NOTES)
        ui.cursor = 1;
    if ((r == R_NOTES) != (ui.rec == R_NOTES))  /* SEQ's pages change: the first one */
        ui.page[D_SEQ] = 0;
    if (r != R_GRID) {                  /* a step held on the grid is let go: its release toggles nothing */
        ui.held = ui.cell = 0;
        sel_clear();
    }
    ui.rec = r;
    audio_rec(r == R_LIVE);
}

/* a button down for longer than HOLD_MS */
static int long_held(int b) { return ui.btn >> b & 1u && plat_ms() - ui.down_ms[b] > HOLD_MS; }

/* released after a short press with nothing else meanwhile: the button's own action */
static int short_press(int b) { return plat_ms() - ui.down_ms[b] <= HOLD_MS && !(ui.used >> b & 1u); }

/* REC let go after a short press: SEQ cycles GRID, NOTES, OFF (a generator: nothing), elsewhere NOTES (LIVE playing), OFF */
static void rec_release(void)
{
    if (!short_press(B_REC))
        return;
    if (ui.rec == R_LIVE || ui.rec == R_ARM)
        rec_set(R_OFF);
    else if (ui.domain == D_SEQ && pat()->src == SRC_STEP)
        rec_set(ui.rec == R_OFF ? R_GRID : ui.rec == R_GRID ? R_NOTES : R_OFF);
    else if (lanes_on() || chords_on() || tonnetz_on())   /* LANES, CHORDS, TONNETZ: the grid only, no NOTES */
        rec_set(ui.rec == R_OFF ? R_GRID : R_OFF);
    else if (ui.domain == D_SEQ && pat()->src == SRC_MIMIC)
        rec_set(ui.rec == R_LISTEN ? R_OFF : R_LISTEN);
    else if (ui.domain == D_SEQ)        /* a generator is never written: an armed REC disarms */
        rec_set(R_OFF);
    else if (ui.domain != D_SEQ)
        rec_set(ui.rec != R_OFF ? R_OFF : audio_playing() ? R_LIVE : R_NOTES);
}

/* the selection's lit steps within LEN, the reference aside: bit c, the page's cell c */
static uint16_t sel_others(void)
{
    const q_pattern *t = pat();
    uint16_t m = 0;
    int c;
    for (c = 0; c < 16; c++)
        if (ui.sel >> c & 1u && c != ui.sel_ref && ui.spage * 16 + c < t->len && t->steps[ui.spage * 16 + c].flags & SF_ON)
            m |= (uint16_t)(1u << c);
    return m;
}

static void sel_clear(void)
{
    ui.sel = 0;
    ui.sel_ref = -1;
}

/* SEL held: the white key's cell in or out; the first in is the reference, then the lowest one left */
static void sel_key(int w)
{
    ui.sel ^= (uint16_t)(1u << w);
    if (ui.sel_ref < 0) {
        ui.sel_ref = w;
        ui.attr = A_VEL;
        ui.lpage = 0;
    } else if (ui.sel_ref == w) {
        ui.sel_ref = ui.sel ? __builtin_ctz(ui.sel) : -1;
    }
}

/* the chosen function's value from the reference to the others */
static void sel_copy(void)
{
    q_pattern *t = pat();
    uint16_t m = sel_others();
    int c;
    for (c = 0; c < 16; c++)
        if (m >> c & 1u)
            attr_copy(&t->steps[ui.spage * 16 + c], &t->steps[ui.spage * 16 + ui.sel_ref], ui.attr);
    ui.dirty = 1;
}

/* K on the selection: the reference's p-lock turned, its value on the others */
static void sel_lock(int k, int d)
{
    q_pattern *t = pat();
    uint16_t m = sel_others();
    int c, locked, v, ref = ui.spage * 16 + ui.sel_ref;
    lock_turn(ref, k, d);
    v = lock_value(t, ref, k, &locked);
    if (!d || !locked)
        return;
    for (c = 0; c < 16; c++)
        if (m >> c & 1u && lock_set(t, ui.spage * 16 + c, lock_param(k), v))
            ui.locks_until = plat_ms() + MSG_MS;
}

/* LIVE, REC held: the step loses the keys' notes, off when none is left; OCT- held, it is cleared */
static void erase_step(q_pattern *t, int n)
{
    q_step *s = &t->steps[n];
    int i, j, m = 0;
    uint8_t keep[4] = {0};
    if (ui.btn >> B_OCTDN & 1u) {
        step_clear(t, n);
        ui.dirty = 1;
        return;
    }
    if (!(s->flags & SF_ON))
        return;
    for (i = 0; i < 4 && (i == 0 || s->note[i]); i++) {
        for (j = 0; j < 8 && ui.erase_notes[j] != s->note[i]; j++) {}
        if (j == 8)
            keep[m++] = s->note[i];
    }
    if (!m) {                           /* the notes stay behind the flag, as a KIT's last pad out */
        s->flags &= (uint8_t)~SF_ON;
    } else if (m < i) {
        __builtin_memcpy(s->note, keep, sizeof keep);
    } else {
        return;
    }
    ui.dirty = 1;
}

/* LIVE, REC held with keys or OCT-: every step from the last erased to the one playing (the step playing only when the
   PATH is not FWD), at each frame: none is missed at a fast tempo */
static void live_erase(void)
{
    q_pattern *t = pat();
    int pos = audio_pos(ui.track), i, on = 0;
    for (i = 0; i < 8; i++)
        on |= ui.erase_notes[i];
    if (!(ui.rec == R_LIVE && ui.btn >> B_REC & 1u && (on || ui.btn >> B_OCTDN & 1u)) || pos < 0 || t->src != SRC_STEP ||
        audio_pattern(ui.track) != ui.p->pattern[ui.track]) {
        ui.erase_pos = -1;
        return;
    }
    pos %= t->len;
    i = ui.erase_pos < 0 || t->path != PATH_FWD ? pos : ui.erase_pos;
    for (;;) {                          /* the step playing again too: a key pressed during it */
        erase_step(t, i);
        if (i == pos)
            break;
        i = (i + 1) % t->len;
    }
    ui.erase_pos = pos;
}

static uint8_t save_held;               /* a backup streams to the computer: a save would change what it reads */

static void save_release(void)
{
    if (!short_press(B_SAVE))
        return;
    if (audio_playing())                /* a flash write stalls the render */
        ui.msg_until = plat_ms() + MSG_MS;
    else if (!save_held && store_save(ui.p) == 0)
        ui.dirty = 0;
}

/* backup and restore over USB (fm1-base device/backup.c) */
int app_backup_playing(void) { return audio_playing(); }

void app_backup_flush(void)             /* stopped, a backup starts: what changed is saved first, so it is in the file */
{
    if (ui.dirty && store_save(ui.p) == 0)
        ui.dirty = 0;
}
void app_backup_hold(int on) { save_held = (uint8_t)on; }

void app_backup_restored(void)
{
    rec_set(R_OFF);                     /* (ui_init clears ui.rec: LISTEN must tell the render first) */
    store_load(ui.p);
    for (int t = 0; t < Q_TRACKS; t++)  /* the render takes each track's model from the restored project */
        audio_track_model(t);
    ui.dirty = 0;
    app_ui_init();                      /* the sound back (the restore muted it) and the screen anew */
}

/* a new pattern or mask for the project: the cursor stays inside the pattern */
static void scene_apply(const q_scene *sc)
{
    int t;
    for (t = 0; t < Q_TRACKS; t++)
        ui.p->pattern[t] = sc->pat[t];
    ui.p->mute = sc->mute;
    sel_clear();
    ui.nchord = 0;
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
    if (!ui.punch || ui.rec != R_LIVE || !audio_playing() || pos < 0 || t->src != SRC_STEP ||
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

/* the track's scale: MARBLES its allowed notes, the rest the project's */
static uint16_t track_scale(void)
{
    const q_pattern *t = pat();
    return t->src == SRC_MARBLES ? gen_allowed(ui.p, t) : gen_scale(ui.p);
}

/* KEYS on for the track: its white keys play the scale, the black ones nothing (a KIT keeps its pads) */
static int keys_in_scale(void)
{
    return !kit() && ui.p->keys == KEYS_ALL;
}

/* KEYS: the white key w (0..15) plays the w-th note of the track's scale up from the tonic in octave 3, -1 past 127
   (a 1-note mask) */
static int scale_note(int w)
{
    uint16_t m = track_scale();
    int n = 48 + ui.p->tonic + 12 * ui.octave;
    for (; n <= 127; n++)
        if (m >> n % 12 & 1 && w-- == 0)
            return n;
    return -1;
}

/* the note key k plays: octave, KEYS (the scale on the white keys, the black ones mute), a KIT's pads; -1 none */
static int key_play_note(int k)
{
    int w;
    for (w = 0; w < 16 && WHITE[w] != k; w++) {}
    if (kit())
        return w < 16 ? KIT_NOTE + w : -1;
    if (keys_in_scale())
        return w < 16 ? scale_note(w) : -1;
    return 53 + k + 12 * ui.octave;
}

/* NOTES: the cursor d steps on, looping on LEN */
static void cursor_move(int d)
{
    int len = pat()->len;
    ui.cursor = ((ui.cursor - 1 + d) % len + len) % len + 1;
}

/* the held step's preview ends, on the track it sounds on */
static void preview_off(void)
{
    int i;
    for (i = 0; i < 4 && ui.preview[i]; i++)
        audio_live_note(ui.track, ui.preview[i], 0);
    __builtin_memset(ui.preview, 0, sizeof ui.preview);
}

/* RANDOM (REC held + black keys 6-8): 0..n - 1 */
static uint32_t dice_rand(uint32_t n)
{
    ui.rng ^= ui.rng << 13;
    ui.rng ^= ui.rng >> 17;
    ui.rng ^= ui.rng << 5;
    return ui.rng % n;
}

/* the page's knobs drawn within their range; a knob that picks lands on a choice's middle, as a detent does; never the
   ones that change what the track is or how loud it plays */
static void dice_page(int pid)
{
    const q_page *pg = &PAGE[pid];
    float start, width;
    int k, n, v;
    for (k = 0; k < 4; k++) {
        if (!pg->label[k] || knob_hidden(pid, k))   /* INSERT: its labels come from INS_LABEL; a knob with no role is hidden */
            continue;
        switch (pid * 4 + k) {          /* MODEL, POLY (the voices), LEVEL, TEMPO, STEPS' LEN and PATH */
        case P_SOUND * 4: case P_SOUND * 4 + 3: case P_KIT * 4: case P_KIT * 4 + 2: case P_MIX * 4 + 3: case P_TEMPO * 4:
        case P_STEPS * 4: case P_STEPS * 4 + 2:   /* LEN: dice_all's own; PATH: XY could freeze the pattern on a step */
            continue;
        }
        if (k == 3 && (pid == P_STEPS || pid == P_GRIDS || pid == P_GRIDSK || pid == P_MARBLES || pid == P_LIFE || pid == P_LIFEK ||
                       pid == P_LANES || pid == P_CHORDS || pid == P_TONNETZ || pid == P_ORBITS || pid == P_MIMIC || pid == P_CIPHER || pid == P_CIPHERK))   /* SOURCE */
            continue;
        v = pg->min[k] + (int)dice_rand((uint32_t)(pg->max[k] - pg->min[k] + 1));
        if ((n = knob_zone(ui.p->sound[ui.track].model, pid, k, &start, &width)) && v >= start)
            v = (int)__builtin_roundf(start + (knob_choice(v, start, width, n) + 0.5f) * width);
        knob_set(pid, k, v);
    }
}

/* STEP: the steps from..to - 1 cleared with their p-locks, then those within LEN lit at a density drawn, as a step made
   by hand, with a note of the scale between 48 and 72 (a KIT: a pad) */
static void dice_steps(int from, int to)
{
    q_pattern *t = pat();
    uint16_t m = gen_scale(ui.p);
    int nn = 0, n, i, c, d;
    for (n = 48; n <= 72; n++)
        nn += m >> n % 12 & 1;
    for (i = from; i < to; i++)
        step_clear(t, i);
    d = 25 + (int)dice_rand(51);
    for (i = from; i < to && i < t->len; i++) {
        q_step *s = &t->steps[i];
        if ((int)dice_rand(100) >= d)
            continue;
        step_default(s);
        if (kit()) {
            s->note[0] = (uint8_t)(KIT_NOTE + dice_rand(16));
        } else {
            for (c = (int)dice_rand((uint32_t)nn), n = 48; !(m >> n % 12 & 1) || c--; n++) {}
            s->note[0] = (uint8_t)n;
        }
        s->vel = (uint8_t)(60 + dice_rand(68));
        step_on(s);
    }
}

/* every page of the domain shown, then in SEQ the source's content */
static void dice_all(void)
{
    q_pattern *t = pat();
    uint8_t id[5];
    int i, c, l, keep;
    for (i = 0; i < pages(ui.domain, id); i++)      /* the cursor's pages: the content below; XY: kept, as PATH */
        if (id[i] != P_CHORD && id[i] != P_TZSTEP && id[i] != P_MOON && id[i] != P_MOONK && id[i] != P_XY)
            dice_page(id[i]);
    if (ui.domain != D_SEQ)
        return;
    switch (t->src) {
    case SRC_STEP:                      /* a new pattern, its pages full */
        knob_set(P_STEPS, 0, 16 * (1 + (int)dice_rand(Q_STEPS / 16)));
        dice_steps(0, Q_STEPS);
        break;
    case SRC_CHORDS:                    /* each of the 8 chords, PRESET's cursor back after */
        keep = ui.chcur;
        for (ui.chcur = 0; ui.chcur < 8; ui.chcur++)
            dice_page(P_CHORD);
        ui.chcur = keep;
        break;
    case SRC_TONNETZ:
        keep = ui.tzcur;
        for (ui.tzcur = 0; ui.tzcur < 8; ui.tzcur++)
            dice_page(P_TZSTEP);
        ui.tzcur = keep;
        break;
    case SRC_ORBITS:
        keep = ui.moon;
        for (ui.moon = 0; ui.moon < t->orbits.moons; ui.moon++)
            dice_page(kit() ? P_MOONK : P_MOON);
        ui.moon = keep;
        break;
    case SRC_LANES:                     /* every cell; a KIT has no OCT lane */
        for (l = 0; l < L_N; l++)
            for (c = 0; c < 16 && !(kit() && l == L_OCT); c++)
                lanes_set(&t->lanes, l, c, (int)dice_rand(LANE_MAX[l] + 1u));
        break;
    }
}

/* black key b (5, 6, 7: keys 6, 7, 8) acts now: not on HOME, in LIVE (REC held erases there), nor with a step, a
   selection or a cell held (the knobs set those); the model on EDIT only */
static int dice_on(int b)
{
    if (ui.domain == D_HOME || ui.rec == R_LIVE || ui.held || ui.sel_ref >= 0 || ui.cell)
        return 0;
    return b != 7 || ui.domain == D_EDIT;
}

static void dice(int b)
{
    ui.rng ^= plat_ms() | 1u;
    if (!ui.rng)
        ui.rng = 1;
    if (b == 5 && ui.domain == D_SEQ && pat()->src == SRC_STEP && (knob_page() == P_STEPS || knob_page() == P_XY)) {
        dice_steps(ui.spage * 16, ui.spage * 16 + 16);   /* the grid's page of steps shown */
    } else if (b == 5) {
        dice_page(knob_page());
    } else if (b == 6) {
        dice_all();
    } else if (b == 7) {            /* always another model, a KIT among them */
        int r = model_rank(ui.p->sound[ui.track].model);
        knob_set(kit() ? P_KIT : P_SOUND, 0, (r + 1 + (int)dice_rand(M_KIT)) % (M_KIT + 1));
    }
    ui.rnd_kind = b - 4;
    ui.rnd_until = plat_ms() + MSG_MS;
    ui.dirty = 1;
}

/* TAP TEMPO (GLO held, black key 1): from the 2nd tap, the mean gap of the last 4 taps; a gap over 2 s starts again */
static void tap_tempo(void)
{
    uint32_t now = plat_ms(), span;
    if (ui.ntap && now - ui.tap_ms[ui.ntap - 1] > 2000u)
        ui.ntap = 0;
    if (ui.ntap == 4)                   /* the oldest drops out */
        for (ui.ntap = 0; ui.ntap < 3; ui.ntap++)
            ui.tap_ms[ui.ntap] = ui.tap_ms[ui.ntap + 1];
    ui.tap_ms[ui.ntap++] = now;
    if (ui.ntap < 2)
        return;
    span = now - ui.tap_ms[0];
    ui.p->tempo = (uint16_t)clamp((int)((60000u * (uint32_t)(ui.ntap - 1) + span / 2) / span), 40, 240);
    ui.tap_until = now + MSG_MS;
    ui.dirty = 1;
}

static void key_down(int k)
{
    q_pattern *t = pat();
    q_step *s = 0;
    int note = 53 + k + 12 * ui.octave, w, b, i;
    if (ui.btn >> B_GLO & 1u) {         /* GLO held: black key 1 taps the tempo, the other keys nothing */
        if (k == BLACK[0])
            tap_tempo();
        return;
    }
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
    if (k == BLACK[0] && ui.savedown) { /* SAVE held: black key 1 twice, a new project (in RAM: a SAVE writes it) */
        if (plat_ms() >= ui.wipe_until) {
            ui.wipe_until = plat_ms() + 2 * MSG_MS;
            return;
        }
        rec_set(R_OFF);                 /* (ui_init clears ui.rec: LISTEN must tell the render first) */
        audio_play(0);
        project_defaults(ui.p);
        ui_init(ui.p);
        ui.dirty = 1;
        return;
    }
    if (w < 16 && ui.savedown) {        /* SAVE held: the white keys 1..8 add the scene to the song */
        if (w < Q_SCENES && ui.p->nsong < Q_SONG) {
            ui.p->song[ui.p->nsong++] = (q_entry){(uint8_t)w, 1};
            ui.entry = ui.p->nsong;
            ui.dirty = 1;
        }
        return;
    }
    if (ui.rec == R_LIVE && ui.btn >> B_REC & 1u) {    /* REC held in LIVE: the key's note is erased, not played */
        if ((note = key_play_note(k)) >= 0)
            for (i = 0; i < 8; i++)
                if (!ui.erase_notes[i]) {
                    ui.erase_notes[i] = (uint8_t)note;
                    break;
                }
        return;
    }
    if (ui.btn >> B_REC & 1u) {         /* REC held: the first black key empties the pattern shown, the second its p-locks */
        for (b = 5; b < 8 && BLACK[b] != k; b++) {}
        if (b < 8) {                    /* RANDOM: armed too */
            if (dice_on(b))
                dice(b);
            return;
        }
        if (ui.rec == R_ARM)            /* (armed: nothing, the first note comes once REC is let go) */
            return;
        if (k == BLACK[0])
            for (i = 0; i < Q_STEPS; i++)
                step_clear(t, i);
        else if (k == BLACK[1])
            t->nlocks = 0;
        ui.dirty = 1;
        return;
    }
    if (ui.domain == D_SEQ && ui.seqdown) {    /* SEQ held: the white keys 1..8 are the patterns */
        if (w < Q_PATTERNS)
            pattern_set(w);
        return;
    }
    if (tonnetz_on() && ui.rec == R_GRID) {    /* TONNETZ on the grid: the white keys 1-8 are the steps */
        q_tonnetz *z = &t->tonnetz;
        if (ui.cell) {                  /* a step held: black keys 1-4 are P, L, R, stay */
            for (b = 0; b < 4 && BLACK[b] != k; b++) {}
            if (b < 4) {
                TZ_SET(z, ui.cell - 1, b == 3 ? TZ_STAY : TZ_P + b);
                ui.dirty = 1;
            }
            ui.cell_edited = 1;         /* another key came: the release is no tap */
            return;
        }
        if (w < 8 && w < z->len) {      /* past LEN: nothing */
            ui.cell = ui.cell_flash = w + 1;
            ui.cell_key = k;
            ui.cell_edited = 0;
            ui.cell_ms = plat_ms();
        }
        return;
    }
    if (chords_on() && ui.rec == R_GRID) {     /* CHORDS on the grid: the white keys 1-8 are the chords */
        q_chords *c = &t->chords;
        if (ui.cell) {                  /* a chord held: a note of the scale gives its degree; out of the scale, nothing */
            int d = (note = key_play_note(k)) >= 0 ? chord_degree(c, note) : -1;
            if (d >= 0) {
                c->ch[ui.cell - 1] = CH_MAKE(d, CH_SIZE(c->ch[ui.cell - 1]));
                ui.dirty = 1;
            }
            ui.cell_edited = 1;         /* another key came: the release is no tap */
            return;
        }
        if (w < 8 && w < c->len) {      /* past LEN: nothing */
            ui.cell = ui.cell_flash = w + 1;
            ui.cell_key = k;
            ui.cell_edited = 0;
            ui.cell_ms = plat_ms();
        }
        return;
    }
    if (lanes_on() && ui.rec == R_GRID) {      /* LANES on the grid: the white keys are the edited lane's cells */
        q_lanes *l = &t->lanes;
        if (ui.cell) {                  /* a NOTE cell held: another key writes its degree (a KIT: its pad) */
            if (ui.lane == L_NOTE && (note = key_play_note(k)) >= 0) {
                lanes_set(l, L_NOTE, ui.cell - 1, kit() ? clamp(note - KIT_NOTE, 0, 15)
                                                       : gen_lanes_degree(l->note, gen_scale(ui.p), ui.p->tonic, note));
                ui.dirty = 1;
            }
            ui.cell_edited = 1;         /* another key came: the release is no tap */
            return;
        }
        if (w < lanes_len(l, ui.lane)) {   /* past LEN: nothing */
            ui.cell = ui.cell_flash = w + 1;
            ui.cell_lane = ui.lane;
            ui.cell_key = k;
            ui.cell_edited = 0;
            ui.cell_ms = plat_ms();
        }
        return;
    }
    if (ui.domain == D_SEQ && t->src != SRC_STEP) {    /* a generator: the keys play the track */
        if (page_id() == P_NOTES) {
            notes_toggle(note % 12);
            return;
        }
    } else if (notes_on()) {            /* the keys write the cursor's step */
        s = &t->steps[ui.cursor - 1];
        if (ui.seldown) {               /* SEL held: a white key puts the cursor on its step (the knobs set the step) */
            if (w < 16 && ui.spage * 16 + w < t->len)
                ui.cursor = ui.spage * 16 + w + 1;
            return;
        }
        if ((note = key_play_note(k)) < 0)
            return;
        if (!ui.nchord) {               /* the first note: a new step at the defaults, or a lit one's notes replaced */
            if (!(s->flags & SF_ON)) {
                step_default(s);
                lock_clear_step(t, ui.cursor - 1);
            }
            __builtin_memset(s->note, 0, sizeof s->note);
        }
        if (ui.nchord < (kit() ? 4 : ui.p->sound[ui.track].poly))
            s->note[ui.nchord++] = (uint8_t)note;
        step_on(s);
        ui.dirty = 1;
        audio_live_note(ui.track, note, 2);
        ui.key_note[k] = (int8_t)note;
        ui.key_track[k] = (int8_t)ui.track;
        return;
    } else if (grid_on()) {             /* the white keys are the page's 16 steps */
        if (ui.seldown && !ui.held) {   /* SEL held: the selection, the black keys on it */
            for (b = 0; b < 11 && BLACK[b] != k; b++) {}
            if (w < 16) {
                sel_key(w);
            } else if (ui.sel_ref >= 0) {
                s = &t->steps[ui.spage * 16 + ui.sel_ref];
                family_key(b, s);
                if (FAMILY[b][0] == A_SLIDE || FAMILY[b][0] == A_ACCENT) {
                    i = ui.attr;
                    ui.attr = FAMILY[b][0];
                    sel_copy();
                    ui.attr = i;
                }
            }
            return;
        }
        if (w < 16 && !ui.held) {
            ui.held = ui.spage * 16 + w + 1;
            ui.held_key = k;
            ui.edited = ui.lpage = ui.nwrote = 0;
            ui.attr = A_VEL;
            s = &t->steps[ui.held - 1];
            if (s->flags & SF_ON && !audio_playing())   /* stopped: a lit step sounds until its release */
                for (i = 0; i < 4 && (i == 0 || s->note[i]); i++)
                    audio_live_note(ui.track, ui.preview[i] = s->note[i], 2);
            return;
        }
        if (!ui.held)
            return;
        ui.edited = ui.dirty = 1;
        s = &t->steps[ui.held - 1];
        if (w == 16) {
            for (b = 0; b < 11 && BLACK[b] != k; b++) {}
            family_key(b, s);
            return;
        }
    }
    if ((note = key_play_note(k)) < 0)
        return;
    if (kit()) {                        /* the white keys are the pads */
        ui.pad = w;
        if (!s)
            ui.padheld = k + 1;
    }
    if (ui.rec == R_ARM && !s) {        /* the first note starts LIVE; played when step 1 is under way to take it */
        rec_set(R_LIVE);
        audio_play(1);
        ui.arm_key = k + 1;
        return;
    }
    /* written to the held step: not overdubbed too; ARP held: the arp keeps the note after its key is released */
    audio_live_note(ui.track, note, s ? 2 : ui.arpdown && !kit() ? 3 : 1);
    if (!s && ui.rec == R_LISTEN) {     /* learnt: the first note of LISTEN starts a new phrase */
        q_mimic *m = &pat()->mimic;
        uint32_t now = plat_ms();
        if (!ui.listen_n)
            m->n = 0;
        gen_mimic_learn(m, note, ui.listen_n ? gen_mimic_gap(now - ui.listen_ms, listen_step_ms(), 0) : 0);
        ui.listen_ms = now;
        ui.listen_n++;
        ui.dirty = 1;
    }
    if (!s && ui.rec == R_OFF) {        /* disarmed: the note, or the chord with the keys still held, is the clipboard */
        for (i = 0; i < NKEYS && ui.key_note[i] < 0; i++) {}
        if (i == NKEYS) {
            uint8_t n = (uint8_t)note;
            clip_notes(&n, 1);
        } else {
            for (i = 0; i < 4 && ui.clip.st.note[i] && ui.clip.st.note[i] != note; i++) {}
            if (i < 4)
                ui.clip.st.note[i] = (uint8_t)note;
        }
    }
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
    if (ui.cell && k == ui.cell_key) {  /* a tap: the cell's value steps up, back to 0 past its top (TRIG toggles) */
        if (!ui.cell_edited && plat_ms() - ui.cell_ms <= HOLD_MS && pat()->src == SRC_LANES) {   /* a long hold: its help */
            q_lanes *l = &pat()->lanes;
            lanes_set(l, ui.lane, ui.cell - 1, (lanes_get(l, ui.lane, ui.cell - 1) + 1) % (LANE_MAX[ui.lane] + 1));
            ui.dirty = 1;
        } else if (!ui.cell_edited && plat_ms() - ui.cell_ms <= HOLD_MS && pat()->src == SRC_TONNETZ) {   /* the next op */
            q_tonnetz *z = &pat()->tonnetz;
            TZ_SET(z, ui.cell - 1, (TZ_OP(z, ui.cell - 1) + 1) % 4);
            ui.dirty = 1;
        } else if (!ui.cell_edited && plat_ms() - ui.cell_ms <= HOLD_MS && pat()->src == SRC_CHORDS) {   /* the next size */
            uint8_t *ch = &pat()->chords.ch[ui.cell - 1];
            *ch = CH_MAKE(CH_DEG(*ch), (CH_SIZE(*ch) + 1) % CS_N);
            ui.dirty = 1;
        }
        ui.cell = 0;
    }
    if (ui.held && k == ui.held_key) {  /* nothing edited: a lit step is cut, an empty one takes the clipboard */
        if (!ui.edited) {
            if (pat()->steps[ui.held - 1].flags & SF_ON)
                clip_cut(ui.held - 1);
            else
                clip_paste(ui.held - 1);
        }
        ui.held = 0;
    }
    if (!ui.held)
        preview_off();
    if (ui.padheld == k + 1)
        ui.padheld = 0;
    for (w = 0; w < 8; w++)             /* (OCT is not changed while REC is held: the note is the one pressed) */
        if (ui.erase_notes[w] && ui.erase_notes[w] == key_play_note(k))
            ui.erase_notes[w] = 0;
    if (ui.key_note[k] < 0)
        return;
    audio_live_note(ui.key_track[k], ui.key_note[k], 0);
    ui.key_note[k] = -1;
    for (w = 0; w < NKEYS && ui.key_note[w] < 0; w++) {}
    if (ui.nchord && w == NKEYS) {      /* NOTES, all the keys up: the cursor moves on by the step's length */
        const q_pattern *t = pat();
        int n = t->steps[ui.cursor - 1].len / 8;
        cursor_move(n > 1 ? n : 1);
        ui.nchord = 0;
    }
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
    if (!ui.seldown)
        sel_clear();
    ui.arpdown = btn >> B_ARP & 1u;
    ui.fxdown = btn >> B_FX & 1u;
    if (!ui.fxdown && ui.punch) {       /* FX let go: every punch-in with it */
        ui.punch = 0;
        audio_punch(0);
    }
    if ((btn & ~ui.btn) || (keys & ~ui.keys))  /* a press: the buttons already held have served */
        ui.used |= ui.btn & btn;
    ui.used &= ~(btn & ~ui.btn);
    for (i = 0; i < NB; i++)
        if ((btn & ~ui.btn) >> i & 1u) {
            ui.down_ms[i] = plat_ms();
            button(i);
        }
    if ((ui.btn & ~btn) >> B_SAVE & 1u)
        save_release();
    if ((ui.btn & ~btn) >> B_REC & 1u)
        rec_release();
    if (ui.domain != D_SEQ && (ui.rec == R_GRID || ui.rec == R_NOTES) && audio_playing())   /* out of SEQ, GRID is NOTES:
                                                                                               playing, LIVE */
        rec_set(R_LIVE);
    if (ui.arm_key && audio_pos(ui.track) >= 0 && (i = ui.arm_key - 1, ui.arm_key = 0, keys >> i & 1u))
        key_down(i);
    for (i = 0; i < NKEYS; i++) {
        if ((keys & ~ui.keys) >> i & 1u)
            key_down(i);
        else if ((ui.keys & ~keys) >> i & 1u)
            key_up(i);
    }
    ui.btn = btn;
    ui.keys = keys;
    if (!(btn >> B_REC & 1u))
        __builtin_memset(ui.erase_notes, 0, sizeof ui.erase_notes);
    live_erase();
    ui.layer = layer_shown() + 1;
    if (ui.domain == D_SEQ && (ui.rec == R_OFF || ui.rec == R_LIVE) && audio_pos(ui.track) >= 0)   /* the page playing shown */
        ui.spage = audio_pos(ui.track) / 16;
    punch_rec();
    d = ui.domain;                      /* after the buttons: SELECT turns the page of the domain just picked */
    n = domain_n(d);
    if ((e = plat_enc(EN_ALGO)) != 0) {
        preview_off();                  /* before the track changes */
        ui.track = clamp(ui.track + e, 0, Q_TRACKS - 1);
        ui.held = ui.cell = 0;
        sel_clear();
        ui.nchord = 0;
        if (ui.cursor > ui.p->pat[ui.track][ui.p->pattern[ui.track]].len)       /* LEN is per track: same rule as when it shrinks */
            ui.cursor = 1;
    }
    if ((e = plat_enc(EN_SELECT)) != 0) {
        if (ui.held || ui.sel_ref >= 0 || (notes_on() && ui.seldown))   /* the step's p-lock page */
            ui.lpage = ((ui.lpage + e) % lpage_n() + lpage_n()) % lpage_n();
        else
            ui.page[d] = ((ui.page[d] + e) % n + n) % n;
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {
        if (notes_on() && ui.nchord) {   /* keys held: the note tied over the next steps, or untied */
            q_step *s = &pat()->steps[ui.cursor - 1];
            for (; e; e -= e > 0 ? 1 : -1)
                s->len = (uint8_t)len_step(s->len, e);
            ui.dirty = 1;
        } else if (notes_on()) {
            cursor_move(e);
        } else if (ui.held) {
            attr_turn(&pat()->steps[ui.held - 1], ui.attr, e);
            ui.edited = ui.dirty = 1;
        } else if (ui.sel_ref >= 0) {
            attr_turn(&pat()->steps[ui.spage * 16 + ui.sel_ref], ui.attr, e);
            sel_copy();
        } else if (ui.domain == D_SEQ && pat()->src == SRC_GRIDS) {   /* the map's 25 rhythms: X and Y on its nodes */
            q_grids *gr = &pat()->grids;
            static const uint8_t NODE[5] = {0, 32, 64, 96, 127};
            int i = clamp((gr->y + 16) / 32, 0, 4) * 5 + clamp((gr->x + 16) / 32, 0, 4);
            i = clamp(i + e, 0, 24);
            gr->x = NODE[i % 5], gr->y = NODE[i / 5];
            ui.dirty = 1;
        } else if (ui.domain == D_SEQ && pat()->src == SRC_MARBLES) {   /* a new draw of the random loops */
            audio_marbles_draw(ui.track);
            ui.draws++;
        } else if (ui.domain == D_SEQ && pat()->src == SRC_LIFE) {      /* SEED: a new grid at the next tour */
            pat()->life.seed = (uint8_t)clamp(pat()->life.seed + e, 0, 127);
            ui.dirty = 1;
        } else if (ui.domain == D_SEQ && pat()->src == SRC_CHORDS) {    /* the chord the CHORD page sets */
            ui.chcur = clamp(ui.chcur + e, 0, pat()->chords.len - 1);
        } else if (ui.domain == D_SEQ && pat()->src == SRC_TONNETZ) {   /* the step the STEP page sets */
            ui.tzcur = clamp(ui.tzcur + e, 0, pat()->tonnetz.len - 1);
        } else if (ui.domain == D_SEQ && pat()->src == SRC_ORBITS) {   /* the moon set (MOON page) */
            ui.moon = clamp(ui.moon + e, 0, pat()->orbits.moons - 1);
        } else if (ui.domain == D_SEQ && pat()->src == SRC_CIPHER) {   /* the next formula, at its advised values */
            gen_cipher_preset(&pat()->cipher, (pat()->cipher.formula + e % CIPHER_N + CIPHER_N) % CIPHER_N);
            ui.dirty = 1;
        } else if (ui.domain == D_SEQ && pat()->src == SRC_MIMIC) {    /* a new draw: SEED */
            pat()->mimic.seed = (uint8_t)(pat()->mimic.seed + e);
            ui.dirty = 1;
        } else if (lanes_on()) {        /* the lane edited; a KIT has no OCT */
            ui.lane = clamp(ui.lane + e, 0, L_N - 1);
            if (kit() && ui.lane == L_OCT)
                ui.lane = e > 0 ? L_DUR : L_NOTE;
            ui.cell = 0;
        } else if (ui.domain == D_SEQ) {
            pattern_set(clamp(ui.p->pattern[ui.track] + e, 0, Q_PATTERNS - 1));
        } else if (ui.domain == D_EDIT && ui.padheld && kit()) {
            knob_turn(P_KIT, 1, e);     /* the held pad's engine */
        } else if (ui.domain == D_EDIT) {
            knob_turn(P_SOUND, 0, e);
        } else if (ui.domain == D_HOME && page_id() == P_SONG) {
            knob_turn(P_SONG, 0, e);
        } else if (ui.domain == D_FX && page_id() == P_INSERT) {   /* the insert's type */
            ui.p->sound[ui.track].itype = (uint8_t)clamp(ui.p->sound[ui.track].itype + e, 0, IT_N - 1);
            ui.dirty = 1;
        } else if (ui.domain == D_LFO) {        /* FREE / TRIG */
            ui.p->sound[ui.track].ltrig = (uint8_t)clamp(ui.p->sound[ui.track].ltrig + e, 0, 1);
            ui.dirty = 1;
        }
    }
    if (ui.page[D_SEQ] >= domain_n(D_SEQ))      /* another pattern or track, no XY page */
        ui.page[D_SEQ] = 0;
    if (ui.cell && !lanes_on() && !chords_on() && !tonnetz_on())   /* another domain: the cell or chord held lets go, its release does nothing */
        ui.cell = 0;
    if (ui.rec == R_LISTEN && (ui.domain != D_SEQ || ui.track != ui.listen_track || ui.p->pattern[ui.track] != ui.listen_pat ||
                               pat()->src != SRC_MIMIC))   /* LISTEN is its pattern's */
        rec_set(R_OFF);
    if (ui.moon >= pat()->orbits.moons)         /* fewer moons: the one set among them */
        ui.moon = pat()->orbits.moons - 1;
    if (ui.chcur >= pat()->chords.len)          /* a shorter progression or sequence: the cursor within it */
        ui.chcur = pat()->chords.len - 1;
    if (ui.tzcur >= pat()->tonnetz.len)
        ui.tzcur = pat()->tonnetz.len - 1;
    if (kit() && ui.lane == L_OCT)              /* the track became a KIT: no OCT lane */
        ui.lane = L_NOTE;
    kpid = knob_page();
    for (i = 0; i < 4; i++) {
        e = plat_enc(EN_K1 + i);
        if (ui.cell) {                          /* a LANES cell held: K1 sets it, the others nothing */
            if (!i && e && pat()->src == SRC_LANES) {
                q_lanes *l = &pat()->lanes;
                lanes_set(l, ui.lane, ui.cell - 1, clamp(lanes_get(l, ui.lane, ui.cell - 1) + e, 0, LANE_MAX[ui.lane]));
                ui.cell_edited = ui.dirty = 1;
            } else if (!i && e && pat()->src == SRC_TONNETZ) {   /* a step held: K1 its op */
                q_tonnetz *z = &pat()->tonnetz;
                TZ_SET(z, ui.cell - 1, clamp(TZ_OP(z, ui.cell - 1) + e, 0, 3));
                ui.cell_edited = ui.dirty = 1;
            } else if (i < 2 && e && pat()->src == SRC_CHORDS) {   /* a chord held: K1 its degree, K2 its size */
                uint8_t *ch = &pat()->chords.ch[ui.cell - 1];
                *ch = i ? CH_MAKE(CH_DEG(*ch), clamp(CH_SIZE(*ch) + e, 0, CS_N - 1)) : CH_MAKE(clamp(CH_DEG(*ch) + e, 0, 6), CH_SIZE(*ch));
                ui.cell_edited = ui.dirty = 1;
            }
        } else if (ui.held) {
            lock_turn(ui.held - 1, i, e);
        } else if (notes_on() && ui.seldown) {
            lock_turn(ui.cursor - 1, i, e);
        } else if (ui.sel_ref >= 0) {
            sel_lock(i, e);
        } else {
            knob_turn(kpid, i, e);
            if (e && ui.rec == R_LIVE && audio_playing())
                rec_lock(kpid, i);
        }
    }
    if (ui.domain == D_SEQ && notes_on())      /* NOTES: the cursor's page, after PRESET, LEN or another track */
        ui.spage = (ui.cursor - 1) / 16;
}

/* the keys lit where they play notes: KEYS on, the white keys sounding the scale's first note; off, the keys in the scale */
static uint32_t scale_keys(void)
{
    uint32_t keys = 0;
    uint16_t m = track_scale();
    int i, r;
    if (kit())
        return 0;
    if (!keys_in_scale()) {
        for (i = 0; i < NKEYS; i++)
            keys |= (uint32_t)(m >> (53 + i) % 12 & 1) << i;
        return keys;
    }
    for (r = scale_note(0) % 12, i = 0; i < 16; i++)
        if (scale_note(i) >= 0 && scale_note(i) % 12 == r)
            keys |= 1u << WHITE[i];
    return keys;
}

/* the buttons that lay a layer on the keys, and the one whose map is shown: held over HOLD_MS with nothing pressed
   before the map came, then kept while it is held (SEL only on the grid and in NOTES); -1 none */
static const uint8_t LAYER_BTN[8] = {B_HOME, B_FX, B_SAVE, B_SEQ, B_ARP, B_SEL, B_REC, B_GLO};
#define B_CELL NB                       /* a LANES cell held long: its layer, no button's */
static int layer_shown(void)
{
    int i, b;
    if (ui.layer && ui.btn >> (ui.layer - 1) & 1u && (ui.layer - 1 != B_SEL || !(ui.used >> B_SEL & 1u)))   /* SEL's gives
                                                                                       the plateau back at its first key */
        return ui.layer - 1;
    for (i = 0; i < 8; i++) {
        b = LAYER_BTN[i];
        if (long_held(b) && !(ui.used >> b & 1u) && (b != B_SEL || grid_on() || notes_on()) && (b != B_REC || ui.rec != R_ARM))
            return b;
    }
    if (ui.cell && !ui.cell_edited && plat_ms() - ui.cell_ms > HOLD_MS && pat()->src == SRC_LANES)   /* (CHORDS: its scene shows the chord held) */
        return B_CELL;
    return -1;
}

/* the keys a layer gives a role */
static uint32_t layer_keys(int b)
{
    uint32_t keys = 0;
    int i;
    switch (b) {
    case B_GLO:                         /* TAP TEMPO */
        return 1u << BLACK[0];
    case B_HOME:                        /* the mutes, the scenes */
        for (i = 0; i < 16; i++)
            keys |= (uint32_t)(i < Q_TRACKS || i >= 8) << WHITE[i];
        return keys;
    case B_CELL:                        /* NOTE: every other key writes its note (a KIT: the white keys, its pads) */
        if (ui.lane != L_NOTE)
            return 0;
        for (i = 0; i < NKEYS; i++)
            if (key_play_note(i) >= 0 && i != ui.cell_key)
                keys |= 1u << i;
        return keys;
    case B_FX:
    case B_SAVE:
    case B_SEQ:
        for (i = 0; i < (b == B_FX ? 16 : 8); i++)
            keys |= 1u << WHITE[i];
        return b == B_SAVE ? keys | 1u << BLACK[0] : keys;   /* SAVE: and the new project */
    case B_SEL:
        if (!notes_on())
            break;
        for (i = 0; i < 16; i++)        /* NOTES: the white keys, the cursor */
            keys |= 1u << WHITE[i];
        return keys;
    case B_REC:
        if (ui.rec != R_LIVE) {         /* CLEAR, CLEAR LOCKS, and RANDOM's keys where they act */
            keys = 1u << BLACK[0] | 1u << BLACK[1];
            for (i = 5; i < 8; i++)
                keys |= (uint32_t)dice_on(i) << BLACK[i];
            return keys;
        }
    }
    return (1u << NKEYS) - 1u;          /* every key: ARP's latch, SEL on the grid, REC's erasing in LIVE */
}

/* the keys whose note is one of the step's (it lit) */
static uint32_t step_keys(const q_step *s)
{
    uint32_t keys = 0;
    int k, i, n;
    for (k = 0; k < NKEYS && s->flags & SF_ON; k++)
        for (i = 0, n = key_play_note(k); i < 4 && n >= 0; i++)
            if ((i == 0 || s->note[i]) && s->note[i] == n)
                keys |= 1u << k;
    return keys;
}

/* the keys lit; *mid: the ones at the mid level */
static uint32_t key_lights(uint32_t *mid)
{
    const q_pattern *t = &ui.p->pat[ui.track][ui.p->pattern[ui.track]];
    uint32_t keys = 0;
    int i, pos = audio_pos(ui.track);
    *mid = 0;
    if (ui.layer)                               /* a layer's map: its keys */
        return layer_keys(ui.layer - 1);
    if (ui.domain == D_SEQ && ui.seqdown)       /* SEQ held: the pattern asked */
        keys = 1u << WHITE[ui.p->pattern[ui.track]];
    else if (ui.domain == D_HOME) {             /* the tracks playing, the scenes memorised */
        for (i = 0; i < Q_TRACKS; i++)
            keys |= (uint32_t)(!(ui.p->mute >> i & 1u)) << WHITE[i];
        for (i = 0; i < Q_SCENES; i++)
            keys |= (uint32_t)ui.p->scene[i].used << WHITE[8 + i];
    } else if (notes_on()) {                    /* the cursor's step's notes */
        keys = step_keys(&t->steps[ui.cursor - 1]);
    } else if (ui.domain == D_SEQ && t->src == SRC_ORBITS) {   /* the moons at mid level, lit as they pass */
        uint32_t n = audio_orbits(ui.track);
        int hits = pos >= 0 && n && audio_pattern(ui.track) == ui.p->pattern[ui.track] ? gen_orbits_hits(&t->orbits, n - 1) : 0;
        for (i = 0; i < t->orbits.moons; i++) {
            *mid |= 1u << WHITE[i];
            if (hits >> i & 1)
                keys |= 1u << WHITE[i];
        }
    } else if (ui.domain == D_SEQ && t->src == SRC_MIMIC && ui.rec == R_LISTEN) {   /* LISTEN: the phrase filling, in eighths */
        for (i = 0; i < t->mimic.n * 8 / MIMIC_EV; i++)
            keys |= 1u << WHITE[i];
    } else if (ui.domain == D_SEQ && t->src == SRC_MIMIC) {     /* the place read in the phrase, in eighths */
        q_mimic_run r;
        audio_mimic(ui.track, &r);
        /* (last[0] past n: a run not yet told of a shorter phrase, no light) */
        if (pos >= 0 && r.nlast && r.last[0] < t->mimic.n && audio_pattern(ui.track) == ui.p->pattern[ui.track])
            keys |= 1u << WHITE[r.last[0] * 8 / t->mimic.n];
    } else if (ui.domain == D_SEQ && t->src == SRC_TONNETZ) {   /* the steps at mid level, the one applied lit; held: black keys 1-4 */
        q_tonnetz_run r;
        audio_tonnetz(ui.track, &r);
        if (ui.cell) {
            for (i = 0; i < 4; i++)
                keys |= 1u << BLACK[i];
        } else {
            for (i = 0; i < t->tonnetz.len; i++)
                *mid |= 1u << WHITE[i];
            if (pos >= 0 && r.applied >= 0 && r.applied < t->tonnetz.len && audio_pattern(ui.track) == ui.p->pattern[ui.track])
                keys |= 1u << WHITE[r.applied];
        }
    } else if (ui.domain == D_SEQ && t->src == SRC_CHORDS) { /* the chords at mid level, the one playing lit; held: the scale's keys */
        q_chords_run r;
        audio_chords(ui.track, &r);
        if (ui.cell) {
            for (i = 0; i < NKEYS; i++) {
                int n = key_play_note(i);
                if (n >= 0 && chord_degree(&t->chords, n) >= 0)
                    keys |= 1u << i;
            }
        } else {
            for (i = 0; i < t->chords.len; i++)
                *mid |= 1u << WHITE[i];
            if (pos >= 0 && r.n && audio_pattern(ui.track) == ui.p->pattern[ui.track])
                keys |= 1u << WHITE[(r.n - 1) / 16 % t->chords.len];
        }
    } else if (ui.domain == D_SEQ && t->src == SRC_LANES) {  /* the edited lane's cells set at mid level, its head lit */
        int len = lanes_len(&t->lanes, ui.lane);
        uint32_t n = audio_lanes(ui.track);
        for (i = 0; i < len; i++)
            if (lanes_get(&t->lanes, ui.lane, i))
                *mid |= 1u << WHITE[i];
        if (!audio_playing())                   /* stopped: the head on cell 1 */
            keys |= 1u << WHITE[0];
        else if (pos >= 0 && n && audio_pattern(ui.track) == ui.p->pattern[ui.track])
            keys |= 1u << WHITE[(n - 1) % (uint32_t)len];
    } else if (ui.domain == D_SEQ && t->src == SRC_STEP) {   /* the steps lit at mid level, the one playing and the selection lit */
        for (i = 0; i < 16; i++) {
            if (t->steps[ui.spage * 16 + i].flags & SF_ON)
                *mid |= 1u << WHITE[i];
            if (ui.spage * 16 + i == pos || ui.sel >> i & 1u)
                keys |= 1u << WHITE[i];
        }
    } else {
        keys = scale_keys();
    }
    return keys;
}

static void lights(void)
{
    uint32_t btn = 1u << DOMAIN_BTN[ui.domain], mid, keys = key_lights(&mid);
    if (audio_playing())
        btn |= 1u << B_PLAY;
    if (ui.rec)
        btn |= 1u << B_REC;
    if (!grid_on() && ui.octave)                /* the octave shifted: its direction lit (on the grid they page the steps) */
        btn |= 1u << (ui.octave > 0 ? B_OCTUP : B_OCTDN);
    plat_leds(btn, keys);
    plat_lights_mid(0, mid);
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

static void attr_text(const q_step *s, int a, char *b);

/* the grid LIFE's scene shows: the render's run while the pattern shown plays, else the one its SEED and DENS
   start (stopped, or another pattern playing) */
static void life_run(const q_pattern *t, q_life_run *out)
{
    if (audio_playing() && audio_pattern(ui.track) == ui.p->pattern[ui.track]) {
        audio_life(ui.track, out);
        if (out->seeded)
            return;
    }
    __builtin_memset(out, 0, sizeof *out);
    gen_life_seed(out, t->life.seed, t->life.dens);
}

static void knob_text(int pid, int k, int v, int model, char *b)
{
    float start, width;
    int n = knob_zone(model, pid, k, &start, &width);
    if (n) {                                            /* a knob that picks: its choice's name */
        int c = knob_choice(v, start, width, n);
        const char *s = model == 15 ? WORDS_NAME[c] : model <= 4 ? HEX_NAME[model - 2][c] : k ? ARP_NAME[c] : CHORD_NAME[c];
        if (model == 15 && v < start) {                 /* ORATOR's blend: its synths at the poles, the value between */
            if (v % 21)
                return int_text(v, b);
            s = v == 0 ? "FORMANT" : v == 21 ? "SAM" : "LPC";
        }
        while ((*b++ = *s++) != 0) {}
        return;
    }
    if (pid >= P_SN1 && pid <= P_SN3) {                 /* as a held step's panel shows it: a step with v in every field */
        int a = SN_ATTR[pid - P_SN1][k];
        q_step st = {{(uint8_t)v}, (uint8_t)v, (uint8_t)v, (uint8_t)v, (uint8_t)cond_value(v),
                     (uint8_t)(a == A_RATCHET ? (v - 1) << 3 : v ? SF_SLIDE | SF_ACCENT : 0), (int8_t)v};
        attr_text(&st, a, b);
        return;
    }
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
    if (pid == P_SCALE || pid == P_KEYS) {
        const char *s = pid == P_KEYS ? KEYS_NAME[v] : k == 0 ? TONIC_NAME[v] : k == 1 ? MODE_NAME[v] : k == 2 ? QUANT_NAME[v]
                                                                                                      : CLICK_NAME[v];
        while ((*b++ = *s++) != 0) {}
        return;
    }
    if (((pid == P_CHORDS || pid == P_TONNETZ) && k == 2) || (pid == P_PROG && k == 2) || (pid == P_PLAY && k == 1) || (pid == P_WALK && k >= 2)) {   /* strikes a bar; VOICE; SCALE, RETURN */
        static const char *const RATE_NAME[5] = {"1/BAR", "2/BAR", "4/BAR", "8/BAR", "16/BAR"};
        const char *s = pid == P_CHORDS || pid == P_TONNETZ ? RATE_NAME[v] : pid == P_WALK ? (k == 2 ? (v ? "IN SCALE" : "FREE") : v ? "ON" : "OFF") : v ? "SMOOTH" : "ROOT";
        while ((*b++ = *s++) != 0) {}
        return;
    }
    if (pid == P_CHORD || pid == P_TZSTEP) {   /* the cursor's chord: its degree and note, its size; a step's op */
        static const char *const RN[7] = {"I", "II", "III", "IV", "V", "VI", "VII"}, *const SZ[CS_N] = {"TRIAD", "SEVENTH", "SUS2", "SUS4"};
        static const char *const OP[4] = {"-", "P", "L", "R"};
        const char *s = pid == P_TZSTEP ? OP[v] : k ? SZ[v] : RN[v];
        while ((*b++ = *s++) != 0) {}
        if (pid == P_CHORD && !k) {         /* "IV F" */
            b[-1] = ' ';
            const char *const NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
            const char *n = NN[gen_life_note(pat()->chords.note, gen_scale(ui.p), ui.p->tonic, v) % 12];
            while ((*b++ = *n++) != 0) {}
        }
        return;
    }
    if ((pid == P_MOON || pid == P_MOONK || pid == P_GRIDSK) && k == 2) {   /* the moon's note, or a pad */
        static const char *const PADS3[3] = {"BD", "SN", "HH"};
        if (pid != P_MOON) {
            if (v < 3) { const char *s = PADS3[v]; while ((*b++ = *s++) != 0) {} }
            else { *b++ = 'P'; int_text(v + 1, b); }
        } else
            note_name(gen_life_note(pat()->orbits.note, gen_scale(ui.p), ui.p->tonic, v), b);
        return;
    }
    if ((pid == P_LIFEK && k == 2) || (pid == P_SEED && k == 2)) {
        const char *s = pid == P_SEED ? (v ? "ON" : "OFF") : v ? "BANDS" : "PADS";
        while ((*b++ = *s++) != 0) {}
        return;
    }
    if ((pid == P_CIPHER || pid == P_CIPHERK) && k == 1) {   /* the formula's name */
        const char *s = CIPHER_DEF[v].name;
        while ((*b++ = *s++) != 0) {}
        return;
    }
    if ((pid == P_LOOP || pid == P_CTIME) && !k && !v) {
        const char *s = "FREE";
        while ((*b++ = *s++) != 0) {}
        return;
    }
    if (pid == P_EVOL && !k) {          /* tours per generation */
        int_text(1 << v, b);
        return;
    }
    if (pid == P_X && k == 3) {
        const char *s = GATE_NAME[v];
        while ((*b++ = *s++) != 0) {}
        return;
    }
    if ((k == 3 && (pid == P_STEPS || pid == P_GRIDS || pid == P_GRIDSK || pid == P_MARBLES || pid == P_LIFE || pid == P_LIFEK || pid == P_LANES || pid == P_CHORDS || pid == P_TONNETZ || pid == P_ORBITS || pid == P_MIMIC || pid == P_CIPHER || pid == P_CIPHERK)) || (pid == P_T && !k)) {
        const char *s = pid == P_T ? T_MODE_NAME[v] : SRC_NAME[v];
        while ((*b++ = *s++) != 0) {}
        return;
    }
    if ((pid == P_GRIDS && k == 2) || (pid == P_CIPHER && k == 2) || (pid == P_NOTES && !k) || (pid == P_LIFE && k == 2) || ((pid == P_LANES || pid == P_CHORDS || pid == P_TONNETZ || pid == P_ORBITS) && k == 1)) {
        note_name(v, b);
        return;
    }
    if ((pid == P_STEPS && k > 0) || (pid == P_XY && k & 1) || ((pid == P_GRIDS || pid == P_GRIDSK || pid == P_MARBLES || pid == P_LIFE || pid == P_LIFEK || pid == P_LANES || pid == P_CHORDS || pid == P_TONNETZ || pid == P_ORBITS || pid == P_MIMIC || pid == P_CIPHER || pid == P_CIPHERK) && !k)) {
        const char *s = pid == P_STEPS && k == 2 ? PATH_NAME[v] : DIV_NAME[v];
        while ((*b++ = *s++) != 0) {}
        return;
    }
    if ((pid == P_SOUND || pid == P_KIT) && !k) {
        const char *s = MODEL_NAME[MODEL_ORDER[v]];
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

/* the step shown as held, its functions and p-locks on screen: a held step, a selection's reference, NOTES' cursor while
   SEL is held; 1..64, 0 none */
static int shown_step(void)
{
    return ui.held ? ui.held : ui.sel_ref >= 0 ? ui.spage * 16 + ui.sel_ref + 1 : notes_on() && ui.seldown ? ui.cursor : 0;
}

/* REC's badge: 1 GRID (in SEQ), 2 NOTES (GRID out of SEQ too), 3 LIVE, 4 ARM (blinking: 0 half the time) */
static int badge(void)
{
    if (ui.rec == R_ARM || ui.rec == R_LISTEN)
        return plat_ms() / 500u & 1u ? 0 : ui.rec == R_ARM ? 4 : 5;
    if (ui.domain == D_SEQ && (pat()->src == SRC_LANES || pat()->src == SRC_CHORDS || pat()->src == SRC_TONNETZ) && ui.rec == R_NOTES)   /* NOTES left from a STEP pattern */
        return 0;
    if (ui.domain == D_SEQ && pat()->src != SRC_STEP && pat()->src != SRC_LANES && pat()->src != SRC_CHORDS && pat()->src != SRC_TONNETZ && ui.rec != R_LIVE)   /* a generator: nothing written */
        return 0;
    return ui.rec == R_GRID ? (ui.domain == D_SEQ ? 1 : 2) : ui.rec;
}

static void view_now(q_view *v)
{
    const q_pattern *t = &ui.p->pat[ui.track][ui.p->pattern[ui.track]];
    int hs = shown_step(), pid = page_id(), k, fpid = hs ? lock_pid() : knob_page(), locked;
    __builtin_memset(v, 0, sizeof *v);  /* the padding too: views are compared whole */
    v->head.track = (int8_t)ui.track;
    v->head.domain = (int8_t)ui.domain;
    v->head.page = (int8_t)ui.page[ui.domain];
    v->head.n = (int8_t)domain_n(ui.domain);
    v->head.msg = plat_ms() < ui.msg_until ? 1 : plat_ms() < ui.locks_until ? 2 : plat_ms() < ui.wipe_until ? 3 :
                  plat_ms() < ui.rnd_until ? 3 + ui.rnd_kind : plat_ms() < ui.tap_until ? 7 : 0;
    v->head.bpm = (uint8_t)ui.p->tempo;
    v->head.pat = (int8_t)(ui.p->pattern[ui.track] + 1);
    v->head.scene = (int8_t)ui.msg_scene;
    v->head.punch = (int8_t)(plat_ms() < ui.punch_until ? ui.msg_punch : 0);
    v->head.badge = (int8_t)badge();
    v->head.bnum = (int8_t)(v->head.badge == 2 ? ui.cursor : 0);
    if (audio_playing() && audio_pattern(ui.track) != ui.p->pattern[ui.track])
        v->head.playing = (int8_t)(audio_pattern(ui.track) + 1);
    if (ui.domain == D_EDIT && kit())
        v->head.pad = (int8_t)(ui.pad + 1);
    v->foot.pid = (int8_t)fpid;
    v->foot.model = ui.p->sound[ui.track].model;
    v->scene.pid = (int8_t)pid;
    if (pid == P_LFO)
        v->head.ltrig = (int8_t)(ui.p->sound[ui.track].ltrig + 1);
    if (pid == P_ENGINE || pid == P_SOUND)     /* the model's name after the page's: the footer gives only SOUND's */
        v->head.model = (int8_t)(ui.p->sound[ui.track].model + 1);
    v->foot.col = 15;
    if (fpid == P_INSERT)
        v->foot.itype = v->head.itype = ui.p->sound[ui.track].itype;
    for (k = 0; k < 4; k++) {
        if (PAGE[fpid].label[k])
            v->foot.v[k] = (int16_t)knob_get(fpid, k);
        if (knob_hidden(fpid, k))
            v->foot.hide |= (uint8_t)(1u << k);
    }
    if (hs)                        /* the lock page: the locked values in colour, the track's in grey */
        for (v->foot.col = 0, k = 0; k < 4; k++) {
            if (lock_param(k) < 0 || knob_hidden(fpid, k)) {
                v->foot.hide |= (uint8_t)(1u << k);
                continue;
            }
            v->foot.v[k] = (int16_t)lock_value(t, hs - 1, k, &locked);
            v->foot.col |= (uint8_t)(locked << k);
        }
    if (pid == P_TEMPO || pid == P_SCALE || pid == P_KEYS || pid == P_GFX || pid == P_SONG) {
        v->scene.anim = 7;                      /* scene_glo.c; ui_frame fills its view */
        if (pid == P_SONG && !ui.entry)
            v->foot.hide = 7;
    } else if (pid == P_ENGINE || pid == P_SOUND || pid == P_PAD || (pid >= P_ADSR && pid < P_SN1)) {
        __builtin_memcpy(v->scene.v, v->foot.v, sizeof v->scene.v);
        v->scene.model = pid == P_PAD ? KIT_MODEL + ui.p->kit[ui.track][ui.pad].engine : ui.p->sound[ui.track].model;
        if (pid == P_PAD) {                     /* a KIT's scene: the edited pad's knobs on its engine's polyhedron */
            v->scene.anim = 1;
            v->scene.pad = (uint8_t)ui.pad;
        } else if ((pid == P_ENGINE || pid == P_SOUND) && scene_has(v->scene.model) && v->scene.model != M_KIT) {
            v->scene.anim = 1;
            if (pid == P_SOUND)         /* the scene shows the sound's ENGINE knobs on both pages */
                for (k = 0; k < 4; k++)
                    v->scene.v[k] = (int16_t)knob_get(P_ENGINE, k);
        } else if (pid >= P_ADSR && pid <= P_MIX) {      /* scene_sound.c */
            v->scene.anim = 6;
        }
    } else if (pid == P_KIT) {
        v->scene.pad = (uint8_t)ui.pad;
        for (k = 0; k < Q_PADS; k++)
            v->scene.eng[k] = ui.p->kit[ui.track][k].engine, v->scene.chr[k] = ui.p->kit[ui.track][k].chr;
    } else if (pid == P_STEPS || pid == P_XY || (pid >= P_SN1 && pid <= P_SN3)) {
        v->scene.anim = 2;
        v->scene.spage = (int8_t)ui.spage;
        v->scene.path = (int8_t)t->path;
        v->scene.showpath = pid == P_STEPS && plat_ms() < ui.path_until;
        v->scene.xlen = (int8_t)(pid == P_XY ? t->xlen : 4);
        v->scene.ylen = (int8_t)(pid == P_XY ? t->ylen : 4);
        v->scene.held = (int8_t)hs;
        v->scene.sel = ui.sel;
        v->scene.model = ui.p->sound[ui.track].model;
        v->scene.attr = (int8_t)ui.attr;
        if (hs)
            v->scene.hst = t->steps[hs - 1];
        for (k = 0; k < t->nlocks; k++)
            if (t->locks[k].step / 16 == ui.spage)
                v->scene.locks |= (uint16_t)(1u << (t->locks[k].step % 16));
        v->scene.cursor = (int8_t)(notes_on() ? ui.cursor : 0);
        v->scene.pos = (int8_t)audio_pos(ui.track);
        v->scene.len = (int8_t)t->len;
        __builtin_memcpy(v->scene.st, &t->steps[ui.spage * 16], sizeof v->scene.st);
    } else if (pid >= P_GRIDS) {        /* a generator: its rank, when the pattern shown is the one playing */
        v->scene.src = (int8_t)t->src;
        v->scene.len = (int8_t)gen_loop(t);
        v->scene.pos = (int8_t)(audio_pattern(ui.track) == ui.p->pattern[ui.track] ? audio_pos(ui.track) : -1);
        if (pid == P_NOTES || pid == P_MARBLES || pid == P_T || pid == P_X)
            v->scene.notes = gen_allowed(ui.p, t);
        if (pid == P_GRIDS || pid == P_GRIDSK || pid == P_MAP || pid == P_DENS)
            v->scene.anim = 4;
        if (pid == P_LIFE || pid == P_LIFEK || pid == P_SEED || pid == P_EVOL)
            v->scene.anim = 8;                  /* scene_life.c; ui_frame fills its view */
        if (pid == P_LANES || pid == P_LENS)
            v->scene.anim = 9;                  /* scene_lanes.c; ui_frame fills its view */
        if (pid == P_CHORDS || pid == P_PROG || pid == P_CHORD)
            v->scene.anim = 10;                 /* scene_chords.c; ui_frame fills its view */
        if (pid == P_TONNETZ || pid == P_WALK || pid == P_PLAY || pid == P_TZSTEP)
            v->scene.anim = 11;                 /* scene_tonnetz.c; ui_frame fills its view */
        if (pid == P_ORBITS || pid == P_MOON || pid == P_MOONK)
            v->scene.anim = 12;                 /* scene_orbits.c; ui_frame fills its view */
        if (pid == P_CIPHER || pid == P_CIPHERK || pid == P_CPARAM || pid == P_CTIME)
            v->scene.anim = 14;                 /* scene_cipher.c; ui_frame fills its view */
        if (pid == P_MIMIC || pid == P_LOOP)
            v->scene.anim = 13;                 /* scene_mimic.c; ui_frame fills its view */
        if (pid == P_MARBLES || pid == P_T || pid == P_X) {
            v->scene.anim = 3;
            v->scene.v[0] = t->marbles.spread;
            v->scene.v[1] = t->marbles.xbias;
            v->scene.v[2] = t->marbles.dejavu;
        }
    } else if (pid == P_TRACKS) {
        v->scene.anim = 5;                  /* HOME's emblems (scene_home.c) */
        for (k = 0; k < Q_TRACKS; k++) {
            v->scene.tr[k].model = ui.p->sound[k].model;
            v->scene.tr[k].pat = (uint8_t)(ui.p->pattern[k] + 1);
            if (audio_playing() && audio_pattern(k) != ui.p->pattern[k])
                v->scene.tr[k].playing = (uint8_t)(audio_pattern(k) + 1);
            v->scene.tr[k].mute = ui.p->mute >> k & 1u;
            v->scene.tr[k].level = ui.p->sound[k].level;
            const q_pattern *pt = &ui.p->pat[k][ui.p->pattern[k]];
            v->scene.tr[k].pos = (int8_t)audio_pos(k);
            v->scene.tr[k].len = (int8_t)(pt->src == SRC_STEP ? pt->len : gen_loop(pt));
        }
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
    if (ui.layer) {                     /* a layer's map instead of the scene */
        __builtin_memset(&v->scene, 0, sizeof v->scene);
        v->scene.layer = (int8_t)ui.layer;
        v->scene.lmode = (int8_t)(ui.layer - 1 == B_REC ? ui.rec == R_LIVE : notes_on());
        v->scene.lkeys = layer_keys(ui.layer - 1);
        v->scene.hot = ui.keys;
    }
}

/* the plateau's view of the SEQ page: its cells, texts and marks, from what the page shows */
static void plateau_view(const q_view *v, q_plateau *p)
{
    int first = v->scene.spage * 16, i, k, n = v->scene.len - first > 16 ? 16 : v->scene.len - first;   /* n: the steps
                                                                                         of this page LEN plays */
    int m = v->scene.path == PATH_SNAKE ? 1 : v->scene.path == PATH_COLUMNS ? 2 : v->scene.path == PATH_SPIRAL ? 3 : 0;
    __builtin_memset(p, 0, sizeof *p);
    for (i = 0; i < 16; i++) {
        const q_step *st = &v->scene.st[i];
        q_pcell *c = &p->c[i];
        c->on = st->flags & SF_ON;
        c->vel = st->vel;
        c->len = st->len;
        c->sel = v->scene.sel >> i & 1u;
        c->extra = STEP_RATCHET(st) > 1 || st->prob < 20 || st->cond || st->flags & (SF_SLIDE | SF_ACCENT) || st->micro ||
                   (v->scene.locks >> i & 1u);
        if (v->scene.model == M_KIT)        /* its first pad */
            int_text(st->note[0] - KIT_NOTE + 1, c->name);
        else
            for (k = 0; TONIC_NAME[st->note[0] % 12][k]; k++)
                c->name[k] = TONIC_NAME[st->note[0] % 12][k];
        if (v->scene.path == PATH_XY && i % 4 < v->scene.xlen && i / 4 < v->scene.ylen)   /* XY plays its grid whatever LEN */
            p->live |= (uint16_t)(1u << i);
        else if (v->scene.path != PATH_XY && i < n)     /* the cells the path reaches within LEN (seq.c step_at) */
            p->live |= (uint16_t)(1u << PATHS[m][i]);
    }
    p->pos = (int8_t)(v->scene.pos >= first && v->scene.pos < first + 16 ? v->scene.pos - first : -1);
    p->cursor = (int8_t)(v->scene.cursor - 1 >= first && v->scene.cursor - 1 < first + 16 ? v->scene.cursor - 1 - first : -1);
    p->end = (int8_t)(v->scene.path != PATH_XY && !m && v->scene.len - 1 >= first && v->scene.len - 1 < first + 15 ? v->scene.len - 1 - first : -1);
    p->held = (int8_t)(v->scene.held - 1 >= first && v->scene.held - 1 < first + 16 ? v->scene.held - 1 - first : -1);
    if (v->scene.showpath && v->scene.path != PATH_RANDOM && v->scene.path != PATH_XY) {
        for (i = 0; i < n; i++)
            p->path[p->npath++] = PATHS[m][v->scene.path == PATH_REV ? n - 1 - i : i];
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
            if (a == A_NOTE) {          /* a chord: its first note and how many more, to keep to the cell */
                int n = 0;
                note_name(v->scene.hst.note[0], p->rows[i][1]);
                for (k = 1; k < 4; k++)
                    n += v->scene.hst.note[k] != 0;
                if (n) {
                    for (k = 0; p->rows[i][1][k]; k++) {}
                    p->rows[i][1][k++] = ' ';
                    p->rows[i][1][k++] = '+';
                    p->rows[i][1][k++] = (char)('0' + n);
                    p->rows[i][1][k] = 0;
                }
            } else {
                attr_text(&v->scene.hst, a, p->rows[i][1]);
            }
            if (a == v->scene.attr)
                p->attr = (int8_t)i;
        }
        if (v->scene.model == M_KIT)
            int_text(v->scene.hst.note[0] - KIT_NOTE + 1, p->held_name);
        else
            note_name(v->scene.hst.note[0], p->held_name);
    }
}

static int head_page_name;              /* the header last drew the page's name (a test reads it) */

static void draw_head(const q_view *v)
{
    char t[8];
    int32_t x;
    cv_begin(240, HEAD_H, C_BLACK);
    cv_rect(6, 5, 20, 20, C_HI);
    int_text(v->head.track + 1, t);
    cv_text(6 + (20 - text_w(&FONT_B, t)) / 2, 7, &FONT_B, t, C_BLACK);
    x = cv_text(34, 7, &FONT_B, DOMAIN_NAME[v->head.domain], C_WHITE);
    head_page_name = 0;
    if (v->head.msg) {
        x = cv_text(x + 10, 7, &FONT_B, v->head.msg == 1 ? "STOP TO SAVE" : v->head.msg == 2 ? "LOCKS FULL" : v->head.msg == 3 ? "AGAIN: ERASE"
                    : v->head.msg == 4 ? "RANDOM PAGE" : v->head.msg == 5 ? "RANDOM ALL" : v->head.msg == 6 ? "RANDOM MODEL" : "TAP",
                    KCOL[3]);
        if (v->head.msg == 7) {         /* TAP and the tempo */
            int_text(v->head.bpm, t);
            cv_text(x + 4, 7, &FONT_B, t, KCOL[3]);
        }
    } else if (v->head.punch)
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
        head_page_name = 1;
        x = cv_text(x + 10, 7, &FONT_B, v->foot.pid == P_INSERT ? INS_NAME[v->head.itype] : PAGE[v->foot.pid].name, C_AMB);
        if (v->head.ltrig)
            x = cv_text(x + 8, 7, &FONT_B, v->head.ltrig == 2 ? "TRIG" : "FREE", C_AMB);
        if (v->head.model)
            x = cv_text(x + 8, 7, &FONT_B, MODEL_NAME[v->head.model - 1], C_WHITE);
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
    x = 234 - text_w(&FONT_S, t);
    cv_text(x, 7, &FONT_S, t, C_GRAY);
    if (v->head.badge) {                /* REC's state, boxed in its colour */
        static const char *const BADGE[5] = {"GRID", "NOTES", "LIVE", "ARM", "LISTEN"};
        static const uint16_t BCOL[5] = {RGB(34, 213, 142), RGB(58, 109, 255), RGB(255, 47, 142), RGB(255, 122, 46), RGB(255, 47, 142)};
        char b[10];
        int i = 0;
        const char *n = BADGE[v->head.badge - 1];
        int32_t w;
        while (*n)
            b[i++] = *n++;
        if (v->head.bnum) {
            b[i++] = ' ';
            int_text(v->head.bnum, b + i);
        } else {
            b[i] = 0;
        }
        w = text_w(&FONT_T, b) + 8;
        cv_rect(x - 6 - w, 8, w, 14, BCOL[v->head.badge - 1]);
        cv_text(x - 2 - w, 9, &FONT_T, b, C_BLACK);
    }
    cv_rect(0, HEAD_H - 1, 240, 1, C_LINE);
    cv_blit(0, 0);
}

#define L_ON RGB(255, 176, 46)           /* a layer's keys */
#define L_TEXT RGB(244, 241, 255)

/* a layer's key named: its tag on the key's colour (pink while pressed), then what it does */
static void layer_name(int32_t x, int32_t y, const char *tag, const char *s, uint16_t col, int hot)
{
    int32_t w = text_w(&FONT_T, tag) + 6 > 22 ? text_w(&FONT_T, tag) + 6 : 22;
    cv_rect(x, y, w, 11, hot ? PINK : col);
    cv_text(x + (w - text_w(&FONT_T, tag)) / 2, y, &FONT_T, tag, C_BLACK);
    cv_text(x + w + 4, y, &FONT_T, s, hot ? PINK : L_TEXT);
}

/* a combination: the button(s) boxed, what it does after */
static void layer_combo(int32_t y, const char *btn, const char *s)
{
    int32_t w = text_w(&FONT_T, btn) + 8;
    cv_rect(8, y, w, 1, C_GRAY);
    cv_rect(8, y + 12, w, 1, C_GRAY);
    cv_rect(8, y, 1, 13, C_GRAY);
    cv_rect(8 + w, y, 1, 13, C_GRAY);
    cv_text(12, y + 1, &FONT_T, btn, C_GRAY);
    cv_text(16 + w, y + 1, &FONT_T, s, L_TEXT);
}

/* a held layer button's map in the scene's place: the 27 keys, its keys lit (pressed: pink), their names in two
   columns, then the combinations (docs/screens/layers.html) */
static void draw_layer(const q_view *v, int32_t oy)
{
    static const uint8_t AFTER[11] = {0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14};   /* the white key each black one follows */
    static const char *const RND_TAG[3] = {"#6", "#7", "#8"}, *const RND_NAME[3] = {"RND PAGE", "RND ALL", "RND MODEL"};
    static const char *const SEL_NAME[5] = {"SELECT THE STEP", "ITS FUNCTIONS", "SET ON ALL", "LOCK ALL", "THEIR LENGTHS"};
    const char *names[16], *tags[16], *combo[3][2] = {{0}};
    char nb[16][12], tb[16][4];
    int b = v->scene.layer - 1, n = 0, i, k;
    int32_t y;
    uint16_t col = b == B_REC ? (v->scene.lmode ? RGB(138, 16, 80) : KCOL[3]) : L_ON;
    for (i = 0; i < 16; i++) {          /* the white keys, numbered when they have a role */
        k = WHITE[i];
        cv_rect(15 * i + 1, oy + 2, 13, 40, v->scene.hot >> k & 1u && v->scene.lkeys >> k & 1u ? PINK
                                           : v->scene.lkeys >> k & 1u ? col : RGB(44, 40, 68));
        if (v->scene.lkeys >> k & 1u) {
            int_text(i + 1, tb[0]);
            cv_text(15 * i + 1 + (13 - text_w(&FONT_T, tb[0])) / 2, oy + 29, &FONT_T, tb[0], C_BLACK);
        }
    }
    for (i = 0; i < 11; i++) {
        int32_t x = (AFTER[i] + 1) * 15 - 5;
        k = BLACK[i];
        cv_rect(x - 1, oy + 2, 12, 25, C_BLACK);
        cv_rect(x, oy + 2, 10, 24, v->scene.hot >> k & 1u && v->scene.lkeys >> k & 1u ? PINK
                                   : v->scene.lkeys >> k & 1u ? col : RGB(18, 15, 34));
    }
    switch (b) {
    case B_HOME:
        for (i = 0; i < 12; i++) {
            k = i < Q_TRACKS ? i : i + 4;
            int_text(k + 1, tb[i]);
            __builtin_memcpy(nb[i], i < Q_TRACKS ? "MUTE T" : "SCENE ", 6);
            int_text(i < Q_TRACKS ? i + 1 : i - 3, nb[i] + 6);
            tags[n] = tb[i], names[n++] = nb[i];
        }
        combo[0][0] = "+ SEL", combo[0][1] = "STORE THE SCENE";
        break;
    case B_CELL:
        combo[0][0] = "+ K1", combo[0][1] = "SET THE CELL";
        if (v->scene.lkeys)
            combo[1][0] = "+ KEY", combo[1][1] = "WRITES ITS NOTE";
        combo[v->scene.lkeys ? 2 : 1][0] = "HOLD", combo[v->scene.lkeys ? 2 : 1][1] = "NOTHING ON RELEASE";
        break;
    case B_FX:
        for (i = 0; i < 16; i++) {
            int_text(i + 1, tb[i]);
            tags[n] = tb[i], names[n++] = PUNCH_NAME[i];
        }
        combo[0][0] = "LET GO", combo[0][1] = "EVERY PUNCH-IN STOPS";
        break;
    case B_SAVE:
    case B_SEQ:
        for (i = 0; i < 8; i++) {
            int_text(i + 1, tb[i]);
            __builtin_memcpy(nb[i], b == B_SAVE ? "SONG + S" : "PATTERN ", 8);
            int_text(i + 1, nb[i] + 8);
            tags[n] = tb[i], names[n++] = nb[i];
        }
        if (b == B_SAVE) {
            tags[n] = "#1", names[n++] = "NEW PROJECT";
            combo[0][0] = "TAP", combo[0][1] = "SAVE, STOPPED";
        }
        break;
    case B_ARP:
        tags[n] = "KEY", names[n++] = "LATCH ITS NOTE";
        break;
    case B_GLO:
        tags[n] = "#1", names[n++] = "TAP TEMPO";
        combo[0][0] = "2 TAPS", combo[0][1] = "SET THE BPM";
        break;
    case B_SEL:
        if (v->scene.lmode) {           /* NOTES: the knobs set the step without SEL */
            tags[n] = "W", names[n++] = "CURSOR THERE";
            combo[0][0] = "+ K1-K4", combo[0][1] = "LOCK THE STEP";
            combo[1][0] = "+ SELECT", combo[1][1] = "LOCK PAGE";
            break;
        }
        tags[n] = "W", names[n++] = SEL_NAME[0];
        tags[n] = "#", names[n++] = SEL_NAME[1];
        combo[0][0] = "+ PRESET", combo[0][1] = SEL_NAME[2];
        combo[1][0] = "+ K1-K4", combo[1][1] = SEL_NAME[3];
        combo[2][0] = "+ OCT", combo[2][1] = SEL_NAME[4];
        break;
    case B_REC:
        if (v->scene.lmode) {
            tags[n] = "KEY", names[n++] = "ERASES ITS NOTE";
            tags[n] = "OCT-", names[n++] = "EVERY STEP";
            combo[0][0] = "TAP", combo[0][1] = "DISARM, PLAYING ON";
            combo[1][0] = "PLAY", combo[1][1] = "STOP, DISARMED";
        } else {
            tags[n] = "#1", names[n++] = "CLEAR";
            tags[n] = "#2", names[n++] = "CLEAR LOCKS";
            for (i = 0; i < 3; i++)     /* RANDOM's keys lit (layer_keys) */
                if (v->scene.lkeys >> BLACK[5 + i] & 1u)
                    tags[n] = RND_TAG[i], names[n++] = RND_NAME[i];
            combo[0][0] = "+ PLAY", combo[0][1] = "LIVE: COUNT-IN OR ARMED";
            combo[1][0] = "TAP", combo[1][1] = "GRID > NOTES > OFF";
            combo[2][0] = "HOLD", combo[2][1] = "NOTHING ON RELEASE";
        }
        break;
    }
    for (i = 0; i < n; i++) {           /* two columns, the first half on the left */
        k = (n + 1) / 2;
        layer_name(8 + 116 * (i >= k), oy + 48 + 11 * (i % k), tags[i], names[i], col,
                   b == B_FX && v->scene.hot >> WHITE[i] & 1u);
    }
    y = oy + 48 + 11 * ((n + 1) / 2) + 4;
    if (b == B_REC)
        cv_text(8, y - 4, &FONT_T, v->scene.lmode ? "AS THE PLAYHEAD PASSES" : "ON THE PATTERN SHOWN", C_GRAY), y += 12;
    if (combo[0][0])
        cv_rect(8, y, 224, 1, C_LINE);
    for (i = 0; i < 3 && combo[i][0]; i++)
        layer_combo(y + 6 + 16 * i, combo[i][0], combo[i][1]);
}

/* the whole 166-pixel scene drawn at y offset oy: each band clips its own rows */
static void draw_scene(const q_view *v, int32_t oy)
{
    char t[16];
    int k, i;
    if (v->scene.layer) {
        draw_layer(v, oy);
        return;
    }
    switch (v->scene.pid) {
    case P_ENGINE:
    case P_SOUND:
    case P_PAD:
        cv_text(10, oy + 8, &FONT_M, MODEL_NAME[v->scene.model], C_WHITE);
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
            if (v->scene.chr[i])        /* CHAR: a bar under the name, 1 already shows */
                cv_rect(x, y + 31, (54 * v->scene.chr[i] + 126) / 127, 2, i == v->scene.pad ? C_BLACK : KCOL[3]);
        }
        break;
    case P_GRIDS:
    case P_GRIDSK:
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
        knob_text(v->foot.pid, k, v->foot.v[k], v->foot.model, t);
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
    ui.held = ui.cell = 0;              /* (a hold's release must not toggle its step nor step its cell) */
    if (ui.p)
        for (k = 0; k < NKEYS; k++)
            key_up(k);
    __builtin_memset(&ui, 0, sizeof ui);
    __builtin_memset(ui.key_note, -1, sizeof ui.key_note);
    __builtin_memset(ui.last_note, 60, sizeof ui.last_note);
    step_default(&ui.clip.st);
    ui.p = p;
    ui.cursor = 1;
    ui.sel_ref = ui.erase_pos = -1;
    ui.full = 1;
    ui.dirty = dirty;
    ui.btn = btn;
    ui.keys = keys;
}

void ui_redraw(void) { ui.full = 1; }

void ui_frame(void)
{
    q_view v;
    static q_glview glv;                   /* the global pages' view: the bands drawn after the frame read it */
    static q_lview lfv;                    /* LIFE's view, the same way */
    static q_laneview lnv;                 /* LANES' */
    static q_chview chv;                   /* CHORDS' */
    static q_tzview tzw;                   /* TONNETZ's */
    static q_orview orw;                   /* ORBITS' */
    static q_ciview civ;                   /* CIPHER's */
    static q_miview miw;                   /* MIMIC's */
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
            /* compared with the last frame drawn, not the last one shown: a change between two draws counts */
            /* (another model, or another pad or its engine on a KIT, is another scene too; so is another sound page) */
            int key = v.scene.anim | v.head.track << 4 | v.head.pat << 7 | v.scene.spage << 11 | v.scene.pad << 15 | v.scene.model << 19
                      | (v.scene.anim == 6 || v.scene.anim == 7 ? v.scene.pid : 0) << 24;
            int fresh = ui.full || key != ui.scene_key;
            ui.scene_key = key;
            ui.scene_ms = now;
            ui.frames++;
            if (v.scene.anim == 1) {
                if (fresh)
                    scene_enter();
                q_sview sv = {v.scene.model, -1, {v.scene.v[0], v.scene.v[1], v.scene.v[2], v.scene.v[3]}, {audio_hits(ui.track), 0, 0}};
                if (v.scene.pid == P_PAD) {     /* scene.model: the pad's engine's model */
                    sv.model = M_KIT;
                    sv.pad_eng = (int8_t)(v.scene.model - KIT_MODEL);
                    for (int k = 0; k < 3; k++)
                        sv.hits[k] = audio_kit_hits(ui.track, k);
                }
                scene_frame(&sv, now);
            } else if (v.scene.anim == 3) {
                const q_pattern *t = pat();
                q_mview mv;
                uint32_t in[16];
                uint8_t notes[16];
                int n, k;
                if (fresh) {                /* the notes decided before: no shot */
                    marbles_enter();
                    ui.notes_seen = 0;
                    while (audio_notes(ui.track, &ui.notes_seen, notes, in, 16) == 16) {}
                }
                n = audio_notes(ui.track, &ui.notes_seen, notes, in, 16);
                for (k = 0; k < n; k++)
                    marbles_drop(notes[k], now, now + in[k]);
                mv = (q_mview){t->marbles.tout, t->marbles.mode, t->marbles.bias, t->marbles.jitter, t->marbles.dejavu,
                               t->marbles.spread, t->marbles.xbias, t->marbles.len,
                               kit() ? 2 : t->marbles.gate, v.scene.pos, v.scene.notes,   /* a KIT's notes: 1/2 a step */
                               gen_kept(v.scene.notes, ui.p->tonic, t->marbles.steps),
                               2500u * DIV96[t->div] / ui.p->tempo};   /* a step in ms: a bar is 240000 / tempo, in 96ths */
                marbles_frame(&mv, now);
            } else if (v.scene.anim == 4) {
                const q_pattern *t = pat();
                static q_gview gv;
                uint32_t in[16];
                uint8_t notes[16];
                int n, k, st;
                if (fresh) {
                    grids_enter();
                    ui.notes_seen = 0;
                    while (audio_notes(ui.track, &ui.notes_seen, notes, in, 16) == 16) {}
                }
                n = audio_notes(ui.track, &ui.notes_seen, notes, in, 16);
                for (k = 0; k < n; k++) {           /* the voice of a note: a KIT's first part on its pad, else VOICE */
                    for (st = 0; kit() && st < 2 && t->grids.pad[st] != notes[k] - KIT_NOTE; st++) {}
                    grids_drop(kit() ? st : t->grids.voice - 1, now + in[k]);
                }
                for (k = 0; k < 3; k++) {
                    for (st = 0; st < 32; st++)
                        gv.level[k][st] = (uint8_t)gen_grids_level(&t->grids, st, k);
                    gv.dens[k] = t->grids.dens[k];
                }
                gv.pos = v.scene.pos;
                gv.voice = (int8_t)(kit() ? 0 : t->grids.voice);
                gv.x = t->grids.x, gv.y = t->grids.y, gv.chaos = t->grids.chaos;
                if (v.scene.pos >= 0) {
                    __builtin_memcpy(gv.top_l, "STEP ", 5);
                    gv.top_l[5] = (char)('0' + (v.scene.pos + 1) / 10);
                    gv.top_l[6] = (char)('0' + (v.scene.pos + 1) % 10);
                    gv.top_l[7] = 0;
                } else {
                    __builtin_memcpy(gv.top_l, "1-32", 5);
                }
                {                                   /* VOICE, and the note a synth track plays or a KIT's pad */
                    static const char *const VN[3] = {"BD ", "SN ", "HH "};
                    __builtin_memcpy(gv.top_r, VN[t->grids.voice - 1], 3);
                    if (kit())
                        knob_text(P_GRIDSK, 2, t->grids.pad[t->grids.voice - 1], M_KIT, gv.top_r + 3);
                    else
                        note_name(t->grids.note, gv.top_r + 3);
                }
                grids_frame(&gv, now);
            } else if (v.scene.anim == 5) {
                q_hview hv;
                if (fresh)
                    scene_enter();
                for (int k = 0; k < Q_TRACKS; k++) {   /* the pattern playing, and the one asked for when it differs */
                    const q_trow *r = &v.scene.tr[k];
                    hv.model[k] = r->model, hv.mute[k] = r->mute, hv.level[k] = r->level, hv.pos[k] = r->pos, hv.len[k] = r->len;
                    hv.pat[k] = r->playing ? r->playing : r->pat;
                    hv.next[k] = r->playing ? r->pat : 0;
                    hv.hits[k] = audio_hits(k);
                }
                hv.sel = (int8_t)ui.track;
                home_frame(&hv, now);
            } else if (v.scene.anim == 6) {
                const q_sound *s = &ui.p->sound[ui.track];
                q_pview pv = {(uint8_t)(v.scene.pid - P_ADSR), v.scene.model, s->itype, (uint8_t)own_env(v.scene.model),
                              v.scene.model == M_KIT, {v.scene.v[0], v.scene.v[1], v.scene.v[2], v.scene.v[3]},
                              audio_hits(ui.track), audio_lfo_phase(ui.track), {0}, {0}, 0, 0};
                int sounding, n = 0, k;
                if (fresh)
                    scene_enter();
                pv.nheld = (int8_t)audio_arp(ui.track, pv.held, &sounding);
                pv.sounding = (int8_t)sounding;
                pv.chord[0] = (uint8_t)(60 + ui.p->tonic);  /* the tonic's 1, 3, 5: the scale's 3rd and 5th degrees */
                for (k = 0; k < 12; k++)
                    if (SCALE_MASK[ui.p->mode] >> k & 1 && (++n == 3 || n == 5))
                        pv.chord[n / 2] = (uint8_t)(pv.chord[0] + k);
                sound_frame(&pv, now);
            } else if (v.scene.anim == 8) {
                const q_pattern *t = pat();
                uint8_t rows[4];
                int n, k, r;
                if (fresh)
                    life_enter();
                life_run(t, &lfv.run);
                lfv.col = (int8_t)(v.scene.pos >= 0 ? v.scene.pos % LIFE_W : -1);
                lfv.played = 0;
                n = lfv.col >= 0 ? gen_life_rows(&t->life, kit(), lfv.run.grid, lfv.col, rows) : 0;
                for (k = 0; k < n; k++)
                    lfv.played |= (uint8_t)(1u << rows[k]);
                lfv.poly = (uint8_t)(kit() && t->life.poly > 3 ? 3 : t->life.poly);
                lfv.autos = t->life.autos, lfv.gen = t->life.gen, lfv.mut = t->life.mut, lfv.seed = t->life.seed;
                lfv.kit = (uint8_t)kit(), lfv.map = t->life.map;
                for (r = 0; r < LIFE_H; r++) {          /* the rows' names: their notes, a KIT's pads or bands */
                    static const char *const PADN[LIFE_H] = {"BD", "SN", "HH", "4", "5", "6", "7", "8"};
                    const char *s = !kit() ? 0 : !t->life.map ? PADN[r] : r == 1 ? "BD" : r == 4 ? "SN" : r == 6 ? "HH" : "";
                    lfv.eng[r] = ui.p->kit[ui.track][t->life.map ? (r < 3 ? 0 : r < 6 ? 1 : 2) : r].engine;
                    if (s) {
                        char *o = lfv.label[r];
                        while ((*o++ = *s++) != 0) {}
                    } else
                        note_name(gen_life_note(t->life.note, gen_scale(ui.p), ui.p->tonic, r), lfv.label[r]);
                }
                lfv.step_ms = 2500u * DIV96[t->div] / ui.p->tempo;
                life_frame(&lfv, now);
            } else if (v.scene.anim == 9) {
                const q_pattern *t = pat();
                uint32_t n = audio_lanes(ui.track);
                if (fresh)
                    lanes_enter();
                lnv.lanes = t->lanes;
                for (int l = 0; l < L_N; l++)  /* the heads: the step in progress, when the pattern shown plays */
                    lnv.head[l] = (int8_t)(!audio_playing() ? 0                  /* stopped: every head on cell 1 */
                                         : v.scene.pos >= 0 && n ? (int)((n - 1) % (uint32_t)lanes_len(&t->lanes, l)) : -1);
                lnv.sel = (uint8_t)ui.lane, lnv.kit = (uint8_t)kit();
                lnv.flash = (int8_t)(ui.cell_lane == ui.lane ? ui.cell_flash - 1 : -1), lnv.flash_ms = ui.cell_ms;
                for (int c = 0; c < 16; c++) {
                    if (kit()) {
                        char *o = lnv.name[c];
                        *o++ = 'P';
                        if (c >= 9) *o++ = '1';
                        *o++ = (char)('0' + (c + 1) % 10);
                        *o = 0;
                    } else {                    /* the note name without its octave: 3 characters at most */
                        char b[6], *o = lnv.name[c];
                        note_name(gen_life_note(t->lanes.note, gen_scale(ui.p), ui.p->tonic, c), b);
                        for (int i = 0; b[i] && i < 3 && (b[i] < '0' || b[i] > '9') && b[i] != '-'; i++) *o++ = b[i];
                        *o = 0;
                    }
                }
                lnv.n = n;
                lnv.step_ms = 2500u * DIV96[t->div] / ui.p->tempo;
                lanes_frame(&lnv, now);
            } else if (v.scene.anim == 10) {
                const q_pattern *t = pat();
                const q_chords *c = &t->chords;
                q_chords_run r;
                uint16_t sc = gen_scale(ui.p);
                audio_chords(ui.track, &r);
                if (fresh)
                    chords_enter();
                chv.chords = *c;
                chv.kit = (uint8_t)kit();
                chv.slot = (int8_t)(v.scene.pos >= 0 && r.n ? (int)((r.n - 1) / 16 % c->len) : -1);
                {                               /* a strike lights as it sounds: on the step playing, not when planned */
                    static int last_pos = -1;
                    static uint8_t strikes;
                    int every = 16 >> c->rate;
                    if (v.scene.pos >= 0 && v.scene.pos != last_pos && v.scene.pos % every == 0)
                        strikes++;
                    last_pos = v.scene.pos;
                    chv.strikes = strikes;
                    chv.step = (uint8_t)(v.scene.pos >= 0 ? v.scene.pos % 16 : 0);
                }
                chv.held = (int8_t)(chords_on() && ui.rec == R_GRID && ui.cell ? ui.cell - 1   /* a chord held, or the CHORD page's */
                                    : v.scene.pid == P_CHORD ? ui.chcur : -1);
                for (int d = 0; d < 7; d++)
                    chv.scale_pc[d] = (uint8_t)(gen_life_note(c->note, sc, ui.p->tonic, d) % 12);
                if (chv.held >= 0) {            /* the held chord: SMOOTH from the chord before it, that one in root position */
                    uint8_t prev[4];
                    int poly = ui.p->sound[ui.track].poly ? ui.p->sound[ui.track].poly : 1, h = chv.held;
                    q_chords root = *c;
                    root.voice = 0;
                    int np = c->voice && c->len > 1 ? gen_chord_notes(ui.p, &root, (h + c->len - 1) % c->len, poly, 0, 0, prev) : 0;
                    chv.n = (uint8_t)gen_chord_notes(ui.p, c, h, poly, prev, np, chv.notes);
                } else {
                    chv.n = chv.slot >= 0 ? r.nlast : 0;
                    __builtin_memcpy(chv.notes, r.last, 4);
                }
                int s = chv.held >= 0 ? chv.held : chv.slot;
                chv.name[0] = 0, chv.banner[0] = 0;
                if (s >= 0) {                   /* the chord's name: its root, m or dim from the scale, its size */
                    static const char *const N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
                    static const char *const SZ[CS_N] = {"", "7", "sus2", "sus4"}, *const SZB[CS_N] = {"", " 7", " SUS2", " SUS4"};
                    static const char *const RN[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};
                    int ch = c->ch[s], d = CH_DEG(ch), root = gen_life_note(c->note, sc, ui.p->tonic, d);
                    int third = (gen_life_note(c->note, sc, ui.p->tonic, d + 2) - root) % 12;
                    int fifth = (gen_life_note(c->note, sc, ui.p->tonic, d + 4) - root) % 12;
                    int seventh = (gen_life_note(c->note, sc, ui.p->tonic, d + 6) - root) % 12;
                    const char *q = CH_SIZE(ch) >= CS_SUS2 ? SZ[CH_SIZE(ch)]          /* the quality, from the scale */
                                  : __builtin_popcount(sc) != 7 ? ""                    /* not a 7-note scale: no quality to name */
                                  : CH_SIZE(ch) == CS_SEVENTH ? (fifth == 6 ? "m7b5" : third == 3 ? "m7" : seventh == 11 ? "maj7" : "7")
                                  : fifth == 6 ? "dim" : third == 3 ? "m" : "";
                    char *o = chv.name;
                    for (const char *p = N[root % 12]; *p; p++) *o++ = *p;
                    for (const char *p = q; *p && o < chv.name + 7; p++) *o++ = *p;
                    *o = 0;
                    if (chv.held >= 0) {        /* "EDIT 3 - IV F 7" */
                        o = chv.banner;
                        for (const char *p = "EDIT "; *p; p++) *o++ = *p;
                        *o++ = (char)('1' + s);
                        for (const char *p = " - "; *p; p++) *o++ = *p;
                        for (const char *p = RN[d]; *p; p++) *o++ = *p;
                        *o++ = ' ';
                        for (const char *p = N[root % 12]; *p; p++) *o++ = *p;
                        for (const char *p = SZB[CH_SIZE(ch)]; *p; p++) *o++ = *p;
                        *o = 0;
                    }
                }
                chv.step_ms = 2500u * DIV96[t->div] / ui.p->tempo;
                chords_frame(&chv, now);
            } else if (v.scene.anim == 11) {
                static const char *const NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
                static q_tz last;
                static int have, last_pos = -1;
                static uint8_t strikes;
                const q_pattern *t = pat();
                const q_tonnetz *z = &t->tonnetz;
                q_tonnetz_run r;
                q_tz cur;
                int base;
                uint8_t x[3];
                audio_tonnetz(ui.track, &r);
                if (fresh) {
                    tonnetz_enter();
                    have = 0;
                    for (int q = 0; q < 3; q++) tzw.trail[q].m = 255;
                }
                tzw.playing = (uint8_t)(v.scene.pos >= 0 && r.n);
                if (tzw.playing)
                    cur = r.cur, base = r.base;
                else                            /* stopped: the start */
                    gen_tz_start(ui.p, z, &cur, &base);
                if (!tzw.playing)               /* stopped: the start alone */
                    for (int q = 0; q < 3; q++) tzw.trail[q].m = 255;
                else if (have && (cur.i != last.i || cur.j != last.j || cur.m != last.m)) {   /* the three chords before */
                    tzw.trail[2] = tzw.trail[1], tzw.trail[1] = tzw.trail[0], tzw.trail[0] = last;
                }
                last = cur, have = 1;
                tzw.tz = *z, tzw.cur = cur, tzw.base = (int8_t)base, tzw.kit = (uint8_t)kit();
                tzw.applied = (int8_t)(tzw.playing ? r.applied : -1), tzw.chaos_hit = r.chaos_hit;
                tzw.scale = gen_scale(ui.p);
                tzw.n = tzw.playing ? r.nlast : 0;
                __builtin_memcpy(tzw.notes, r.last, 4);
                if (v.scene.pos >= 0 && v.scene.pos != last_pos && v.scene.pos % (16 >> z->rate) == 0)
                    strikes++;                  /* a strike lights as it sounds */
                last_pos = v.scene.pos;
                tzw.strikes = strikes;
                tzw.step = (uint8_t)(v.scene.pos >= 0 ? v.scene.pos % 16 : 0);
                gen_tz_pcs(cur, base, x);
                char *o = tzw.name;
                for (const char *p = NN[x[0]]; *p; p++) *o++ = *p;
                if (cur.m) *o++ = 'm';
                *o = 0;
                tzw.held = (int8_t)(tonnetz_on() && ui.rec == R_GRID && ui.cell ? ui.cell - 1   /* a step held, or the STEP page's */
                                    : v.scene.pid == P_TZSTEP ? ui.tzcur : -1);
                tzw.banner[0] = 0;
                if (tzw.held >= 0) {            /* "STEP 3 - R > DM": what the step would give from the chord shown */
                    static const char OPC[4] = {'-', 'P', 'L', 'R'};
                    int op = TZ_OP(z, tzw.held);
                    q_tz nx = gen_tz_step(cur, op, z->scale, tzw.scale, base);
                    gen_tz_pcs(nx, base, x);
                    o = tzw.banner;
                    for (const char *p = "STEP "; *p; p++) *o++ = *p;
                    *o++ = (char)('1' + tzw.held);
                    for (const char *p = " - "; *p; p++) *o++ = *p;
                    *o++ = OPC[op];
                    for (const char *p = " > "; *p; p++) *o++ = *p;
                    for (const char *p = NN[x[0]]; *p; p++) *o++ = *p;
                    if (nx.m) *o++ = 'm';
                    *o = 0;
                }
                tzw.step_ms = 2500u * DIV96[t->div] / ui.p->tempo;
                tonnetz_frame(&tzw, now);
            } else if (v.scene.anim == 12) {
                static const char *const PADS3[3] = {"BD", "SN", "HH"};
                const q_pattern *t = pat();
                if (fresh)
                    orbits_enter();
                orw.o = t->orbits, orw.kit = (uint8_t)kit();
                orw.playing = (uint8_t)(v.scene.pos >= 0);
                orw.n = orw.playing ? audio_orbits(ui.track) : 0;
                orw.sel = (int8_t)ui.moon;     /* PRESET's moon, on both pages */
                for (int m = 0; m < 4; m++) {   /* the moons' notes, or pads */
                    char *o = orw.name[m];
                    int val = t->orbits.val[m];
                    if (!kit())
                        note_name(gen_life_note(t->orbits.note, gen_scale(ui.p), ui.p->tonic, val), o);
                    else if (val < 3)
                        o[0] = PADS3[val][0], o[1] = PADS3[val][1], o[2] = 0;
                    else
                        o[0] = 'P', int_text(val + 1, o + 1);
                }
                orw.step_ms = 2500u * DIV96[t->div] / ui.p->tempo;
                orbits_frame(&orw, now);
            } else if (v.scene.anim == 13) {
                const q_pattern *t = pat();
                if (fresh)
                    mimic_enter();
                miw.m = t->mimic, miw.kit = (uint8_t)kit(), miw.listen = ui.rec == R_LISTEN;
                miw.playing = (uint8_t)(v.scene.pos >= 0);
                if (miw.playing)
                    audio_mimic(ui.track, &miw.r);
                else
                    __builtin_memset(&miw.r, 0, sizeof miw.r);
                miw.step_ms = 2500u * DIV96[t->div] / ui.p->tempo;
                mimic_frame(&miw, now);
            } else if (v.scene.anim == 14) {
                const q_pattern *t = pat();
                uint32_t n, L;
                if (fresh)
                    cipher_enter();
                civ.c = t->cipher, civ.kit = (uint8_t)kit(), civ.playing = (uint8_t)(v.scene.pos >= 0);
                n = civ.playing ? audio_cipher(ui.track) : 0, L = t->cipher.len;
                civ.cur = !civ.playing ? 0 : L ? (n + L - 1) % L : n ? n - 1 : 0;   /* the step played last */
                {
                    int p = gen_cipher_byte(&t->cipher, civ.cur) & 15;
                    uint16_t sc = gen_scale(ui.p);
                    mv_name(kit() ? KIT_NOTE + p : gen_life_note(t->cipher.note, sc, ui.p->tonic, p * t->cipher.range * __builtin_popcount(sc) / 16),
                            kit(), civ.name);
                }
                cipher_frame(&civ, now);
            } else if (v.scene.anim == 7) {
                static char title[14];
                int pid = v.scene.pid, k, w;
                if (fresh)
                    scene_enter();
                glv.page = pid == P_TEMPO ? GP_TEMPO : pid == P_SCALE ? GP_SCALE : pid == P_KEYS ? GP_KEYS : pid == P_GFX ? GP_FX : GP_SONG;
                for (k = 0; k < 4; k++)
                    glv.k[k] = v.foot.v[k];
                if (pid == P_GFX) {
                    glv.title = DIV_NAME[ui.p->dtime];
                } else {                    /* "TONIC MODE" */
                    const char *t = TONIC_NAME[ui.p->tonic], *m = MODE_NAME[ui.p->mode];
                    char *o = title;
                    while (*t)
                        *o++ = *t++;
                    *o++ = ' ';
                    while (*m)
                        *o++ = *m++;
                    *o = 0;
                    glv.title = title;
                }
                glv.notes = TONIC_NAME;
                glv.nopt[0] = glv.nopt[1] = 0;
                if (pid == P_SCALE) {
                    glv.opt[0] = QUANT_NAME, glv.nopt[0] = 3;
                    glv.opt[1] = CLICK_NAME, glv.nopt[1] = 4;
                } else if (pid == P_KEYS) {
                    glv.opt[0] = KEYS_NAME, glv.nopt[0] = 2;
                }
                glv.tonic = ui.p->tonic;
                glv.mask = SCALE_MASK[ui.p->mode];
                glv.pos = (int8_t)audio_pos(ui.track);
                glv.inscale = (uint8_t)keys_in_scale();
                glv.kit = (uint8_t)kit();
                glv.held = 0;
                for (k = 0; k < NKEYS; k++) {
                    if (ui.key_note[k] < 0)
                        continue;
                    if (pid == P_SCALE && ui.key_track[k] == ui.track && !kit())   /* a pad is no pitch */
                        glv.held |= 1u << ui.key_note[k] % 12;
                    if (pid == P_KEYS)
                        glv.held |= 1u << k;
                }
                for (w = 0; w < 16; w++)
                    glv.keyn[w] = (int8_t)(glv.inscale ? scale_note(w) : 53 + WHITE[w] + 12 * ui.octave);
                glv.hits = 0;
                for (k = 0; k < Q_TRACKS; k++)
                    glv.hits += audio_hits(k);
                glv.song.n = (int8_t)ui.p->nsong;
                glv.song.cursor = (int8_t)ui.entry;
                glv.song.playing = (int8_t)(audio_song_entry() + 1);
                glv.song.on = (int8_t)ui.p->song_on;
                __builtin_memcpy(glv.song.e, ui.p->song, sizeof glv.song.e);
                glo_frame(&glv, now);
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
                else if (v.scene.anim == 3)
                    marbles_band(-BAND_H * b);
                else if (v.scene.anim == 4)
                    grids_band(-BAND_H * b);
                else if (v.scene.anim == 5)
                    home_band(-BAND_H * b);
                else if (v.scene.anim == 6)
                    sound_band(-BAND_H * b);
                else if (v.scene.anim == 7)
                    glo_band(&glv, -BAND_H * b);
                else if (v.scene.anim == 8)
                    life_band(&lfv, -BAND_H * b);
                else if (v.scene.anim == 9)
                    lanes_band(&lnv, -BAND_H * b);
                else if (v.scene.anim == 10)
                    chords_band(&chv, -BAND_H * b);
                else if (v.scene.anim == 11)
                    tonnetz_band(&tzw, -BAND_H * b);
                else if (v.scene.anim == 12)
                    orbits_band(&orw, -BAND_H * b);
                else if (v.scene.anim == 13)
                    mimic_band(&miw, -BAND_H * b);
                else if (v.scene.anim == 14)
                    cipher_band(&civ, -BAND_H * b);
                else
                    plateau_band(-BAND_H * b);
                cv_blit(0, HEAD_H + BAND_H * b);
            }
        }
    } else {
        ui.scene_key = -1;          /* coming back to an animated page is a fresh arrival: no reaction to notes played meanwhile */
        if (ui.full || __builtin_memcmp(&v.scene, &ui.shown.scene, sizeof v.scene))
            for (b = 0; b < 2; b++) {
                cv_begin(240, BAND_H, C_BLACK);
                draw_scene(&v, -BAND_H * b);
                cv_blit(0, HEAD_H + BAND_H * b);
            }
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
    else if (!strcmp(what, "octave")) *got = ui.octave;
    else if (!strcmp(what, "sel")) *got = ui.sel;
    else if (!strcmp(what, "layer")) *got = ui.layer;
    else if (!strcmp(what, "nsteps_on")) {
        int i;
        for (*got = 0, i = 0; i < Q_STEPS; i++)
            *got += t->steps[i].flags & SF_ON ? 1 : 0;
    }
    else if (!strcmp(what, "leds_lit") || !strcmp(what, "leds_mid")) {
        uint32_t mid, lit = key_lights(&mid);
        *got = (int)(what[5] == 'l' ? lit : mid);
    } else if (!strcmp(what, "badge")) *got = ui.rec == R_ARM ? 4 : ui.rec == R_LISTEN ? 5 : badge();
    else if (!strcmp(what, "selref")) *got = ui.sel_ref + 1;
    else if (!strcmp(what, "clip_n")) for (*got = 1; *got < 4 && ui.clip.st.note[*got]; ++*got) {}
    else if (!strcmp(what, "clip_note")) *got = ui.clip.st.note[0];
    else if (!strcmp(what, "clip_on")) *got = ui.clip.st.flags & SF_ON;
    else if (!strcmp(what, "clip_locks")) *got = ui.clip.nlocks;
    else if (!strcmp(what, "spage")) *got = ui.spage + 1;
    else if (!strcmp(what, "model")) *got = s->model;
    else if (!strcmp(what, "poly")) *got = s->poly;
    else if (!strcmp(what, "tune")) *got = s->tune;
    else if (!strcmp(what, "harm")) *got = s->harm;
    else if (!strcmp(what, "timb")) *got = s->timb;
    else if (!strcmp(what, "decay")) *got = s->decay;
    else if (!strcmp(what, "names_wide")) {    /* the ENGINE choices' names wider than a footer column, at its least font */
        int i;
        for (*got = 0, i = 0; i < 96; i++)
            *got += text_w(&FONT_XS, HEX_NAME[i / 32][i % 32]) > 54;
        for (i = 0; i < 12; i++)
            *got += text_w(&FONT_XS, ARP_NAME[i]) > 54 || (i < 11 && text_w(&FONT_XS, CHORD_NAME[i]) > 54) ||
                    (i < 6 && text_w(&FONT_XS, WORDS_NAME[i]) > 54);
    }
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
    else if (!strcmp(what, "scene_key")) *got = ui.scene_key;
    else if (!strcmp(what, "flying")) *got = marbles_flying();
    else if (!strcmp(what, "plive") || !strcmp(what, "pend") || !strcmp(what, "pnotelen")) {
        static q_view pv;
        static q_plateau pp;
        view_now(&pv);
        plateau_view(&pv, &pp);
        *got = what[1] == 'l' ? pp.live : what[1] == 'e' ? pp.end : (int)strlen(pp.rows[0][1]);
    }
    else if (!strcmp(what, "hits")) *got = (int)audio_hits(ui.track);
    else if (!strncmp(what, "kit_hits_", 9)) *got = m >= 1 && m <= 3 ? (int)audio_kit_hits(ui.track, m - 1) : 0;
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
    else if (!strcmp(what, "keys")) *got = ui.p->keys;
    else if (!strcmp(what, "keynote")) *got = ui.last_note[ui.track];
    else if (!strcmp(what, "keyleds")) *got = (int)scale_keys();
    else if (!strcmp(what, "overdub")) *got = ui.rec == R_LIVE && audio_playing();
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
    else if (!strcmp(what, "msg_wipe")) *got = plat_ms() < ui.wipe_until;
    else if (!strcmp(what, "msg_random")) *got = plat_ms() < ui.rnd_until ? ui.rnd_kind : 0;
    else if (!strcmp(what, "msg_tap")) *got = plat_ms() < ui.tap_until;
    else if (!strcmp(what, "head_page_name")) *got = head_page_name;
    else if (!strcmp(what, "active")) *got = (int)audio_load().active;
    else if (!strcmp(what, "src")) *got = t->src;
    else if (!strcmp(what, "gx")) *got = t->grids.x;
    else if (!strcmp(what, "gy")) *got = t->grids.y;
    else if (!strcmp(what, "gdens1")) *got = t->grids.dens[0];
    else if (!strcmp(what, "mlen")) *got = t->marbles.len;
    else if (!strcmp(what, "mgate")) *got = t->marbles.gate;
    else if (!strcmp(what, "mnotes")) *got = t->marbles.notes[0] | t->marbles.notes[1] << 8;
    else if (!strcmp(what, "mdejavu")) *got = t->marbles.dejavu;
    else if (!strcmp(what, "mmode")) *got = t->marbles.mode;
    else if (!strcmp(what, "mtout")) *got = t->marbles.tout;
    else if (!strcmp(what, "gvoice")) *got = t->grids.voice;
    else if (!strcmp(what, "gnote")) *got = t->grids.note;
    else if (!strcmp(what, "gpad")) *got = t->grids.pad[t->grids.voice - 1] + 1;
    else if (!strcmp(what, "cyformula")) *got = t->cipher.formula;
    else if (!strcmp(what, "cya")) *got = t->cipher.a;
    else if (!strcmp(what, "cyb")) *got = t->cipher.b;
    else if (!strcmp(what, "cyc")) *got = t->cipher.c;
    else if (!strcmp(what, "cydens")) *got = t->cipher.dens;
    else if (!strcmp(what, "cylen")) *got = t->cipher.len;
    else if (!strcmp(what, "cystart")) *got = t->cipher.start;
    else if (!strcmp(what, "cyspeed")) *got = t->cipher.speed;
    else if (!strcmp(what, "cyrange")) *got = t->cipher.range;
    else if (!strcmp(what, "cynote")) *got = t->cipher.note;
    else if (!strcmp(what, "lpoly")) *got = t->life.poly;
    else if (!strcmp(what, "lnote")) *got = t->life.note;
    else if (!strcmp(what, "lmap")) *got = t->life.map;
    else if (!strcmp(what, "lseed")) *got = t->life.seed;
    else if (!strcmp(what, "ldens")) *got = t->life.dens;
    else if (!strcmp(what, "lauto")) *got = t->life.autos;
    else if (!strcmp(what, "lgen")) *got = t->life.gen;
    else if (!strcmp(what, "lmut")) *got = t->life.mut;
    else if (!strcmp(what, "lnbase")) *got = t->lanes.note;
    else if (!strcmp(what, "cnote")) *got = t->chords.note;
    else if (!strcmp(what, "clen")) *got = t->chords.len;
    else if (!strcmp(what, "crate")) *got = t->chords.rate;
    else if (!strcmp(what, "cstrum")) *got = t->chords.strum;
    else if (!strcmp(what, "cvoice")) *got = t->chords.voice;
    else if (!strcmp(what, "tnote")) *got = t->tonnetz.note;
    else if (!strcmp(what, "tlen")) *got = t->tonnetz.len;
    else if (!strcmp(what, "trate")) *got = t->tonnetz.rate;
    else if (!strcmp(what, "tstrum")) *got = t->tonnetz.strum;
    else if (!strcmp(what, "tvoice")) *got = t->tonnetz.voice;
    else if (!strcmp(what, "tchaos")) *got = t->tonnetz.chaos;
    else if (!strcmp(what, "tscale")) *got = t->tonnetz.scale;
    else if (!strcmp(what, "tret")) *got = t->tonnetz.ret;
    else if (!strcmp(what, "onote")) *got = t->orbits.note;
    else if (!strcmp(what, "omoons")) *got = t->orbits.moons;
    else if (!strcmp(what, "osel")) *got = ui.moon;
    else if (!strcmp(what, "morder")) *got = t->mimic.order;
    else if (!strcmp(what, "mdrift")) *got = t->mimic.drift;
    else if (!strcmp(what, "mloop")) *got = t->mimic.loop;
    else if (!strcmp(what, "mseed")) *got = t->mimic.seed;
    else if (!strcmp(what, "mn")) *got = t->mimic.n;
    else if (!strcmp(what, "mlisten")) { q_mimic_run r; audio_mimic(ui.track, &r); *got = r.listen; }
    else if (!strncmp(what, "mnote_", 6) && atoi(what + 6) >= 1 && atoi(what + 6) <= MIMIC_EV) *got = t->mimic.ev[atoi(what + 6) - 1][0];
    else if (!strncmp(what, "mgap_", 5) && atoi(what + 5) >= 1 && atoi(what + 5) <= MIMIC_EV) *got = t->mimic.ev[atoi(what + 5) - 1][1];
    else if (!strcmp(what, "chcur")) *got = ui.chcur;
    else if (!strcmp(what, "tzcur")) *got = ui.tzcur;
    else if (!strcmp(what, "mdraws")) *got = ui.draws;
    else if (!strncmp(what, "oper_", 5) && atoi(what + 5) >= 1 && atoi(what + 5) <= 4) *got = t->orbits.per[atoi(what + 5) - 1];
    else if (!strncmp(what, "opha_", 5) && atoi(what + 5) >= 1 && atoi(what + 5) <= 4) *got = t->orbits.pha[atoi(what + 5) - 1];
    else if (!strncmp(what, "oval_", 5) && atoi(what + 5) >= 1 && atoi(what + 5) <= 4) *got = t->orbits.val[atoi(what + 5) - 1];
    else if (!strncmp(what, "top_", 4) && atoi(what + 4) >= 1 && atoi(what + 4) <= 8) *got = TZ_OP(&t->tonnetz, atoi(what + 4) - 1);
    else if (!strncmp(what, "cdeg_", 5) && atoi(what + 5) >= 1 && atoi(what + 5) <= 8) *got = CH_DEG(t->chords.ch[atoi(what + 5) - 1]);
    else if (!strncmp(what, "csize_", 6) && atoi(what + 6) >= 1 && atoi(what + 6) <= 8) *got = CH_SIZE(t->chords.ch[atoi(what + 6) - 1]);
    else if (!strcmp(what, "lsel")) *got = ui.lane;
    else if (!strcmp(what, "lcell")) *got = ui.cell;
    else if (!strncmp(what, "lv_", 3) && what[3] >= '0' && what[3] < '0' + L_N && what[4] == '_' && atoi(what + 5) >= 1 && atoi(what + 5) <= 16)
        *got = lanes_get(&t->lanes, what[3] - '0', atoi(what + 5) - 1);
    else if (!strncmp(what, "llen_", 5) && what[5] >= '0' && what[5] < '0' + L_N && !what[6]) *got = lanes_len(&t->lanes, what[5] - '0');
    else if (!strcmp(what, "lempty")) {         /* the grid LIFE's scene shows */
        q_life_run r;
        life_run(t, &r);
        *got = 1;
        for (int k = 0; k < LIFE_H; k++)
            if (r.grid[k]) *got = 0;
    }
    else if (!strcmp(what, "mnote")) *got = t->marbles.note;
    else if (!strncmp(what, "trlen_", 6) && m >= 1 && m <= Q_TRACKS) *got = ui.shown.scene.tr[m - 1].len;
    else if (!strncmp(what, "trlevel_", 8) && m >= 1 && m <= Q_TRACKS) *got = ui.shown.scene.tr[m - 1].level;
    else if (!strcmp(what, "pad")) *got = ui.pad + 1;
    else if (!strncmp(what, "pad", 3) && u && m >= 1 && m <= Q_PADS) {
        const q_pad *pd = &ui.p->kit[ui.track][m - 1];
        if (!strncmp(what, "padtune_", 8)) *got = pd->tune;
        else if (!strncmp(what, "padharm_", 8)) *got = pd->harm;
        else if (!strncmp(what, "padengine_", 10)) *got = pd->engine;
        else if (!strncmp(what, "padlevel_", 9)) *got = pd->level;
        else if (!strncmp(what, "padchar_", 8)) *got = pd->chr;
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
