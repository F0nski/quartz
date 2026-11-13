# QUARTZ source ORBITS — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** une huitième source de pattern, ORBITS : 1 à 4 lunes de période et phase propres qui jouent leur note (degré ou pad) en passant devant la planète, accentuées en conjonction ; ses pages ORBITS et MOON (PRESET = lune réglée) et sa scène radar « V2 ».

**Architecture :** le jeu dans `gen.c` (`gen_step` pour `SRC_ORBITS`, compteur `orbits_n` dans `q_gen`) ; `ui.c` gagne les pages, PRESET, les LED et l'anim 12 ; `audio_orbits` rend le compteur ; `app/scene_orbits.c` dessine le radar, la planète tournante et le décor.

**Tech Stack :** C (gnu11, sans libc ni libm) ; `tests/run_tests.sh`.

**Spec :** `docs/superpowers/specs/2026-11-13-quartz-orbits-design.md` ; maquette `docs/screens/orbits.html`, `v(g, 2)`, `sphere`, `farPlanet`, `topBar` : tons et formes tels quels, y de la maquette − 30.

## Global Constraints

- Branche `orbits` (partie de `tonnetz`, spec en `d2c047d`) ; identité `13540044+F0nski@users.noreply.github.com` ; pas de `Co-Authored-By` ; jamais `midi_out.txt` ni `base` ; `git status` avant chaque commit.
- Pas de libc ni libm (`fm_cosf`, `fm_sinf`) ; `scene_orbits.c` inclus dans `app_ui.c`, `tests/unit/scene_test.c`, `tests/unit/plateau_test.c` après `scene_tonnetz.c` ; adresses `llvm-nm` (`~/emsdk/upstream/bin/llvm-nm`) des cinq symboles audio relevées après la Task 1 et inchangées après la Task 4.
- `Q_VERSION` 11, sans migration ; `./build.sh` passe.
- Passage : lune m au pas n si `n % per[m] == pha[m] % per[m]` ; notes dans l'ordre des lunes ; 2 passages ou plus : accent sur toutes ; `len` 4, `vel` 100, `prob` 20 ; synthé `gen_life_note(note, gen_scale(p), p->tonic, val[m])`, KIT `KIT_NOTE + val[m]`.
- Défauts : NOTE 48, MOONS 3, per {8, 6, 5, 3}, pha {0, 0, 0, 0}, val {0, 2, 4, 7}.
- Textes en anglais ; couleurs des lunes 1..4 : `ORANGE`, `KBLUE`, `KGREEN`, `MB_WHITE` ; rose au passage.
- Mesures CPU en fin de chantier.

## Review Focus

1. MOONS réduit pendant la lecture alors que PRESET pointe une lune au-delà : la lune réglée est ramenée dans MOONS, la page MOON règle une lune active (script `orbits.txt`, Task 2).
2. PERIOD changée pendant la lecture : le passage suivant suit la nouvelle période, aucune division par zéro (PERIOD 1..32) (test `orbits_period_change`, Task 1).
3. PHASE ≥ PERIOD : le passage tombe sur `PHASE mod PERIOD` (test `orbits_phase_mod`, Task 1).
4. Conjonction de 4 lunes au même pas : 4 notes, toutes accentuées (test `orbits_four`, Task 1).
5. Pattern affiché ≠ pattern joué : ni rose ni clignotement, lunes à leur place de départ (test `orbits_idle`, Task 3, et vue remplie avec `pos` −1, Task 4).

---

### Task 1: le jeu

**Files :** Modify `app/project.h`, `app/seq.c`, `app/store.c`, `app/gen.h`, `app/gen.c` ; Test `tests/unit/gen_test.c`, `tests/unit/store_test.c`

**Produces :**
```c
/* project.h */
enum { SRC_STEP, SRC_GRIDS, SRC_MARBLES, SRC_LIFE, SRC_LANES, SRC_CHORDS, SRC_TONNETZ, SRC_ORBITS, SRC_N };
/* note 1..127, moons 1..4; per 1..32, pha 0..31, val 0..15 (a degree; a KIT's pad - 1) */
typedef struct { uint8_t note, moons, per[4], pha[4], val[4]; } q_orbits;
/* q_pattern: q_orbits orbits; after q_tonnetz tonnetz */
#define Q_VERSION 11
/* gen.h, in q_gen */
uint32_t orbits_n;               /* ORBITS: the steps played since the reset */
/* gen.h */
int gen_orbits_hits(const q_orbits *o, uint32_t n);   /* bit m: moon m passes at step n */
```
`gen_loop` → 16. `orbits_step` (statique) : `n = orbits_n++` ; notes des lunes de `gen_orbits_hits` ; rend le masque d'accent `(1 << nb) − 1` si nb ≥ 2, sinon 0.

- [ ] **Step 1: Tests** dans `gen_test.c` (`orbits_setup(model)`, `src = SRC_ORBITS`) :
  - `orbits_defaults` : valeurs des Global Constraints, `sizeof(q_orbits) == 14`.
  - `orbits_passes` : défauts : hits(0) = 0b111 ; hits(1) = 0 ; hits(5) = 0b100 ; hits(6) = 0b010 ; hits(8) = 0b001 ; hits(24) = 0b011 ; hits(120) = 0b111 ; et pour 0 < n < 120, hits(n) ≠ 0b111.
  - `orbits_notes` : C majeur, au pas 0 : notes {48, 52, 55}, accent 7 ; pas 5 : {55}, accent 0 ; KIT : pas 0 → {KIT_NOTE, KIT_NOTE + 2, KIT_NOTE + 4}.
  - `orbits_phase_mod` : pha[0] = 11, per[0] = 8 : hits au pas 3 a le bit 0, pas 0 non.
  - `orbits_period_change` : per[0] 8 → 1 après 5 pas : la lune 0 passe aux pas 5, 6, 7.
  - `orbits_four` : 4 lunes de période 4 : pas 0 → 4 notes, accent 15.
  - `orbits_moons` : MOONS 1 : seul le bit 0 ; le compteur continue au tour (pas 16 = `n` 16).
  - `loop_lengths` : `SRC_ORBITS` → 16.
  `store_test.c` : `orbits_clamped_on_load` : note 0, moons 9, per 0 et 40, pha 50, val 20 → 1, 4, 1 et 32, 31, 15.
- [ ] **Step 2:** échec de compilation. **Step 3:** implémenter ; adresses `llvm-nm` après `./build.sh`. **Step 4:** tout passe. **Step 5:** commit `ORBITS: moons on orbits, their run in gen.c`.

### Task 2: pages, PRESET, LED

**Files :** Modify `app/ui.c`, `app/audio.h`, `app/audio.c` ; Create `app/tests/orbits.txt`

**Consumes :** `q_orbits`, `gen_orbits_hits` (Task 1).

**Produces :** `P_ORBITS, P_MOON` en fin d'enum ; `{"ORBITS", {"DIV", "NOTE", "MOONS", "SOURCE"}, {0, 1, 1, 0}, {DIV_N - 1, 127, 4, SRC_N - 1}}`, `{"MOON", {"PERIOD", "PHASE", "NOTE"}, {1, 0, 0}, {32, 31, 15}}` ; `GEN_PAGES[7][5]` avec `{P_ORBITS, P_MOON}` ; NOTE de P_ORBITS masqué sur un KIT ; K3 de P_MOON : libellé `PAD` et valeur `BD`/`SN`/`HH`/`P4`…`P16` sur un KIT, nom de la note (`gen_life_note`) sinon ; `ui.moon` (0..3, ramené à MOONS − 1) ; PRESET sur `P_MOON` : `ui.moon` ± 1 ; sur `P_ORBITS` : pattern ; REC court : rien (chemin des générateurs) ; LED : blanches 1..MOONS mi-niveau, pleine si `gen_orbits_hits(o, audio_orbits(track) − 1)` a le bit et que le pattern affiché joue ; `uint32_t audio_orbits(int track);` ; sondes `onote`, `omoons`, `osel`, `oper_<m>`, `opha_<m>`, `oval_<m>` (m 1..4).

- [ ] **Step 1: Script** `app/tests/orbits.txt` : `turn K4 7` → `expect src 7` ; bornes NOTE, MOONS ; page MOON : `turn PRESET 1` → `expect osel 1` ; bornes PERIOD 1..32, PHASE 0..31, NOTE 0..15 sur `oper_2`… ; retour page ORBITS, `spin K3 -3` (MOONS 1) → `expect osel 0` ; `tap REC` → `expect badge 0` ; KIT : K2 de ORBITS sans effet, une capture de la page MOON (`PAD`) ; captures des deux pages.
- [ ] **Step 2:** échec. **Step 3:** implémenter. **Step 4:** tout passe ; captures. **Step 5:** commit `ORBITS: its pages ORBITS and MOON, PRESET picks the moon`.

### Task 3: la scène

**Files :** Create `app/scene_orbits.c` ; Modify `app/scene.h`, `app/app_ui.c`, `tests/unit/scene_test.c`, `tests/unit/plateau_test.c`

**Consumes :** `q_orbits`, `gen_orbits_hits` (Task 1).

**Produces :**
```c
/* scene.h: ORBITS' radar (scene_orbits.c); ui.c fills the view */
typedef struct {
    q_orbits o;                /* the pattern's */
    uint32_t n;                /* the steps played (audio_orbits); 0 stopped */
    int8_t sel;                /* the moon set on MOON, -1 on ORBITS */
    uint8_t playing, kit;
    char name[4][5];           /* the moons' notes ("C3") or pads ("BD") */
    uint32_t step_ms;
} q_orview;
void orbits_enter(void);
void orbits_frame(const q_orview *v, uint32_t ms);
void orbits_band(const q_orview *v, int32_t oy);
```
Passage vu par la scène : quand `n` change, `gen_orbits_hits(o, n − 1)` donne les lunes qui viennent de passer (rose 160 ms) et le réalignement (toutes, au moins 2 : clignotement blanc 2 fois en 300 ms). Position d'une lune : `u = ((n − 1 + f) − pha) mod per`, f la fraction du pas en cours (temps depuis le changement de `n` / `step_ms`, bornée à 1). Sphère : `p3d_tri` par facette visible (UV sphère 8 × 5, inclinaison 0,35, lumière (−0,5, 0,6, 0,62), 6 bleus ; la lointaine 5 tons sombres). Orbites : 32 segments `p3d_line`. Crans, points du chemin, étoiles, étiquettes et textes en dessin direct après `p3d_draw`.

- [ ] **Step 1: Tests** dans `scene_test.c` : `orbits_budget` (4 lunes, période 32 : ≤ 300) ; `orbits_pass_pink` (n passe de 0 à 1, défauts : rose à la porte de chaque orbite (x 104, y 90 − R)) ; `orbits_sync_flash` (passage des 3 : blanc sur la planète à +30 ms, pas à +400 ms ; passage de 2 sur 3 (n 24 → 25) : pas de blanc) ; `orbits_ticks` (période 7 : 7 crans sur l'orbite 1) ; `orbits_idle` (`playing` 0 : aucun rose) ; `orbits_knobs_visible` (NOTE (nom), MOONS, PERIOD, PHASE, val, sel, n changent un pixel).
- [ ] **Step 2:** échec. **Step 3:** écrire la scène. **Step 4:** tout passe. **Step 5:** commit `ORBITS: its radar scene`.

### Task 4: la scène à l'écran

**Files :** Modify `app/ui.c`, `docs/superpowers/specs/2026-11-10-quartz-groovebox-design.md` ; Create `app/tests/orbits_scene.txt`

**Consumes :** `q_orview`, `orbits_*` (Task 3) ; `audio_orbits`, `ui.moon` (Task 2).

**Produces :** `P_ORBITS`, `P_MOON` → `anim = 12` ; vue : `o`, `n = audio_orbits` si le pattern affiché joue (sinon 0, `playing` 0), `sel` (`ui.moon` sur `P_MOON`, −1 sinon), noms des lunes, `step_ms` ; `orbits_band` dans la boucle des bandes ; spec globale sections 6 et 7.

- [ ] **Step 1: Script** `app/tests/orbits_scene.txt` : `turn K4 7`, `expect anim 12`, `tap PLAY`, `wait 2500`, `shot orbits_play.png` ; page MOON, `turn PRESET 1`, `wait 300`, `shot orbits_moon.png` ; KIT, `shot orbits_kit.png` ; `tap PLAY`.
- [ ] **Step 2:** échec. **Step 3:** implémenter ; adresses inchangées depuis la Task 1. **Step 4:** tout passe ; `./build.sh` ; regarder les captures. **Step 5:** commit `ORBITS: the scene on the SEQ pages`.

Après la Task 4 : revue de la branche par un relecteur neuf, corrections, puis test sur l'appareil.
