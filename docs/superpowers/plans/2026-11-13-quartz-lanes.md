# QUARTZ source LANES — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** une cinquième source de pattern, LANES : cinq lignes (TRIG, NOTE, OCT, DUR, VEL) qui bouclent chacune sur leur longueur, leurs deux pages SEQ, leur saisie en GRILLE et leur scène « Focus ».

**Architecture :** le jeu vit dans `gen.c` (`gen_step` pour `SRC_LANES`, compteur `lanes_n` dans `q_gen`) ; `ui.c` gagne les pages LANES et LENS, la saisie des cases (PRESET = ligne, blanches = cases) et l'anim 9 ; `audio_lanes` rend le compteur à l'écran ; `app/scene_lanes.c` dessine les rangées de blocs avec p3d.

**Tech Stack :** C (gnu11, sans libc ni libm) ; `tests/run_tests.sh` (unitaires puis scripts du simulateur) ; `tools/device/`.

**Spec :** `docs/superpowers/specs/2026-11-13-quartz-lanes-design.md` ; maquette `docs/screens/lanes.html`, fonctions `row`, `block`, `drawC3` (direction C3) : géométries et tons tels quels, coordonnées de la spec section 6 (zone de scène 240 × 166).

## Global Constraints

- Travail sur la branche `lanes` (déjà créée depuis `main`, spec commitée en `d5b3a8f`) ; identité git personnelle (`13540044+F0nski@users.noreply.github.com`) ; pas de `Co-Authored-By`. `midi_out.txt` et `base` ne sont pas à nous : ne jamais les ajouter ; vérifier `git status` avant chaque commit.
- Pas de libc ni libm ; le code audio garde ses adresses : `scene_lanes.c` inclus dans `app_ui.c`, `tests/unit/scene_test.c` et `tests/unit/plateau_test.c` après `scene_life.c` ; comparer `pq_init`, `seq_block`, `app_render`, `sysex_byte`, `fm1_timer5_irq` avec `llvm-nm` sur `app/build/quartz.elf` avant et après la Task 5.
- `Q_VERSION` 8, aucune migration.
- Valeurs de la spec : lignes `L_TRIG, L_NOTE, L_OCT, L_DUR, L_VEL` = 0..4 ; LEN 1..16 ; TRIG 0..1, NOTE 0..15, OCT 0..4 (−2..+2), DUR 0..4 → `len` 2, 4, 8, 16, 32, VEL 0..3 → 40, 70, 100, 100 + accent ; NOTE de base 1..127 ; un tour 16 pas.
- Défauts d'un pattern : NOTE 48 ; TRIG 1 0 1 1 0 1 0 (LEN 7) ; NOTE 0 4 2 5 3 (LEN 5) ; OCT 2 2 3 (LEN 3) ; DUR 1 3 1 2 (LEN 4) ; VEL 3 1 2 1 3 2 (LEN 6) ; cases au-delà 0, OCT 2.
- Rose réservé à ce qui joue (les têtes) ; la ligne éditée en bleu vif (`KBLUE`), les autres en tons sombres ; blanc pour une case touchée. Textes de l'écran en anglais.
- Mesures CPU sur l'appareil : en fin de chantier, groupées avec les autres (pas dans ce plan).

## Review Focus

1. LEN raccourci pendant la lecture sous la tête courante (tête en case 9, LEN passe à 4) : la tête suivante est `n mod 4`, jamais une case au-delà de LEN (test `lanes_len_shrink`, Task 1).
2. Piste qui passe de synthé à KIT avec la ligne OCT éditée : PRESET ne propose plus OCT, la ligne éditée tombe sur NOTE, OCT LEN masqué (script `lanes_entry.txt`, Task 3).
3. NOTE de base 127 avec NOTE 15 et OCT +2 : note jouée dans 1..127 ; NOTE de base 1 avec OCT −2 : note ≥ 1 (test `lanes_note_clamp`, Task 1).
4. Case tenue + K1 puis relâchée : la valeur n'est pas incrémentée une seconde fois au relâchement (script `lanes_entry.txt`, Task 3).
5. Pattern affiché différent de celui qui joue : la scène montre les têtes sur la case 1, sans rose (test `lanes_idle_heads`, Task 4, et vue remplie avec `pos` −1, Task 5).

---

### Task 1: le jeu et ses notes

**Files :** Modify `app/project.h`, `app/seq.c` (défauts), `app/store.c` (bornes au chargement), `app/gen.h`, `app/gen.c` ; Test `tests/unit/gen_test.c`, `tests/unit/store_test.c`

**Produces :**
```c
/* project.h */
enum { SRC_STEP, SRC_GRIDS, SRC_MARBLES, SRC_LIFE, SRC_LANES, SRC_N };
enum { L_TRIG, L_NOTE, L_OCT, L_DUR, L_VEL, L_N };   /* LANES' lanes */
/* note 1..127; len 1..16; v: TRIG 0..1, NOTE 0..15, OCT 0..4 (-2..+2), DUR 0..4, VEL 0..3 */
typedef struct { uint8_t note, len[L_N], v[L_N][16]; } q_lanes;
/* q_pattern: q_lanes lanes; after q_life life */
#define Q_VERSION 8

/* gen.h, in q_gen */
uint32_t lanes_n;                /* LANES: the steps played since the reset; lane l's head is lanes_n mod len[l] */
/* the degree 0..15 above note (in the scale) nearest to pitch; ties: the lower */
int gen_lanes_degree(int note, uint16_t scale, int tonic, int pitch);
```
`gen_loop` rend 16 pour `SRC_LANES`. `gen_step` pour LANES (`lanes_step`, statique) : `c = lanes_n++` ; rien si `v[L_TRIG][c % len[L_TRIG]]` vaut 0 ; sinon synthé : `gen_life_note(note, gen_scale(p), p->tonic, deg) + 12 * (oct - 2)` borné à 1..127 (un pas d'octave hors bornes : la note garde l'octave qui tient) ; KIT : `KIT_NOTE + deg` (deg déjà 0..15), OCT ignoré ; `len`, `vel` selon la table ; VEL 3 rend l'accent sur le slot 0 ; `prob` 20, `micro` 0. Le KIT se lit comme pour LIFE (`p->sound[track].model == M_KIT`). `gen_reset` remet `lanes_n` à 0 (il est déjà appelé au départ et au changement de pattern).

- [ ] **Step 1: Tests** dans `gen_test.c` (`lanes_setup(model)` sur le modèle de `life_setup`, `src = SRC_LANES`) :
  - `lanes_defaults` : un pattern neuf porte les défauts des Global Constraints.
  - `lanes_heads` : défauts, 420 pas joués : la suite (note, len, vel) des pas 0..419 est égale à celle des pas 420..839 ; et pour 1 ≤ m < 420, les 60 premiers pas ne se répètent pas à l'identique au décalage m (au moins une différence).
  - `lanes_no_reset_at_wrap` : TRIG LEN 16 tout à 1, NOTE LEN 5 : le pas 16 (deuxième tour) joue la case NOTE 1, pas la case 0.
  - `lanes_reset` : 7 pas, `gen_reset`, un pas : la note est celle du pas 0.
  - `lanes_len_shrink` : NOTE LEN 16, 9 pas, NOTE LEN passe à 4 : le pas suivant joue la case `9 mod 4` = 1.
  - `lanes_trig_off` : TRIG tout à 0 : 16 pas, aucun `SF_ON`.
  - `lanes_notes_synth` : C majeur, NOTE 48, deg 0, 1, 2, 7 → 48, 50, 52, 60 ; OCT 0 et 4 → −24 et +24.
  - `lanes_note_clamp` : NOTE 127, deg 15, OCT 4 : note dans 1..127 ; NOTE 1, OCT 0 : note ≥ 1.
  - `lanes_kit` : KIT, deg 4, OCT 0 → `KIT_NOTE + 4` ; deg 15 → `KIT_NOTE + 15`.
  - `lanes_dur_vel` : DUR 0..4 → `len` 2, 4, 8, 16, 32 ; VEL 0..2 → 40, 70, 100 sans accent ; VEL 3 → 100 et le bit d'accent du slot 0.
  - `lanes_degree` : C majeur, NOTE 48 : pitch 48 → 0, 52 → 2, 61 → 7 (C#4 : 60 et 62 à égalité, la plus basse), 30 → 0, 100 → 15.
  - `lanes_pattern_switch` : via `seq.c` comme `life_pattern_switch_reseeds` : deux patterns LANES, passage au second en fin de boucle : son premier pas joue ses cases 0.
  - `loop_lengths` (existant) : `SRC_LANES` → 16.
  Dans `store_test.c`, à côté du test des bornes de LIFE : `lanes_clamped_on_load` : `note` 0, `len` 0 et 40, `v[L_NOTE]` 99, `v[L_OCT]` 9 → 1, 1 et 16, 15, 4.
- [ ] **Step 2:** `tests/run_tests.sh` : échec de compilation (`SRC_LANES`, `q_lanes` inconnus).
- [ ] **Step 3:** Implémenter dans `project.h`, `seq.c` (`project_defaults`, à côté de `pt->life`), `store.c` (`clamp_project`, après `q_life`), `gen.h`, `gen.c`.
- [ ] **Step 4:** `tests/run_tests.sh` : tout passe (les scripts existants inclus).
- [ ] **Step 5:** Commit `LANES: a source of polymetric lanes, its run in gen.c`.

### Task 2: les pages SEQ de LANES

**Files :** Modify `app/ui.c` ; Create `app/tests/lanes.txt`

**Consumes :** `SRC_LANES`, `q_lanes`, `L_*` (Task 1).

**Produces :** `P_LANES, P_LENS` en fin de l'enum des pages ; leurs lignes de `PAGE[]` :
```c
{"LANES", {"DIV", "NOTE", "TRIG LEN", "SOURCE"}, {0, 1, 1, 0}, {DIV_N - 1, 127, 16, SRC_N - 1}},
{"LENS", {"NOTE LEN", "OCT LEN", "DUR LEN", "VEL LEN"}, {1, 1, 1, 1}, {16, 16, 16, 16}},
```
- `GEN_PAGES` devient `[4][5]` avec `{P_LANES, P_LENS}` ; `pages()` rend 2 pour LANES.
- `knob_hidden` : sur un KIT, `P_LANES` K2 et `P_LENS` K2.
- `SRC_NAME` gagne « LANES » ; DIV et SOURCE partagés avec les autres générateurs ; NOTE par `note_name`.
- Sondes : `lnbase` (`lanes.note`), `llen_<l>` (`lanes.len[l]`, l 0..4).

- [ ] **Step 1: Script** `app/tests/lanes.txt`, sur le modèle de `life.txt` : `tap SEQ`, `turn K4 4` → `expect src 4`, `expect page 0`, `shot lanes_page1.png` ; NOTE aux bornes 1 et 127 ; TRIG LEN aux bornes 1 et 16 (`expect llen_0`) ; `turn SELECT 1`, les quatre LEN de LENS aux bornes, `shot lanes_page2.png` ; `turn SELECT 1` → retour page 0 ; KIT (`tap EDIT`, `turn PRESET 20`, `expect model 24`, `tap SEQ`) : `turn K2 5` ne change pas `lnbase`, page LENS `turn K2 5` ne change pas `llen_2`, `shot lanes_kit.png`.
- [ ] **Step 2:** `tests/run_tests.sh` : `lanes.txt` échoue (sondes inconnues).
- [ ] **Step 3:** Implémenter dans `ui.c`.
- [ ] **Step 4:** `tests/run_tests.sh` : tout passe ; regarder les trois captures.
- [ ] **Step 5:** Commit `LANES: its pages LANES and LENS`.

### Task 3: la saisie des cases

**Files :** Modify `app/ui.c`, `app/audio.h`, `app/audio.c` ; Create `app/tests/lanes_entry.txt`

**Consumes :** `q_lanes`, `L_*`, `gen_lanes_degree` (Task 1) ; `P_LANES` (Task 2).

**Produces :** dans l'état `ui` : `lane` (ligne éditée, `L_TRIG` au démarrage, non sauvegardée), `cell` (case tenue 1..16, 0 aucune), `cell_key`, `cell_edited`, `cell_ms` et `cell_flash` (la dernière case touchée, pour la scène : `cell_flash` = case 1..16, `cell_ms` = `plat_ms()` du toucher). Une aide `lanes_on()` : `ui.domain == D_SEQ && pat()->src == SRC_LANES`. Sondes : `lsel` (`ui.lane`), `lv_<l>_<c>` (`lanes.v[l][c - 1]`, c 1..16), `lcell` (`ui.cell`).
```c
/* audio.h: a LANES pattern's step count as the render left it (0 stopped or just reset) */
uint32_t audio_lanes(int track);
```
`audio.c` : rend `seq.g[track].lanes_n` (sur le modèle de `audio_life`).

Règles (spec section 5) :
- `rec_release` : dans SEQ sur un pattern LANES, OFF ↔ GRID (jamais NOTES) ; `badge` montre GRID dans ce cas.
- PRESET dans SEQ sur un pattern LANES : `ui.lane` ± 1, borné à 0..4, OCT sauté sur un KIT (au lieu de changer de pattern ; le pattern reste sur SEQ tenu + blanches). Si la piste devient un KIT avec `ui.lane == L_OCT`, la ligne éditée vaut `L_NOTE`.
- En GRID (LANES), blanche w < LEN de la ligne : appui → `cell = w + 1`, `cell_edited = 0`, `cell_flash`/`cell_ms` posés ; w ≥ LEN : rien.
- Relâchement de `cell_key` sans édition : TRIG bascule ; autres lignes `v = (v + 1) % (max + 1)` (max : NOTE 15, OCT 4, DUR 4, VEL 3). Puis `cell = 0`.
- Case tenue + K1 (toute page) : `v` ± 1 borné à 0..max ; `cell_edited = 1` ; les autres encodeurs ne font rien tant qu'une case est tenue.
- Case NOTE tenue + autre touche : synthé `gen_lanes_degree(lanes.note, gen_scale(ui.p), ui.p->tonic, key_play_note(k))` ; KIT : `key_play_note(k) - KIT_NOTE` borné à 0..15 ; `cell_edited = 1`. Autre ligne : rien.
- LED (`key_light`) : LANES dans SEQ, armé ou non : cases `v != 0` à mi-niveau, tête de la ligne éditée pleine (`(audio_lanes(track) - 1) mod len`, seulement si le pattern affiché joue), au-delà de LEN éteintes.
- Désarmé, les touches jouent la piste (chemin des générateurs existant).
- Aide des boutons tenus : une case tenue montre `+ K1 VALUE` et, sur NOTE, `+ KEY NOTE`.

- [ ] **Step 1: Script** `app/tests/lanes_entry.txt` : `tap SEQ`, `turn K4 4`, `tap REC` → `expect badge 1` ; `turn PRESET 1` → `expect lsel 1` ; `tapkey w1` sur NOTE (case 1, valeur 0) → `expect lv_1_1 1` ; 15 `tapkey w1` → `expect lv_1_1 0` (retour à 0 après 15) ; `key 2 down`, `turn K1 3`, `key 2 up` → `expect lv_1_2 7` (4 + 3, pas d'incrément au relâchement) ; `key 4 down`, `key 11 down`, `key 11 up`, `key 4 up` → `expect lv_1_3 9` (touche 11 : note 64 à l'octave 0, `53 + k` ; C majeur depuis 48 : degré 9) ; `turn PRESET -1` → `expect lsel 0`, `tapkey w2` → `expect lv_0_2 1` ; une blanche au-delà de LEN (`tapkey w9`, TRIG LEN 7) : `expect lv_0_9 0`, `expect lcell 0` ; `tap REC` → `expect badge 0` ; KIT avec `lsel 2` (OCT) : après le passage au KIT `expect lsel 1`, `turn PRESET 1` → `expect lsel 3` ; `shot lanes_entry.png`.
- [ ] **Step 2:** `tests/run_tests.sh` : `lanes_entry.txt` échoue (sondes inconnues).
- [ ] **Step 3:** Implémenter dans `ui.c`, `audio.h`, `audio.c`.
- [ ] **Step 4:** `tests/run_tests.sh` : tout passe ; regarder la capture.
- [ ] **Step 5:** Commit `LANES: cells entered on the grid, PRESET picks the lane`.

### Task 4: la scène

**Files :** Create `app/scene_lanes.c` ; Modify `app/scene.h`, `app/app_ui.c`, `tests/unit/scene_test.c`, `tests/unit/plateau_test.c` (inclusion après `scene_life.c`)

**Consumes :** `q_lanes`, `L_N` (Task 1).

**Produces :**
```c
/* scene.h: LANES' rows of blocks (scene_lanes.c); ui.c fills the view */
typedef struct {
    q_lanes lanes;             /* the pattern's */
    int8_t head[L_N];          /* each lane's cell playing, -1 none (stopped, or another pattern plays) */
    uint8_t sel, kit;          /* the lane edited; a KIT: no OCT row */
    int8_t flash;              /* the cell touched 0..15 on the edited lane, -1 none */
    uint32_t flash_ms;         /* when */
    char name[16][4];          /* NOTE cells' texts: note names, or "P1".."P16" on a KIT */
    uint32_t n, step_ms;       /* audio_lanes' count (the floor slides a notch when it changes), a step in ms */
} q_laneview;
void lanes_frame(const q_laneview *v, uint32_t ms);  /* builds the primitives */
void lanes_band(const q_laneview *v, int32_t oy);    /* p3d_draw, then the texts */
```
Géométrie : spec section 6 (coordonnées, hauteurs, sol filaire), maquette `drawFocus(g, 1)` et `decor(g, 1)` pour les tons et les facettes ; mur rose `#9a1659` ; case touchée blanche pendant 300 ms après `flash_ms` ; le sol glisse de la fraction `(ms - t_pas) / step_ms`, où `t_pas` est l'heure où `lanes_frame` a vu `n` changer, et ne bouge pas quand `n` ne change plus (arrêt). Ordre d'envoi : ligne éditée, vue d'ensemble, murs, sol. Textes : `FONT_S`, `EDIT <LANE>` et `LEN <n>`, `-2 -1 0 +1 +2`, `1/4 1/2 1 2 4`, `pp mf f ff`.

- [ ] **Step 1: Tests** dans `scene_test.c` (`lnview()` : défauts de Task 1, `head` {3, 2, 1, 0, 5}, `sel` 1, `flash` −1 ; `lnband(b)` sur le modèle de `lband`) :
  - `lanes_budget` : cinq lignes de 16 cases toutes à leur max, LEN 16 : `p3d_count() <= 300`.
  - `lanes_heads_pink` : du rose dans la case de chaque tête, en haut, et dans la case `head[sel]` en bas.
  - `lanes_idle_heads` : `head` tout à −1 : aucun rose.
  - `lanes_wall_follows_len` : LEN de NOTE 5 puis 9 : les pixels roses sombres du mur changent de colonne, en haut et en bas.
  - `lanes_kit_four_rows` : `kit` 1 : la cinquième rangée du haut est vide (rien sous y 70), la ligne OCT n'est jamais dessinée.
  - `lanes_flash_white` : `flash` 2 à `ms` 0 : du blanc dans la case 3 du bas à +100 ms, plus à +400 ms.
  - `lanes_floor_slides` : `n` passe de 4 à 5 à `ms` 0, `step_ms` 125 : les pixels du sol diffèrent entre +20 ms et +80 ms ; `n` inchangé ensuite (arrêt) : deux images à +300 ms et +600 ms identiques.
  - `lanes_budget_floor_last` : au pire cas du budget, toutes les cases sont dessinées (le sol peut manquer).
  - `lanes_knobs_visible` (règle des scènes) : une image de référence, puis chaque changement seul d'un `len[l]`, de `sel`, de `name[0]` (NOTE de base) et d'une tête (DIV : la tête avance) change au moins un pixel des bandes.
- [ ] **Step 2:** `tests/run_tests.sh` : échec (`lanes_frame` inconnu).
- [ ] **Step 3:** Écrire `app/scene_lanes.c`, déclarer dans `scene.h`, inclure dans `app_ui.c` et les deux tests.
- [ ] **Step 4:** `tests/run_tests.sh` : tout passe.
- [ ] **Step 5:** Commit `LANES: its scene of block rows`.

### Task 5: la scène à l'écran

**Files :** Modify `app/ui.c` ; Create `app/tests/lanes_scene.txt`

**Consumes :** `q_laneview`, `lanes_frame`, `lanes_band` (Task 4) ; `audio_lanes`, `ui.lane`, `ui.cell_flash`, `ui.cell_ms` (Task 3) ; pages `P_LANES`, `P_LENS` (Task 2).

`ui.c` : sur `P_LANES` et `P_LENS`, `v->scene.anim = 9` ; la vue remplie depuis `pat()->lanes`, `head[l] = (audio_lanes - 1) mod len[l]` quand `audio_pos >= 0`, que le pattern affiché est celui qui joue et que `audio_lanes > 0`, sinon −1 ; `name[c]` par `note_name(gen_life_note(note, gen_scale(ui.p), ui.p->tonic, c))` ou `"P<c+1>"` sur un KIT ; `sel`, `flash`, `flash_ms` depuis `ui` ; `n = audio_lanes(track)`, `step_ms = 2500 * DIV96[div] / tempo` comme pour LIFE ; `lanes_band(&lnv, …)` dans la boucle des bandes ; la clé de scène inclut l'anim comme les autres.

- [ ] **Step 1: Script** `app/tests/lanes_scene.txt` : `tap SEQ`, `turn K4 4`, `expect anim 9`, `tap PLAY`, `wait 1500`, `shot lanes_play.png` ; `turn PRESET 1`, `wait 300`, `shot lanes_note.png` ; `turn SELECT 1`, `expect anim 9`, `turn K1 -2` (NOTE LEN 3), `wait 600`, `shot lanes_len.png` ; KIT comme dans `life_scene.txt`, `shot lanes_kit_play.png` ; `tap PLAY`.
- [ ] **Step 2:** `tests/run_tests.sh` : `lanes_scene.txt` échoue (`anim` 2 au lieu de 9).
- [ ] **Step 3:** Implémenter ; relever les adresses `llvm-nm` des cinq symboles audio avant et après (`./build.sh` puis `llvm-nm app/build/quartz.elf | grep -E ' (pq_init|seq_block|app_render|sysex_byte|fm1_timer5_irq)$'`).
- [ ] **Step 4:** `tests/run_tests.sh` : tout passe ; les adresses n'ont pas bougé ; regarder les quatre captures (têtes roses, mur, ligne éditée en bas, valeurs écrites en anglais).
- [ ] **Step 5:** Commit `LANES: the scene on the SEQ pages`.

Après la Task 5 : mise à jour de la spec globale (section 6 : une ligne LANES ; section 7 : PRESET choisit la ligne d'un pattern LANES), revue de la branche, puis test sur l'appareil par l'utilisateur (`fm1_install --yes --classic`) ; mesure CPU reportée à la session de mesures finale (pire cas : cinq lignes de 16 cases, DIV 1/32 ; ligne dans `docs/cpu.md`).
