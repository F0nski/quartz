# QUARTZ étape 5a (moteur 3D, scène planète) — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** un firmware QUARTZ installable où une piste FM (6-OP A/B/C, 2-OP FM) affiche sur ENGINE et SOUND la planète low poly animée, pilotée par HARM, TIMB, MORPH, DECAY et par les notes jouées.

**Architecture :** `p3d.c` tient une liste de primitives 2D (triangles pleins et traits) projetées une fois par image, triées par profondeur, puis dessinées dans chaque bande du canvas de fm1-base (`cv_*`). `scene.c` construit la planète dans cette liste et dessine son décor. `audio.c` compte les note-ons par piste ; `ui.c` appelle la scène sur ENGINE et SOUND d'une piste FM, au plus 30 fois par seconde, et affiche le FPS sur la page CPU.

**Tech Stack :** C (gnu11, sans libc dans l'app), canvas `base/device/gfx.c`, `base/include/fastmath.h` ; tests `tests/run_tests.sh` (unitaires C + scripts du simulateur hôte).

**Spec :** `docs/superpowers/specs/2026-11-11-quartz-step5a-gfx-design.md` (et la spec globale, section 9).

## Global Constraints

- Identité git `f0nski <13540044+F0nski@users.noreply.github.com>`, branche `step5a-gfx` ; pas de ligne `Co-Authored-By`.
- Code, commentaires et commits en anglais ; pas de libc dans le code de l'app ; aucun appel à libm (`sinf`, `cosf`, `sqrtf`, `powf`, `fmaxf`…) : `fm_sinf`, `fm_cosf`, `fm_sqrtf`, `fm_powf`, `fm_expf` de `fastmath.h`.
- Zone de scène : 240×166, dessinée en deux bandes de 83 lignes (`BAND_H`), `oy = 0` puis `oy = -83` ; un dessin hors de la bande est coupé par `cv_*`.
- Modèles FM : `MODEL_NAME` indices 2, 3, 4 (6-OP A/B/C) et 10 (2-OP FM).
- Aucun dégradé, aucune transparence, aucune forme ronde ; rose réservé aux notes jouées.
- Couleurs (maquette `docs/screens/scenes.html`) : orange `#ffc196 #ff7a2e #a3410c` ; bleu `#8fb4ff #3a6dff #1c3590` ; vert `#8ff5c8 #22d58e #0f6d48` ; rose `#ffb8dc #ff2f8e #8a1050` ; fond `#120b26` ; sol `#1a0f33`, lignes du sol `#5b2f8c` ; étoiles `#ffffff` (grosses) et `#6c5fa0` (petites) ; ombre au sol `#0b0618`.
- Lumière (repère écran : x à droite, y vers le bas, z vers le spectateur) : `L = normalize(-0.5, -0.65, 0.6)` ; ton d'une face `d = n·L` : `d > 0.55` clair, `d > 0.15` base, sinon ombre.
- Animation au temps (`plat_ms`), jamais au nombre d'images. Scène redessinée au plus toutes les 33 ms.
- Le rendu audio ne fait que lire `q_project` ; la boucle principale est seule à l'écrire.
- Zéro bloc perdu dans le pire cas sur l'appareil (tâche 4).

## Review Focus

1. Une face ou un trait qui sort de la zone (vaisseau sur une orbite large avec TIMB au maximum, anneau très penché) : rien n'est écrit hors du canvas, pas de plantage (test `offscreen_clipped`, tâche 1).
2. Un triangle dégénéré (trois points alignés, ou de hauteur nulle) : rien de dessiné, pas de division par zéro (test `degenerate_triangle`, tâche 1).
3. MODEL tourné d'un modèle FM vers un autre pendant la lecture : la zone passe aux barres dès l'image suivante, sans reste de la planète (script `scene.txt`, tâche 3).
4. Notes très rapprochées (ratchets, 6 voix) : chaque note relance le flash, le compteur ne perd pas de note même s'il fait le tour de `uint32_t` (comparaison par différence, test `hits_count_note_ons`, tâche 2).
5. Piste changée (ALGORITHM) d'une piste FM vers une piste non FM, sur ENGINE : les barres reviennent à l'image suivante ; retour vers la piste FM : la planète revient (script `scene.txt`, tâche 3).

---

## Structure des fichiers

```
app/p3d.c, app/p3d.h       (nouveaux) liste de primitives, triangles, traits, maillages, sphère
app/scene.c, app/scene.h   (nouveaux) la scène planète
app/app.c                  #include "p3d.c", "scene.c" (avant ui.c)
app/audio.c, app/audio.h   audio_hits
app/ui.c                   scène sur ENGINE/SOUND des modèles FM, 30 i/s, ligne FPS, expect scene3d / hits / fps
tests/run_tests.sh         build hôte d'abord (police générée), -Iapp/build/gen
tests/unit/p3d_test.c      (nouveau)
tests/unit/scene_test.c    (nouveau)
tests/unit/audio_test.c    hits_count_note_ons
app/tests/scene.txt        (nouveau)
docs/cpu.md                mesures
```

---

### Task 1: p3d — primitives, remplissage, tri, maillages

**Files:**
- Create: `app/p3d.h`, `app/p3d.c`
- Modify: `app/app.c` (include `p3d.c` après `store.c`, avant `ui.c`), `tests/run_tests.sh`
- Test: `tests/unit/p3d_test.c`

**Interfaces:**
- Produces (`p3d.h`) :
  ```c
  typedef struct { float x, y, z; } p3_vec;
  typedef struct { uint8_t a, b, c; } p3_face;              /* n = (b - a) x (c - a) points out of the mesh */
  typedef struct { const p3_vec *v; const p3_face *f; uint16_t nv, nf; } p3_mesh;
  typedef struct { float ay, ax, s, cx, cy; } p3_pose;      /* rotation about y then x (scenes.html rot), scale, screen centre */
  void p3d_begin(void);                                     /* empties the list */
  void p3d_tri(float x0, float y0, float x1, float y1, float x2, float y2, float z, uint16_t c);
  void p3d_line(float x0, float y0, float x1, float y1, float z, uint8_t width, uint16_t c);   /* width 1 or 2 */
  void p3d_mesh(const p3_mesh *m, const p3_pose *p, const uint16_t tones[3]);   /* culled, toned, one tri per face */
  void p3d_draw(int32_t oy);                                /* sorts once per frame (far first), then draws into the band */
  int  p3d_count(void);                                     /* primitives in the list (tests) */
  void p3d_sphere(int lat, int lon, p3_vec *v, p3_face *f, p3_mesh *out);   /* scenes.html sphereMesh, quads split in 2 */
  void p3d_fill(float x0, float y0, float x1, float y1, float x2, float y2, uint16_t c);  /* the raster, drawn now */
  ```
- Valeurs : liste de 160 primitives au plus (au-delà : ignorées), `PLAT_POOL`. Profondeur `z` : plus grand = plus près ; tri croissant, stable (insertion), fait une fois par image au premier `p3d_draw` après `p3d_begin`/ajouts. Projection orthographique : écran = `(cx + s·x, cy + s·y)`, profondeur d'une face = moyenne des `z` tournés. Face écartée si `n.z <= 0` (après rotation). Coordonnées passées à `p3d_draw` relatives à la zone de scène ; `cv_*` ajoute `oy`.
- Règle de remplissage : un pixel `(x, y)` est rempli si son centre `(x + 0.5, y + 0.5)` est dans le triangle, bord gauche et bord haut inclus, droit et bas exclus ; par lignes horizontales (`cv_rect(x, y, w, 1, c)`), lignes hors bande sautées sans calcul de bords.
- Trait : `cv_line` arrondi au pixel ; largeur 2 = le même décalé de 1 en x.
- `run_tests.sh` : `./build.sh --host` déplacé en tête (il génère `app/build/gen/felucca_font.h`) ; les tests C compilés avec `-Iapp/build/gen -Ibase/device`. Le test inclut `gfx.c` et définit `lcd_sync`/`lcd_blit` vides.

- [ ] **Step 1: Tests** (`p3d_test.c`, canvas `cv_begin(240, 83, 0)` ; `px(x, y)` lit `swap16(cv_px[y * 240 + x])`) :
  - `fill_matches_centres` : triangles aux sommets non demi-entiers (ex. `(10.25, 5.25) (60.75, 70.25) (5.25, 40.75)`, et 4 autres dont un très plat) : chaque pixel de la bande est rempli ssi son centre passe le test des trois fonctions d'arête (même orientation que le triangle) ;
  - `band_split_identical` : un triangle de y = 40 à y = 140 dessiné en bande 0 (`oy = 0`) puis bande 1 (`oy = -83`) : les 166 lignes réunies égalent la référence de `fill_matches_centres` calculée sur 166 lignes ;
  - `degenerate_triangle` : trois points alignés, puis `y0 == y1 == y2` : aucun pixel changé ;
  - `offscreen_clipped` : triangle `(-500, -500) (800, 10) (10, 900)` et trait `(-300, 20)–(600, 60)` largeur 2 : pas de plantage (exécuté sous `-fsanitize=address` dans ce test), seuls les pixels du canvas changent ;
  - `far_drawn_first` : deux triangles qui se recouvrent, ajoutés le proche (`z = 1`, rouge) puis le lointain (`z = -1`, bleu) : le pixel commun est rouge ;
  - `mesh_culls_and_tones` : `p3d_sphere(5, 10)` posée `s = 26, cx = 120, cy = 60` sans rotation : `p3d_count()` entre 30 et 50 (moitié arrière écartée) ; le pixel `(108, 48)` (haut-gauche) a le ton clair, `(132, 72)` (bas-droite) le ton ombre ;
  - `sphere_closed` : `p3d_sphere(5, 10)` : 60 sommets, 80 faces ; chaque normale `(b−a)×(c−a)` a un produit scalaire positif avec le centre de sa face.
- [ ] **Step 2: Lancer** `tests/run_tests.sh` : échec de compilation (`p3d.h` absent).
- [ ] **Step 3: Implémenter** `p3d.h`, `p3d.c` selon l'interface ; `p3d.c` inclut `fastmath.h`.
- [ ] **Step 4: Lancer** `tests/run_tests.sh` : `all tests ok` ; `./build.sh` construit `app/build/quartz.fwsc` ; noter la place restante du pool affichée par le build.
- [ ] **Step 5: Commit** `P3D: flat-shaded triangles and lines sorted by depth, drawn band by band`

---

### Task 2: audio — compteur de notes par piste

**Files:**
- Modify: `app/audio.h`, `app/audio.c` (`note_on`, vers la ligne 341)
- Test: `tests/unit/audio_test.c`

**Interfaces:**
- Produces : `uint32_t audio_hits(int track);` — note-ons joués par la piste depuis le démarrage (séquenceur, clavier, arpégiateur, générateurs), incrémenté dans `note_on` après `pq_note_on` ; `static volatile uint32_t hits[Q_TRACKS]`. L'interface compare par différence (`now != last`).

- [ ] **Step 1: Test** `hits_count_note_ons` : `setup()` ; `audio_live_note(1, 64, 1)` ; `block(256)` → `audio_hits(1) == 1`, les autres pistes à 0 ; deux notes de plus sur la piste 1 au même bloc → 3 ; une note-off ne change rien ; une note qui attend un changement de moteur (`model_change_while_playing_one_switch_per_block` comme modèle) ne compte qu'au bloc où elle sonne.
- [ ] **Step 2: Lancer** : échec (`audio_hits` non défini).
- [ ] **Step 3: Implémenter.**
- [ ] **Step 4: Lancer** `tests/run_tests.sh` : `all tests ok`.
- [ ] **Step 5: Commit** `Audio: note-ons counted per track, for the scenes`

---

### Task 3: la scène planète et son affichage

**Files:**
- Create: `app/scene.h`, `app/scene.c`
- Modify: `app/app.c` (include `scene.c` après `p3d.c`), `app/ui.c`
- Test: `tests/unit/scene_test.c`, `app/tests/scene.txt`

**Interfaces:**
- Consumes : tâche 1 (`p3d_*`), tâche 2 (`audio_hits`).
- Produces (`scene.h`) :
  ```c
  int  scene_has(int model);                        /* 1 for 2, 3, 4, 10 */
  /* knobs: HARM, TIMB, MORPH, DECAY 0..127; hits: audio_hits of the track; ms: plat_ms() */
  void scene_frame(const int16_t knobs[4], uint32_t hits, uint32_t ms);   /* builds the frame's primitives */
  void scene_band(int32_t oy);                      /* background, then p3d_draw(oy) */
  ```
- Géométrie (coordonnées de la zone, `t` = ms / 1000) :
  - fond `#120b26` ; sol `#1a0f33` sous l'horizon `y = 120`, ligne d'horizon, 21 lignes de fuite de `(120 + 10i, 120)` à `(120 + 70i, 166)`, `i = -10..10`, et 6 lignes horizontales `y = 120 + 46·p`, `p = ((k + frac(0.6 t)) / 6)^2.2`, `k = 0..5` ; couleur `#5b2f8c` ;
  - 30 étoiles : `i = 0..29`, `x = (73 i − v t) mod 240`, `y = 4 + (41 i) mod 100`, grosses (2×2, blanches, `v = 6`) si `i % 4 == 0`, sinon 1×1 `#6c5fa0`, `v = 3` ;
  - planète : `p3d_sphere(5, 10)`, `s = 26`, centre `(120, 62)`, `ay = 0.6 t`, `ax = 0` ; tons orange, roses pendant le flash ; ombre au sol : quadrilatère `(86, 138) (154, 138) (142, 144) (98, 144)` `#0b0618` (2 triangles, `z = -100`) ;
  - inclinaison `tilt = 0.15 + 1.0 · MORPH / 127` rad : un point de l'orbite d'angle `a` et de rayon `r` est `(r cos a, −r sin a · sin tilt, r sin a · cos tilt)` autour du centre de la planète (profondeur = 3e composante, échelle 1 : rayons en pixels) ;
  - anneau : rayon `1.85 · 26`, 12 segments ; chaque segment = quadrilatère de 4 px d'épaisseur perpendiculaire au segment (2 triangles), profondeur = celle de son milieu, ton clair si profondeur > 0, ombre sinon (orange) ;
  - vaisseaux : `n = 2 + round(3 · HARM / 127)` ; vaisseau `i` : rayon `42 + i · (8 + 10 · TIMB / 127)`, angle `a = t · 1.4 / (1 + 0.5 i) + 2.1 i`, direction = tangente projetée de l'orbite ; 3 triangles (scenes.html `ship`, en repère local du vaisseau : `(8,0)(−5,5)(−2,0)` ton base, `(8,0)(−2,0)(−5,−5)` ton clair, `(−5,3)(−10,0)(−5,−3)` rose base) ; tons bleu pour `i` pair, vert pour `i` impair ; profondeur = celle du vaisseau ;
  - flash : à chaque `hits` différent du précédent, `t0 = ms` ; `h = exp(−(ms − t0) / (50 + 250 · DECAY / 127))` (ms) ; planète rose si `h > 0.5` ; tirs si `h > 0.35` : trait rose clair largeur 2 de chaque vaisseau vers `vaisseau + 0.75 · (centre − vaisseau)`, profondeur `+1000`.
- `ui.c` :
  - `q_view.scene` gagne `uint8_t s3d` (1 quand la planète s'affiche) ; `view_now` le pose pour `pid == P_ENGINE || pid == P_SOUND` et `scene_has(model)` ; sur SOUND avec `s3d`, `scene.v` reçoit les valeurs ENGINE de la piste (`knob_get(P_ENGINE, k)`) au lieu de celles de SOUND : la scène lit toujours `scene.v` ;
  - `ui_frame` : si `v.scene.s3d`, la zone est redessinée quand `plat_ms() − ui.scene_ms >= 33` ou que la vue a changé : `scene_frame(v.scene.v, audio_hits(ui.track), plat_ms())` puis, pour chaque bande, `cv_begin(240, BAND_H, C_BLACK)`, `scene_band(-BAND_H * b)`, `cv_blit` ; sinon comme avant ;
  - FPS : compteur d'images de scène dessinées, figé chaque seconde dans `ui.fps` ; `cpu[8]`, 8e ligne `"SCENE FPS"`, lignes espacées de 20 px ;
  - `ui_expect` : `scene3d` (= `ui.shown.scene.s3d`), `hits` (= `audio_hits(ui.track)`), `fps`.

- [ ] **Step 1: Tests unitaires** (`scene_test.c`, inclut `gfx.c`, `fastmath.h`, `p3d.c`, `scene.c` ; `audio_hits` non utilisé, le test passe `hits` lui-même) :
  - `planet_orange_then_pink` : `scene_frame({64,64,64,64}, 0, 1000)` puis bande 0 : le pixel `(120, 62)` est l'un des 3 orange ; `scene_frame(..., 1, 1010)` : rose ; `scene_frame(..., 1, 2000)` (DECAY 64 : `h < 0.5`) : orange ;
  - `harm_sets_ships` : HARM 0 → `p3d_count()` = base + 2·3 triangles de vaisseaux ; HARM 127 → base + 5·3 (même `ms`, pas de flash) ;
  - `morph_tilts_ring` : la colonne `x = 120` porte un pixel orange (anneau) au-dessus de `y = 30` avec MORPH 127, aucun avec MORPH 0 ;
  - `time_not_frames` : `scene_frame` à `ms = 5000` appelé une fois ou après 10 appels à des `ms` plus petits : bande 0 identique pixel à pixel (hors flash) ;
  - `scene_has_fm_only` : 2, 3, 4, 10 → 1 ; 0, 5, 20, 24 → 0.
- [ ] **Step 2: Script** `app/tests/scene.txt` (même style que `kit.txt`) : boot ; `tap EDIT` ; `turn PRESET 2` → `expect model 2`, `expect scene3d 1`, `shot scene_engine.png` ; `turn K1 127` (HARM) puis `shot scene_harm.png` ; `key 6 down`, `wait 50`, `expect hits 1`, `shot scene_hit.png`, `key 6 up` ; `turn SELECT 1` → page SOUND, `expect scene3d 1` ; `turn K1 1` (MODEL 3) → `expect scene3d 1` ; `turn K1 2` (MODEL 5) → `expect scene3d 0`, `shot scene_off.png` ; retour à 6-OP A (`turn K1 -3`), `turn SELECT -1` (ENGINE), `tap PLAY`, `wait 500`, `expect scene3d 1` ; `turn ALGO 1` (piste 2, VA VCF) → `expect scene3d 0` ; `turn ALGO -1` → `expect scene3d 1` ; `tap PLAY` ; `tap GLO`, page CPU : `shot cpu_fps.png`.
- [ ] **Step 3: Lancer** `tests/run_tests.sh` : échecs attendus (scène absente).
- [ ] **Step 4: Implémenter** `scene.h`, `scene.c`, l'intégration `ui.c`.
- [ ] **Step 5: Lancer** `tests/run_tests.sh` : `all tests ok` ; ouvrir `app/build/host/scene_engine.png`, `scene_hit.png`, `scene_off.png`, `cpu_fps.png` et les comparer à la maquette (planète, anneau, vaisseaux, sol, étoiles ; barres revenues sur `scene_off`). Les montrer à l'utilisateur.
- [ ] **Step 6: Build appareil** `./build.sh` : `app/build/quartz.fwsc` construit ; noter le pool restant.
- [ ] **Step 7: Commit** `Scene: the FM planet on ENGINE and SOUND, driven by the knobs and the notes; FPS on the CPU page`

---

### Task 4: mesure sur l'appareil (avec l'utilisateur)

**Files:**
- Modify: `docs/cpu.md`

- [ ] **Step 1: Installer** (l'utilisateur, depuis `~/perso/fm1`) : `! spike-cpu/.tools/venv/bin/python quartz/base/tools/fm1_install.py --yes --classic quartz/app/build/quartz.fwsc`.
- [ ] **Step 2: Pire cas** : pistes 1 et 2 en POLY 3, piste 1 MODAL, piste 2 6-OP A, 6 voix jouées en continu, delay et reverb, PHASER en insert sur les deux pistes, COMP ; piste 2 affichée sur ENGINE ; 30 s. Relever sur la page CPU : CORE 1, CORE 2, LOST BLOCKS (doit rester 0), SCENE FPS. Puis la même chose sur une page sans scène, pour comparer.
- [ ] **Step 3: Test 3c au même passage** : les 16 punch-ins tenus un à un pendant la lecture, puis enregistrés en overdub et relus (spec 3c, section 6) ; LOST BLOCKS reste 0.
- [ ] **Step 4: Écrire** dans `docs/cpu.md` une ligne « Step 5a: … planet on ENGINE » (cœurs, FPS, blocs perdus), et le pire cas 3c ; si des blocs sont perdus ou si le FPS est sous 15, s'arrêter et en parler avec l'utilisateur avant d'optimiser.
- [ ] **Step 5: Commit** `CPU: step 5a and 3c worst cases measured on the device`
