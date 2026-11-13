# QUARTZ source TONNETZ — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** une septième source de pattern, TONNETZ : un accord par mesure qui marche sur le Tonnetz par une séquence écrite de P, L, R (avec CHAOS, SCALE FREE / IN SCALE, RETURN), joué comme CHORDS ; ses pages TONNETZ, WALK, PLAY, sa saisie en GRILLE et sa scène « carte qui suit ».

**Architecture :** le jeu vit dans `gen.c` (`gen_step` pour `SRC_TONNETZ`, état `q_tonnetz_run` dans `q_gen`, fonctions du Tonnetz partagées avec l'écran) ; le SMOOTH de CHORDS devient une fonction statique partagée ; `ui.c` gagne les pages, la saisie (état `ui.cell`) et l'anim 11 ; `app/scene_tonnetz.c` dessine la carte. `seq.c` n'est pas touché (STRUM y est déjà).

**Tech Stack :** C (gnu11, sans libc ni libm) ; `tests/run_tests.sh`.

**Spec :** `docs/superpowers/specs/2026-11-13-quartz-tonnetz-design.md` ; maquette `docs/screens/tonnetz.html`, `drawT1(g, false)`, `lattice`, `topStrip` : tons et formes tels quels, y de la maquette − 30.

## Global Constraints

- Branche `tonnetz` (partie de `chords`, spec en `b3191b0`) ; identité `13540044+F0nski@users.noreply.github.com` ; pas de `Co-Authored-By` ; jamais `midi_out.txt` ni `base` ; `git status` avant chaque commit.
- Pas de libc ni libm (`fm_cosf`…) ; `scene_tonnetz.c` inclus dans `app_ui.c`, `tests/unit/scene_test.c`, `tests/unit/plateau_test.c` après `scene_chords.c` ; adresses `llvm-nm` (`~/emsdk/upstream/bin/llvm-nm`) de `pq_init`, `seq_block`, `app_render`, `sysex_byte`, `fm1_timer5_irq` relevées après la Task 1 et inchangées après la Task 5.
- `Q_VERSION` 10, aucune migration ; `./build.sh` doit passer (pool).
- Tonnetz : nœud `(i, j)` = classe `7i + 4j` ; majeur `(i, j)` = `(i,j) (i+1,j) (i,j+1)`, mineur `(i, j)` = `(i+1,j) (i,j+1) (i+1,j+1)` ; P : majeur→mineur `(i, j−1)`, mineur→majeur `(i, j+1)` ; L : `(i, j)` ↔ `(i, j)` ; R : majeur→mineur `(i−1, j)`, mineur→majeur `(i+1, j)`.
- Séquence : `seq` 16 bits, pas i dans les bits `2i..2i+1`, 0 `—`, 1 P, 2 L, 3 R ; défauts R L R L (LEN 4), NOTE 48, RATE 2, STRUM 0, VOICE 1, CHAOS 0, SCALE 1 (IN SCALE), RET 1.
- CHAOS : probabilité `chaos / 128` par pas appliqué, remplacement par l'une des deux autres de P, L, R (un `—` : P, L ou R), tirage par le `rng` du séquenceur.
- Jeu d'accord : comme CHORDS (RATE, STRUM, accent sur toutes les notes de la première frappe, vélocité 100, `prob` 20, POLY : fondamentale, tierce, quinte ; SMOOTH avec la basse à 7 demi-tons au plus de celle de ROOT ; POLY monté à 3 en réglant la source ; KIT : rien).
- Textes de l'écran en anglais ; rose pour ce qui joue, blanc pour le tenu, orange pour CHAOS et RETURN.
- Mesures CPU : en fin de chantier.

## Review Focus

1. Marche FREE longue (des milliers de mesures, CHAOS 127) : `(i, j)` ne déborde pas et la carte reste centrée (test `tonnetz_long_walk`, Task 1 ; la scène ne dépend que de positions relatives, Task 4).
2. LEN réduit pendant la lecture, RETURN actif : la mesure suivante applique un pas < LEN ou revient au départ, jamais un pas hors LEN (test `tonnetz_len_shrink`, Task 1).
3. IN SCALE dans une gamme où aucun voisin ne reste dans la gamme (CHROM : tous restent ; pentatonique : peu) : l'accord reste, rien ne casse (test `tonnetz_in_scale_stuck`, Task 1).
4. NOTE changée pendant la lecture : la marche continue depuis l'accord en cours, les noms des nœuds suivent (script `tonnetz_scene.txt`, capture, Task 5).
5. Pas tenu puis pattern changé : le relâchement n'écrit rien dans l'autre pattern (règle `ui.cell` existante ; script `tonnetz_entry.txt`, Task 3).

---

### Task 1: le jeu

**Files :** Modify `app/project.h`, `app/seq.c` (défauts), `app/store.c`, `app/gen.h`, `app/gen.c` ; Test `tests/unit/gen_test.c`, `tests/unit/store_test.c`

**Produces :**
```c
/* project.h */
enum { SRC_STEP, SRC_GRIDS, SRC_MARBLES, SRC_LIFE, SRC_LANES, SRC_CHORDS, SRC_TONNETZ, SRC_N };
enum { TZ_STAY, TZ_P, TZ_L, TZ_R };
/* note 1..127, len 1..8, rate 0..4, strum 0..7, voice 0..1, chaos 0..127, scale 0 FREE / 1 IN SCALE, ret 0..1; seq: step i in bits 2i..2i+1 */
typedef struct { uint8_t note, len, rate, strum, voice, chaos, scale, ret; uint16_t seq; } q_tonnetz;
#define TZ_OP(t, i) ((t)->seq >> 2 * (i) & 3)
#define TZ_SET(t, i, o) ((t)->seq = (uint16_t)(((t)->seq & ~(3u << 2 * (i))) | (unsigned)(o) << 2 * (i)))
/* q_pattern: q_tonnetz tonnetz; after q_chords chords */
#define Q_VERSION 10

/* gen.h */
typedef struct { int16_t i, j; uint8_t m; } q_tz;          /* a triad on the Tonnetz: m 0 major, 1 minor */
typedef struct { uint32_t n; q_tz cur; int8_t base, applied; uint8_t chaos_hit, last[4], nlast; } q_tonnetz_run;
/* base: the pitch class added to the lattice's (the start's root at (0, 0)); applied: the step of the last bar, -1 start or return */
/* q_gen gains: q_tonnetz_run tonnetz; */
q_tz gen_tz_move(q_tz s, int op);                        /* P, L, R; TZ_STAY: s */
void gen_tz_pcs(q_tz s, int base, uint8_t pcs[3]);       /* root, third, fifth */
q_tz gen_tz_step(q_tz s, int op, int in_scale, uint16_t scale, int base);   /* IN SCALE: from op, P->L->R->P, the first in the scale, else s */
void gen_tz_start(const q_project *p, const q_tonnetz *t, q_tz *s, int *base);   /* the start triad at (0, 0) and its base */
```
`gen_loop` rend 16 pour `SRC_TONNETZ`. Le SMOOTH de `gen_chord_notes` passe dans `static void voice_smooth(int t[4], int n, const uint8_t *prev, int nprev)`, appelé par `gen_chord_notes` et par `tonnetz_step` (les tests CHORDS ne changent pas). `tonnetz_step(g, p, track, pt, k, rng, st)` : aligné sur le tour comme CHORDS (`n` = k au premier pas) ; au pas 0 d'une mesure `b = n / 16` : `b == 0` → départ (`gen_tz_start`), sinon avec RETURN `r = (b − 1) % (len + 1)`, `r == len` → départ, sinon pas `r` ; sans RETURN pas `(b − 1) % len` ; CHAOS ; `gen_tz_step` ; puis les frappes comme `chords_step` (notes ROOT : fondamentale dans `[note, note + 11]`, tierce et quinte au-dessus, coupées à POLY ; SMOOTH : `voice_smooth`).

- [ ] **Step 1: Tests** dans `gen_test.c` (`tonnetz_setup(model)` sur le modèle de `chords_setup`, `src = SRC_TONNETZ`, POLY 3) :
  - `tonnetz_defaults` : R L R L, LEN 4, NOTE 48, RATE 2, VOICE 1, CHAOS 0, SCALE 1, RET 1 ; `sizeof(q_tonnetz) == 10`.
  - `tonnetz_moves` : depuis C (base 0) : P → {0,3,7} (Cm), L → {4,7,11} (Em), R → {9,0,4} (Am) ; depuis Am : P → A {9,1,4}, L → F {5,9,0}, R → C {0,4,7} ; chaque transformation partage deux classes avec l'accord de départ ; P(P(s)) == s, idem L, R.
  - `tonnetz_start` : C majeur, NOTE 48 → C {0,4,7} ; NOTE 50 → Dm {2,5,9} ; NOTE 59 (B, diminuée) → mineure {11,2,6}.
  - `tonnetz_walk_free` : SCALE FREE, RETURN 0, RATE 0, ROOT : fondamentales des mesures 0..5 : C, A, F, D, A# (B♭), G (R L R L puis R depuis B♭ = Gm).
  - `tonnetz_walk_return` : RETURN, IN SCALE : mesures 0..5 : C, Am, F, Dm, F (L depuis Dm donne B♭, hors gamme → R, l'essai suivant à partir de L, donne F), puis C.
  - `tonnetz_in_scale` : 16 tours de séquences P P P P et L R P L en IN SCALE : chaque note jouée est dans C majeur.
  - `tonnetz_in_scale_stuck` : gamme majeure pentatonique (l'index de `MAJ PENTA` dans `MODE_NAME` de `ui.c`, qui suit `SCALE_MASK`), séquence P : l'accord reste quand aucune transformation ne reste dans la gamme ; rien ne casse.
  - `tonnetz_chaos` : CHAOS 0 : deux départs (`gen_reset`) donnent la même suite ; CHAOS 127 : sur 40 mesures, au moins 30 pas appliqués différents de la séquence (`chaos_hit` à 1 sur ces mesures).
  - `tonnetz_len_shrink` : RETURN, LEN 8, 6 mesures, LEN passe à 3 : `applied` < 3 ou −1 à la mesure suivante.
  - `tonnetz_long_walk` : FREE, CHAOS 127, 4000 mesures : `cur.i`, `cur.j` dans ±2000, notes dans 1..127.
  - `tonnetz_play_like_chords` : RATE 0..4 frappes et `len`, accent (masque 7 pour 3 notes), STRUM dans `flags`, KIT : rien.
  - `loop_lengths` : `SRC_TONNETZ` → 16.
  `store_test.c` : `tonnetz_clamped_on_load` : note 0, len 0 et 9, rate 7, strum 9, voice 3, chaos 200, scale 5, ret 4 → 1, 1 et 8, 4, 7, 1, 127, 1, 1.
- [ ] **Step 2:** `tests/run_tests.sh` : échec de compilation.
- [ ] **Step 3:** Implémenter ; relever les adresses `llvm-nm` après `./build.sh` (référence pour la Task 5).
- [ ] **Step 4:** `tests/run_tests.sh` : tout passe, tests CHORDS compris ; `./build.sh` passe.
- [ ] **Step 5:** Commit `TONNETZ: a chord walk on the Tonnetz, its run in gen.c`.

### Task 2: les pages SEQ

**Files :** Modify `app/ui.c` ; Create `app/tests/tonnetz.txt`

**Consumes :** `q_tonnetz`, `SRC_TONNETZ` (Task 1).

**Produces :** `P_TONNETZ, P_WALK, P_PLAY` en fin de l'enum des pages :
```c
{"TONNETZ", {"DIV", "NOTE", "RATE", "SOURCE"}, {0, 1, 0, 0}, {DIV_N - 1, 127, 4, SRC_N - 1}},
{"WALK", {"LEN", "CHAOS", "SCALE", "RETURN"}, {1, 0, 0, 0}, {8, 127, 1, 1}},
{"PLAY", {"STRUM", "VOICE"}, {0, 0}, {7, 1}},
```
`GEN_PAGES[6][5]` avec `{P_TONNETZ, P_WALK, P_PLAY}` ; `pages()` rend 3 ; textes RATE comme CHORDS, SCALE `FREE`/`IN SCALE`, RETURN `OFF`/`ON`, VOICE `ROOT`/`SMOOTH` ; `SRC_NAME` « TONNETZ » ; régler SOURCE sur TONNETZ (synthé, POLY < 3) : POLY 3 ; sondes `tnote`, `tlen`, `trate`, `tstrum`, `tvoice`, `tchaos`, `tscale`, `tret`.

- [ ] **Step 1: Script** `app/tests/tonnetz.txt` : `turn K4 6` → `expect src 6`, `expect poly 3` ; bornes de chaque encodeur des 3 pages ; une capture par page.
- [ ] **Step 2:** échec. **Step 3:** implémenter. **Step 4:** tout passe ; captures. **Step 5:** commit `TONNETZ: its pages TONNETZ, WALK and PLAY`.

### Task 3: la saisie

**Files :** Modify `app/ui.c`, `app/audio.h`, `app/audio.c` ; Create `app/tests/tonnetz_entry.txt`

**Consumes :** `q_tonnetz`, `TZ_*` (Task 1) ; `ui.cell` et ses règles ; `P_TONNETZ` (Task 2).

**Produces :** `void audio_tonnetz(int track, q_tonnetz_run *out);` ; `tonnetz_on()` ; sondes `top_<i>` (i 1..8).

Règles (spec section 5) : REC OFF ↔ GRID et badge comme CHORDS (NOTES restant d'un STEP : badge 0) ; blanche w < 8 et < LEN : appui → `cell` ; relâché court sans autre touche : opération suivante (`(op + 1) % 4`) ; tenu + noire 1..4 (`BLACK[0..3]`) : P, L, R, `—` ; tenu + K1 : opération ± (0..3) ; autres encodeurs : rien ; LED : pas dans LEN mi-niveau, `applied` plein (si le pattern affiché joue) ; tenu : les noires 1–4 ; pas de couche d'aide.

- [ ] **Step 1: Script** `app/tests/tonnetz_entry.txt` : `turn K4 6`, `tap REC` → badge 1 ; `tapkey w0` → `expect top_1 0` (R → `—`) ; `tapkey w0` → `top_1 1` ; `key 4 down`, `key 3 down`/`up` (noire 2 : vérifier `BLACK[1]` = touche 3), `key 4 up` → `top_3 2` (L) ; tenu + `turn K1 1` → +1 ; tenu + une blanche → aucune écriture, relâché sans changement ; `tapkey w5` (au-delà de LEN 4) → `lcell 0` ; tenu puis SEQ tenu + blanche 2 (pattern 2), relâché → `top_` du pattern 2 inchangé ; `shot tonnetz_entry.png`.
- [ ] **Step 2:** échec. **Step 3:** implémenter. **Step 4:** tout passe. **Step 5:** commit `TONNETZ: steps entered on the grid`.

### Task 4: la scène

**Files :** Create `app/scene_tonnetz.c` ; Modify `app/scene.h`, `app/app_ui.c`, `tests/unit/scene_test.c`, `tests/unit/plateau_test.c`

**Consumes :** `q_tz`, `gen_tz_pcs`, `gen_tz_step`, `q_tonnetz`, `TZ_OP` (Task 1).

**Produces :**
```c
/* scene.h: TONNETZ's map (scene_tonnetz.c); ui.c fills the view */
typedef struct {
    q_tonnetz tz;              /* the pattern's */
    q_tz cur, trail[3];        /* the chord shown and the three before (trail[k].m 255: none) */
    int8_t base, applied, held;   /* the pitch-class base; the step of the last bar (-1 none); the step held (-1 none) */
    uint8_t chaos_hit, playing, kit, step;   /* the last step swapped by CHAOS; the pattern shown plays; a KIT; the step in the bar */
    uint8_t notes[4], n, strikes;            /* the last strike's notes (ascending); a change lights the strike */
    uint16_t scale;            /* the project's scale, absolute pitch classes */
    char name[8], banner[24];  /* the chord's name; "STEP 3 - R > DM" when held */
    uint32_t step_ms;
} q_tzview;
void tonnetz_enter(void);
void tonnetz_frame(const q_tzview *v, uint32_t ms);
void tonnetz_band(const q_tzview *v, int32_t oy);
```
Géométrie : spec section 6 (maille 46 × 40, centre de l'accord à (120, 92), caméra qui rejoint le centre en 250 ms, positions relatives à la caméra : seule la différence `cur − caméra` compte, ce qui tient la marche longue) ; nœuds et noms en `cv_rect` / `glo_text` après `p3d_draw` ; arêtes, traces et facettes en primitives ; bande du haut et bandeau comme la maquette `topStrip`.

- [ ] **Step 1: Tests** dans `scene_test.c` (`tzview()` : défauts, C au départ, notes {48,52,55}) : `tonnetz_budget` (≤ 300) ; `tonnetz_triangle_pink` (rose au centre de l'accord, à (120, 92)) ; `tonnetz_octaves` (pixels de texte au-dessus des 3 nœuds de l'accord ; repère blanc sous le nœud de 48) ; `tonnetz_strum_nodes` (STRUM 7 : à +5 ms le nœud de 48 agrandi, pas celui de 55 ; à +80 ms celui de 55) ; `tonnetz_scale_dims` (IN SCALE : le nœud de C# plus sombre qu'en FREE) ; `tonnetz_chaos_tile` (`chaos_hit` : orange dans la tuile du pas appliqué) ; `tonnetz_held_banner` ; `tonnetz_idle` (pas de rose à l'arrêt) ; `tonnetz_far_walk` (cur à (900, −700) : même budget et triangle au centre) ; `tonnetz_knobs_visible` (chaque réglage du tableau de la spec change un pixel).
- [ ] **Step 2:** échec. **Step 3:** écrire la scène. **Step 4:** tout passe. **Step 5:** commit `TONNETZ: its map scene`.

### Task 5: la scène à l'écran

**Files :** Modify `app/ui.c`, `docs/superpowers/specs/2026-11-10-quartz-groovebox-design.md` ; Create `app/tests/tonnetz_scene.txt`

**Consumes :** `q_tzview`, `tonnetz_*` (Task 4) ; `audio_tonnetz` (Task 3) ; `gen_tz_*` (Task 1).

**Produces :** sur `P_TONNETZ`, `P_WALK`, `P_PLAY`, `anim = 11` ; vue remplie dans `ui_frame` (trace des trois accords précédents tenue côté UI au changement de `cur` ; frappes comptées sur le pas qui sonne, comme CHORDS ; nom : fondamentale + `m` ; bandeau `STEP <n> - <op> > <ACCORD>` avec `gen_tz_step` depuis l'accord en cours) ; à l'arrêt, l'accord de départ ; `tonnetz_band` dans la boucle des bandes ; spec globale section 6 : une ligne TONNETZ.

- [ ] **Step 1: Script** `app/tests/tonnetz_scene.txt` : `turn K4 6`, `expect anim 11`, `tap PLAY`, `wait 3000`, `shot tonnetz_play.png` ; NOTE changée (`spin K2 2`), `wait 1500`, `shot tonnetz_note.png` ; `tap REC`, `key 4 down`, `wait 200`, `shot tonnetz_held.png`, `key 4 up` ; page WALK, CHAOS 127, `wait 3000`, `shot tonnetz_chaos.png` ; KIT, `shot tonnetz_kit.png`.
- [ ] **Step 2:** échec (`anim`). **Step 3:** implémenter ; adresses inchangées depuis la Task 1. **Step 4:** tout passe ; `./build.sh` ; regarder les captures. **Step 5:** commit `TONNETZ: the scene on the SEQ pages`.

Après la Task 5 : revue de la branche par un relecteur neuf, corrections, puis test sur l'appareil.
