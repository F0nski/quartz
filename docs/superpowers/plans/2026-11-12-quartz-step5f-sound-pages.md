# QUARTZ étape 5f (pages de son) — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** les pages ADSR, FILTER, LFO, ARP, INSERT et MIX dessinées en rubans low poly obliques qui réagissent aux notes, mesurées sans bloc perdu.

**Architecture :** `scene_sound.c` construit chaque page en primitives 2D de `p3d` (`p3d_tri`, `p3d_line`, la profondeur pour l'ordre), nourri par `q_pview` que prépare `ui.c` ; `anim` 6. Deux accesseurs audio donnent la phase du LFO et l'état de l'arp.

**Tech Stack :** C (gnu11, sans libc), `p3d`, `fastmath.h` ; `tests/run_tests.sh` ; `tools/device/`.

**Spec :** `docs/superpowers/specs/2026-11-12-quartz-step5f-sound-pages-design.md` ; maquette `docs/screens/sound.html` (fonctions `ribbon`, `box`, `wbox`, `adsr`, `filter`, `resp`, `lfoPts`, `arp`, `insert`, `hexknob`, `mix` : les géométries et les formules de déformation s'y lisent telles quelles, en remplaçant `y` d'écran par `y − 30` de la zone de scène).

## Global Constraints

- Branche `step5f-sound` partie de l'étape 5e ; commits par l'utilisateur ; pas de `Co-Authored-By`.
- Pas de libc ni libm (`fastmath.h`) ; ≤ 300 primitives par page ; le nouveau fichier inclus par `app_ui.c`, `tests/unit/scene_test.c`, `tests/unit/plateau_test.c` après `scene_home.c` ; le code audio garde ses adresses (le layout de `patches/fm1-base-layout.patch` : le code de l'écran en dernier).
- Projection oblique `(DX, DY) = (12, −9)` ; zone de tracé x 16–216, y 32–142 de la zone de scène ; rose réservé aux notes.
- L'en-tête, le pied de page et le son ne changent pas.

## Review Focus

1. Arrivée sur une page pendant que la piste joue, ou changement de piste : aucune note d'avant ne fait réagir la page (test `sound_fresh`, Task 2).
2. Valeurs extrêmes des encodeurs (ENV AMT ±63 qui pousse la cible hors de 0..1, RES 127, DEPTH −64, GATE 5 %, OCT 4 en UP-DN) : rien hors de la zone, budget tenu (test `sound_budget_extremes`, Tasks 2 et 3).
3. Plus de 12 pas d'arpège, ou un arp à 8 notes tenues : la page de 12 qui contient le pas joué, sans débordement (test `sound_arp_long`, Task 3).
4. Aucune voix ne sonne : losange du LFO immobile au début, pas de plantage sur la phase −1 (test `sound_lfo_idle`, Task 2).
5. KIT sur ARP, moteur à enveloppe propre sur ADSR : texte attendu, pas de blocs (tests `sound_arp_kit`, `sound_adsr_own_env`, Tasks 2 et 3).

---

### Task 1: les accesseurs audio

**Files:** Modify `app/mod.h`, `app/mod.c`, `app/audio.h`, `app/audio.c` ; Test `tests/unit/mod_test.c`, `tests/unit/audio_test.c`

**Produces :**
- `float mod_lfo_phase(int v);` — la phase 0..1 du LFO de la voix `v` (`MV[v].ph`).
- `float audio_lfo_phase(int track);` — celle du premier slot de `pool` de la piste en `V_ON` ou `V_RELEASED`, −1 sans voix.
- `int audio_arp(int track, uint8_t held[ARP_HELD], int *sounding);` — copie `arp[track].h[i].note` (ordre d'arrivée), rend `nh` ; `*sounding` = `arp[track].sn` si `snd`, sinon −1. Lu sans verrou depuis la boucle principale, comme `audio_hits`.

- [ ] **Step 1: Tests** : `lfo_phase_advances` (mod_test : voix 0, LFO en TRIG, `mod_note_on` puis 10 blocs : `mod_lfo_phase(0)` > 0 et < 1, et revient à 0 au `mod_note_on` suivant) ; `audio_lfo_phase_none` (audio_test : aucune note : −1) ; `audio_lfo_phase_voice` (une note live sur la piste 0, un bloc : ≥ 0) ; `audio_arp_held` (ARP en UP sur la piste 0, notes live 64 puis 60 tenues, quelques blocs : rend 2, `held` = {64, 60}, `sounding` ∈ {60, 64}) ; `audio_arp_off` (ARP OFF, aucune note : rend 0, `sounding` −1).
- [ ] **Step 2: Lancer** `tests/run_tests.sh` : échecs (symboles absents).
- [ ] **Step 3: Implémenter** les trois fonctions.
- [ ] **Step 4: Lancer** `tests/run_tests.sh` : `all tests ok`.
- [ ] **Step 5: Commit** `Audio: the LFO's phase and the arp's notes for the screen`

### Task 2: la scène, ADSR, FILTER, LFO

**Files:** Create `app/scene_sound.c` ; Modify `app/scene.h`, `app/app_ui.c`, `tests/unit/scene_test.c`, `tests/unit/plateau_test.c`

**Produces :**
```c
enum { SP_ADSR, SP_FILTER, SP_LFO, SP_ARP, SP_INSERT, SP_MIX };
typedef struct {
    uint8_t page, model, itype, own_env, kit;   /* itype: ui.c's insert type (0 INSERT OFF); kit: the track is a KIT */
    int16_t k[4];                               /* the page's 4 knob values, in PAGE[]'s ranges */
    uint32_t hits;                              /* audio_hits(track) */
    float lfo_ph;                               /* audio_lfo_phase, -1 none */
    uint8_t held[8], chord[3]; int8_t nheld, sounding;   /* audio_arp; chord: the tonic's 1-3-5 when nothing is held */
} q_pview;
void sound_frame(const q_pview *v, uint32_t ms);
void sound_band(int32_t oy);
```
`sound_frame` remet ses compteurs quand `sc.fresh` (comme `home_frame`) : la note est « nouvelle » quand `hits` a bougé depuis l'image précédente, et l'instant de la dernière note sert aux animations roses (0,15 s, 0,4 s, 0,9 s, 1 s selon la page).

Helpers internes (fichier) : `sp_ribbon(const float *x, const float *y, int n, float base, float k, float d, tones par segment)`, `sp_box`, `sp_wbox`, `sp_floor` — ceux de la maquette ; les tons des encodeurs et le rose de la spec, section 2.

- [ ] **Step 1: Tests** (`sound_tests()` appelé de `main`) :
  - `sound_budget` : chaque page SP_ADSR, SP_FILTER, SP_LFO, chaque TYPE et SHAPE, une note toutes les 300 ms sur 1,5 s : `p3d_count() <= 300`.
  - `sound_budget_extremes` : FILTER avec ENV AMT −64 et +63, RES 127, CUTOFF 0 et 127 ; LFO DEPTH −64 et 63 : ≤ 300, et aucun pixel non noir hors de y 0–166 de la zone (dessin des deux bandes).
  - `sound_adsr_pink` : durées de la maquette (`0,05 + 0,55 × v` pour A et D, `0,05 + 0,8 × v` pour R, maintien 0,45 s, 2,3 s sur la largeur) ; ATTACK 127, DECAY, SUSTAIN, RELEASE 0 (attaque sur x 16–68), une note : à +0,3 s du rose seulement à x < 68 + 12 ; à +0,8 s (maintien, x 70–120) du rose à x ≥ 68 et aucun à x < 60.
  - `sound_adsr_own_env` : `own_env` 1 : le texte « FILTER ONLY » présent (pixels ambre `C_AMB` à y 26 ± 6).
  - `sound_filter_pink` : une note : rose pendant 0,4 s puis plus de rose à +1 s.
  - `sound_lfo_idle` : `lfo_ph` −1 : deux images à 0,5 s d'écart identiques hors du rose.
  - `sound_fresh` : `hits` à 5 à l'arrivée (`scene_enter`), sans changement ensuite : aucun pixel rose.
- [ ] **Step 2: Lancer** : échecs.
- [ ] **Step 3: Implémenter** le fichier pour ces trois pages selon la spec, section 3 ; le nom du modèle et la précision de droite tracés dans la bande (polices de `ui.c`, comme `scene_home.c` le fait).
- [ ] **Step 4: Lancer** `tests/run_tests.sh` : `all tests ok`.
- [ ] **Step 5: Commit** `Scene: the ENV and LFO pages as low-poly ribbons`

### Task 3: ARP, INSERT, MIX

**Files:** Modify `app/scene_sound.c`, `tests/unit/scene_test.c`

**Consumes :** `q_pview`, les helpers de la Task 2.

- ARP : la suite des notes selon MODE (`AMODE_NAME` : OFF, UP, DOWN, UP-DN, RAND, ORDER) comme la maquette, `held` triées pour UP/DOWN/UP-DN, dans l'ordre d'arrivée pour ORDER ; RAND : un ordre fixe tiré de `p3d_hash`, le bloc rose est celui dont la note vaut `sounding`.
- INSERT : 72 points, formules de la maquette par type ; types dans l'ordre de `INS_NAME`.
- MIX : `hexknob` de la maquette (hexagone en relief, 11 graduations, index) ; vumètre 14 pavés par côté.

- [ ] **Step 1: Tests** :
  - `sound_budget` étendu à SP_ARP (MODE × OCT, 8 notes tenues), SP_INSERT (les 8 types), SP_MIX.
  - `sound_arp_pink` : `held` {64, 60, 67}, UP, OCT 1, `sounding` 64 : un seul bloc rose, le 2e des 3 (rose dans le tiers central du tracé seulement).
  - `sound_arp_long` : OCT 4, UP-DN, 3 notes (22 pas), `sounding` sur la 2e octave descendante : ≤ 300, du rose présent.
  - `sound_arp_chord` : `nheld` 0 : aucun ton bleu plein (seulement le fil éteint `#2e2850`).
  - `sound_arp_kit` : `kit` 1 : aucun ton bleu, du texte gris.
  - `sound_insert_off` : `itype` 0 : aucun pixel bleu (les trois tons bleus).
  - `sound_mix_meter` : une note, PAN à fond à gauche (−64) : pavés roses dans la colonne L, aucun dans R.
- [ ] **Step 2: Lancer** : échecs.
- [ ] **Step 3: Implémenter** les trois pages selon la spec, section 3.
- [ ] **Step 4: Lancer** `tests/run_tests.sh` : `all tests ok`.
- [ ] **Step 5: Commit** `Scene: the ARP and FX pages`

### Task 4: ui.c, simulateur, appareil

**Files:** Modify `app/ui.c` ; Create `app/tests/sound.txt` ; Modify `docs/cpu.md`

**Consumes :** `sound_frame`, `sound_band`, `q_pview` (Task 2) ; `audio_lfo_phase`, `audio_arp` (Task 1).

- `view_now` : P_ADSR, P_FILTER, P_LFO, P_ARP, P_INSERT, P_MIX → `v->scene.anim = 6` (la clé de scène tient sur 3 bits) ; `ui_frame` remplit un `q_pview` (`page` = `pid − P_ADSR`, sauf P_INSERT → SP_INSERT, P_MIX → SP_MIX ; `own_env(model)`, `model == M_KIT`, le type d'insert de la piste) et appelle `sound_frame` puis `sound_band` par bande, comme `anim` 5.
- `chord` : la tonique `60 + p->tonic` et les 3e et 5e bits levés de `SCALE_MASK[p->mode]` comptés depuis 0.
- `draw_scene` : retirer ces six pages du bloc des barres (P_ENGINE, P_SOUND, P_PAD, P_GFX y restent).

- [ ] **Step 1: Script** `sound.txt` : `tap ENV`, `expect anim 6`, une note (`key 0 down`/`up`), `shot sound_adsr.png` ; `turn SELECT 1` (FILTER), `shot` ; `tap LFO`, `tap ARP`, `tap FX` (et `turn SELECT 1` vers MIX), `expect anim 6` et un `shot` chacun ; sur INSERT, `turn PRESET 1` (un autre type) puis `shot`. Lancer : échec (`anim` différent).
- [ ] **Step 2: Implémenter** ; lancer `tests/run_tests.sh` : `all tests ok` ; regarder les captures face à `docs/screens/sound.html`.
- [ ] **Step 3: Appareil** : build avec miroir, pire cas de `docs/cpu.md` (`tools/device/worst_case.py`), page FILTER 45 s : zéro bloc perdu, SCENE FPS relevé ; une ligne dans `docs/cpu.md` ; build normal réinstallé.
- [ ] **Step 4: Commit** `UI: the ENV, LFO, ARP and FX pages drawn`
