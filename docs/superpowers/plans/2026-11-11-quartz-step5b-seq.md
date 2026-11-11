# QUARTZ étape 5b (plateau SEQ, scène MARBLES) — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** un firmware QUARTZ installable où les pages STEPS et XY montrent le plateau H2 animé et où les pages MARBLES, T, X d'un pattern MARBLES montrent les billes qui tombent sur les notes jouées.

**Architecture :** `plateau.c` dessine le plateau à partir d'une vue préparée par `ui.c` (textes compris : il ne lit ni le projet ni les tables de `ui.c`), directement dans chaque bande, du fond vers l'avant. `scene.c` gagne la scène MARBLES, nourrie par un anneau de notes tenu par `audio.c`. `ui.c` généralise le drapeau animé de 5a (`s3d`) à trois scènes.

**Tech Stack :** C (gnu11, sans libc dans l'app), canvas `base/device/gfx.c`, `app/p3d.c`, `fastmath.h` ; tests `tests/run_tests.sh`.

**Spec :** `docs/superpowers/specs/2026-11-11-quartz-step5b-seq-design.md` (et `2026-11-11-quartz-step5a-gfx-design.md`, spec globale section 9).

## Global Constraints

- Identité git `f0nski <13540044+F0nski@users.noreply.github.com>`, branche `step5b-seq` ; pas de ligne `Co-Authored-By`.
- Code, commentaires, commits en anglais ; pas de libc ; aucun appel libm (`fastmath.h`).
- Zone 240×166 en deux bandes de 83 lignes (`oy = 0`, `oy = -83`) ; coordonnées de zone.
- Couleurs du plateau : fond `#07041a`, lignes de fuite `#160f36`, cubes `#2a1d55`, case `#0e0a26` bordée `#2a1d55`, case hors boucle `#06031a` bordée `#120c2c`, pas éteint `#16112f`, pulse `#5b2f8c`, bloc `#ffffff #c9c3e4 #6f6890`, éteint (pas tenu) `#4a4466 #332e4c #221d36`, orange `#ffc196 #ff7a2e #a3410c`, vert `#8ff5c8 #22d58e #0f6d48`, rose `#ffb8dc #ff2f8e #8a1050`, chemin `#8a6ad8` et flèches `#b89cff`, texte sur bloc `#06041a`.
- Couleurs MARBLES : fond `#0c0820`, plots `#3a2c64`, socle des cases `#2a2050`, billes `#ffffff #c9c3e4 #6f6890` (orange si DEJA VU > 64), colonnes bleues `#8fb4ff #3a6dff` (orange si DEJA VU > 64), texte `#f4f1ff`, gris `#8b84a8`.
- Animations au temps (`plat_ms`), redessin au plus toutes les 30 ms (règle de 5a).
- Noms de notes sans octave (`TONIC_NAME[note % 12]`) ; KIT : numéro du pad `note[0] − KIT_NOTE + 1`.
- Zéro bloc perdu dans le pire cas sur l'appareil (tâche 6).

## Review Focus

1. Changement de page de pas (`spage`), de pattern ou de piste pendant la lecture : le viseur et le saut rose ne restent pas sur une case d'une autre page ; pas de faux saut à l'arrivée (test `no_hit_on_arrival`, tâche 3).
2. LEN qui ne remplit pas la page (LEN 5, LEN 17 sur la page 2) et X LEN × Y LEN sur XY : cases sombres et trait de fin au bon endroit, aucun bloc dessiné hors boucle (test `beyond_len_dark`, tâche 3).
3. Rafale de notes MARBLES (T OUT 3, divisions rapides) : jamais plus de 6 billes, la plus ancienne remplacée, aucun débordement de l'anneau quand l'interface ne lit pas pendant > 16 notes (tests `six_marbles_max` tâche 5, `ring_overrun_skips_oldest` tâche 2).
4. Notes permises réduites à une seule classe, ou toutes les 12 : cases de largeur correcte, aucune division par zéro (test `bins_one_and_twelve`, tâche 5).
5. Pas tenu sur la page 3 d'un pattern de 64 pas, puis relâché : le plateau revient en grand sans reste du panneau d'attributs (script `plateau.txt`, tâche 4).

---

## Structure des fichiers

```
app/p3d.c                    règle des lignes [y haut, y bas)
app/audio.c, audio.h         anneau de notes par piste, audio_notes
app/plateau.c, plateau.h     (nouveaux) le plateau SEQ
app/scene.c, scene.h         scène MARBLES
app/app.c                    #include "plateau.c" (après scene.c, avant ui.c)
app/ui.c                     vue du plateau et de MARBLES, drapeau animé
tests/unit/p3d_test.c, audio_test.c, scene_test.c, plateau_test.c (nouveau)
app/tests/plateau.txt, app/tests/marbles_scene.txt (nouveaux)
docs/cpu.md
```

---

### Task 1: p3d — lignes dont le centre est dans [y haut, y bas)

**Files:** Modify `app/p3d.c` (`p3d_fill`) ; Test `tests/unit/p3d_test.c`

- [ ] **Step 1: Test** `flat_on_centres` : triangles `(10, 10.5) (60, 10.5) (30, 40.5)` et `(10, 40.5) (60, 40.5) (30, 10.5)` : chaque pixel rempli ssi son centre est dans le triangle, ligne `y = 10` (centre 10,5 sur le bord haut) incluse, ligne `y = 40` (centre sur le bord bas) exclue ; aucun NaN (tester sous `-fsanitize=undefined` en plus d'ASan pour ce fichier : ajouter `-fsanitize=undefined -fno-sanitize-recover` à la ligne C de `run_tests.sh`).
- [ ] **Step 2: Lancer** `tests/run_tests.sh` : échec (ligne 10 manquante ou NaN signalé).
- [ ] **Step 3: Corriger** : `ya = ceil(y0 − 0.5)`, `yb = ceil(y2 − 0.5)`, bornés à `[0, cv_h]` après clamp des y à ±300.
- [ ] **Step 4: Lancer** : `all tests ok`.
- [ ] **Step 5: Commit** `P3D: rows whose centre is in [top, bottom), no 0/0 on a flat edge`

---

### Task 2: audio — anneau des notes jouées

**Files:** Modify `app/audio.h`, `app/audio.c` (`note_on`) ; Test `tests/unit/audio_test.c`

**Interfaces:**
- Produces : `int audio_notes(int track, uint32_t *seen, uint8_t *out, int max);` — copie dans `out` les notes jouées par la piste après la `*seen`-ième (au plus `max`, les plus anciennes d'abord), avance `*seen`, rend leur nombre ; en retard de plus de 16, saute aux 16 dernières. Anneau `static volatile uint8_t ring[Q_TRACKS][16]`, écrit dans `note_on` avant `hits[track]++` ; le compteur de l'anneau est `hits` (une seule écriture par note).

- [ ] **Step 1: Tests** : `notes_in_order` (3 notes live sur la piste 2 → `audio_notes(2, &s, out, 8) == 3`, `out` = 60, 64, 67 dans l'ordre, un second appel rend 0) ; `ring_overrun_skips_oldest` (20 notes sur 20 blocs sans lecture → l'appel rend 16, `out[0]` est la 5e note, `seen` = total) ; une note sur la piste 0 n'apparaît pas sur la piste 1.
- [ ] **Step 2: Lancer** : échec (`audio_notes` non défini).
- [ ] **Step 3: Implémenter.**
- [ ] **Step 4: Lancer** `tests/run_tests.sh` : `all tests ok`.
- [ ] **Step 5: Commit** `Audio: the notes played per track, for the scenes`

---

### Task 3: le plateau

**Files:** Create `app/plateau.h`, `app/plateau.c` ; Modify `app/app.c` ; Test `tests/unit/plateau_test.c`

**Interfaces:**
- Produces (`plateau.h`) :
  ```c
  typedef struct { uint8_t on, vel, extra, len; char name[4]; } q_pcell;   /* len: q_step.len (4 = one step) */
  typedef struct {
      q_pcell c[16];
      uint16_t live;          /* bit i: cell i is inside LEN and X LEN x Y LEN */
      int8_t pos;             /* the cell playing, -1: none on this page or stopped */
      int8_t cursor;          /* REC stopped: the write cursor's cell, else -1 */
      int8_t end;             /* the loop's last cell when the loop ends in the page, else -1 */
      int8_t held, attr;      /* held cell or -1; held: the PRESET row 0..8 */
      int8_t npath;           /* cells in path (0: no path shown) */
      uint8_t path[16];
      char top_l[8], top_r[6];         /* "STEP 05" or "17-32"; the playing note or "--" */
      char rows[9][2][10];             /* held: label and value of each attribute, in ATTR_ROW order */
      char held_name[6];
  } q_plateau;
  void plateau_enter(void);                                  /* shown anew: no hit for the notes before */
  void plateau_frame(const q_plateau *p, uint32_t hits, uint32_t ms);
  void plateau_band(int32_t oy);
  ```
- Valeurs : coins de case `(ox + (gx − gy)·A, oy0 + (gx + gy)·B)` ; normal `A = 25, B = 12.5, origine (120, 34)` ; pas tenu `A = 16, B = 8, origine (64, 48)`. Bloc : marge 0,18 case, hauteur `5 + 22·vel/127` (pas tenu : `5 + 16·vel/127`), faces gauche (base), droite (ombre), dessus (clair) + contour du dessus (ombre), nom centré sur le dessus en `FONT_T` (`#06041a`, rose foncé `#3a0620` en saut). Repère : triangle orange `(q.x − 6, q.y − 1) (q.x, q.y − 4) (q.x, q.y + 2)` au coin droit `q` du dessus. Bande verte : de `(gx + 0.5, gy + 0.62)` à `(gx + 0.5 + L, gy + 0.72)`, `L = min(len / 8, 4 − gx − 0.18)`, avant le bloc. Ordre de dessin : sol (toutes les cases), trait de fin, onde, puis cases par `gx + gy` croissant (bande verte, bloc, nom, repère, éclats), chemin, viseur, textes du haut.
- Saut : `hits` change alors que `pos >= 0` → `hit_ms = ms`, `hit_cell = pos` ; actif 150 ms (bloc rose, levé de `6·(1 − k)` px, `k` = fraction écoulée ; onde carrée de demi-taille `0.5 + 1.6k` cases ; 3 éclats montant de `26k` px). Viseur rose pendant le saut, vert clair sinon ; blanc sur `cursor`.
- Pulse : quand `pos` passe sur une case d'indice multiple de 4, les bordures des cases à distance < 3 de `pos` passent en `#5b2f8c` pendant 200 ms.
- Décor : 17 lignes de `(120, 4)` vers `(120 + 40k, 166)`, `k = −8..8` ; 6 bandes `y = 4 + 162·((k + frac(0.5 t)) / 6)²` ; cubes en `(28, 20)` et `(212, 16)`, carré de demi-côté 7 aplati ×0,5 tournant à `0.6 t`, arêtes verticales de 9 px.
- Pas tenu : panneau x 128–234, 9 lignes de 17 px depuis y = 8 ; ligne `attr` en polygone orange `(x0, y) (x0 + w, y) (x0 + w, y + 15) (x0 + 6, y + 15) (x0, y + 9)`, texte foncé ; autres : filet `#1a1433` en bas, libellé gris, valeur blanche ; `held_name` en `FONT_M` orange centré en (64, 150).
- `app.c` : `#include "plateau.c"` après `scene.c`.

- [ ] **Step 1: Tests** (`plateau_test.c`, inclut `gfx.c`, `p3d.c`, `plateau.c` ; une vue de base : 16 pas actifs vel 100, `live = 0xFFFF`, `pos = cursor = end = held = −1`) :
  - `block_height_follows_vel` : cellule 0 seule active ; pixels de ton bloc dans la colonne x = 120 au-dessus de y = 46 : ≥ 20 avec vel 127, ≤ 8 avec vel 0 ;
  - `beyond_len_dark` : `live = 0x00FF`, `end = 7` : le centre de la case 15 (120, 121) vaut `#06031a` (bande 1) ; aucun pixel de ton bloc sous la rangée 2 ; au moins 10 pixels orange le long du bord après la case 7 ;
  - `extra_marker` : cellule 5 `extra = 1` : des pixels orange `#ff7a2e` dans le rectangle de la case 5 ; aucun sans `extra` ;
  - `held_is_orange` : `held = 5` : des pixels orange dans la zone x < 128 ; aucun pixel `#ffffff` en x < 128 ; des pixels `#ff7a2e` (ligne `attr`) en x ≥ 128 ;
  - `hit_turns_pink` : `pos = 0`, frame à `hits = 1` puis `hits = 2` à +10 ms : pixels `#ff2f8e` présents ; à +400 ms : aucun pixel `#ff2f8e` ;
  - `no_hit_on_arrival` : `plateau_enter()`, frame avec `hits = 50`, `pos = 0` : aucun pixel `#ff2f8e` ;
  - `path_drawn` : `npath = 16` (SNAKE) : des pixels `#b89cff` ; `npath = 0` : aucun.
- [ ] **Step 2: Lancer** : échec (`plateau.c` absent).
- [ ] **Step 3: Implémenter.**
- [ ] **Step 4: Lancer** `tests/run_tests.sh` : `all tests ok`.
- [ ] **Step 5: Commit** `Plateau: the SEQ steps as low-poly blocks on a wire floor, the sight, hits and the held step`

---

### Task 4: le plateau dans l'interface

**Files:** Modify `app/ui.c` ; Test `app/tests/plateau.txt`

**Interfaces:**
- Consumes : tâche 3 (`q_plateau`, `plateau_*`), `audio_hits`.
- `q_view.scene.s3d` devient `anim` : 0 aucune, 1 planète, 2 plateau, 3 MARBLES (tâche 5). `scene3d` dans `ui_expect` rend `anim == 1` ; nouveau `anim` rend la valeur.
- `view_now` : `anim = 2` pour `P_STEPS` et `P_XY` hors pattern générateur. `ui_frame` : branche animée commune (30 ms, `enter` quand `anim` ou piste change ou `ui.full`) ; pour 2, construit un `q_plateau` depuis `v.scene` :
  - `c[i]` : `on = flags & SF_ON`, `vel`, `len`, `extra` = ratchet > 1 ‖ prob < 100 ‖ cond ‖ SF_SLIDE ‖ SF_ACCENT ‖ micro ‖ bit `locks`, `name` = `TONIC_NAME[note[0] % 12]` ou pad ;
  - `live` bit i : `spage·16 + i < len` et (`i % 4 < xlen`, `i / 4 < ylen`) ;
  - `pos` : `v.scene.pos − spage·16` si dans 0..15, sinon −1 ; `cursor` : `v.scene.cursor − 1 − spage·16` si dans 0..15 ;
  - `end` : `len − 1 − spage·16` si dans 0..14, sinon −1 ;
  - `npath`/`path` : les 16 cases de `PATHS[m]` (même choix de `m` que l'affichage actuel, ordre inversé pour REV) quand `showpath` et chemin ni RANDOM ni XY ;
  - `top_l` : `STEP nn` (numéro absolu) si `pos >= 0`, sinon `a-b` de la page ; `top_r` : note du pas en cours ou `--` ;
  - pas tenu : `held = ui.held − 1 − spage·16` (si sur la page, sinon le plateau reste en grand), `rows` par `ATTR_NAME`/`attr_text` dans l'ordre `ATTR_ROW`, `attr` = rang de `v.scene.attr` dans `ATTR_ROW`, `held_name` = `note_name` de la note tenue.
- L'ancien dessin STEPS/XY de `draw_scene` est retiré.

- [ ] **Step 1: Script** `plateau.txt` : boot ; `tap SEQ` → `expect anim 2`, `shot plateau_stopped.png` ; `tap PLAY`, `wait 400`, `shot plateau_play.png` ; `key 0 down` → `shot plateau_held.png`, `key 0 up`, `expect anim 2` ; `turn K3 3` (PATH SNAKE) → `shot plateau_path.png` ; `turn K3 -3` ; `turn K1 -11` (LEN 5) → `shot plateau_len5.png` ; `turn SELECT 1` (XY) → `expect anim 2`, `shot plateau_xy.png` ; `tap PLAY` ; `tap EDIT` → `expect anim 0` ; plus le cas 5 de la Review Focus : LEN 64 (`turn K1 59`), page 3 (touche de page comme `seq.txt`), pas tenu puis relâché → `expect anim 2`, `shot plateau_page3.png`.
- [ ] **Step 2: Lancer** : échec (`anim` inconnu).
- [ ] **Step 3: Implémenter.**
- [ ] **Step 4: Lancer** `tests/run_tests.sh` : `all tests ok` (les scripts existants qui testent STEPS par `expect` restent verts) ; regarder les captures et les comparer à la maquette H2.
- [ ] **Step 5: Commit** `UI: the SEQ plateau on STEPS and XY`

---

### Task 5: la scène MARBLES

**Files:** Modify `app/scene.h`, `app/scene.c`, `app/ui.c` ; Test `tests/unit/scene_test.c`, `app/tests/marbles_scene.txt`

**Interfaces:**
- Consumes : tâche 2 (`audio_notes`), tâche 4 (`anim`).
- Produces (`scene.h`) :
  ```c
  int  marbles_bin(uint16_t allowed, int note, int *nbins);   /* the bin of note's pitch class (nearest allowed one) */
  void marbles_enter(void);                                   /* no marble in flight, history kept */
  void marbles_drop(int note, uint32_t ms);
  /* knobs: SPREAD, X BIAS, DEJA VU 0..127; allowed: gen_allowed; top_l "5/16", top_r "D DORIAN" */
  void marbles_frame(const uint8_t knobs[3], uint16_t allowed, const char *top_l, const char *top_r, uint32_t ms);
  void marbles_band(int32_t oy);
  int  marbles_flying(void);                                  /* marbles in flight (tests) */
  int  marbles_hist(int bin);                                 /* a bin's history 0..6 (tests) */
  ```
- Valeurs : cases = classes permises dans l'ordre croissant depuis C, largeur `224 / n`, à partir de x = 8 ; socle à y = 146 ; nom à y = 150 (`FONT_S`) ; colonne : prisme de 8 px de large, hauteur `4·hist`, face base et dessus clair (dessus décalé de 2 px) ; plots : 5 rangées y = 22 + 19k, colonnes `x = 120 + i·gap + (k % 2)·gap/2 + (xbias − 64)/64·7·k/4`, gardées si 10 ≤ x ≤ 230. Bille : départ (120, 0) ; durée 500 ms ; `y = 140·u` (`u` = fraction écoulée) ; `x = 120 + (tx − 120)·min(1, rangées passées + progression dans la rangée) / 5` (dévie après chaque rangée, puis tombe droit) ; icosaèdre `s = 7`, `ay = 4t`, `ax = 3t`. Arrivée : `hist[bin] = min(6, hist + 1)`, flash rose 150 ms de la colonne et du nom. Décroissance : −1 toutes les 4 s par case non nulle. Au-delà de 6 billes, la plus ancienne est retirée. L'historique est remis à zéro quand `allowed` change.
- `ui.c` : `anim = 3` pour `P_MARBLES`, `P_T`, `P_X` d'un pattern `SRC_MARBLES` ; à chaque image, `audio_notes(ui.track, &ui.notes_seen, …)` puis `marbles_drop` par note (à l'entrée, `notes_seen` est remis au compteur courant sans lâcher) ; `top_l` = position `pos+1/len` (`-/len` à l'arrêt), `top_r` = `TONIC_NAME[tonic] MODE_NAME[mode]`.

- [ ] **Step 1: Tests** (`scene_test.c`) :
  - `bins_one_and_twelve` : `allowed = 1 << 2` (D seul) → `nbins == 1`, toute note → 0 ; `allowed = 0xFFF` → 12 cases, note 61 → 1 ;
  - `nearest_bin` : `allowed = 0x2A5` (C D F G A) : note 61 (C#, à égale distance de C et D) → case 0 (la plus basse) ; note 64 (E) → case 2 (F) ; note 70 (A#) → case 4 (A) ;
  - `marble_lands_in_its_bin` : `marbles_drop(62, 0)` avec D permis ; frames jusqu'à 600 ms : `marbles_flying() == 0`, `marbles_hist(bin de D) == 1` ; à 600 + 4100 ms : 0 ;
  - `six_marbles_max` : 9 drops au même instant → `marbles_flying() == 6` ;
  - `deja_vu_orange` : DEJA VU 100, une bille en vol à mi-course : des pixels `#ff7a2e` dans la zone, aucun à DEJA VU 0.
- [ ] **Step 2: Script** `marbles_scene.txt` : un pattern MARBLES (SOURCE réglé comme dans `marbles.txt`), `tap PLAY`, page MARBLES → `expect anim 3`, `wait 2000`, `shot marbles_play.png` ; page T, X → `expect anim 3` ; page NOTES → `expect anim 0` ; `tap PLAY`.
- [ ] **Step 3: Lancer** : échecs attendus.
- [ ] **Step 4: Implémenter.**
- [ ] **Step 5: Lancer** `tests/run_tests.sh` : `all tests ok` ; `./build.sh` ; regarder `marbles_play.png`.
- [ ] **Step 6: Commit** `Scene: MARBLES, a marble for every note played, falling into its note's bin`

---

### Task 6: mesure sur l'appareil (avec l'utilisateur)

**Files:** Modify `docs/cpu.md`

- [ ] **Step 1: Installer** (l'utilisateur) : `! cd ~/perso/fm1 && spike-cpu/.tools/venv/bin/python quartz/base/tools/fm1_install.py --yes --classic quartz/app/build/quartz.fwsc`.
- [ ] **Step 2: Pire cas plateau** : pire cas de 5a, piste 1 en pattern STEP de 16 pas actifs avec ratchets, affichée sur STEPS, 30 s : LOST BLOCKS 0, SCENE FPS relevé.
- [ ] **Step 3: MARBLES** : une piste MARBLES T OUT 3 affichée sur MARBLES : même relevé.
- [ ] **Step 4: Écrire** les lignes « Step 5b » dans `docs/cpu.md` ; si un bloc est perdu ou FPS < 15, en parler avant d'optimiser.
- [ ] **Step 5: Commit** `CPU: step 5b measured on the device`
