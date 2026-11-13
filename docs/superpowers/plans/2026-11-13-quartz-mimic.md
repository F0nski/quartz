# QUARTZ source MIMIC — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** une neuvième source de pattern, MIMIC : une phrase apprise au clavier en LISTEN (notes et écarts), improvisée à la manière du Continuator (ORDER, DRIFT, LOOP, SEED) ; ses pages MIMIC et LOOP, PRESET = nouveau tirage, et sa scène « tête de lecture en couleurs ».

**Architecture :** le jeu et l'apprentissage (fonctions pures) dans `gen.c`, le rendu `q_mimic_run` dans `q_gen` ; `audio.c` gagne `CMD_LISTEN` et `audio_mimic` ; `ui.c` gagne les pages, PRESET, les LED, l'état `R_LISTEN` qui écrit la phrase, et l'anim 13 ; `app/scene_mimic.c` dessine les deux rouleaux.

**Tech Stack :** C (gnu11, sans libc ni libm) ; `tests/run_tests.sh`.

**Spec :** `docs/superpowers/specs/2026-11-13-quartz-mimic-design.md` ; maquette `docs/screens/mimic.html` (`vc`, `improv`, `layout`, `labels`, `topBar`) : formes et tons tels quels, y de la maquette − 30.

## Global Constraints

- Branche `mimic` (partie de `orbits`, spec en `c14b0b6`) ; identité `13540044+F0nski@users.noreply.github.com` ; pas de `Co-Authored-By` ; jamais `midi_out.txt` ni `base` ; `git status` avant chaque commit.
- Pas de libc ni libm ; `scene_mimic.c` inclus dans `app_ui.c`, `tests/unit/scene_test.c`, `tests/unit/plateau_test.c` après `scene_orbits.c` ; adresses `llvm-nm` (`~/emsdk/upstream/bin/llvm-nm`) des cinq symboles audio relevées après la Task 1 et inchangées après la Task 5.
- `Q_VERSION` 12, sans migration ; `./build.sh` passe et sa vérification de la marge du pool aussi (sinon `MIMIC_EV` 24, spec section 3).
- `q_mimic` : `n` 0..32, `order` 1..2, `drift` 0..127, `loop` 0 (FREE) ou 1..16, `seed` 0..255, `ev[i][0]` note 1..127, `ev[i][1]` écart 0..7. Défauts : notes 60 62 64 67 64 62 60, écarts 2 1 1 2 1 1 4, ORDER 1, DRIFT 16, LOOP 0, SEED 0.
- Note jouée : `ev[i][0]` ; KIT : `KIT_NOTE + (note − KIT_NOTE) mod 16` (modulo positif) ; `len` 4, `vel` 100, `prob` 20, aucun accent ; 4 notes au plus par pas.
- Textes en anglais ; quarts : `KBLUE`, `KGREEN`, `ORANGE`, `MB_WHITE` ; rose au jeu et pour DRIFT ; flèche d'un lien `#c9c3e4`.
- Mesures CPU en fin de chantier.

## Review Focus

1. LISTEN quitté sans note jouée : l'ancienne phrase reste intacte et rejoue (script `mimic.txt`, Task 3).
2. Phrase d'une seule note, ou toutes ses notes à écart 0 : pas de boucle infinie au même pas, 4 notes au plus puis attente d'un pas (test `mimic_all_chord`, Task 1).
3. ORDER, DRIFT ou la phrase changés pendant la lecture : `cur`/`prev` hors de la phrase (n réduit) ne lisent jamais au-delà de `n` (test `mimic_shrunk`, Task 1).
4. Changement de pattern, de piste ou de source en LISTEN : LISTEN quitté, le rendu reprend (script `mimic.txt`, Task 3).
5. Notes jouées très lentement (> 7 pas) ou le tempo au minimum : écart borné à 7, pas de débordement du calcul (test `mimic_gap`, Task 1).

---

### Task 1: le jeu

**Files :** Modify `app/project.h`, `app/seq.c` (défauts), `app/store.c` (bornes), `app/gen.h`, `app/gen.c` ; Test `tests/unit/gen_test.c`, `tests/unit/store_test.c`

**Produces :**
```c
/* project.h */
enum { ..., SRC_ORBITS, SRC_MIMIC, SRC_N };
#define MIMIC_EV 32
/* n 0..MIMIC_EV, order 1..2, drift 0..127, loop 0 FREE / 1..16, seed 0..255; ev[i]: note 1..127, gap 0..7 steps to the next */
typedef struct { uint8_t n, order, drift, loop, seed; uint8_t ev[MIMIC_EV][2]; } q_mimic;
/* q_pattern: q_mimic mimic; after q_orbits orbits */
#define Q_VERSION 12
/* gen.h */
typedef struct {
    uint32_t n, rng, at;            /* steps since the reset; MIMIC's xorshift; at: the n of the last step that played */
    int8_t cur, prev, from;         /* places in the phrase, -1 none; from: the place left before last[0] */
    uint8_t wait, seed, seeded, listen;   /* seed: the SEED rng was drawn from; seeded 0: draw at the next step */
    uint8_t last[4], nlast, jumps;  /* the last step's places; bit i: last[i] came from DRIFT */
} q_mimic_run;
/* q_gen: q_mimic_run mimic; */
int gen_mimic_gap(uint32_t ms, uint32_t step_ms, int lo);   /* ms in steps, rounded, lo..7 */
void gen_mimic_learn(q_mimic *m, int note, int gap);        /* the last event's gap := gap; note appended (gap 1); the oldest out past MIMIC_EV */
```
`gen_loop` → 16. `mimic_step` (statique, appelé par `gen_step`) suit la spec section 2 « Improviser » : rien si `listen` ou `n` 0 ; tirage de `rng` depuis `seed` quand `!seeded || seed != m->seed` ou au redépart LOOP (`loop && r->n % loop == 0`, avec `cur = prev = −1`, `wait = 0`) ; `r->n++` à chaque pas ; `cur`/`prev` ≥ `n` traités comme −1. Le xorshift : celui de `q_life_run` (graine non nulle : `seed * 2654435761u | 1`).

- [ ] **Step 1: Tests** dans `gen_test.c` (`mimic_setup(model, notes, gaps, count)` remplit `src = SRC_MIMIC`, la phrase, ORDER 1, DRIFT 0 ; `play(k)` appelle `gen_step` et rend les notes du pas) :
  - `mimic_defaults` : valeurs des Global Constraints ; `sizeof(q_mimic) == 69`.
  - `mimic_in_order` : phrase 60 62 64 écarts 1 2 1 : pas 0 → {60}, 1 → {62}, 2 → {}, 3 → {64}, 4 → {60}.
  - `mimic_chord` : phrase 60 64 67 écarts 0 0 2 : pas 0 → {60, 64, 67}, 1 → {}, 2 → {60, 64, 67}.
  - `mimic_all_chord` : phrase 60 62 64 65 67 écarts tous 0 : chaque pas joue 4 notes au plus et `gen_step` revient.
  - `mimic_order2` : phrase A B A C (60 62 60 64, écarts 1) : ORDER 2, sur 200 pas, après 62 puis 60 vient toujours 64 ; ORDER 1, après 60 sortent 62 et 64.
  - `mimic_fallback` : ORDER 2, `prev` sans place correspondante (phrase modifiée en cours) : l'ordre 1 joue quand même.
  - `mimic_drift` : DRIFT 127 sur 400 pas : chaque place de la phrase par défaut est jouée et `jumps` est vu à 1.
  - `mimic_loop` : LOOP 8, DRIFT 64 : les notes des pas 8..15 et 16..23 égalent celles des pas 0..7.
  - `mimic_seed` : deux runs SEED 5 → mêmes 64 pas ; SEED 6 → une suite différente (DRIFT 64) ; SEED changé en cours : le tirage suivant change.
  - `mimic_silent` : `listen` 1 → rien ; `n` 0 → rien.
  - `mimic_kit` : KIT, note 60 → `KIT_NOTE + 8` ; note 30 → `KIT_NOTE + 10`.
  - `mimic_shrunk` : `cur` 6 puis `n` 3 : le pas suivant joue une note de la phrase.
  - `mimic_gap` : `gen_mimic_gap(250, 125, 0)` 2 ; `(60, 125, 0)` 0 ; `(63, 125, 0)` 1 ; `(5000, 125, 0)` 7 ; `(0, 125, 1)` 1 ; `(4000000000u, 1000, 0)` 7.
  - `mimic_learn` : n 0, learn 60 g0, 62 g2 → ev {60,2},{62,1}, n 2 ; 33 learns → n 32, ev[0] le 2e appris.
  - `loop_lengths` : `SRC_MIMIC` → 16.
  `store_test.c` : `mimic_clamped_on_load` : n 40, order 0 et 3, drift 200, loop 20, note 0, écart 9 → 32, 1 et 2, 127, 16, 60, 7.
- [ ] **Step 2:** échec de compilation. **Step 3:** implémenter ; `./build.sh` (marge du pool) ; adresses `llvm-nm`. **Step 4:** tout passe. **Step 5:** commit `MIMIC: a phrase improvised Continuator-like, its run in gen.c`.

### Task 2: pages, PRESET, LED

**Files :** Modify `app/ui.c`, `app/audio.h`, `app/audio.c` ; Create `app/tests/mimic.txt` (première partie)

**Consumes :** `q_mimic`, `q_mimic_run` (Task 1).

**Produces :** `P_MIMIC, P_LOOP` en fin d'enum des pages ; `{"MIMIC", {"DIV", "ORDER", "DRIFT", "SOURCE"}, {0, 1, 0, 0}, {DIV_N - 1, 2, 127, SRC_N - 1}}`, `{"LOOP", {"LOOP"}, {0}, {16}}` (0 affiché `FREE`) ; `GEN_PAGES[8][5]` avec `{P_MIMIC, P_LOOP}` ; `SRC_NAME` « MIMIC » ; SOURCE → MIMIC monte POLY à 3 sur un synthé (ligne `SRC_ORBITS` de `ui.c:518`) ; PRESET : `seed = (seed + e) & 255` ; `void audio_mimic(int track, q_mimic_run *out);` (copie comme `audio_tonnetz`) ; LED : blanche `1 + last[0] * 8 / n` en lecture du pattern affiché ; sondes `msrc`… comme ORBITS : `morder`, `mdrift`, `mloop`, `mseed`, `mn`, `mnote_<i>`, `mgap_<i>` (i 1..32).

- [ ] **Step 1: Script** `app/tests/mimic.txt` : `turn K4 8` → `expect src 8`, `expect poly 3` ; bornes ORDER 1..2, DRIFT 0..127 ; page LOOP : bornes 0..16 ; `turn PRESET 1` → `expect mseed 1` ; `turn PRESET -2` → `expect mseed 255` ; `expect mn 7`, `expect mnote_4 67`, `expect mgap_7 4` ; captures des deux pages.
- [ ] **Step 2:** échec. **Step 3:** implémenter. **Step 4:** tout passe. **Step 5:** commit `MIMIC: its pages MIMIC and LOOP, PRESET draws again`.

### Task 3: LISTEN

**Files :** Modify `app/ui.c`, `app/audio.h`, `app/audio.c`, `docs/superpowers/specs/2026-11-10-quartz-groovebox-design.md` ; Modify `app/tests/mimic.txt` (deuxième partie)

**Consumes :** `gen_mimic_gap`, `gen_mimic_learn`, `q_mimic_run.listen` (Task 1) ; pages (Task 2).

**Produces :** `R_LISTEN` après `R_ARM` ; `void audio_mimic_listen(int track, int on);` → `CMD_LISTEN` : le rendu met `listen` ; à 0, il repart comme au reset (`cur = prev = from = −1`, `wait = 0`, `seeded = 0`, `n = 0`) ; REC court sur un pattern MIMIC en SEQ : `R_OFF` ↔ `R_LISTEN` (via `rec_set`, qui appelle `audio_mimic_listen`) ; `ui.listen_ms` (la dernière note apprise, 0 = aucune encore) ; une note du clavier en LISTEN sonne (chemin habituel) puis : première note → `n = 0` ; sinon gap `gen_mimic_gap(now − listen_ms, step_ms, 0)` ; `gen_mimic_learn` ; sortie de LISTEN après au moins une note : écart de la dernière `gen_mimic_gap(now − listen_ms, step_ms, 1)` ; `step_ms = 2500 * DIV96[div] / tempo` ; LISTEN quitté au changement de pattern, de piste, de source, et de domaine hors SEQ ; badge 5 `LISTEN`, clignotant comme ARM (la vue de l'en-tête affiche `LISTEN` pour 5) ; LED en LISTEN : blanches 1..`n * 8 / 32` ; spec globale sections 6 et 7.

- [ ] **Step 1: Script** (suite de `mimic.txt`) : `tap REC` → `expect rec 5` ; `key 1 down`, `key 1 up`, `wait 250` (DIV 1/16 à 120 BPM : 2 pas), `key 3 down`, `key 3 up`, `wait 125`, `tap REC` → `expect rec 0`, `expect mn 2`, `expect mgap_1 2`, `expect mgap_2 1` ; `tap REC`, `tap REC` sans note → `expect mn 2` (phrase gardée) ; LISTEN, 33 notes → `expect mn 32` ; LISTEN puis `turn PATTERN 1` → `expect rec 0` ; LISTEN puis SOURCE changée → `expect rec 0` ; `shot mimic_listen.png` en LISTEN.
- [ ] **Step 2:** échec. **Step 3:** implémenter. **Step 4:** tout passe. **Step 5:** commit `MIMIC: LISTEN learns the phrase from the keyboard`.

### Task 4: la scène

**Files :** Create `app/scene_mimic.c` ; Modify `app/scene.h`, `app/app_ui.c`, `tests/unit/scene_test.c`, `tests/unit/plateau_test.c`

**Consumes :** `q_mimic`, `q_mimic_run` (Task 1).

**Produces :**
```c
/* scene.h: MIMIC's two rolls (scene_mimic.c); ui.c fills the view */
typedef struct {
    q_mimic m;                 /* the pattern's */
    q_mimic_run r;             /* audio_mimic; zeroed when the pattern shown does not play */
    uint8_t playing, kit, listen;
    uint32_t step_ms;
} q_miview;
void mimic_enter(void);
void mimic_frame(const q_miview *v, uint32_t ms);
```
Positions, tailles et couleurs : spec section 5. La scène garde ses 24 derniers événements joués (place, pas `at`, bit DRIFT) en les relevant quand `r.at` change ; `mimic_enter` les vide. La tête avance de `min(1, (ms − t_at) / (step_ms × max(1, écart)))` dans sa case. Saut : `last[0] != (from + 1) % n` avec `from ≥ 0`. Cases, notes d'impro, tête et flèche en `p3d_tri`/`p3d_line` ; lignes, textes, losanges et pointillés en dessin direct après `p3d_draw`. Noms des pads sur un KIT : ceux d'ORBITS.

- [ ] **Step 1: Tests** dans `scene_test.c` : `mimic_budget` (32 événements, 24 joués : ≤ 300) ; `mimic_cells` (phrase par défaut : une case par événement, la 4e (67) sur la ligne la plus haute) ; `mimic_head` (`last[0]` 3 : un pixel rose dans la case 3 du rouleau YOU) ; `mimic_jump_arrow` (`from` 1, `last[0]` 5 : flèche ; bit DRIFT : rose ; sans : `#c9c3e4` ; `from` 4, `last[0]` 5 : pas de flèche) ; `mimic_listen` (`listen` 1 : ni tête ni impro, dernière case rose) ; `mimic_knobs_visible` (ORDER, DRIFT, LOOP, SEED, DIV via `step_ms`, `listen` changent un pixel).
- [ ] **Step 2:** échec. **Step 3:** écrire la scène. **Step 4:** tout passe. **Step 5:** commit `MIMIC: its two-roll scene`.

### Task 5: la scène à l'écran

**Files :** Modify `app/ui.c` ; Create `app/tests/mimic_scene.txt`

**Consumes :** `q_miview`, `mimic_*` (Task 4) ; `audio_mimic` (Task 2) ; `R_LISTEN` (Task 3).

**Produces :** `P_MIMIC`, `P_LOOP` → `anim = 13` ; vue : `m`, `r = audio_mimic` si le pattern affiché joue (sinon zéro, `playing` 0), `kit`, `listen = ui.rec == R_LISTEN`, `step_ms` ; `mimic_enter` au changement d'anim, comme les autres.

- [ ] **Step 1: Script** `app/tests/mimic_scene.txt` : `turn K4 8`, `expect anim 13`, `tap PLAY`, `wait 2500`, `shot mimic_play.png` ; page LOOP, `spin K1 4`, `wait 1500`, `shot mimic_loop.png` ; `tap REC`, `key 1 down`, `key 1 up`, `shot mimic_listen_scene.png`, `tap REC` ; KIT, `shot mimic_kit.png` ; `tap PLAY`.
- [ ] **Step 2:** échec. **Step 3:** implémenter ; adresses inchangées depuis la Task 1. **Step 4:** tout passe ; `./build.sh` ; regarder les captures. **Step 5:** commit `MIMIC: the scene on the SEQ pages`.

Après la Task 5 : revue de la branche par un relecteur neuf, corrections, puis test sur l'appareil.
