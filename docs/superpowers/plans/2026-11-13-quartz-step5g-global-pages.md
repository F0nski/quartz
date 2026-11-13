# QUARTZ étape 5g (pages globales) — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** les pages TEMPO, SCALE, KEYS, FX (GLO) et SONG (HOME) dessinées à plat, très lisibles, qui montrent en rose ce qui joue, mesurées sans bloc perdu.

**Architecture :** `scene_glo.c` dessine chaque page directement dans la bande (`cv_rect`, `cv_line`, `cv_text`), nourri par `q_gview` que prépare `ui.c` ; `glo_frame` tient l'état qui dépend du temps (dernière note, dernier temps), `glo_band` dessine. `anim` 7. Aucune fonction audio nouvelle.

**Tech Stack :** C (gnu11, sans libc) ; `tests/run_tests.sh` ; `tools/device/`.

**Spec :** `docs/superpowers/specs/2026-11-13-quartz-step5g-global-pages-design.md` ; maquette `docs/screens/global.html` (fonctions `tempoA`, `scaleA`, `octave`, `opts`, `keysA`, `fxA`, `songA` : géométries et tailles telles quelles, `y` d'écran − 30 = `y` de la zone de scène ; les polices de la maquette correspondent à `FONT_L` (34 px), `FONT_M` (24 et 12 px), `FONT_S` (9–10 px), `FONT_XS` (8 px)).

## Global Constraints

- Travail sur `main` à jour (5f commité) ; commits par l'utilisateur ; pas de `Co-Authored-By`.
- Pas de libc ni libm ; le nouveau fichier inclus par `app_ui.c`, `tests/unit/scene_test.c`, `tests/unit/plateau_test.c` après `scene_sound.c` ; le code audio garde ses adresses (le code de l'écran en dernier ; vérifier `pq_init`, `seq_block`, `app_render`, `sysex_byte`, `fm1_timer5_irq` avec `llvm-nm` sur `app/build/quartz.elf` avant/après).
- Les textes que la page écrit et que `ui.c` possède déjà (noms des notes, des modes, des divisions, options QUANT/CLICK/KEYS) passent par `q_gview` en pointeurs : pas de copie des tables.
- Couleurs de la spec, section 2 ; rose (`PINK` de ui.c, `#ff2f8e`) réservé à ce qui joue. L'en-tête, le pied de page et le son ne changent pas.

## Review Focus

1. Arrivée sur FX ou TEMPO pendant la lecture, ou changement de piste : aucune note ni aucun temps d'avant ne fait réagir la page (test `glo_fresh`, Task 2).
2. SONG vide (`n` 0, curseur 0) et SONG pleine (32 entrées, 16 répétitions) : « EMPTY » sans rangée ; la bande et les cases ne sortent pas de x 8–232 (tests `glo_song_empty`, `glo_song_full`, Task 2).
3. Titres longs (« C# MIN PENTA », tempo 240) : rien n'est écrit dans les colonnes 0–3 ni 236–239 (test `glo_title_fits`, Task 1).
4. Pas de lecture au-delà de 16 (LEN 64) : la case allumée est `pos % 16` (test `glo_tempo_pos`, Task 1).
5. KEYS OFF ou piste non Marbles avec IN SCALE = MARBLES : clavier chromatique, aucune croix (test `glo_keys_chromatic`, Task 1).

---

### Task 1: la vue, TEMPO, SCALE, KEYS

**Files:** Create `app/scene_glo.c` ; Modify `app/scene.h`, `app/app_ui.c`, `tests/unit/scene_test.c`, `tests/unit/plateau_test.c`

**Produces :**
```c
enum { GP_TEMPO, GP_SCALE, GP_KEYS, GP_FX, GP_SONG };
typedef struct {
    uint8_t page;                       /* GP_* */
    int16_t k[4];                       /* the page's 4 knob values, in ui.c PAGE[]'s ranges */
    const char *title;                  /* SCALE, KEYS: the scale ("A LOCRIAN"); FX: the delay's division ("1/4") */
    const char *const *notes;           /* the 12 pitch classes' names from C (ui.c TONIC_NAME) */
    const char *const *opt[2];          /* the option rows' names: SCALE QUANT, CLICK; KEYS IN SCALE */
    uint8_t nopt[2];
    uint8_t tonic, inscale;             /* KEYS: inscale 1 when the track's white keys play the scale */
    uint16_t mask;                      /* the scale: bit n = n semitones above the tonic (SCALE_MASK) */
    int8_t pos;                         /* TEMPO: the track's step while playing, -1 stopped */
    uint16_t held;                      /* SCALE: pitch classes held (bit n = class n); KEYS: white keys held (bit w) */
    int8_t keyn[16];                    /* KEYS: the note each white key plays, -1 none */
    uint32_t hits;                      /* FX: the note-ons of all tracks (audio_hits summed) */
    struct { int8_t n, cursor, playing, on; q_entry e[Q_SONG]; } song;   /* cursor 1..n, 0 none; playing 1..n, 0 none */
} q_gview;
void glo_frame(const q_gview *v, uint32_t ms);   /* the time-driven state: last note, last beat; sc.fresh resets them */
void glo_band(const q_gview *v, int32_t oy);     /* draws the page into the band whose top is oy */
```
Fraîcheur comme `sound_frame` (`sc.fresh = !sc.seen; sc.seen = 1;` ; frais : dernière note et dernier temps reculés de 100 s). TEMPO : un temps commence quand `pos` passe à un multiple de 4 ; les jauges reculent de 30 % puis reviennent en 0,25 s.

- [ ] **Step 1: Tests** (`glo_tests()` appelé de `main`, bandes lues comme `count_px_from` mais avec `glo_band`) :
  - `glo_tempo_pos` : TEMPO, `pos` 37, une image : du rose ; la case rose est la 6e (`37 % 16`) : du rose dans la colonne x 14 + 2 × 27 ± 10, aucun ailleurs dans la mesure ; `pos` −1 : aucun rose.
  - `glo_title_fits` : SCALE titre « C# MIN PENTA », TEMPO `k[0]` 240 : aucun pixel non noir dans les colonnes 0–3 et 236–239 des deux bandes.
  - `glo_scale_held` : SCALE, tonique 9, `mask` LOCRIAN (`0x56B`), `held` = bit 2 (D) : du rose ; `held` 0 : aucun rose, du bleu (la tonique) et du vert.
  - `glo_keys_chromatic` : KEYS, `inscale` 0 : aucun pixel du fil des croix (`#2e2850`) sur les touches noires ; `inscale` 1 : présent.
  - `glo_keys_held` : KEYS, `held` = bit 3 : du rose.
- [ ] **Step 2: Lancer** `tests/run_tests.sh` : échecs (symboles absents).
- [ ] **Step 3: Implémenter** `scene_glo.c` pour TEMPO, SCALE, KEYS selon la spec, section 3, et la maquette ; inclure le fichier dans `app_ui.c`, `scene_test.c`, `plateau_test.c` ; un petit `glo_num(int, char *)` local pour les nombres (le fichier est inclus avant `ui.c`).
- [ ] **Step 4: Lancer** `tests/run_tests.sh` : `all tests ok`.
- [ ] **Step 5: Commit** `Scene: TEMPO, SCALE and KEYS drawn flat and labelled`

### Task 2: FX et SONG

**Files:** Modify `app/scene_glo.c`, `tests/unit/scene_test.c`

**Consumes :** `q_gview`, `glo_frame`, `glo_band` (Task 1).

- FX : échos espacés de `{8, 10, 13, 17, 22, 30, 40, 60, 90}[k[0]]` px, chaque écho × `0,15 + 0,8 × k[1] / 127` ; la queue de reverb en colonnes de 2 px (un triangle plein), longue de `30 + 180 × k[2] / 127`, ton = clair orange × `0,35 + 0,65 × k[3] / 127`. Une note : l'écho `k` est rose de `0,12 k` à `0,12 (k + 1)` s, la queue balayée en rose (ombre) en 1,2 s.
- SONG : la bande sur x 14–226 (chaque entrée `212 × reps / total` px) ; 5 rangées de 22 px depuis y 46 de la zone de scène, la première `max(0, min(cursor − 3, n − 5))` ; cases de 6 × 8 px tous les 8,5 px depuis x 70.

- [ ] **Step 1: Tests** :
  - `glo_fx_note` : FX, `hits` 0 puis 1 à 50 ms (deux `glo_frame`) : du rose ; 3 s plus tard : aucun.
  - `glo_fresh` : `scene_enter`, FX avec `hits` 7 dès la première image : aucun rose ; TEMPO arrivé avec `pos` 4 (un temps) : jauges pleines (pas de recul) à la première image.
  - `glo_song_empty` : SONG, `n` 0 : du texte gris, aucun pixel vert (`#22d58e`).
  - `glo_song_full` : 32 entrées de 16 répétitions, curseur 32 : aucun pixel non noir dans les colonnes 0–7 et 233–239 sous la ligne de titre.
  - `glo_song_playing` : `on` 1, `playing` 2, curseur 2 : du rose ; `playing` 0 : aucun.
- [ ] **Step 2: Lancer** : échecs.
- [ ] **Step 3: Implémenter** FX et SONG.
- [ ] **Step 4: Lancer** `tests/run_tests.sh` : `all tests ok`.
- [ ] **Step 5: Commit** `Scene: GLO's FX and HOME's SONG drawn flat`

### Task 3: ui.c, simulateur, appareil

**Files:** Modify `app/ui.c`, `docs/cpu.md` ; Create `app/tests/global.txt` (`glo.txt` existe déjà et teste autre chose)

**Consumes :** `q_gview`, `glo_frame`, `glo_band`.

- `view_now` : P_TEMPO, P_SCALE, P_KEYS, P_GFX, P_SONG → `v->scene.anim = 7` (commentaire de `anim` mis à jour) ; la clé de scène inclut la page pour `anim` 7 comme pour 6.
- `ui_frame` : `anim` 7 remplit un `q_gview` (`page` selon pid ; `k` = `foot.v` ; `title` : SCALE et KEYS « TONIC MODE » composé dans un tampon, FX `DIV_NAME[dtime]` ; `notes` = `TONIC_NAME` ; `opt` : QUANT_NAME/CLICK_NAME, KEYS_NAME ; `mask` = `SCALE_MASK[mode]` ; `pos` = `audio_pos(ui.track)` ; `held` : SCALE les classes `key_note[k] % 12` des touches tenues, KEYS les touches blanches tenues ; `inscale` = `keys_in_scale()` ; `keyn` : `scale_note(w)` en gamme, sinon `53 + WHITE[w] + 12 × octave` ; `hits` somme des 4 pistes ; `song` depuis le projet, `playing` = `audio_song_entry() + 1`, `on` = `song_on`), `scene_enter` si frais, `glo_frame`, puis `glo_band` par bande.
- `draw_scene` : retirer les cas P_TEMPO, P_SCALE, P_KEYS, P_SONG et P_GFX (P_GFX sort du bloc des barres) ; le remplissage `v->scene.song`/`tempo` devenu inutile dans `view_now` part aussi.

- [ ] **Step 1: Script** du simulateur : `tap GLO`, `expect anim 7`, `shot glo_tempo.png` ; `tap PLAY`, `wait 300`, `shot glo_tempo_play.png`, `tap PLAY` ; `turn SELECT 1` (SCALE), `key 0 down`, `wait 30`, `shot`, `key 0 up` ; `turn SELECT 1` (KEYS), même chose ; `turn SELECT 1` (FX), une note, `shot` ; `tap HOME`, `turn SELECT 1` (SONG), `expect anim 7`, `shot`. Lancer : échec (`anim` différent).
- [ ] **Step 2: Implémenter** ; `tests/run_tests.sh` : `all tests ok` ; regarder les captures face à `docs/screens/global.html`.
- [ ] **Step 3: Appareil** : build avec miroir, pire cas de `docs/cpu.md` (`tools/device/worst_case.py`), GLO TEMPO 45 s : zéro bloc perdu, SCENE FPS relevé ; une ligne dans `docs/cpu.md` ; build normal réinstallé (`--classic`), adresses chaudes inchangées.
- [ ] **Step 4: Commit** `UI: the global pages drawn`

## Hors périmètre

- KIT, NOTES ; CPU reste du texte ; progression des répétitions de SONG ; mesure réelle du compresseur ; le pied de page de SCALE.
