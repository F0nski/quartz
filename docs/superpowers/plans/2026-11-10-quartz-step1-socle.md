# QUARTZ étape 1 (Socle) — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** un firmware QUARTZ installable où l'on joue et programme 4 pistes Plaits au pas à pas, avec sauvegarde et une page CPU.

**Architecture :** une app fm1-base en C (`app/app.c` inclut les autres `.c` de l'app : c'est son unity build) ; le séquenceur produit des événements datés à l'échantillon ; un pool de 6 voix Plaits mono-moteur (C++ derrière une API C) est rendu à 3 voix par cœur ; l'interface est un ensemble de pages par domaine.

**Tech Stack :** C (gnu11) et C++ (gnu++11, sans exceptions ni RTTI) ; fm1-base (submodule, commit `b0976f2`) ; Plaits et stmlib vendorisés depuis `pichenettes/eurorack` commit `f4cfa769` ; toolchain JieLi clang `jieli-linux-toolchains-20260730.1` ; SDK AC79 (3 fichiers) ; simulateur hôte et émulateur fm1-emu 0.4.0.

**Spec :** `docs/superpowers/specs/2026-11-10-quartz-groovebox-design.md` (sections 2, 3, 4.1, 5.1, 7, 8, 9.1, 10, 11, 12 « 1. Socle », 13).

## Global Constraints

- Identité git : toujours `f0nski <13540044+F0nski@users.noreply.github.com>`, remote `git@github-f0nski:F0nski/quartz.git`. Pas de ligne `Co-Authored-By`.
- Code, commentaires et messages de commit en anglais.
- Fréquence d'échantillonnage : `Q_SR = 44117.647f` ; blocs `APP_BLOCK = 256`.
- Pool de **6 voix**, 3 par cœur (voix 0–2 : cœur 1, voix 3–5 : cœur 2) ; vol de la voix la plus ancienne ; une voix libre n'est pas rendue.
- POLY de 1 à 4 par piste ; une voix n'instancie que le modèle choisi ; changer de modèle réinitialise les voix de la piste.
- Longueur de piste 1–64 pas ; divisions `1/32, 1/16T, 1/16, 1/8T, 1/8, 1/4T, 1/4, 1/2, 1/1` ; tempo 40–240 BPM.
- Pas de constructeurs statiques C++ (placement new dans `.pool`).
- Mise en page de l'écran : en-tête 30 px, zone de scène 240×166 (en deux bandes de 240×83), pied 44 px.
- Couleurs des encodeurs : K1 `RGB(58,109,255)`, K2 `RGB(34,213,142)`, K3 `RGB(240,240,250)`, K4 `RGB(255,122,46)` ; notes jouées `RGB(255,47,142)`.
- La sauvegarde se fait sur SAVE, jamais pendant la lecture.
- Une étape n'est validée qu'avec zéro bloc audio perdu dans le pire cas sur l'appareil.

## Review Focus

1. Tempo, longueur ou division changés pendant la lecture : le pas suivant tombe au bon échantillon et aucune note ne reste bloquée (test `tempo_change_keeps_grid`, `length_shrink_wraps`, tâche 2).
2. Changement de modèle pendant que la piste sonne : pas de plantage, pas de bruit, voix réinitialisées (test `engine_switch_while_sounding`, tâche 4 ; script `model_switch.txt`, tâche 7).
3. Pool épuisé (4 pistes en POLY 4) : vol de la plus ancienne, le note-off d'une note volée n'éteint pas la nouvelle (test `stolen_note_off_ignored`, tâche 3).
4. Second cœur indisponible (refusé ou abandonné) : les 6 voix sont rendues sur le cœur 1 sans perte de son (test `render_without_cpu2`, tâche 5).
5. Objet de sauvegarde absent, tronqué ou d'une autre version : le projet par défaut se charge, sans plantage (test `store_rejects_bad_object`, tâche 6).

---

## Structure des fichiers

```
quartz/
  base/                         submodule fm1-base @ b0976f2
  patches/fm1-base-device-libs.patch   ajoute device_libs à base/tools/build.py
  build.sh                      applique le patch, vérifie l'environnement, appelle base/tools/build.py ../app
  tests/run_tests.sh            tests unitaires hôte puis scripts du simulateur
  tests/unit/                   tests C/C++ compilés avec cc/c++ de l'hôte
  docs/cpu.md                   mesures CPU de l'étape (tâche 8)
  app/
    fm1.toml  app.h  app.c  words.tsv
    project.h                   modèle de données du projet
    seq.h  seq.c                horloge et séquenceur STEP
    voices.h  voices.c          pool de voix
    audio.c                     app_render : séquenceur -> voix -> mix, deux cœurs
    store.c                     sauvegarde / chargement du projet
    ui.h  ui.c                  domaines, pages, couches, saisie, dessin
    dsp/qvoice.h  dsp/qvoice.cc voix Plaits mono-moteur (dérivée de plaits::Voice)
    dsp/pq.h  dsp/pq.cc         API C des voix
    dsp/cxx/{algorithm,cmath,cstring,new}   en-têtes C++ minimaux
    dsp/cxx_runtime.cc          __cxa_pure_virtual, operator delete
    third_party/plaits/  third_party/stmlib/   vendorisés et patchés
    tests/*.txt                 scripts du simulateur
```

---

### Task 1: Dépôt, build et app minimale

Reprend les correctifs du spike `spike-cpu` (`~/perso/fm1/spike-cpu`), qui reste la référence : `app/fm1.toml`, `app/cxx/*`, `app/cxx_runtime.cc`, le patch de `base/tools/build.py`, les patchs Plaits/stmlib.

**Files:**
- Create: `.gitmodules`, `base` (submodule), `patches/fm1-base-device-libs.patch`, `build.sh`, `.gitignore`, `tests/run_tests.sh`, `app/fm1.toml`, `app/app.h`, `app/app.c`, `app/words.tsv`, `app/dsp/cxx/*`, `app/dsp/cxx_runtime.cc`, `app/third_party/plaits/**`, `app/third_party/stmlib/**`, `app/tests/boot.txt`, `README.md`

**Interfaces:**
- Produces : `./build.sh [--host|--web|--release X.Y]` (sortie `app/build/quartz.fwsc`, `app/build/host/quartz_host`) ; `tests/run_tests.sh` (code de sortie 0 = tout passe) ; contrat fm1-base dans `app/app.h` (`app_init`, `app_render`, `app_panic`, `app_ui_init`, `app_frame`, `app_input_only`, `app_say`, `app_expect`).

- [ ] **Step 1 : submodule et patch.** `git submodule add https://github.com/charlesvestal/fm1-base base`, `git -C base checkout b0976f2`, `git -C base submodule update --init`. Écrire `patches/fm1-base-device-libs.patch` = le `git -C spike-cpu/base diff tools/build.py` du spike, commentaire « spike-cpu » remplacé par « quartz ».
- [ ] **Step 2 : `build.sh`.** POSIX sh, `set -e`. Exporte `JIELI_TOOLCHAIN` (défaut `$PWD/.tools/toolchain`) et `AC79_SDK` (défaut `$PWD/.tools/sdk`) ; applique le patch si `git -C base apply --reverse --check` échoue ; crée le lien `app/libc_include -> $JIELI_TOOLCHAIN/pi32v2/include` s'il manque ; `cd base && exec python3 tools/build.py ../app "$@"`. Un message clair et `exit 1` si la toolchain ou `uboot.boot` manquent (récupération : `base/tools/get_toolchain.sh`, et les 3 fichiers SDK du miroir `amitv87/fw-AC79_AIoT_SDK` tag `AC79NN_SDK_V1.2.1_2023-12-13`, cf. spike).
- [ ] **Step 3 : app minimale.** Copier `base/template/{app.h,app.c,fm1.toml,words.tsv}` puis : `APP_NAME "QUARTZ"`, `name = "QUARTZ"`, `id_prefix = "FM-1_98"`, `usb_product = "QUARTZ FM-1"`, `cpu2 = true`, `cpu2_fallback = true`, `ble = false`, `include = ["third_party", "dsp"]`, `device_include = ["dsp/cxx"]`, `o2 = ["dsp/*.cc", "third_party/plaits/**/*.cc", "third_party/stmlib/**/*.cc"]`, `o2_cflags`, `vendored`, `device_libs` identiques au spike. `app.c` : écran noir avec « QUARTZ » en `FONT_B`, silence. `app_expect` connaît `booted` (1).
- [ ] **Step 4 : Plaits et stmlib.** Copier `plaits/dsp`, `plaits/resources.*`, `plaits/LICENSE` et `stmlib/{stmlib.h,dsp,utils,LICENSE}` du commit `f4cfa769`. Patchs : `stmlib.h` définit `TEST` s'il ne l'est pas ; `plaits/user_data.h` remplacé par le stub du spike ; `plaits/dsp/dsp.h` : `kSampleRate = 44117.647f` et `kCorrectedSampleRate = 44117.647f` (commentaire : FM-1 codec rate). Supprimer `plaits/dsp/voice.cc` et `voice.h` (remplacés tâche 4).
- [ ] **Step 5 : `tests/run_tests.sh` et `app/tests/boot.txt`.** Le script compile et lance chaque `tests/unit/*_test.c` (`cc -std=gnu11 -O1 -Iapp -lm`) et `*_test.cc` (`c++ -std=gnu++11 -DTEST -Iapp -Iapp/third_party -Iapp/dsp`, plus les `.cc` de Plaits/stmlib et `app/dsp/qvoice.cc app/dsp/pq.cc`), puis `./build.sh --host` et `app/build/host/quartz_host app/tests/X.txt app/build/host` pour chaque script ; s'arrête au premier échec. `boot.txt` : `wait 600`, `expect booted 1`, `shot boot.png`.
- [ ] **Step 6 : vérifier.** Run : `./build.sh` → `checks ok` et `app/build/quartz.fwsc` ; `tests/run_tests.sh` → `boot.txt` passe.
- [ ] **Step 7 : `.gitignore`** (`.tools/`, `app/build/`, `app/libc_include`) **et commit.** `git commit -m "Set up the QUARTZ app on fm1-base with vendored Plaits"`

### Task 2: Séquenceur STEP

**Files:**
- Create: `app/project.h`, `app/seq.h`, `app/seq.c`, `tests/unit/seq_test.c`

**Interfaces:**
- Produces (`project.h`) :
  ```c
  #define Q_TRACKS 4
  #define Q_STEPS 64
  #define Q_SR 44117.647f
  enum { DIV_32, DIV_16T, DIV_16, DIV_8T, DIV_8, DIV_4T, DIV_4, DIV_2, DIV_1, DIV_N };
  typedef struct { uint8_t on, note, vel, pad; } q_step;
  typedef struct { uint8_t len, div; q_step steps[Q_STEPS]; } q_pattern;
  typedef struct { uint8_t model, poly, glide, level; int8_t tune; uint8_t harm, timb, morph, decay; } q_sound; /* 0..127 sauf model 0..23, poly 1..4, tune -24..24 */
  typedef struct { uint8_t version; uint16_t tempo; q_sound sound[Q_TRACKS]; q_pattern pat[Q_TRACKS]; } q_project; /* tempo en BPM entiers 40..240 */
  void project_defaults(q_project *p);   /* version 1, 120 BPM, piste i : model 8 (VA), poly 1, len 16, DIV_16, pas éteints, note 60, vel 100, harm/timb/morph 64, decay 64, level 100 */
  ```
- Produces (`seq.h`) :
  ```c
  typedef struct { uint32_t offset; uint8_t track, note, vel, on; } q_event;
  typedef struct { /* privé */ } q_seq;
  double seq_step_frames(int tempo, int div);                 /* Q_SR * 60 / tempo * beats(div) ; beats : 1/8, 1/6, 1/4, 1/3, 1/2, 2/3, 1, 2, 4 */
  void seq_start(q_seq *s);                                   /* pas 0 de chaque piste au prochain échantillon */
  void seq_stop(q_seq *s, q_event *out, int *n);              /* note-off des notes en cours */
  int  seq_block(q_seq *s, const q_project *p, uint32_t nframes, q_event *out, int max); /* événements du bloc, triés par offset */
  int  seq_pos(const q_seq *s, int track);                    /* pas en cours, -1 à l'arrêt */
  ```

- [ ] **Step 1 : tests.** Dans `seq_test.c` (assert + `main`), avec une piste à 16 pas tous actifs, les autres éteintes :
  - `step_frames_values` : `seq_step_frames(120, DIV_16) == 5514.705875` (±1e-3) ; `DIV_16T` = 2/3 de `DIV_8` ; `DIV_1` = 16 × `DIV_16`.
  - `steps_land_on_exact_frames` : sur 64 blocs de 256, le note-on du pas k tombe à l'échantillon absolu `ceil(k * spf)` ; le note-off au milieu, `ceil((k + 0.5) * spf)`.
  - `polyrhythm` : piste 0 en 16 pas `DIV_16`, piste 1 en 12 pas `DIV_8T` ; les deux bouclent sur 4 temps et reviennent au pas 0 au même échantillon, `ceil(2 * Q_SR)` à 120 BPM.
  - `length_shrink_wraps` : longueur passée de 16 à 4 alors que la piste est au pas 9 : le pas suivant joué est le 0.
  - `tempo_change_keeps_grid` : tempo 120 → 60 pendant le pas 3 : le pas 4 tombe à `start(3) + spf(120)` et le pas 5 à `start(4) + spf(60)`.
  - `stop_releases_notes` : `seq_stop` pendant une note émet son note-off.
  - `events_sorted` : les offsets d'un bloc sont croissants et < `nframes`.
- [ ] **Step 2 : lancer** `tests/run_tests.sh` → échec de compilation (`seq.h` absent).
- [ ] **Step 3 : implémenter `seq.c` et `project_defaults` (dans `seq.c`).** Chaque piste garde le numéro absolu de l'échantillon où son pas courant a commencé et la durée de ce pas ; le pas suivant commence à `debut + spf` arrondi par `ceil` à partir d'une ancre en `double` réinitialisée à chaque changement de tempo ou de division (règle du test `tempo_change_keeps_grid`). Un pas éteint n'émet rien.
- [ ] **Step 4 : lancer** `tests/run_tests.sh` → `seq_test` passe.
- [ ] **Step 5 : commit** `"Add the step sequencer clock"`

### Task 3: Pool de voix

**Files:**
- Create: `app/voices.h`, `app/voices.c`, `tests/unit/voices_test.c`

**Interfaces:**
- Produces :
  ```c
  #define Q_VOICES 6
  enum { V_FREE, V_ON, V_RELEASED };
  typedef struct { uint8_t state; int8_t track; uint8_t note; uint32_t age; } q_vslot;
  typedef struct { q_vslot v[Q_VOICES]; uint32_t clock; } q_voices;
  void voices_init(q_voices *p);
  int  voices_note_on(q_voices *p, int track, int note, int poly);   /* slot 0..5 */
  int  voices_note_off(q_voices *p, int track, int note);            /* slot relâché, -1 si aucun */
  void voices_free(q_voices *p, int slot);                           /* le rendu signale le silence */
  void voices_release_track(q_voices *p, int track);                 /* tous ses slots -> V_FREE */
  ```

- [ ] **Step 1 : tests** (`voices_test.c`) :
  - `mono_retriggers_same_slot` : poly 1, deux note-on successifs sur la piste 0 → même slot, note mise à jour.
  - `poly_uses_own_slots` : poly 3, trois notes → trois slots distincts ; une quatrième vole la plus ancienne de la piste.
  - `balances_cores` : six note-on sur six pistes fictives (track 0..3 puis 0..1 en poly 2) → trois slots dans 0–2 et trois dans 3–5.
  - `steals_global_oldest` : pool plein, une note d'une piste qui n'a aucun slot vole le slot de plus petit `age`.
  - `stolen_note_off_ignored` : la piste 0 perd sa note 60 au profit de la piste 1 ; `voices_note_off(p, 0, 60)` renvoie -1 et la note de la piste 1 reste `V_ON`.
  - `released_slot_reused_first` : un slot `V_RELEASED` est pris avant de voler un `V_ON`.
- [ ] **Step 2 : lancer** → échec de compilation.
- [ ] **Step 3 : implémenter `voices.c`.** Ordre de choix d'un slot pour une nouvelle note : (1) en mono, le slot de la piste ; (2) si la piste a déjà `poly` slots, son plus ancien ; (3) un `V_FREE` sur le cœur le moins chargé (cœur = slot / 3) ; (4) le `V_RELEASED` le plus ancien ; (5) le `V_ON` le plus ancien.
- [ ] **Step 4 : lancer** → passe.
- [ ] **Step 5 : commit** `"Add the voice pool"`

### Task 4: Voix Plaits mono-moteur

**Files:**
- Create: `app/dsp/qvoice.h`, `app/dsp/qvoice.cc`, `app/dsp/pq.h`, `app/dsp/pq.cc`, `tests/unit/pq_test.cc`

**Interfaces:**
- Produces (`pq.h`, `extern "C"`) :
  ```c
  void pq_init(void);                                   /* construit les 6 voix (placement new en .pool), modèle 8 */
  void pq_set_engine(int v, int model);                 /* 0..23 dans l'ordre d'enregistrement de Plaits ; réinitialise la voix */
  void pq_set_patch(int v, float harm, float timb, float morph, float decay, float glide); /* 0..1 */
  void pq_note_on(int v, float note, float vel);        /* note MIDI (tune inclus), vel 0..1 */
  void pq_note_off(int v);
  void pq_render(int v, float *out, unsigned n);        /* ajoute n échantillons mono, ~±1 */
  int  pq_silent(int v);                                /* 1 : relâchée et sous 1e-4 depuis 50 ms */
  unsigned pq_voice_bytes(void);                        /* octets par voix (objet + tampon) */
  ```
- Consumes : rien des tâches 2–3.

- [ ] **Step 1 : tests** (`pq_test.cc`) :
  - `pitch_is_right` : modèle 8 (VA), note 69, rendu 1 s après note-on : fréquence mesurée par passages à zéro = 440 Hz ±1 %.
  - `all_models_sound` : pour chaque modèle 0–23, après note-on, 0,2 s de rendu : au moins un échantillon > 0,01 en valeur absolue, aucun NaN ni infini.
  - `engine_switch_while_sounding` : modèle 8 en train de sonner, `pq_set_engine(v, 17)` puis 0,2 s de rendu : pas de NaN, crête < 4.
  - `release_goes_silent` : note-on, 0,1 s, note-off, puis rendu jusqu'à `pq_silent` = 1 en moins de 5 s (decay 0,3).
  - `voice_bytes_reported` : affiche `pq_voice_bytes()` ; échoue au-delà de 24 576 (la règle du spec — réduire le delay — s'applique entre 16 384 et 24 576 ; au-delà, revoir le tampon).
- [ ] **Step 2 : lancer** → échec de compilation.
- [ ] **Step 3 : `QVoice` dans `qvoice.{h,cc}`.** Copie de `plaits::Voice` (MIT, en-tête d'origine conservé) où les 24 membres moteur sont remplacés par un stockage aligné de la taille du plus grand moteur (`static_assert`) ; `SetEngine(int)` détruit l'ancien et construit le nouveau par placement new, l'initialise avec l'allocateur du tampon de la voix (16 384 octets), charge `fm_patches_table[model - 2]` pour 2–4, appelle `Reset()`. `Render` reprend celui de Plaits sans `EngineQuantizer`, avec le moteur courant.
- [ ] **Step 4 : `pq.cc`.** Stockage des 6 voix comme dans `spike-cpu/app/plaits_bridge.cc`. Trigger : `trigger_patched = true`, `level_patched = true` avec `level = vel` ; note glissée vers la cible par un filtre à un pôle de constante `glide` (0 = immédiat, 1 = 0,5 s) ; bloc interne de `kBlockSize`.
- [ ] **Step 5 : lancer** → passe ; noter la valeur de `pq_voice_bytes()` dans le message de commit.
- [ ] **Step 6 : commit** `"Add single-engine Plaits voices (N bytes each)"`

### Task 5: Rendu audio sur deux cœurs

**Files:**
- Create: `app/audio.c`, `tests/unit/audio_test.c`
- Modify: `app/app.c` (`#include "project.h"`, `"seq.c"`, `"voices.c"`, `"audio.c"` ; `app_init` appelle `audio_init`)

**Interfaces:**
- Consumes : `seq_block`, `q_event` (tâche 2) ; `voices_*` (tâche 3) ; `pq_*` (tâche 4).
- Produces :
  ```c
  void audio_init(q_project *p);           /* pq_init, voices_init, modèles des pistes, plat_cpu2_set(1) */
  void audio_play(int on);                 /* seq_start / seq_stop (note-off envoyés) */
  void audio_live_note(int track, int note, int on);   /* clavier, file sans verrou vers le rendu */
  void audio_track_model(int track);       /* voices_release_track au prochain bloc */
  typedef struct { uint32_t core1_pct, core2_pct, xruns, active; int cpu2; } q_load;
  q_load audio_load(void);                 /* moyennes sur ~1 s */
  void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);   /* contrat fm1-base */
  ```
- [ ] **Step 1 : tests** (`audio_test.c`, avec des doublures de `plat_cpu2_*` et `pq_*` qui enregistrent les appels) :
  - `splits_block_at_events` : un note-on à l'offset 100 → `pq_render` appelé pour [0,100) puis [100,256) sur la voix concernée.
  - `cores_get_their_voices` : six voix actives → les voix 3–5 rendues par le job du cœur 2, 0–2 sur le cœur 1.
  - `render_without_cpu2` : `plat_cpu2_run` renvoie 0 → les six voix rendues sur le cœur 1, même sortie.
  - `free_voices_skipped` : une voix `V_FREE` n'est jamais rendue ; `pq_silent` = 1 sur une voix relâchée → `voices_free`.
  - `live_notes_reach_voices` : `audio_live_note(1, 64, 1)` → `pq_note_on` au bloc suivant avec la note `64 + tune`.
  - `slot_engine_follows_track` : un slot qui jouait la piste 0 (modèle 8) est attribué à la piste 1 (modèle 13) → `pq_set_engine(slot, 13)` avant `pq_note_on` ; aucun appel si le modèle est déjà le bon.
- [ ] **Step 2 : lancer** → échec.
- [ ] **Step 3 : implémenter `audio.c`.** Par bloc : vider la file live, `seq_block`, découper le bloc aux offsets des événements, appliquer les événements (note-on/off via le pool puis `pq_*` ; à chaque note-on, `pq_set_engine` si le slot n'a pas le modèle de la piste), rendre chaque segment (voix 3–5 via `plat_cpu2_run`, repli sur le cœur 1 si 0 ou si `plat_cpu2_wait` renvoie -1), mixer en mono × `level/127 × 0,25`, sortir en stéréo × `master_q12`. Charge : `plat_ticks24` autour du travail de chaque cœur, rapportée au budget `n × 544` ticks.
- [ ] **Step 4 : lancer** → passe ; `./build.sh` → `checks ok`.
- [ ] **Step 5 : commit** `"Render the sequencer through the voice pool on both cores"`

### Task 6: Sauvegarde

**Files:**
- Create: `app/store.c`, `tests/unit/store_test.c`
- Modify: `app/app.h` (`enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_PROJECT, OBJ_NOBJ }`, `APP_STORE_MAP` avec `OBJ_PROJECT` à `0x97000u`), `app/app.c`

**Interfaces:**
- Produces :
  ```c
  int  store_load(q_project *p);         /* 0 = chargé ; sinon project_defaults et -1 */
  int  store_save(const q_project *p);   /* 0 = ok ; -1 = refusé pendant la lecture ; sinon le code de plat_store_save */
  ```
  Format : l'objet est `q_project` tel quel (version 1, ~1,1 Ko ≤ `PLAT_STORE_MAX`), précédé de rien ; une taille ou une version différente = objet rejeté.
- [ ] **Step 1 : tests** (`store_test.c`, doublure de `plat_store_*` en mémoire) :
  - `roundtrip` : projet modifié (tempo 97, piste 2 modèle 13, pas 5 actif note 67) → save → load identique (`memcmp`).
  - `store_rejects_bad_object` : objet absent, tronqué de 10 octets, ou version 2 → `store_load` renvoie -1 et le projet par défaut.
  - `no_save_while_playing` : lecture en cours → `store_save` renvoie -1 et aucun appel à `plat_store_save`.
- [ ] **Step 2 : lancer** → échec.
- [ ] **Step 3 : implémenter `store.c`** ; `app_init` appelle `store_load` avant `audio_init`.
- [ ] **Step 4 : lancer** → passe.
- [ ] **Step 5 : commit** `"Save and load the project"`

### Task 7: Interface (pages, couches, saisie, écran)

**Files:**
- Create: `app/ui.h`, `app/ui.c`, `app/tests/nav.txt`, `app/tests/seq.txt`, `app/tests/model_switch.txt`, `app/tests/save.txt`
- Modify: `app/app.c` (`app_frame` → `ui_frame`, `app_expect` → `ui_expect`), `app/words.tsv`

**Interfaces:**
- Consumes : `audio_play`, `audio_live_note`, `audio_track_model`, `audio_load`, `seq_pos`, `store_save`, `q_project`.
- Produces : `void ui_frame(void)` ; `int ui_expect(const char *what, int *got)` qui connaît : `domain` (0 EDIT, 1 SEQ, 2 GLO), `page`, `track` (1–4), `playing`, `rec`, `cursor`, `model`, `poly`, `tune`, `harm`, `tempo`, `len`, `div`, `pos`, `stepon_N`, `stepnote_N` (N = 1..64), `dirty`, `msg_stop_to_save`, `active`.

Comportement (spec sections 7, 8, 9.1, réduit à l'étape 1) :

| Contrôle | Étape 1 |
|---|---|
| ALGORITHM | piste 1–4 |
| SELECT | page suivante / précédente du domaine |
| PRESET | sur EDIT : modèle −/+ ; ailleurs : rien |
| EDIT p1 | K1 HARM, K2 TIMB, K3 MORPH, K4 DECAY (0–127, 1 par cran) |
| EDIT p2 | K1 MODEL (0–23), K2 TUNE (−24..24), K3 GLIDE (0–127), K4 POLY (1–4) |
| SEQ p1 | K1 LENGTH (1–64), K2 DIV ; touches blanches = pas de la page de 16 ; OCT−/OCT+ = page de pas |
| GLO p1 | K1 TEMPO (40–240) |
| GLO p2 | CPU : cœur 1 %, cœur 2 % (ou « OFF »), blocs perdus, voix actives, octets par voix |
| PLAY | lecture / stop |
| REC | arme la saisie pas à pas (à l'arrêt) : une touche écrit la note au curseur et avance ; le curseur repart à 1 en armant |
| SAVE | sauvegarde à l'arrêt ; en lecture, « STOP TO SAVE » 1,5 s dans l'en-tête |
| Touches hors SEQ | jouent la piste courante (note = 53 + touche + 12 × octave, vel 100) |
| Pas activé à la main | note = dernière note jouée sur la piste (60 au départ), vel 100 |
| LED | PLAY si lecture, REC si armé, bouton du domaine courant ; dans SEQ, touches des pas actifs, et la touche du pas en cours |

Écran : en-tête 30 px (pastille de piste, nom du domaine et de la page, « n/m ») ; zone 240×166 en deux bandes 240×83 : EDIT = 4 barres verticales aux couleurs K1–K4 et le nom du modèle ; SEQ = grille des 16 pas de la page (actif, éteint, en cours en rose, curseur en blanc) ; GLO = tempo en grand / tableau CPU ; pied 44 px : 4 libellés et valeurs aux couleurs K1–K4. Redessiner seulement ce qui a changé.

- [ ] **Step 1 : scripts** :
  - `nav.txt` : `tap EDIT`, `expect domain 0`, `turn SELECT 1`, `expect page 2`, `turn SELECT 1`, `expect page 1` (rebouclage) ; `turn ALGO 2`, `expect track 3` ; `tap GLO`, `turn K1 -100`, `expect tempo 40` (borne) ; `shot` de chaque page.
  - `seq.txt` : `tap SEQ`, `turn K1 -8`, `expect len 8` ; `key 0 down`/`up` (1re blanche), `expect stepon_1 1` ; `tap OCT+`, `key 0 down`/`up`, `expect stepon_17 1` ; `tap REC`, `tap EDIT`, `key 7 down`/`up`, `expect stepnote_1 60`, `expect cursor 2` ; `tap REC`, `tap PLAY`, `wait 500`, `expect playing 1`, `expect pos >-1`.
  - `model_switch.txt` : `tap PLAY`, `key 7 down`, `tap EDIT`, `turn SELECT 1`, `turn K1 9`, `wait 200`, `expect model 17`, `key 7 up`, `tap PLAY`, `wait 3000`, `expect active 0`.
  - `save.txt` : modifier tempo et un pas ; `tap PLAY`, `tap SAVE`, `expect msg_stop_to_save 1`, `expect dirty 1` ; `tap PLAY`, `tap SAVE`, `expect dirty 0` ; `reboot`, `wait 600`, `expect tempo …`, `expect stepon_1 1`.
- [ ] **Step 2 : lancer** `tests/run_tests.sh` → les scripts échouent sur `expect` (clés inconnues).
- [ ] **Step 3 : implémenter `ui.c`** ; ajouter dans `words.tsv` les mots affichés (noms des modèles, des pages, « STOP TO SAVE »).
- [ ] **Step 4 : lancer** → tous les scripts passent ; regarder les captures `app/build/host/*.png`.
- [ ] **Step 5 : commit** `"Add the step 1 pages, layers and screen"`

### Task 8: Vérification sur l'émulateur et sur le FM-1

**Files:**
- Create: `docs/cpu.md`

- [ ] **Step 1 : émulateur.** `./build.sh` puis `fm1-emu --fwsc app/build/quartz.fwsc --ms 5000 --keys '3000:PLAY' --snap 4900` (depuis un dossier qui contient `work/`) : démarre sans faute, capture lisible.
- [ ] **Step 2 : appareil** (fait par l'utilisateur ; sauvegarde M-Flasher d'abord) : `.tools/venv/bin/python base/tools/fm1_install.py --yes --classic app/build/quartz.fwsc`.
- [ ] **Step 3 : pire cas.** Les 4 pistes en MODAL (modèle 20), POLY 2 sur les pistes 1 et 2, 16 pas actifs en `DIV_16` à 240 BPM, DECAY 127 → 6 voix actives en permanence. Lire GLO p2 après 60 s : blocs perdus = 0. Recommencer avec ADDITIVE (modèle 12).
- [ ] **Step 4 : `docs/cpu.md`** : date, version, charges des deux cœurs pour les deux cas, octets par voix, blocs perdus. Une ligne en tête : la règle de validation (0 bloc perdu).
- [ ] **Step 5 : commit** `"Record the step 1 CPU measurements"` ; pousser.
