# QUARTZ étape 5e (HOME, page TRACKS) — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** la page TRACKS de HOME en scène animée d'emblèmes sur socles, mesurée sans bloc perdu.

**Architecture :** `scene_home.c` sur la caméra de 5d1, nourri par `q_hview` que prépare `ui.c` (données de `q_trow` et les 4 `audio_hits`) ; `anim` 5.

**Tech Stack :** C (gnu11, sans libc), `p3d`, `fastmath.h` ; `tests/run_tests.sh` ; `tools/device/`.

**Spec :** `docs/superpowers/specs/2026-11-12-quartz-step5e-home-design.md` ; maquette `docs/screens/home.html` (`emblems`, `emblem`, `pedestal`, `hexAt`).

## Global Constraints

- Branche `step5e-home` partie de `step5d2-scenes` ; commits par l'utilisateur ; pas de `Co-Authored-By`.
- Mêmes règles que 5d : pas de libc ni libm ; ≤ 300 primitives ; nouveau fichier inclus par `app_ui.c`, `scene_test.c`, `plateau_test.c` ; code audio aux mêmes adresses ; build avec miroir tenant en RAM.

## Review Focus

1. Notes sur plusieurs pistes dans la même image, et une piste qui joue pendant que l'on arrive sur la page : chaque piste réagit aux siennes, aucune aux notes d'avant (test `home_hits_per_track`).
2. Un pattern de 64 pas ou plus (`len` jusqu'à 64) : les segments de pas restent lisibles, la position modulo la longueur (test `home_long_pattern`).
3. Une piste sans position (arrêt : `pos` −1) : pas de pas rose, pas de plantage (test `home_stopped`).

---

### Task 1: la scène des emblèmes

**Files:** Create `app/scene_home.c` ; Modify `app/scene.h`, `app/scene.c` (membre `sb.home`), `app/scene_dance.c` (`dn_ball` avec tons et fil), `app/app_ui.c`, `tests/unit/scene_test.c`, `tests/unit/plateau_test.c`

**Produces :** `typedef struct { uint8_t model[4], pat[4], next[4], mute[4]; int8_t pos[4], len[4]; uint32_t hits[4]; int8_t sel; } q_hview;` (`pat` le pattern qui joue, `next` celui en attente ou 0) ; `void home_frame(const q_hview *v, uint32_t ms); void home_band(int32_t oy);` ; `home_frame` remet ses compteurs quand `sc.fresh` (passé par `scene_enter`).

- [ ] **Step 1: Tests** : `home_budget` (les 11 familles réparties sur les 4 pistes, notes sur toutes : ≤ 300) ; `home_hits_per_track` (une note sur la piste 2 : rose sur son socle seul — compter le rose par quart de largeur) ; `home_mute` (piste 3 coupée, une note : pas de rose sur elle) ; `home_selected_chip` (sel 0 : la pastille 1 orange) ; `home_long_pattern` (len 64, pos 63 : pas de plantage, un segment rose) ; `home_stopped` (pos −1 : aucun pas rose).
- [ ] **Step 2: Lancer** : échecs.
- [ ] **Step 3: Implémenter.**
- [ ] **Step 4: Lancer** `tests/run_tests.sh` : `all tests ok`.
- [ ] **Step 5: Commit** `Scene: HOME's tracks as emblems on neon pedestals`

### Task 2: ui.c, simulateur, appareil

**Files:** Modify `app/ui.c` (vue TRACKS, `anim` 5, clé de scène sur 3 bits, appel de `home_frame`/`home_band`), `app/tests/home.txt` ; `docs/cpu.md`

- [ ] **Step 1: Script** : dans `home.txt`, TRACKS : `expect anim 5`, une note, un mute (touche blanche 1–4), `shot` ; lancer : `all tests ok`.
- [ ] **Step 2: Implémenter** ; lancer : `all tests ok`.
- [ ] **Step 3: Appareil** : build avec miroir, pire cas, HOME TRACKS 45 s : zéro bloc perdu, FPS ≥ 15 ; `docs/cpu.md` ; build normal réinstallé.
- [ ] **Step 4: Commit** `UI: HOME's tracks page animated`
