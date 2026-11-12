# QUARTZ étape 5d2 (chorale, funambule, dancefloor) — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** un firmware QUARTZ où STRING MACH, CHORDS, STRING, SWARM, NOISE et PARTICLE montrent leur scène sur ENGINE et SOUND — chaque modèle a alors la sienne — mesuré sans bloc perdu sur l'appareil.

**Architecture :** trois fichiers de scène sur la caméra de 5d1 (`p3d_camera`, primitives du monde), inclus par `app_ui.c` ; fonds projetés dans `*_frame` et dessinés dans la bande (tampons dans `sb`) ; `scene.c` aiguille par modèle.

**Tech Stack :** C (gnu11, sans libc), `app/p3d.c`, `fastmath.h` ; `tests/run_tests.sh` ; appareil par `tools/device/`.

**Spec :** `docs/superpowers/specs/2026-11-12-quartz-step5d2-scenes-design.md`. Maquettes : `docs/screens/chords.html` (`choir`), `string.html` (`walker`, `vib`), `noise.html` (`dance`, `grains`) : leurs constantes font foi pour ce que la spec ne fixe pas.

## Global Constraints

- Branche `step5d2-scenes` partie de `step5d1-scenes` ; commits par l'utilisateur (hook) ; pas de `Co-Authored-By`.
- Code et commentaires en anglais ; pas de libc (pas de `NULL` : `0`) ; pas de libm ; pas de cast vers un pointeur `volatile` dans le C de l'app.
- ≤ 300 primitives par image, traits compris ; faces fermées par `p3d_box3`/`p3d_beam3`, faces ouvertes offertes dans les deux sens (comme `gd_face`).
- Nouveaux fichiers inclus par `app/app_ui.c`, `tests/unit/scene_test.c` et `tests/unit/plateau_test.c` ; après chaque tâche, `app_render`, `fm1_timer5_irq`, `pq_init`, `seq_block`, `sysex_byte` aux mêmes adresses (`nm -n`) ; le build avec miroir (`FM1B_MIRROR=1 FM1B_POOL_SPARE=0 ./build.sh`) doit tenir en RAM.
- Notes : `sc.hit_ms[0]`, `sc.hits[0]`, `sc.fresh` (5d1).

## Review Focus

1. Encodeurs croisés aux extrêmes pour chaque moteur (pas seulement k, k, k) : budget et pas de NaN — la leçon du VCF de 5d1 (tests `*_cross_knobs`).
2. Une note pendant la réaction de la précédente (baguette du chef, saut de l'acrobate, éclair des dalles) : pas de saut de position (tests `*_burst`).
3. CHORDS à OCT renversé 3 fois (deux notes à 24) : deux gradins à la même hauteur, rien hors de l'écran (test `choir_oct_inv3`).
4. SWARM au rayon maximal : des grains hors du plancher (|p| > 1) ne retombent pas sur une autre dalle (test `dance_swarm_off_floor`).
5. Passage d'une scène 5d2 à une scène 5d1 ou 5c qui partage `sb` : chacune revient identique (test `scenes_share_room` étendu).

---

### Task 1: la chorale d'androïdes

**Files:** Create `app/scene_choir.c` ; Modify `app/scene.c`, `scene.h`, `app/app_ui.c`, `tests/unit/scene_test.c`, `tests/unit/plateau_test.c`

**Produces :** `void choir_frame(const q_sview *v, uint32_t ms); void choir_band(int32_t oy);` ; `scene_has` vrai pour 6, 14 ; membre `sb.choir` (silhouettes des tuyaux, lignes du fond).

Valeurs : spec section 2 et `choir()` de `chords.html` (accords, renversement trié, gradin `h = 0,06 + p/24`, `x = −1,1 + 2,2 i/(N−1)`, formes `i % 3`, couleurs `K[i % 4]`, chef en `z = −0,55`, `beat`).

- [ ] **Step 1: Tests** : `choir_budget` et `choir_cross_knobs` (les 2 moteurs, HARM/TIMB/MORPH en 0, 64, 127 croisés, une note : ≤ 300, pas de NaN) ; `choir_voices` (HARM 127 → 4 voix, HARM 0 → 2 voix : nombre de têtes blanches, ou un compteur exposé) ; `choir_note_reacts` (image à 50 ms d'une note ≠ même instant sans la note ; à 2 s, égales) ; `choir_oct_inv3` (HARM 0, TIMB 127 : les deux gradins à la même hauteur) ; `choir_burst` (deux notes à 60 ms : la main du chef ne remonte pas d'un coup).
- [ ] **Step 2: Lancer** : échecs.
- [ ] **Step 3: Implémenter** ; mettre à jour `scene_has_six_more` (6 et 14 ont une scène).
- [ ] **Step 4: Lancer** `tests/run_tests.sh` : `all tests ok` ; vérifications de Global Constraints.
- [ ] **Step 5: Commit** `Scene: CHORDS and STRING MACH as an android choir on the chord's steps`

### Task 2: le funambule

**Files:** Create `app/scene_rope.c` ; Modify les mêmes

**Produces :** `rope_frame`, `rope_band` ; `scene_has` vrai pour 19.

Valeurs : spec section 3 et `walker()` / `vib()` de `string.html` ; la corde garde le point de pincement et l'instant de la dernière note.

- [ ] **Step 1: Tests** : `rope_budget` / `rope_cross_knobs` ; `rope_note_reacts` ; `rope_settles` (MORPH 127 : 2 s après une note, la corde a la même image qu'au repos) ; `rope_burst` (une note pendant le saut : la hauteur affichée de l'acrobate ne saute pas de plus de 0,05).
- [ ] **Step 2–4:** échecs, implémenter, `all tests ok`.
- [ ] **Step 5: Commit** `Scene: STRING as an acrobat on a laser rope that rings on each note`

### Task 3: le dancefloor

**Files:** Create `app/scene_dance.c` ; Modify les mêmes

**Produces :** `dance_frame`, `dance_band` ; `scene_has` vrai pour 16, 17, 18 (tous les modèles ont alors une scène).

Valeurs : spec section 4 et `dance()` / `grains()` de `noise.html` ; PARTICLE : décroissance `exp(−a (3 + 12 MORPH))` ; SWARM : traîne à `t − 0,05 − 0,3 MORPH` en ton de base ; un grain hors du plancher (|x| ou |z| > 1) n'allume rien ; dalles allumées : le dessus seul.

- [ ] **Step 1: Tests** : `dance_budget` / `dance_cross_knobs` (3 moteurs) ; `dance_note_flash` (rose à 40 ms, plus de rose à 200 ms) ; `dance_morph_visible` (PARTICLE et SWARM : MORPH 0 et 127 donnent des images différentes) ; `dance_swarm_off_floor` (HARM 127 : aucune dalle allumée par un grain hors du plancher) ; `scenes_share_room` étendu (chorale, dancefloor, crêtes, MODAL).
- [ ] **Step 2–4:** échecs, implémenter, `all tests ok`.
- [ ] **Step 5: Commit** `Scene: the noise engines light a neon dancefloor`

### Task 4: simulateur, scripts, appareil

**Files:** Create `app/tests/scenes5d2.txt` ; Modify `app/tests/scene.txt`, `app/tests/plateau.txt` (si besoin), `docs/cpu.md`

- [ ] **Step 1:** `scene.txt` : la vérification « pas de scène » passe par une page sans scène (ENV, `tap ENV`) au lieu d'un modèle ; `scenes5d2.txt` : les six modèles par `turn PRESET`, `expect scene3d 1`, une note, `shot` ; SOUND. Lancer : `all tests ok` ; regarder les captures.
- [ ] **Step 2: Appareil** : build avec miroir, installation, `tools/device/worst_case.py`, piste 1 sur CHORDS, STRING, NOISE puis SWARM, 45 s chacune (`tools/device/measure.sh`) : zéro bloc perdu, SCENE FPS ≥ 15 ; sinon s'arrêter et alléger.
- [ ] **Step 3:** `docs/cpu.md` ; réinstaller le build normal.
- [ ] **Step 4: Commit** `CPU: step 5d2 scenes measured on the device`
