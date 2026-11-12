# QUARTZ étape 5c (scènes KIT, MODAL, SPEECH) — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** un firmware QUARTZ où les pistes KIT (page PAD), BASS DRUM, SNARE, HI-HAT, MODAL et SPEECH (pages ENGINE et SOUND) montrent leur scène animée à la place des barres.

**Architecture :** `scene.c` reçoit une vue (`q_sview` : modèle, encodeurs, compteurs de frappes) au lieu de ses trois arguments, et choisit la scène selon le modèle. `p3d.c` gagne des tons par face et un contour des faces porté par le triangle lui-même (pas de primitive de plus). `audio.c` compte les frappes d'un KIT par moteur de pad.

**Tech Stack :** C (gnu11, sans libc dans l'app), canvas `base/device/gfx.c`, `app/p3d.c`, `fastmath.h` ; tests `tests/run_tests.sh`.

**Spec :** `docs/superpowers/specs/2026-11-12-quartz-step5c-scenes-design.md` (et `2026-11-11-quartz-step5a-gfx-design.md`, spec globale section 9). Maquettes : `docs/screens/scenes.html` (« Kit », « Modal »), `docs/screens/speech.html` (« En direct ») ; reprendre leurs constantes (positions, tons, durées) quand la spec ne les fixe pas.

## Global Constraints

- Identité git `f0nski <13540044+F0nski@users.noreply.github.com>`, nouvelle branche `step5c-scenes` partie de `step5b-seq` ; pas de ligne `Co-Authored-By`. Les commits sont faits par l'utilisateur si un hook les bloque : laisser alors la commande.
- Code, commentaires, commits en anglais ; pas de libc ; aucun appel libm (`fastmath.h`).
- Zone 240×166 en deux bandes de 83 lignes (`oy = 0`, `oy = -83`) ; coordonnées de zone (maquette − 30 en y).
- Au plus 300 primitives par image (`P3D_MAX` reste 320).
- Tons : bleu `#8fb4ff #3a6dff #1c3590`, vert `#8ff5c8 #22d58e #0f6d48`, orange `#ffc196 #ff7a2e #a3410c`, rose `#ffb8dc #ff2f8e #8a1050`, violet `#e4ddff #9a7cff #4a36b0`, fond `#0c0820`, gris texte `#8b84a8`, cyan `#5fe0ff`.
- Modèles : SPEECH 15, MODAL 20, BASS DRUM 21, SNARE 22, HI-HAT 23, KIT 24 (`M_KIT`) ; moteurs de pad `PAD_BD`, `PAD_SN`, `PAD_HH`.
- Animations au temps (`plat_ms`), redessin au plus toutes les 30 ms (règle de 5a) ; maillages fixes construits une fois.

## Review Focus

1. Pad édité changé (pad tenu, autre pad joué) ou moteur du pad changé sur la page KIT puis retour sur PAD : pas de flash des frappes d'avant, le trait cyan sous le bon polyèdre (test `kit_no_flash_on_arrival`, tâche 4).
2. Rafale de frappes HH (ratchets, 1/32) : le tétraèdre bascule d'un quart de tour par frappe, sans dériver ni sauter en arrière (test `hh_full_turn_after_four`, tâche 4).
3. HARM qui change de figure MODAL pendant un frémissement : la plaque se recalcule sans NaN et reste sous 300 primitives (test `modal_budget_and_harm`, tâche 5, sous ASan comme les autres).
4. SPEECH tournée au plus de profil (±0,6 rad) : pas de pupille dessinée hors de la tête (test `pupils_only_from_front`, tâche 6).
5. Piste passée de MODAL à KIT sur SOUND : l'écran passe aux pages PAD/KIT sans reste de scène, et la page KIT n'a pas de scène (script `scenes5c.txt`, tâche 7).

---

## Structure des fichiers

```
app/p3d.c, p3d.h             tons par face, contour, P3D_MAXV 160, diagonales des quads de p3d_sphere marquées
app/audio.c, audio.h         audio_kit_hits
app/scene.c, scene.h         q_sview ; scènes KIT (et tambours seuls), MODAL, SPEECH
app/ui.c                     pages des scènes, vue q_sview, sonde kit_hits_N
tests/unit/p3d_test.c, audio_test.c, scene_test.c
app/tests/scenes5c.txt       (nouveau)
docs/cpu.md
```

---

### Task 1: p3d — tons par face et contour

**Files:**
- Modify: `app/p3d.h`, `app/p3d.c`
- Test: `tests/unit/p3d_test.c`

**Interfaces:**
- Produces :
  - `p3_face` devient `{ uint8_t a, b, c, hide; }` : bit 0 cache l'arête a→b, bit 1 b→c, bit 2 c→a (0 : toutes tracées). Les initialisations existantes `{a, b, c}` restent valides.
  - `void p3d_mesh_parts(const p3_mesh *m, const p3_pose *p, const uint16_t (*tones)[3], const uint8_t *part, const uint16_t *edge);` : face `i` prend `tones[part[i]]` (`part` NULL : `tones[0]`) ; `edge` non NULL : chaque triangle visible trace ses arêtes non cachées en 1 px de cette couleur, juste après son remplissage, dans l'ordre du tri. `p3d_mesh(m, p, tones)` appelle `p3d_mesh_parts(m, p, &tones, NULL, NULL)` (même rendu qu'avant).
  - `p3d_sphere` marque la diagonale de chaque quad coupé en deux (`hide` sur l'arête a→c des deux triangles).
  - `P3D_MAXV` passe à 160 (tête 11 × 14 = 154 sommets, plaque 12 × 12 = 144).

- [ ] **Step 1: Tests**
  - `parts_take_their_tones` : un quad de 2 faces face au spectateur, `part = {0, 1}`, tons `{RED×3}` et `{BLUE×3}` : un pixel au centre de chaque triangle est rouge, l'autre bleu.
  - `edge_skips_diagonal` : un carré (0,0)-(40,40) en deux triangles `{0,1,2,hide=4}`, `{0,2,3,hide=1}`, remplissage BLUE, contour RED : les 4 côtés ont des pixels RED, le pixel (20, 20) sur la diagonale est BLUE.
  - `edge_drawn_in_depth_order` : un triangle à contour derrière un triangle plein qui le couvre en partie : aucun pixel du contour dans la partie couverte.
  - `sphere_marks_diagonals` : `p3d_sphere(5, 10, …)` : chaque face d'une rangée médiane a exactement un bit de `hide`, les faces des pôles aucun.
- [ ] **Step 2: Lancer** `tests/run_tests.sh` : échec de compilation (`p3d_mesh_parts` non défini).
- [ ] **Step 3: Implémenter.** `p3_prim` garde `c` pour le remplissage et gagne `uint16_t edge; uint8_t emask;` (arêtes à tracer, 0 aucune) ; `p3d_draw` trace `cv_line` sur chaque arête de `emask` après `p3d_fill`, avec le même décalage `oy` et le même saut hors bande que les traits.
- [ ] **Step 4: Lancer** : `all tests ok` (la planète et MARBLES inchangées : leurs tests passent).
- [ ] **Step 5: Commit** `P3D: tones per face part, a face outline without the quads' diagonals`

### Task 2: audio — frappes d'un KIT par moteur

**Files:**
- Modify: `app/audio.c` (note-on, à côté de `hits[track]++`), `app/audio.h`
- Test: `tests/unit/audio_test.c`

**Interfaces:**
- Produces : `uint32_t audio_kit_hits(int track, int engine);` — note-ons d'un KIT dont le pad a le moteur `engine` (`PAD_BD`, `PAD_SN`, `PAD_HH`) au moment du note-on ; compter par différence comme `audio_hits`.

- [ ] **Step 1: Test** `kit_hits_per_engine` : piste 1 en `M_KIT`, `P.kit[1][2].engine = PAD_SN`, `P.kit[1][0].engine = PAD_BD` ; `audio_live_note(1, KIT_NOTE + 2, 1)`, `block(256)` : SN +1, BD et HH +0, piste 0 inchangée sur les trois moteurs ; puis `KIT_NOTE + 0` : BD +1. Une note hors pads (`KIT_NOTE + 16`) ne compte rien.
- [ ] **Step 2: Lancer** : échec (`audio_kit_hits` non défini).
- [ ] **Step 3: Implémenter** : `static volatile uint32_t kit_hits[Q_TRACKS][3];` incrémenté au note-on quand `kit`, avec `prj->kit[track][vpad[v]].engine`.
- [ ] **Step 4: Lancer** : `all tests ok`.
- [ ] **Step 5: Commit** `Audio: a KIT's note-ons counted per pad engine`

### Task 3: scene — une vue par image, `scene_has` étendu

**Files:**
- Modify: `app/scene.h`, `app/scene.c`, `app/ui.c:1588-1592`
- Test: `tests/unit/scene_test.c`

**Interfaces:**
- Produces :
  - `typedef struct { uint8_t model; int8_t pad_eng; int16_t k[4]; uint32_t hits[3]; } q_sview;` — `hits[0]` : `audio_hits` de la piste ; pour un KIT, `hits[e] = audio_kit_hits(track, e)` et `pad_eng` le moteur du pad édité (sinon −1).
  - `void scene_frame(const q_sview *v, uint32_t ms);` remplace l'ancienne signature ; `scene_band(oy)` dessine la scène du dernier `scene_frame`.
  - `int scene_has(int model);` vrai pour 2, 3, 4, 10, 15, 20, 21, 22, 23, 24.
  - `scene_enter()` : la prochaine image ne flashe rien, pour toutes les scènes (et pour chaque compteur de `hits`).
- [ ] **Step 1: Tests** : les tests de la planète passent par un `q_sview` (`model = 2`) ; `scene_has_six_more` (vrai pour 15, 20 à 24, faux pour 0, 5, 19).
- [ ] **Step 2: Lancer** : échec de compilation.
- [ ] **Step 3: Implémenter** : `scene_frame` aiguille selon `v->model` (planète pour les modèles FM ; les autres scènes arrivent aux tâches 4 à 6 : en attendant, rien) ; `ui.c` remplit un `q_sview` (`model` = modèle de la piste, `k` = `v.scene.v`, `hits[0] = audio_hits(ui.track)`).
- [ ] **Step 4: Lancer** : `all tests ok` (le script `scene.txt` inchangé passe).
- [ ] **Step 5: Commit** `Scene: one view per frame, the scenes chosen by model`

### Task 4: la scène KIT et les tambours seuls

**Files:**
- Modify: `app/scene.c`
- Test: `tests/unit/scene_test.c`

**Interfaces:**
- Consumes : `q_sview`, `p3d_mesh`.
- Produces : `scene_frame` dessine le tunnel et les polyèdres pour les modèles 21 à 24.

Valeurs de la spec (section 3) et de la maquette « Kit » : tunnel de 7 rectangles centrés en (120, 83), `#22184a`/`#3a2470`, lignes de fuite aux coins ; octaèdre bleu x = 60, cube vert x = 128, tétraèdre orange x = 194, à y ≈ 82 ; noms à y = 148 ; flash rose sur la durée de DECAY (`0,06 + 0,3·DECAY/127` s) ; HH : un quart de tour par frappe, tourné en 120 ms ; SN : 6 faces écartées le long de leur normale puis recollées ; modèles 21 à 23 : leur seul polyèdre en x = 120, 1,6 fois plus grand. Encodeurs : KIT → `k` = TUNE, HARM, TIMB, DECAY du pad édité, appliqués au polyèdre de `pad_eng` (les autres à 64 ; TUNE −24..24 joue sur la taille ±25 %, grave gros) ; tambour seul → HARM ampleur, TIMB rotation, MORPH durée de la réaction, DECAY flash.

- [ ] **Step 1: Tests**
  - `kit_engine_flashes_alone` : KIT, `hits` SN +1 : des pixels roses dans la colonne du cube (x 108..148), aucun dans celles de l'octaèdre et du tétraèdre ; 2 s plus tard, plus de rose.
  - `hh_full_turn_after_four` : modèle 23, 4 frappes espacées de 200 ms puis 1 s de repos : l'image est identique à celle sans frappe au même instant (4 quarts de tour = un tour), et après 1 frappe elle diffère.
  - `pad_tune_sizes` : `pad_eng = PAD_BD`, TUNE −24 puis +24 : la largeur rose/bleue de l'octaèdre sur la ligne y = 82 est plus grande à −24.
  - `kit_no_flash_on_arrival` : frappes avant `scene_enter`, puis une image : aucun rose.
  - `cyan_under_edited_pad` : `pad_eng = PAD_HH` : pixels cyan sous x = 194, aucun sous x = 60.
  - `single_drum_centred` : modèle 22 : polyèdre vert autour de x = 120, rien en x = 60 et 194.
- [ ] **Step 2: Lancer** : échecs.
- [ ] **Step 3: Implémenter.**
- [ ] **Step 4: Lancer** : `all tests ok`.
- [ ] **Step 5: Commit** `Scene: KIT polyhedra in a wire tunnel, single drums centred`

### Task 5: la scène MODAL

**Files:**
- Modify: `app/scene.c`
- Test: `tests/unit/scene_test.c`

**Interfaces:**
- Produces : `scene_frame` dessine la plaque pour le modèle 20.

Valeurs (spec section 4, maquette « Modal ») : plaque 11 × 11 quads, sommets 12 × 12 en (u − 0,5)·2, hauteur `−zf·amp`, `zf = cos(nπx)cos(mπy) − cos(mπx)cos(nπy)`, `amp = 0,16` plus le frémissement `0,05·e^(−t/τ)·(0,4 + 0,6·TIMB/127)·sin(16t)` avec `τ = 0,15 + 0,6·MORPH/127` s ; pose : échelle 62, centre (120, 82), `ax = 0,95`, `ay = 0,6 + 0,25·sin(0,2t)` ; `m = 1 + round(2·HARM/127)`, `n = m + 1 + round(2·HARM/127)` ; parties : |zf au centre| < 0,12 → lignes nodales (`#8ff5c8 #8ff5c8 #22d58e`, rose pendant DECAY : `0,15 + 0,6·DECAY/127` s), > 0 → bleu, sinon `#22d58e #0f6d48 #0a3f2a` ; contour `#0a0f2e` ; ellipses du sol `#1a1438` centrées (120, 94), rayons r·24 × r·9, r = 1..5, dessinées par la bande ; percuteur en losange orange au-dessus, x = 120, taille `6 + 4·TIMB/127`, descend de 14 px pendant 120 ms à chaque note. Plaque recalculée seulement si (m, n) ou le frémissement changent.

- [ ] **Step 1: Tests**
  - `modal_budget_and_harm` : HARM 0 puis 127, une note : `p3d_count() <= 300` à chaque image sur 2 s, et les deux images au repos diffèrent.
  - `nodal_lines_pink_on_note` : une note : des pixels roses dans la plaque ; 2 s après, aucun.
  - `still_at_rest` : sans note, deux images à 10 ms d'écart ne diffèrent que peu (la rotation lente) : moins de 2 % des pixels.
- [ ] **Step 2: Lancer** : échecs.
- [ ] **Step 3: Implémenter.**
- [ ] **Step 4: Lancer** : `all tests ok`.
- [ ] **Step 5: Commit** `Scene: MODAL as a Chladni relief that shivers on a strike`

### Task 6: la scène SPEECH

**Files:**
- Modify: `app/scene.c`
- Test: `tests/unit/scene_test.c`

**Interfaces:**
- Produces : `scene_frame` dessine la tête pour le modèle 15.

Valeurs (spec section 5 ; `speech.html`, `HEAD_BASE`, `headMesh`, `HEAD_PART`, `speech()`) : sphère 10 × 14 déformée comme `HEAD_BASE`, parties `eye`/`mouth`/`skin` calculées une fois au centre des faces ; y modèle vers le haut, écran vers le bas (inverser y et l'ordre des faces) ; mâchoire (`y < −0,5`, `z > −0,3`) descendue de `open·0,3` ; `open = |sin(2π·s/dur)|·(0,4 + 0,6·MORPH/127)` pendant `dur = 0,15 + 0,35·DECAY/127` s après une note ; taille `0,85 + 0,3·TIMB/127`, échelle 48, centre (120, 74), `ay = 0,6·sin(0,5t)`, `ax = −0,08` ; teinte `HARM·3/128` → vert, bleu, violet ; contour : teinte de base éclaircie de 15 % vers le blanc, rose si `open > 0,3` ; yeux `#0b2a1f #06201a #03140f` ; bouche idem, rose `#ff2f8e #8a1050 #4a0828` si `open > 0,15` ; pupilles : losanges 6 × 4 en `(±0,24·size, −0,1·size, 0,66·size)` tournés avec la tête, `#8ff5ff` (rose clair si `open > 0,4`), seulement si leur z tourné > `0,25·size` ; grille `#1e1640` pas 12, ligne de scan `#0f6d48` à `(60t) mod 166` ; 9 barres de phonèmes x = 30 + 20i, larges de 14, du bas (y = 158) sur `|sin(1,7i + 2,3·⌊4t⌋)|·open·24` px.

- [ ] **Step 1: Tests**
  - `mouth_opens_on_note` : une note à 0 ms, image à 60 ms : des pixels roses dans la région de la bouche (x 105..135, y 90..110) ; à 2 s, aucun.
  - `harm_tints` : HARM 0, 64, 127 au repos : la couleur dominante de la tête passe par vert, bleu, violet.
  - `pupils_only_from_front` : sur 20 s au repos, toute image avec des pixels `#8ff5ff` les a dans la boîte de la tête (x 70..170).
  - `head_budget` : `p3d_count() <= 300` sur 2 s avec notes.
- [ ] **Step 2: Lancer** : échecs.
- [ ] **Step 3: Implémenter.**
- [ ] **Step 4: Lancer** : `all tests ok`.
- [ ] **Step 5: Commit** `Scene: SPEECH as the floating low poly head that speaks`

### Task 7: ui — les pages des scènes

**Files:**
- Modify: `app/ui.c` (bloc `pid == P_ENGINE || … || pid == P_PAD`, vers la ligne 1178 ; appel de `scene_frame` ; sonde, vers la ligne 1779)
- Create: `app/tests/scenes5c.txt`

**Interfaces:**
- Consumes : `q_sview`, `scene_has`, `audio_kit_hits`.
- Produces : sonde `kit_hits_N` (N = 1 BD, 2 SN, 3 HH) : `audio_kit_hits(ui.track, N − 1)`.

Règles : `anim = 1` sur ENGINE et SOUND quand `scene_has(model)` et le modèle n'est pas `M_KIT` ; sur PAD quand la piste est un KIT (`k` = les 4 valeurs de PAD du pad édité, `pad_eng` = son moteur, `hits[e] = audio_kit_hits`). La page KIT n'a pas de scène. Le pad édité ou son moteur qui change compte comme une nouvelle scène (`scene_enter`) : les ajouter à la clé `scene_key`.

- [ ] **Step 1: Script** `scenes5c.txt` : piste 1 en SPEECH (`turn PRESET` jusqu'à `expect model 15`) → `expect scene3d 1`, note, `shot speech_hit.png` ; MODAL (`expect model 20`) → `scene3d 1`, `shot modal.png` ; SNARE (22) → `scene3d 1` ; KIT (24) → page PAD `scene3d 1`, pad joué → `expect kit_hits_1 1` ou selon son moteur, `shot kit_pad.png` ; `turn SELECT 1` → page KIT `expect scene3d 0` ; retour PAD `scene3d 1` ; modèle 19 (STRING) sur SOUND → `scene3d 0`.
- [ ] **Step 2: Lancer** `tests/run_tests.sh` : échec du script.
- [ ] **Step 3: Implémenter.**
- [ ] **Step 4: Lancer** : `all tests ok` ; regarder les captures dans `app/build/host` et les comparer aux maquettes.
- [ ] **Step 5: Commit** `UI: the KIT, drum, MODAL and SPEECH scenes on their pages`

### Task 8: mesure sur l'appareil

**Files:**
- Modify: `docs/cpu.md`

- [ ] **Step 1:** Construire le firmware (`./build.sh`) et demander à l'utilisateur de l'installer.
- [ ] **Step 2:** Avec l'utilisateur, pire cas de la spec section 8 : MODAL affichée sur ENGINE en jeu, puis SPEECH ; relever charge des deux cœurs, LOST BLOCKS, SCENE FPS. Critère : zéro bloc perdu.
- [ ] **Step 3:** Ajouter les lignes à `docs/cpu.md` (format des lignes 5a).
- [ ] **Step 4: Commit** `CPU: step 5c scenes measured on the device`
