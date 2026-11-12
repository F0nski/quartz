# QUARTZ étape 5d1 (crêtes, bowl, jardin, plateformes) — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** un firmware QUARTZ où VA VCF, PHASE DIST, VA, WAVESHAPE, WAVE TERRAIN, WAVETABLE, GRAIN FORM, ADDITIVE et CHIPTUNE montrent leur scène sur ENGINE et SOUND, mesuré sans bloc perdu sur l'appareil.

**Architecture :** `p3d` gagne une caméra en perspective (primitives en coordonnées du monde, lumière dans le repère de la caméra, brouillard en 8 paliers, 32 couleurs de fil). Chaque scène vit dans son fichier, inclus par `app_ui.c` (code de l'écran, en fin de flash) ; `scene.c` aiguille par modèle. Les outils de mesure sur l'appareil entrent dans le dépôt.

**Tech Stack :** C (gnu11, sans libc dans l'app), `app/p3d.c`, `fastmath.h` ; tests `tests/run_tests.sh` ; appareil par USB-MIDI (`base/tools/fm1_install.py`, `fm1_remote.py`, `fm1_mirror.py`).

**Spec :** `docs/superpowers/specs/2026-11-12-quartz-step5d1-scenes-design.md`. Maquettes : `docs/screens/analog.html` (`crests`), `wavetable.html` (`bowl`), `spectral.html` (`garden`), `chiptune.html` (`platforms`) : leurs constantes font foi pour ce que la spec ne fixe pas (y de la maquette − 30 = zone de scène).

## Global Constraints

- Branche `step5d1-scenes` partie de `step5c-scenes` ; les commits sont faits par l'utilisateur (un hook les bloque) : laisser la commande ; pas de ligne `Co-Authored-By`.
- Code, commentaires, commits en anglais ; pas de libc ; aucun appel libm (`fastmath.h`) ; pas de cast vers un pointeur `volatile` dans le code C de l'app (vérification du build).
- Au plus 300 primitives par image (`P3D_MAX`), traits compris ; zone 240×166 en deux bandes de 83 lignes.
- Les nouveaux fichiers de scène sont inclus par `app/app_ui.c` (et par `tests/unit/scene_test.c`), jamais par `app.c` : le code de l'écran reste en fin de flash (`docs/cpu.md`). Après tout changement, vérifier que `fm1_timer5_irq` et `app_render` n'ont pas bougé : `nm -n app/build/quartz.elf`.
- Les maillages variables vont dans le tampon partagé `sb` de `scene.c` (un membre par scène, `sb_owner`), reconstruits quand leur scène revient.
- Notes : `v->hits[0]` et `sc.hit_ms[0]` (5c) ; un compteur de notes vu depuis `scene_enter` pour les sauts, l'arpège, le kickflip et le score.

## Review Focus

1. Un sommet derrière la caméra (Z < 0,3) ou une face vue par la tranche : pas de NaN, pas de triangle géant (test `world_clip_behind`, tâche 1).
2. Rafale de notes plus rapide que le saut du robot (0,22 s) ou le kickflip (0,2 s) : le robot repart de sa position courante, sans téléportation (test `chip_burst_no_jump_back`, tâche 6).
3. Encodeurs aux extrêmes (0 et 127) pour chaque moteur : budget tenu, formes d'onde sans NaN (VA VCF à résonance maximale, PHASE DIST à HARM 127) (tests `*_budget`, tâches 3 à 6).
4. Changement de modèle entre deux scènes 5d1 qui partagent le tampon (crêtes ↔ bowl ↔ MARBLES) : chaque scène revient identique (test `scenes_share_meshes` étendu, tâche 4).
5. Passage d'un moteur à l'autre dans la même scène (VA ↔ WAVESHAPE ; WAVETABLE ↔ WAVE TERRAIN) : la forme change sans flash de note (test `crests_engine_switch`, tâche 3).

---

## Structure des fichiers

```
app/p3d.c, p3d.h               caméra, primitives du monde, brouillard, 32 fils
app/scene.c, scene.h           scene_has, aiguillage, prototypes des scènes 5d1, membres du tampon sb
app/scene_va.c                 (nouveau) survol de crêtes
app/scene_wave.c               (nouveau) bowl en hoverboard
app/scene_garden.c             (nouveau) jardin de cristal
app/scene_chip.c               (nouveau) plateformes
app/app_ui.c                   #include des quatre, après scene.c
tests/unit/p3d_test.c, scene_test.c
app/tests/scenes5d1.txt        (nouveau)
tools/device/                  (nouveau) snap.py, measure.sh, worst_case.py, README court en tête de chaque fichier
docs/cpu.md
```

---

### Task 1: p3d — caméra en perspective

**Files:** Modify `app/p3d.h`, `app/p3d.c` ; Test `tests/unit/p3d_test.c`

**Interfaces — Produces :**
- `typedef struct { float yaw, pitch, dist, h, f, cy; } p3_cam;`
- `void p3d_camera(const p3_cam *c, p3_vec light, uint16_t fog, float far);` (la lumière est normalisée ici ; vaut jusqu'au prochain appel)
- `int p3d_project(p3_vec w, float *sx, float *sy, float *z);` (0 si Z < 0,3)
- `void p3d_tri3(const p3_vec p[3], const uint16_t tones[3], const uint16_t *edge, float bias);`
- `void p3d_quad3(const p3_vec p[4], const uint16_t tones[3], const uint16_t *edge, float bias);` (diagonale p0–p2 cachée)
- `void p3d_box3(p3_vec c, p3_vec half, const uint16_t tones[3], const uint16_t *edge);`
- `void p3d_beam3(p3_vec a, p3_vec b, float w, const uint16_t tones[3], const uint16_t *edge);`
- `void p3d_line3(p3_vec a, p3_vec b, uint8_t width, uint16_t c, float bias);`
- `uint16_t p3d_mix(uint16_t a, uint16_t b, float k);` (RGB565 par canal)
- Profondeur de tri : `−(Z moyen − bias)` (plus grand = plus proche, comme aujourd'hui) ; faces arrière écartées ; ton par la normale dans le repère de la caméra (seuils 0,6 et 0,2) ; brouillard `clamp((z − 0,35 far)/(0,65 far), 0, 0,85)` arrondi au huitième sur la face, le fil et le trait. `emask` : bits 0–2 les arêtes, 3–7 l'indice dans `edges[32]`.

- [ ] **Step 1: Tests** :
  - `world_quad_projects` : caméra yaw 0, pitch 0, dist 4, h 0, f 100, cy 83 ; quad du monde (−1..1, −1..1, z 0) face à la caméra : pixels remplis autour de (120, 83), rien en (120 ± 40, 83) ;
  - `world_back_face_culled` : le même quad aux sommets dans l'ordre inverse : `p3d_count() == 0` ;
  - `world_fog_darkens` : deux quads identiques à z 0 et z 10 (far 12, brouillard noir) : le lointain est plus sombre ;
  - `world_clip_behind` : un quad dont un sommet a Z < 0,3 n'est pas ajouté ; aucun NaN (UBSan) ;
  - `edge_palette_32` : 20 couleurs de fil différentes dans une image : chaque triangle tracé avec la sienne.
- [ ] **Step 2: Lancer** `tests/run_tests.sh` : échec de compilation.
- [ ] **Step 3: Implémenter.**
- [ ] **Step 4: Lancer** : `all tests ok` (planète, MARBLES, 5c inchangés).
- [ ] **Step 5: Commit** `P3D: a perspective camera, world primitives, fog, 32 wire colours`

### Task 2: outils de mesure sur l'appareil dans le dépôt

**Files:** Create `tools/device/snap.py`, `tools/device/measure.sh`, `tools/device/worst_case.py`

**Interfaces — Produces :**
- `tools/device/snap.py OUT.png SECONDS` : garde le miroir allumé SECONDS (keep-alive chaque seconde, l'écran entier redemandé 1,5 s avant la fin) et écrit l'écran (repris de la session : `fm1_mirror.Screen`, `B.frame(MR_REQ, …)`).
- `tools/device/worst_case.py OUT.txt` : écrit le montage du pire cas (commandes `fm1_remote.py`, avec `expect` pour le simulateur) depuis un démarrage à neuf : pistes 1 MODAL et 2 6-OP A, POLY 3, pattern P8, LEN 16, accord de 3 notes sur les 16 pas, PHASER sur les deux, DELAY et REVERB au maximum, COMP au maximum.
- `tools/device/measure.sh LABEL SECS ACTIONS…` : les actions, l'attente, puis la page CPU de GLO (vérifiée par l'en-tête capturé, SELECT tourné jusqu'à elle) capturée dans `app/build/device/m_LABEL.png`.
- Chemins relatifs à la racine du dépôt ; Python : `${FM1_PY:-python3}` ; aucune donnée personnelle.

- [ ] **Step 1:** Le montage passe au simulateur : `python3 tools/device/worst_case.py /tmp/w.txt`, puis le jouer dans `app/build/host/quartz_host` avec `tap PLAY` et `expect playing 1` : aucun `FAIL`.
- [ ] **Step 2: Commit** `Tools: the worst case, the screen and the CPU page over USB, for the device measures`

### Task 3: le survol de crêtes

**Files:** Create `app/scene_va.c` ; Modify `app/scene.c`, `scene.h`, `app/app_ui.c`, `tests/unit/scene_test.c`

**Interfaces — Consumes :** tâche 1. **Produces :** `void va_frame(const q_sview *v, uint32_t ms); void va_band(int32_t oy);` ; `scene_has` vrai pour 0, 1, 8, 9.

Valeurs : spec section 3 et `crests()` / `backdropA()` / `wave()` de `analog.html` ; 16 cellules sur `|x| ≤ 5,4` (17 abscisses), 8 bandes ; le relief dans `sb` (17 × 9 hauteurs) ; table VA VCF de 64 dans `sb`, recalculée quand HARM, TIMB ou MORPH change.

- [ ] **Step 1: Tests** : `crests_budget` (les 4 moteurs, encodeurs 0 et 127, une note : ≤ 300 primitives, pas de NaN) ; `crests_note_pink` (une note : rose dans la zone ; 2 s après : aucun) ; `crests_harm_changes` (VA VCF : HARM 0 et 127 donnent deux images différentes) ; `crests_engine_switch` (VA puis WAVESHAPE sans `scene_enter` et sans note : images différentes, aucun rose).
- [ ] **Step 2: Lancer** : échecs.
- [ ] **Step 3: Implémenter.**
- [ ] **Step 4: Lancer** : `all tests ok` ; vérifier `nm -n` (Global Constraints).
- [ ] **Step 5: Commit** `Scene: the analog engines fly over crests shaped by their wave`

### Task 4: le bowl en hoverboard

**Files:** Create `app/scene_wave.c` ; Modify `scene.c`, `scene.h`, `app_ui.c`, `scene_test.c`

**Produces :** `wave_frame`, `wave_band` ; `scene_has` vrai pour 5, 13.

Valeurs : spec section 4 et `bowl()` de `wavetable.html` ; cuvette 8 × 8 (81 sommets dans `sb`) ; parois seulement là où elles ont une hauteur ; 3 profils de 8 segments (WAVETABLE) ou 10 tirets (WAVE TERRAIN) ; dalles et clôture dans la bande.

- [ ] **Step 1: Tests** : `bowl_budget` (2 moteurs, encodeurs 0 et 127, une note, un kickflip : ≤ 300) ; `bowl_trail_on_note` (rose après une note, plus rien 2 s après) ; `bowl_kickflip_every_other` (notes 1 et 2 : images du rider différentes à 100 ms de chaque note) ; `scenes_share_meshes` étendu (crêtes, bowl, MARBLES, MODAL dans un ordre puis un autre : chaque scène identique).
- [ ] **Step 2–4:** échecs, implémenter, `all tests ok`.
- [ ] **Step 5: Commit** `Scene: WAVETABLE and WAVE TERRAIN as a hoverboard in a neon bowl`

### Task 5: le jardin de cristal

**Files:** Create `app/scene_garden.c` ; Modify `scene.c`, `scene.h`, `app_ui.c`, `scene_test.c`

**Produces :** `garden_frame`, `garden_band` ; `scene_has` vrai pour 11, 12.

Valeurs : spec section 5 et `garden()` / `spec()` de `spectral.html` ; tige en trait 2 px, feuille et pétale en un triangle, cœur en un quad ; 11 éclats ; arches, mur, pont et grille dans la bande ; pollen et lucioles en 2D après `p3d_draw`.

- [ ] **Step 1: Tests** : `garden_budget` (2 moteurs, extrêmes, note : ≤ 300) ; `garden_hearts_pink` (une note : rose ; 2 s après : aucun) ; `garden_timb_moves_peak` (ADDITIVE : TIMB 0 puis 127, la fleur la plus haute passe de la gauche à la droite : comparer la colonne du pixel de tige le plus haut).
- [ ] **Step 2–4:** échecs, implémenter, `all tests ok`.
- [ ] **Step 5: Commit** `Scene: the spectral engines as a crystal garden, a flower per harmonic`

### Task 6: les plateformes

**Files:** Create `app/scene_chip.c` ; Modify `scene.c`, `scene.h`, `app_ui.c`, `scene_test.c`

**Produces :** `chip_frame`, `chip_band` ; `scene_has` vrai pour 7.

Valeurs : spec section 6 et `platforms()` / `arp()` / `hopAt()` de `chiptune.html` ; lune (21 cubes) et nuages projetés en carrés 2D dans la bande ; l'arpège avance d'un pas par note (`hits[0]` depuis `scene_enter`) ; le saut part de la position courante du robot.

- [ ] **Step 1: Tests** : `chip_budget` (HARM 0 et 127, une note : ≤ 300) ; `chip_hop_to_target` (une note : 0,3 s après, le robot est au-dessus du bloc visé : pixels cyan dans sa colonne) ; `chip_burst_no_jump_back` (3 notes à 50 ms d'écart : la position du robot entre deux images successives ne recule pas de plus que l'écart entre deux blocs) ; `chip_score` (5 notes : le texte `00050` est dessiné, comparer à `text_w` et à des pixels `#ffd25a`).
- [ ] **Step 2–4:** échecs, implémenter, `all tests ok`.
- [ ] **Step 5: Commit** `Scene: CHIPTUNE as a robot hopping on the arpeggio's blocks`

### Task 7: simulateur et appareil

**Files:** Create `app/tests/scenes5d1.txt` ; Modify `docs/cpu.md`

- [ ] **Step 1: Script** `scenes5d1.txt` : depuis VA VCF (modèle 0), chaque modèle 0, 1, 5, 7, 8, 9, 11, 12, 13 par `turn PRESET` : `expect scene3d 1`, une note, `shot` ; puis SOUND (`turn SELECT 1`) : `scene3d 1`. Lancer : `all tests ok` ; regarder les captures face aux maquettes.
- [ ] **Step 2: Appareil** (par USB, sans l'utilisateur) : build avec miroir (`FM1B_MIRROR=1 FM1B_POOL_SPARE=0 ./build.sh`), installation, montage `tools/device/worst_case.py`, puis piste 1 passée successivement sur VA VCF, WAVETABLE, ADDITIVE, CHIPTUNE, chacune affichée 45 s sur ENGINE : `tools/device/measure.sh`. Critère : zéro bloc perdu ; relever cœurs et SCENE FPS.
- [ ] **Step 3:** Lignes dans `docs/cpu.md` ; réinstaller le build normal.
- [ ] **Step 4: Commit** `CPU: step 5d1 scenes measured on the device`
