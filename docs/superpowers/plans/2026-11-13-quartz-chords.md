# QUARTZ source CHORDS — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** une sixième source de pattern, CHORDS : une progression de 1 à 8 accords en degrés de la gamme, frappée RATE fois par mesure, égrenée par STRUM, conduite par VOICE ; ses pages CHORDS et PROG, sa saisie en GRILLE et sa scène « Heptagone ».

**Architecture :** le jeu vit dans `gen.c` (`gen_step` pour `SRC_CHORDS`, état `q_chords_run` dans `q_gen`, `gen_chord_notes` partagé avec l'écran) ; STRUM passe par les bits 5–7 de `q_step.flags`, lus par `seq.c` ; `ui.c` gagne les pages, la saisie (sur l'état `ui.cell` de LANES) et l'anim 10 ; `app/scene_chords.c` dessine l'heptagone et le clavier.

**Tech Stack :** C (gnu11, sans libc ni libm) ; `tests/run_tests.sh` ; `tools/device/`.

**Spec :** `docs/superpowers/specs/2026-11-13-quartz-chords-design.md` ; maquette `docs/screens/chords-source.html`, fonction `drawCv(g, {})` (C1 et son mode tenu) : tons et formes tels quels, coordonnées de la spec section 6 (zone de scène 240 × 166, la maquette a 30 px d'en-tête en plus).

## Global Constraints

- Branche `chords` (partie de `lanes`, spec en `1a901bf`) ; identité `13540044+F0nski@users.noreply.github.com` ; pas de `Co-Authored-By` ; ne jamais ajouter `midi_out.txt` ni `base` ; `git status` avant chaque commit.
- Pas de libc ni libm ; `scene_chords.c` inclus dans `app_ui.c`, `tests/unit/scene_test.c` et `tests/unit/plateau_test.c` après `scene_lanes.c` ; adresses de `pq_init`, `seq_block`, `app_render`, `sysex_byte`, `fm1_timer5_irq` inchangées (`~/emsdk/upstream/bin/llvm-nm app/build/quartz.elf`) avant et après les Tasks 2 et 6.
- `Q_VERSION` 9, aucune migration. La build appareil (`./build.sh`) doit passer (marge du pool, `pool_spare` 4096).
- Valeurs de la spec : tailles TRIAD {0,2,4}, SEVENTH {0,2,4,6}, SUS2 {0,1,4}, SUS4 {0,3,4} ; ordre gardé sous POLY : fondamentale, 2e degré empilé, 4e (septième), 3e (quinte) ; RATE 0..4 → 1, 2, 4, 8, 16 frappes, `len` 128 / frappes ; STRUM 0..7, une unité = 1/24 de pas (200 dans les unités de `seq.c`, 4800 par pas) ; vélocité 100, première frappe d'une mesure accentuée, `prob` 20 ; SMOOTH : renversements k = 0..n−1 × octaves −2..+1, coût = somme des écarts à la note la plus proche de la frappe précédente, égalité → le plus grave, hors 1..127 écarté.
- Défauts : degrés 0, 5, 3, 4 en TRIAD, LEN 4, NOTE 48, RATE 2, STRUM 0, VOICE 1 (SMOOTH) ; `ch[4..7]` à 0.
- Textes de l'écran en anglais ; rose pour ce qui joue, blanc pour l'accord tenu.
- Mesures CPU sur l'appareil : en fin de chantier, pas dans ce plan.

## Review Focus

1. LEN raccourci pendant que l'accord en cours est au-delà (accord 6, LEN passe à 3) : l'accord suivant est `(n / 16) mod 3`, jamais un accord hors LEN (test `chords_len_shrink`, Task 1).
2. Taille qui change entre deux accords sous SMOOTH (3 notes puis 4, ou POLY 1) : aucun débordement de `chords_last`, voicing calculé sur ce qui existe (test `chords_smooth_counts`, Task 1).
3. SMOOTH sur des tours qui se répètent : pas de dérive d'octave d'un tour à l'autre (test `chords_smooth_stable`, Task 1).
4. Accord tenu + touche hors gamme (C# en C majeur) : rien ne change ; la LED de cette touche est éteinte (script `chords_entry.txt`, Task 4).
5. RATE 16 et STRUM 7 : les notes d'une frappe débordent sur la suivante ; chaque note part quand même, de la plus grave à la plus aiguë (test `chords_strum_overlaps`, Task 2).

---

### Task 1: le jeu et ses accords

**Files :** Modify `app/project.h`, `app/seq.c` (défauts), `app/store.c` (bornes), `app/gen.h`, `app/gen.c` ; Test `tests/unit/gen_test.c`, `tests/unit/store_test.c`

**Produces :**
```c
/* project.h */
enum { SRC_STEP, SRC_GRIDS, SRC_MARBLES, SRC_LIFE, SRC_LANES, SRC_CHORDS, SRC_N };
enum { CS_TRIAD, CS_SEVENTH, CS_SUS2, CS_SUS4, CS_N };            /* a chord's size */
/* note 1..127, len 1..8, rate 0..4 (1..16 strikes a bar), strum 0..7, voice 0 ROOT / 1 SMOOTH; ch[i]: degree bits 0-2
   (0..6), size bits 3-4 */
typedef struct { uint8_t note, len, rate, strum, voice, ch[8]; } q_chords;
#define CH_DEG(c) ((c) & 7)
#define CH_SIZE(c) ((c) >> 3 & 3)
#define CH_MAKE(d, s) ((uint8_t)((d) | (s) << 3))
/* q_pattern: q_chords chords; after q_lanes lanes */
#define Q_VERSION 9
#define SF_STRUM(s) ((s)->flags >> 5 & 7)                            /* a generator's step: its notes 1/24 step apart */

/* gen.h */
typedef struct { uint32_t n; uint8_t last[4], nlast, strikes; } q_chords_run;   /* steps since the reset; the last strike's notes; strikes (wrap) */
/* q_gen gains: q_chords_run chords; */
/* chord slot's notes, ascending, at most poly: ROOT, or SMOOTH from prev (nprev 0: root position). Returns their number. */
int gen_chord_notes(const q_project *p, const q_chords *c, int slot, int poly, const uint8_t *prev, int nprev, uint8_t out[4]);
```
`gen_loop` rend 16 pour `SRC_CHORDS`. `chords_step` (statique) : `c = chords.n++` ; KIT : rien ; slot `(c / 16) % len`, pas `c % 16`, frappe si `pas % (16 / frappes) == 0` ; notes par `gen_chord_notes(…, poly de la piste, last, nlast)` (nlast 0 au premier accord après le reset) ; `last`/`nlast` mis à jour, `strikes++` ; `len` 128 / frappes, `vel` 100, `prob` 20, bits 5–7 de `flags` = strum ; rend 1 (accent sur le slot 0) à la première frappe d'une mesure, sinon 0.

- [ ] **Step 1: Tests** dans `gen_test.c` (`chords_setup(model)` sur le modèle de `lanes_setup`, `src = SRC_CHORDS` ; aide `chords_strike(pt, n, st)` = `gen_step(…, n % 16, …)`) :
  - `chords_defaults` : degrés 0, 5, 3, 4 en TRIAD, LEN 4, NOTE 48, RATE 2, STRUM 0, VOICE 1 ; `sizeof(q_chords) == 13`.
  - `chords_one_per_bar` : RATE 0 : une seule frappe par mesure, au pas 0 ; ROOT ; les mesures 0..7 jouent les fondamentales 48, 57, 53, 55, 48, 57… ; `gen_reset` puis un pas : de nouveau 48.
  - `chords_sizes` : ROOT, POLY 4, C majeur : I TRIAD {48,52,55} ; vi {57,60,64} ; V SEVENTH {55,59,62,65} ; I SUS2 {48,50,55} ; I SUS4 {48,53,55}.
  - `chords_poly_cut` : POLY 2, I TRIAD → {48,52} ; POLY 3, V SEVENTH → {55,59,65}.
  - `chords_smooth` : SMOOTH, POLY 3, progression I puis IV : 2e accord {48,53,57} ; le premier après le reset {48,52,55}.
  - `chords_smooth_counts` : SMOOTH, I TRIAD puis V SEVENTH POLY 4, puis POLY 1 : notes dans 1..127, au plus POLY, sans assertion d'ASan.
  - `chords_smooth_stable` : défauts, 3 tours de la progression (12 mesures) : les notes des mesures 4..7 égalent celles des mesures 8..11.
  - `chords_note_clamp` : NOTE 127, SEVENTH, SMOOTH et ROOT : toutes les notes dans 1..127.
  - `chords_rate_len` : RATE 0..4 : 1, 2, 4, 8, 16 frappes sur 16 pas, `len` 128, 64, 32, 16, 8 ; la frappe du pas 0 accentuée, les autres non.
  - `chords_strum_bits` : STRUM 5 → `SF_STRUM(&st) == 5` et bits 0–4 de `flags` = `SF_ON`.
  - `chords_kit` : KIT : 16 pas, aucun `SF_ON`.
  - `chords_len_shrink` : RATE 0, LEN 8, 6 mesures jouées, LEN passe à 3 : la mesure suivante joue l'accord `6 mod 3` = 0.
  - `loop_lengths` : `SRC_CHORDS` → 16.
  Dans `store_test.c` : `chords_clamped_on_load` : note 0, len 0 et 12, rate 9, strum 9, voice 4, `ch[0]` degré 7 → 1, 1 et 8, 4, 7, 1, degré 6.
- [ ] **Step 2:** `tests/run_tests.sh` : échec de compilation (`SRC_CHORDS` inconnu).
- [ ] **Step 3:** Implémenter (`project.h`, défauts dans `project_defaults`, bornes dans `clamp_project` après `q_lanes`, `gen.h`, `gen.c`). Notes d'un accord : `gen_life_note(note, gen_scale(p), p->tonic, degré + écart)` pour chaque degré empilé, ordre de garde de la spec, puis tri croissant.
- [ ] **Step 4:** `tests/run_tests.sh` : tout passe ; `./build.sh` passe.
- [ ] **Step 5:** Commit `CHORDS: a chord progression source, its run in gen.c`.

### Task 2: STRUM dans le séquenceur

**Files :** Modify `app/seq.c` ; Test `tests/unit/seq_test.c`

**Consumes :** `SF_STRUM`, `SRC_CHORDS`, `q_chords` (Task 1).

**Produces :** dans `seq.c`, pour un pattern générateur, la note `i` d'un pas (dans l'ordre de `note[]`, croissant) part `200 × SF_STRUM(st) × i` unités plus tard (le pas fait 4800 unités) ; un pattern STEP ignore ces bits.

- [ ] **Step 1: Tests** dans `seq_test.c` (pattern 0 de la piste 0 en `SRC_CHORDS`, ROOT, RATE 0, POLY 3) :
  - `chords_strum_staggers` : STRUM 3 : les trois note-on de la frappe 0 à `at16(0, 0)`, `at16(0, 600)`, `at16(0, 1200)`, notes croissantes.
  - `chords_strum_zero` : STRUM 0 : les trois à `at16(0, 0)`.
  - `chords_strum_overlaps` : RATE 4 (16 frappes), STRUM 7 : sur deux pas, 6 note-on, chacun à `at16(k, 1400 × i)`.
  - `step_ignores_strum_bits` : pattern STEP, un pas avec `flags |= 7 << 5` et trois notes : les trois à `at16(k, 0)`.
- [ ] **Step 2:** `tests/run_tests.sh` : `seq_test` échoue.
- [ ] **Step 3:** Implémenter dans `seq.c` (la boucle des notes du pas, à côté de `f = 200 * st->micro …`). Relever les adresses `llvm-nm` avant et après.
- [ ] **Step 4:** `tests/run_tests.sh` : tout passe ; adresses inchangées.
- [ ] **Step 5:** Commit `CHORDS: STRUM, a generator step's notes one after the other`.

### Task 3: les pages SEQ de CHORDS

**Files :** Modify `app/ui.c` ; Create `app/tests/chords.txt`

**Consumes :** `q_chords`, `SRC_CHORDS` (Task 1).

**Produces :** `P_CHORDS, P_PROG` en fin de l'enum des pages ; leurs lignes de `PAGE[]` :
```c
{"CHORDS", {"DIV", "NOTE", "RATE", "SOURCE"}, {0, 1, 0, 0}, {DIV_N - 1, 127, 4, SRC_N - 1}},
{"PROG", {"LEN", "STRUM", "VOICE"}, {1, 0, 0}, {8, 7, 1}},
```
- `GEN_PAGES` devient `[5][5]` avec `{P_CHORDS, P_PROG}` ; `pages()` rend 2 pour CHORDS.
- Textes : RATE `1/BAR` `2/BAR` `4/BAR` `8/BAR` `16/BAR` ; VOICE `ROOT` / `SMOOTH` ; NOTE par `note_name` ; `SRC_NAME` gagne « CHORDS ».
- Sondes : `cnote`, `clen`, `crate`, `cstrum`, `cvoice`.

- [ ] **Step 1: Script** `app/tests/chords.txt` sur le modèle de `lanes.txt` : `turn K4 5` → `expect src 5` ; NOTE 1..127 ; RATE 0..4 ; `shot chords_page1.png` ; page 2 : LEN 1..8, STRUM 0..7, VOICE 0..1 ; `shot chords_page2.png` ; retour page 1.
- [ ] **Step 2:** `tests/run_tests.sh` : `chords.txt` échoue.
- [ ] **Step 3:** Implémenter dans `ui.c` (mêmes endroits que LANES : enum, `PAGE[]`, `GEN_PAGES`, `pages()`, `knob_get`, `knob_set`, textes des valeurs, sondes).
- [ ] **Step 4:** `tests/run_tests.sh` : tout passe ; regarder les captures.
- [ ] **Step 5:** Commit `CHORDS: its pages CHORDS and PROG`.

### Task 4: la saisie des accords

**Files :** Modify `app/ui.c`, `app/audio.h`, `app/audio.c` ; Create `app/tests/chords_entry.txt`

**Consumes :** `q_chords`, `CH_*`, `q_chords_run` (Task 1) ; l'état `ui.cell`, `cell_key`, `cell_edited`, `cell_ms`, `cell_flash` de LANES ; `P_CHORDS` (Task 3).

**Produces :**
```c
/* audio.h: a CHORDS pattern's run as the render left it */
void audio_chords(int track, q_chords_run *out);
```
Une aide `chords_on()` : `ui.domain == D_SEQ && pat()->src == SRC_CHORDS`. Sondes : `cdeg_<i>`, `csize_<i>` (i 1..8).

Règles (spec section 5) :
- `rec_release` et `badge` : CHORDS comme LANES (OFF ↔ GRID, badge GRID).
- En GRID, blanche w < 8 et w < LEN : appui → `cell = w + 1` (même état que LANES) ; sinon rien.
- Tenu + autre touche : `n = key_play_note(k)` ; si `n >= 0`, le degré d (0..6) tel que `gen_life_note(chords.note, gen_scale(ui.p), ui.p->tonic, d) % 12 == n % 12` ; `cell_edited = 1`. Aucun d (note hors gamme) : rien.
- Tenu + K1 : degré ± (0..6) ; + K2 : taille ± (0..3) ; K3/K4 : rien.
- Relâché court (≤ `HOLD_MS`) sans édition : taille suivante, modulo 4. Long : rien.
- La couche d'aide `B_CELL` de LANES ne s'affiche que pour un pattern LANES.
- LED : accords dans LEN à mi-niveau, celui qui joue plein (`(chords.n − 1) / 16 % len`, si le pattern affiché joue) ; un accord tenu : les touches dont `key_play_note` est dans la gamme.

- [ ] **Step 1: Script** `app/tests/chords_entry.txt` (C majeur, défauts) : `turn K4 5`, `tap REC` → `expect badge 1` ; `tapkey w2` → `expect csize_3 1` ; `key 4 down` (accord 3), `key 12 down`/`up` (`53 + 12` = 65, F), `key 4 up` → `expect cdeg_3 3`, `expect csize_3 1` (pas de changement de taille au relâchement) ; tenu + `key 1` (54, F#, hors gamme) → `cdeg_3` inchangé ; tenu + `turn K1 2` → degré + 2 ; tenu + `turn K2 1` → taille + 1 ; `tapkey w6` (accord 7, au-delà de LEN 4) → `expect lcell 0`, `cdeg_7` inchangé ; appui long (`key 0 down`, `wait 600`, `key 0 up`) → `csize_1` inchangé et `expect layer 0` pendant le maintien ; `tap REC` → `expect badge 0` ; `shot chords_entry.png`.
- [ ] **Step 2:** `tests/run_tests.sh` : échec (sondes inconnues).
- [ ] **Step 3:** Implémenter dans `ui.c`, `audio.h`, `audio.c` (`audio_chords` copie `seq.g[track].chords`).
- [ ] **Step 4:** `tests/run_tests.sh` : tout passe.
- [ ] **Step 5:** Commit `CHORDS: chords entered on the grid`.

### Task 5: la scène

**Files :** Create `app/scene_chords.c` ; Modify `app/scene.h`, `app/app_ui.c`, `tests/unit/scene_test.c`, `tests/unit/plateau_test.c`

**Consumes :** `q_chords`, `CH_*`, `CS_*` (Task 1).

**Produces :**
```c
/* scene.h: CHORDS' heptagon (scene_chords.c); ui.c fills the view */
typedef struct {
    q_chords chords;           /* the pattern's */
    int8_t slot, held;         /* the chord playing (-1 none: stopped or another pattern plays), the chord held (-1 none) */
    uint8_t notes[4], n;       /* the notes shown on the keyboard: the last strike's, or the held chord's */
    uint8_t strikes;           /* q_chords_run.strikes: a change lights the strike */
    uint8_t step, kit;         /* the step in the bar 0..15; a KIT */
    uint8_t scale_pc[7];       /* the degrees' pitch classes from NOTE (I..VII) */
    char name[8];              /* the chord's name: "C", "Am", "G7", "Fsus4" (the held one's when held) */
    char banner[24];           /* "EDIT 3 · IV F" when held */
    uint32_t step_ms;          /* a step in ms (STRUM's delay: step_ms x strum / 24) */
} q_chview;
void chords_enter(void);
void chords_frame(const q_chview *v, uint32_t ms);
void chords_band(const q_chview *v, int32_t oy);
```
Géométrie et tons : spec section 6 et `drawCv(g, {})` (y de la maquette − 30). Le polygone d'un accord relie ses degrés (au moins 3, comme `shape` dans la maquette), en `p3d_tri` depuis son centre ; le fantôme de l'accord précédent (`slot − 1` modulo LEN) en `p3d_line`. La frappe : quand `strikes` change, l'heure est notée ; la note i du clavier est blanche de `t + i × strum × step_ms / 24` à 140 ms plus tard.

- [ ] **Step 1: Tests** dans `scene_test.c` (`chview()` : défauts de Task 1, slot 0, notes {48,52,55}, held −1) :
  - `chords_budget` : SEVENTH, POLY 4, RATE 4, 16 crans : `p3d_count() <= 300`.
  - `chords_poly_pink` : du rose dans le triangle des sommets I, III, V ; aucun rose dans le losange du sommet IV.
  - `chords_idle` : slot −1 : aucun rose.
  - `chords_held_white` : held 2 (IV) : polygone en blanc sur IV, VI, I, plus de rose sur V ; pixels clairs du bandeau dans (124..234, 3..15).
  - `chords_keyboard` : les touches de 48, 52, 55 en rose ; 50 non.
  - `chords_strum_order` : STRUM 7, step_ms 125 : à +5 ms la touche 48 blanche et 55 non ; à +80 ms 55 blanche.
  - `chords_kit_text` : kit 1 : pas de polygone, du texte au centre.
  - `chords_knobs_visible` : chaque changement seul de `notes` (NOTE ou VOICE), `chords.rate`, `chords.len`, `chords.strum` (avec une frappe) change au moins un pixel.
- [ ] **Step 2:** `tests/run_tests.sh` : échec (`chords_frame` inconnu).
- [ ] **Step 3:** Écrire `app/scene_chords.c`, déclarer, inclure.
- [ ] **Step 4:** `tests/run_tests.sh` : tout passe.
- [ ] **Step 5:** Commit `CHORDS: its heptagon scene`.

### Task 6: la scène à l'écran

**Files :** Modify `app/ui.c`, `docs/superpowers/specs/2026-11-10-quartz-groovebox-design.md` ; Create `app/tests/chords_scene.txt`

**Consumes :** `q_chview`, `chords_*` (Task 5) ; `audio_chords` (Task 4) ; `gen_chord_notes` (Task 1).

**Produces :** sur `P_CHORDS` et `P_PROG`, `v->scene.anim = 10` ; la vue remplie dans `ui_frame` : `slot` depuis `audio_chords` quand `v.scene.pos >= 0` et `n > 0` ; `notes` = dernière frappe, ou, accord tenu, `gen_chord_notes` (VOICE, depuis l'accord qui le précède dans la progression, calculé en ROOT) ; `name`, `banner` (`EDIT <touche> · <degré> <note>[ 7| SUS2| SUS4]`), `scale_pc`, `step`, `step_ms = 2500 * DIV96[div] / tempo` ; `chords_band` dans la boucle des bandes.

- [ ] **Step 1: Script** `app/tests/chords_scene.txt` : `turn K4 5`, `expect anim 10`, `tap PLAY`, `wait 2500`, `shot chords_play.png` ; `tap REC`, `key 4 down`, `wait 200`, `shot chords_held.png`, `key 4 up` ; `turn SELECT 1`, `expect anim 10`, `turn K2 5` (STRUM), `wait 1500`, `shot chords_strum.png` ; KIT, `shot chords_kit.png` ; `tap PLAY`.
- [ ] **Step 2:** `tests/run_tests.sh` : échec (`anim` différent de 10).
- [ ] **Step 3:** Implémenter ; adresses `llvm-nm` avant et après ; spec globale section 6 : une ligne CHORDS.
- [ ] **Step 4:** `tests/run_tests.sh` : tout passe ; adresses inchangées ; `./build.sh` passe ; regarder les captures.
- [ ] **Step 5:** Commit `CHORDS: the scene on the SEQ pages`.

Après la Task 6 : revue de la branche, puis test sur l'appareil par l'utilisateur ; mesure CPU reportée à la session finale.
