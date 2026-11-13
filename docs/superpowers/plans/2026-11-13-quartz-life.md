# QUARTZ source LIFE — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** une quatrième source de pattern, LIFE : une grille du jeu de la vie 16 × 8 lue colonne par colonne, ses trois pages SEQ et sa scène de molécules.

**Architecture :** le jeu vit dans `gen.c` (`gen_step` pour `SRC_LIFE`, état dans `q_gen.life`) ; `ui.c` gagne les pages LIFE, SEED, EVOL et l'anim 8 ; `audio_life` copie l'état du rendu pour l'écran ; `app/scene_life.c` dessine la grille en atomes et liaisons avec p3d.

**Tech Stack :** C (gnu11, sans libc ni libm) ; `tests/run_tests.sh` (unitaires puis scripts du simulateur) ; `tools/device/`.

**Spec :** `docs/superpowers/specs/2026-11-13-quartz-life-design.md` ; maquette `docs/screens/life.html`, fonctions `frontFrame`, `flatMolecules` (direction M3) : géométries, effets et durées telles quelles, coordonnées de la spec section 6 (zone de scène).

## Global Constraints

- Travail sur une branche `life` partant de `main` ; identité git personnelle (`13540044+F0nski@users.noreply.github.com`) ; pas de `Co-Authored-By`. Fichiers non suivis qui ne sont pas les nôtres (`docs/manual/`, `midi_out.txt`, plan et spec step-entry) : ne jamais les ajouter ; vérifier `git status` avant chaque commit.
- Pas de libc ni libm (`fastmath.h` si besoin d'un sinus) ; le code audio garde ses adresses : `scene_life.c` inclus dans `app_ui.c`, `tests/unit/scene_test.c` et `tests/unit/plateau_test.c` après `scene_glo.c` ; comparer `pq_init`, `seq_block`, `app_render`, `sysex_byte`, `fm1_timer5_irq` avec `llvm-nm` sur `app/build/quartz.elf` avant et après la Task 4.
- `Q_VERSION` 7, aucune migration.
- Valeurs de la spec : grille 16 × 8 torique B3/S23 ; graine : probabilité DENS / 127 × 0,55 ; MUT ajoute `(mut * 6 + 63) / 127` cellules ; GEN 1/2/4/8 tours ; POLY 1..4, 3 au plus sur un KIT ; défauts POLY 2, NOTE 48, PADS, SEED 0, DENS 70, AUTO 1, GEN index 1 (2 tours), MUT 0.
- Rose réservé à ce qui joue et aux naissances (palette de `scene.c` : `PINK3`, `KBLUE`, `KGREEN`, `ORANGE`, `MB_WHITE`) ; textes de l'écran en anglais.
- Mesures CPU sur l'appareil : en fin de chantier, groupées avec les autres (pas dans ce plan).

## Review Focus

1. Pattern changé pendant la lecture d'un pattern LIFE vers un autre LIFE, ou STOP/PLAY : la grille repart de la graine du nouveau pattern, jamais de l'ancienne (test `life_pattern_switch_reseeds`, Task 1).
2. SEED tourné plusieurs fois dans un même tour : un seul resemis, au pas 0 suivant, avec la dernière valeur (test `life_seed_change_next_tour`, Task 1).
3. NOTE près de 127 avec POLY 4 : aucune note au-delà de 127 ni à 0 (test `life_degrees_clamp`, Task 1).
4. Changement de piste synthé ↔ KIT avec MAP réglé : NOTE et MAP ne s'affichent jamais ensemble, POLY affiché reste 1..4 mais le jeu plafonne à 3 (script `life.txt`, Task 2).
5. Arrivée sur la scène pendant la lecture : pas de pluie de resemis ni d'éclair de naissance pour ce qui a eu lieu avant (test `life_no_flash_on_arrival`, Task 3).

---

### Task 1: le jeu et ses notes

**Files :** Modify `app/project.h`, `app/seq.c` (défauts), `app/gen.h`, `app/gen.c` ; Test `tests/unit/gen_test.c`

**Produces :**
```c
/* project.h */
enum { SRC_STEP, SRC_GRIDS, SRC_MARBLES, SRC_LIFE, SRC_N };
/* poly 1..4, note 1..127, map 0 PADS / 1 BANDS, seed and dens 0..127, autos 0..1, gen 0..3 (1, 2, 4, 8 tours), mut 0..127 */
typedef struct { uint8_t poly, note, map, seed, dens, autos, gen, mut; } q_life;
/* q_pattern: q_life life; after q_marbles marbles */
#define Q_VERSION 7

/* gen.h */
#define LIFE_W 16
#define LIFE_H 8
/* LIFE's run, the render's (bit c of a row: column c; row 0 the lowest) */
typedef struct {
    uint16_t grid[LIFE_H], prev[2][LIFE_H], born[LIFE_H], mut[LIFE_H];   /* prev[0]: the generation before */
    uint32_t rng;                     /* its own xorshift, from SEED and DENS: the same pair, the same run */
    uint8_t seeded, seed, dens;       /* the pair the grid was drawn from */
    uint8_t tours, stuck;             /* tours since the last generation; stuck: dead or frozen, AUTO off */
    uint8_t gens, reseeds;            /* counters (wrap): the scene starts its animations on a change */
} q_life_run;
/* q_gen gains: q_life_run life; */
/* the rows the column col plays, lowest first, at most POLY (3 on a KIT); BANDS: one row per band, its lowest living
   cell. Returns their number. */
int gen_life_rows(const q_life *l, int kit, const uint16_t grid[LIFE_H], int col, uint8_t rows[4]);
```
`gen_loop` rend 16 pour `SRC_LIFE`. Défauts de `q_life` posés dans `project_defaults` (`seq.c`, à côté de `pt->marbles`).

Déroulé de `gen_step` pour LIFE (`life_step`, statique) :
- pas encore semée (`seeded` 0, donc après chaque `gen_reset`) : semer depuis `pt->life.seed`/`dens` ; `born` = toute la grille ;
- au pas `k == 0` (hors premier semis) : si SEED ou DENS diffèrent de ceux de la grille, resemer (`reseeds++`) ; sinon `tours++`, et si `tours == 1 << gen` : `tours = 0`, génération (`gens++`), puis MUT ; si MUT vaut 0 et que la grille est vide ou égale à `grid` ou `prev[0]` d'avant : AUTO → tirage suivant de `rng` (`reseeds++`, SEED inchangé), sinon `stuck = 1` ;
- la colonne `k` : `gen_life_rows`, puis les notes ; une ligne dont le bit est dans `born` est accentuée (bit du slot dans la valeur rendue) ; `vel` 95, `len` 4, `prob` 20, comme GRIDS.

Notes : synthé `life_note(note, scale, tonic, r)` : `gen_quantize(note, gen_scale(p), p->tonic, 0)` puis monter de r classes de la gamme, borné à 127 (au-delà : la dernière note ≤ 127 de la gamme) ; KIT PADS : `KIT_NOTE + r` ; BANDS : `KIT_NOTE + bande` (lignes 0–2, 3–5, 6–7).

Graine (`life_seed`, statique) : `rng = (seed * 2654435761u) ^ (dens * 40503u) ^ 0x9e3779b9u`, jamais 0 (`| 1`) ; chaque cellule, ligne puis colonne : vivante si `(gen_rand(&rng) >> 8) < dens * 0.55 / 127 × 2^24` (en entier : `(uint32_t)dens * 72657u`, soit 0,55 × 2^24 / 127 arrondi). MUT : `(mut * 6 + 63) / 127` cases tirées par `rng`, mises vivantes et marquées dans `mut` et `born`.

- [ ] **Step 1: Tests** dans `gen_test.c` (`life_setup(model)` : `project_defaults`, `src = SRC_LIFE`, `memset(&G)`; aide `life_tour(pt, notes[16][4], acc[16])` qui joue les 16 pas) :
  - `life_blinker` : grille posée à la main (3 cellules horizontales ligne 3, colonnes 4–6), `life_next` : verticale colonnes 5, lignes 2–4 ; encore : de retour à l'horizontale.
  - `life_glider_wraps` : un planeur en haut à droite, 4 × 16 générations : revient à sa forme, décalé de (+16, +16) modulo (16, 8), soit à sa place d'origine ; aucune perte de cellule au passage du bord.
  - `life_block_still` : un bloc 2 × 2 ne change pas.
  - `life_seed_repeats` : SEED 17, DENS 70, deux `gen_reset` puis un pas : grilles identiques ; SEED 18 : différente ; DENS 0 : grille vide et aucune note sur un tour ; DENS 127 : entre 40 % et 70 % de cellules vivantes.
  - `life_gen_every_n_tours` : GEN index 2 (4 tours), MUT 0, grille d'un clignotant : `gens` passe à 1 au pas 0 du 5e tour, pas avant, et la grille ne change qu'à ce pas.
  - `life_auto_reseeds_dead` : après le premier pas, `G.life.grid` vidé à la main, AUTO 1, GEN index 0 : au tour suivant `reseeds` + 1 ; AUTO 0 : `stuck` 1, aucune note, `reseeds` inchangé.
  - `life_frozen_kept_without_auto` : un bloc seul, AUTO 0 : `stuck` 1 et le bloc joue encore à chaque tour ; AUTO 1 : resemé.
  - `life_mut_never_stuck` : bloc seul, MUT 127, AUTO 1 : sur 8 générations, `reseeds` inchangé, 6 cellules ajoutées par génération (au plus, une case déjà vivante compte).
  - `life_seed_change_next_tour` : en cours de tour (pas 5), SEED passe à 3 puis à 9 : la grille ne change qu'au pas 0 suivant, `reseeds` + 1 exactement, grille égale à celle d'un semis SEED 9.
  - `life_rows_poly` : colonne avec les lignes 1, 2, 5, 7 vivantes : POLY 2 → {1, 2} ; POLY 4 sur un KIT → {1, 2, 5} ; BANDS → {1, 5, 7} (une par bande, la plus basse) ; BANDS POLY 2 → {1, 5}.
  - `life_notes_synth` : gamme C majeur (`tonic` 0, mode majeur), NOTE 48, lignes 0, 1, 2 → 48, 50, 52 ; NOTE 49 → base 48 (égalité : la plus basse, comme `gen_quantize`).
  - `life_degrees_clamp` : NOTE 127, POLY 4, lignes 0–3 : toutes les notes dans 1..127.
  - `life_notes_kit` : PADS ligne 4 → `KIT_NOTE + 4` ; BANDS ligne 4 → `KIT_NOTE + 1`, ligne 7 → `KIT_NOTE + 2`.
  - `life_born_accented` : juste après un semis, toute note jouée est accentuée ; un bloc stable une génération plus tard : aucune.
  - `life_pattern_switch_reseeds` : via `seq.c` comme les tests GRIDS existants, deux patterns LIFE (SEED 1 et SEED 2), passage de l'un à l'autre à la fin de boucle : la grille au premier pas du second est celle d'un semis SEED 2.
  - `loop_lengths` (existant) : ajouter `SRC_LIFE` → 16.
- [ ] **Step 2:** `tests/run_tests.sh` : échec de compilation (`SRC_LIFE`, `q_life` inconnus).
- [ ] **Step 3:** Implémenter dans `project.h`, `seq.c`, `gen.h`, `gen.c` (`life_next(const uint16_t in[LIFE_H], uint16_t out[LIFE_H])` statique : voisins sur le tore, rotations de 16 bits ; `life_seed`, `life_note`, `life_step`, `gen_life_rows`) ; `gen_step` aiguille `SRC_LIFE` vers `life_step`.
- [ ] **Step 4:** `tests/run_tests.sh` : tous les tests passent (les scripts du simulateur existants inclus : `Q_VERSION` change, `src` max passe à 3 sans effet ailleurs).
- [ ] **Step 5:** Commit `LIFE: a Game of Life source, its run in gen.c`.

### Task 2: les pages SEQ de LIFE

**Files :** Modify `app/ui.c` ; Create `app/tests/life.txt`

**Consumes :** `SRC_LIFE`, `q_life` (Task 1).

**Produces :** `P_LIFE, P_SEED, P_EVOL` ajoutés en fin de l'enum des pages (après `P_KEYS`) ; leurs lignes de `PAGE[]` :
```c
{"LIFE", {"DIV", "POLY", "NOTE", "SOURCE"}, {0, 1, 1, 0}, {DIV_N - 1, 4, 127, SRC_N - 1}},
{"SEED", {"SEED", "DENS", "AUTO"}, {0, 0, 0}, {127, 127, 1}},
{"EVOL", {"GEN", "MUT"}, {0, 0}, {3, 127}},
```
- `GEN_PAGES` devient `[3][5]` avec `{P_LIFE, P_SEED, P_EVOL}` ; `pages()` rend 3 pour LIFE.
- Sur un KIT, K3 de LIFE est MAP (0..1, « PADS » / « BANDS ») : même mécanisme que les libellés variables existants (comme INSERT) ou un libellé choisi selon `kit()` ; NOTE est masqué sur un KIT et MAP sur un synthé ; POLY reste 1..4 à l'écran.
- Textes : DIV par `DIV_NAME`, NOTE par `note_name`, SOURCE par `SRC_NAME` (« LIFE » ajouté), AUTO « OFF »/« ON », GEN « 1 » « 2 » « 4 » « 8 ».
- Lecture/écriture des valeurs dans les deux `switch` existants (`P_LIFE * 4 + k` …), DIV et SOURCE partagés avec GRIDS et MARBLES.
- Sondes du simulateur : `lpoly`, `lnote`, `lmap`, `lseed`, `ldens`, `lauto`, `lgen`, `lmut`.

- [ ] **Step 1: Script** `app/tests/life.txt`, sur le modèle de `grids.txt` : `tap SEQ`, `turn K4 3` → `expect src 3`, `expect page 1`, `shot life_page1.png` ; NOTE : `spin K3 -127` → `expect lnote 1`, `spin K3 127` → `expect lnote 127` ; POLY : `spin K2 -9` → `expect lpoly 1`, `spin K2 9` → `expect lpoly 4` ; page 2 : SEED 0..127, DENS 0..127, AUTO 0..1 aux bornes, `shot life_page2.png` ; page 3 : GEN 0..3, MUT 0..127, `shot life_page3.png` ; `turn SELECT 1` → `expect page 1` ; KIT (`tap EDIT`, `turn PRESET 20`, `expect model 24`, `tap SEQ`) : K3 règle MAP (`turn K3 1` → `expect lmap 1`, `expect lnote` inchangé), `shot life_kit.png` ; SEQ tenu + blanche 2 → `expect pattern 2`, `expect src 0`.
- [ ] **Step 2:** `tests/run_tests.sh` : `life.txt` échoue (sondes inconnues).
- [ ] **Step 3:** Implémenter dans `ui.c`.
- [ ] **Step 4:** `tests/run_tests.sh` : tout passe ; regarder les quatre captures (pied de page lisible, NOTE ou MAP selon la piste).
- [ ] **Step 5:** Commit `LIFE: its pages LIFE, SEED, EVOL`.

### Task 3: la scène

**Files :** Create `app/scene_life.c` ; Modify `app/scene.h`, `app/app_ui.c`, `tests/unit/scene_test.c`, `tests/unit/plateau_test.c` (inclusion après `scene_glo.c`)

**Consumes :** `q_life_run`, `LIFE_W`, `LIFE_H` (Task 1).

**Produces :**
```c
/* scene.h: LIFE's molecules (scene_life.c); ui.c fills the view */
typedef struct {
    q_life_run run;            /* audio_life's copy */
    int8_t col;                /* the column read, -1 stopped */
    uint8_t played;            /* bit r: row r plays in col (gen_life_rows) */
    uint8_t poly, autos, gen, mut, kit, map, seed;   /* the pattern's q_life, poly already capped on a KIT */
    char label[LIFE_H][5];     /* the rows' names, row 0 first: notes, pads "BD" "SN" "HH" "4".."8", BANDS "BD" "SN" "HH" on rows 1, 4, 6 */
    uint32_t step_ms;          /* a step in ms */
} q_lview;
void life_enter(void);                           /* arrival: the counters seen as they are, no animation */
void life_frame(const q_lview *v, uint32_t ms);  /* builds the primitives */
void life_band(const q_lview *v, int32_t oy);    /* p3d_draw, then the texts */
```
Géométrie et effets : spec section 6, maquette `flatMolecules(c, t, true)` et `frontFrame`, avec la grille à x = 22 + 13,5 c, y = 18 + 18 (7 − r) (coin haut gauche de la case). Atome : hexagone de rayon 5,5 (× 1,35 quand il joue), 4 triangles en 3 tons, sommets tournés de `ms` × 0,6 rad/s ; liaisons : `p3d_line` largeur 1 (doubles, écart ±1,6 px) ou 2 (diagonales) ; fantôme : losange de 4 lignes de rayon 4,5. Animations datées par `ms` : naissance 350 ms (au changement de `run.gens` ou `run.reseeds` vu par `life_frame`), photons 300 ms et rose 150 ms au changement de `col`, pluie au resemis 350 ms, mort 450 ms (cellule dans `prev[0]` et plus dans `grid`). Ordre d'envoi : atomes de la colonne lue, autres atomes, liaisons, photons et électrons, fantômes. Bande du haut : « S017 » (`FONT_S`, aligné à droite sur x 232, rose 600 ms après un resemis), losanges GEN à partir de x 120 (pas 8), emblème AUTO à x 196 ; crans POLY au-dessus de la colonne (y 14–17) ; « FROZEN » (grille non vide) ou « EXTINCT » au centre quand `run.stuck`.

- [ ] **Step 1: Tests** dans `scene_test.c` (`lview()` : vue par défaut, POLY 2, `col` 5 ; `lband(b)` : `cv_begin(240, 83, 0)` puis `life_band(&LV, -83 * b)`) :
  - `life_budget_full` : grille pleine (toutes les cellules, toutes nées), POLY 4 : `p3d_count() <= 300` et du rose dans la colonne 5 (x 89 à 103) sur les deux bandes.
  - `life_column_first` : grille pleine : chaque ligne qui joue a du rose dans sa case de la colonne 5.
  - `life_no_flash_on_arrival` : `life_enter`, puis une image avec `gens` et `reseeds` déjà non nuls, `col` −1 : aucun rose.
  - `life_birth_pink_then_blue` : `gens` + 1 avec une naissance en (2, 3), `col` −1 : rose dans sa case à +100 ms, plus de rose à +500 ms, du bleu.
  - `life_valence_ghost` : trois cellules en L autour de (8, 4) vide : du bleu (`KBLUE[1]`) en filaire dans la case (8, 4) ; aucune cellule autour : rien.
  - `life_stuck_grey` : `run.stuck` 1, AUTO 0 : aucun bleu ni rose dans la grille, du texte au centre (pixels non noirs vers y 80).
  - `life_knobs_visible` (règle des scènes) : une image de référence, puis chaque changement seul de `poly`, `gen`, `autos`, `seed`, `mut` (avec une génération), `map` (KIT) et du libellé de la ligne 0 change au moins un pixel des bandes.
- [ ] **Step 2:** `tests/run_tests.sh` : échec (`life_frame` inconnu).
- [ ] **Step 3:** Écrire `app/scene_life.c`, déclarer dans `scene.h`, inclure dans `app_ui.c` et les deux tests.
- [ ] **Step 4:** `tests/run_tests.sh` : tout passe.
- [ ] **Step 5:** Commit `LIFE: its scene of molecules`.

### Task 4: la scène à l'écran

**Files :** Modify `app/audio.h`, `app/audio.c`, `app/ui.c` ; Create `app/tests/life_scene.txt`

**Consumes :** `q_lview`, `life_enter`, `life_frame`, `life_band` (Task 3) ; `gen_life_rows` (Task 1) ; pages `P_LIFE`, `P_SEED`, `P_EVOL` (Task 2).

**Produces :**
```c
/* audio.h: a LIFE pattern's run as the render left it (a copy read while it may write: one frame off at worst) */
void audio_life(int track, q_life_run *out);
```
`audio.c` : copie `seq.g[track].life`. `ui.c` : sur `P_LIFE`, `P_SEED`, `P_EVOL`, `v->scene.anim = 8` ; `fresh` → `life_enter()` ; la vue remplie depuis `pat()->life`, `audio_life`, `audio_pos` (−1 quand le pattern affiché n'est pas celui qui joue, comme GRIDS), `gen_life_rows` pour `played`, `step_ms = 2500 * DIV96[div] / tempo` ; `life_band(&lv, …)` dans la boucle des bandes ; la clé de scène inclut l'anim comme les autres.

- [ ] **Step 1: Script** `app/tests/life_scene.txt` : `tap SEQ`, `turn K4 3`, `expect anim 8`, `tap PLAY`, `wait 1500`, `shot life_play.png` ; `turn SELECT 1`, `expect anim 8`, `turn K1 5` (resemis), `wait 2500`, `shot life_reseed.png` ; `turn SELECT 1`, `turn K1 -3` (GEN 1), `wait 2500`, `shot life_gen.png` ; KIT comme dans `grids_scene.txt`, `shot life_kit_play.png` ; `tap PLAY`.
- [ ] **Step 2:** `tests/run_tests.sh` : `life_scene.txt` échoue (`anim` 2 au lieu de 8).
- [ ] **Step 3:** Implémenter ; relever les adresses `llvm-nm` des cinq symboles audio avant et après (`./build.sh` puis `llvm-nm app/build/quartz.elf | grep -E ' (pq_init|seq_block|app_render|sysex_byte|fm1_timer5_irq)$'`).
- [ ] **Step 4:** `tests/run_tests.sh` : tout passe ; les adresses n'ont pas bougé ; regarder les quatre captures (atomes, liaisons, fantômes, bande du haut, rose sur la colonne lue).
- [ ] **Step 5:** Commit `LIFE: the scene on the SEQ pages`.

Après la Task 4 : revue de la branche, puis test sur l'appareil par l'utilisateur (`fm1_install --yes --classic`) ; mesure CPU reportée à la session de mesures finale (pire cas : grille dense, POLY 4, MUT 127 ; ligne dans `docs/cpu.md`).
