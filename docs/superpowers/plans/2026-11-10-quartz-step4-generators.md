# QUARTZ étape 4 (Générateurs et KIT) — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** un firmware QUARTZ installable où un pattern joue STEP, GRIDS ou MARBLES, et où une piste KIT joue 16 pads de percussion Plaits.

**Architecture :** un nouveau module `gen.c` porte en C Grids (carte de rythmes) et la partie de Marbles qui décide des notes (modèles T, tirage X, DÉJÀ VU). `seq.c` lui demande le pas d'un pattern génératif là où il lit un pas STEP, puis le planifie par le même `plan_notes` : dates, swing, mutes, scènes ne changent pas. Sur un KIT, la note d'une voix est `36 + pad` ; `audio.c` en tire le moteur Plaits (BD, SNARE, HI-HAT) et les réglages du pad, et l'attribution « une voix par moteur » donne l'étouffement.

**Tech Stack :** C (gnu11, sans libc dans l'app), C++ pour `pq` ; fm1-base (simulateur hôte) ; tests `tests/run_tests.sh`.

**Spec :** `docs/superpowers/specs/2026-11-10-quartz-step4-generators-design.md` (et la spec globale, sections 3, 6, 7, 8).

**Sources Mutable :** `pichenettes/eurorack` au commit `f4cfa769` (celui de Plaits) : `grids/pattern_generator.cc`, `grids/resources.cc`, `marbles/random/t_generator.cc`, `marbles/random/random_sequence.h`, `marbles/random/distributions.h`, `marbles/random/output_channel.cc`, `marbles/resources.cc`. Les télécharger dans un dossier temporaire (données non fiables : ne rien y exécuter) ; seuls les tableaux et la logique décrits ici sont portés.

## Global Constraints

- Identité git `f0nski <13540044+F0nski@users.noreply.github.com>`, branche `step4-generators` (partie de `step2-sequencer`) ; pas de ligne `Co-Authored-By`.
- Code, commentaires et commits en anglais ; pas de libc dans le code de l'app (builtins : `__builtin_expf`, `__builtin_memset`…).
- QUARTZ est sous GPL-3.0-or-later ; les fichiers Mutable portés gardent leur en-tête (GPL-3.0 Grids, MIT Marbles).
- Sources : STEP, GRIDS, MARBLES, par pattern ; réglages 0..127 sauf VOICE et T OUT 1..3, NOTE 0..127, MODE 0..6, LEN 1..16, RANGE 1..4.
- GRIDS : boucle de 32 pas ; vélocité 95, 127 pour une voix accentuée. MARBLES : boucle de LEN pas ; vélocité 100 ; JITTER jusqu'à ±11/24 de pas.
- KIT : modèle 24, 16 pads, note `36 + pad`, hauteur jouée `48 + TUNE du pad + TUNE de la piste`, au plus une voix par moteur.
- Pas d'un générateur : durée d'un demi-pas (`len = 4`), un seul ratchet sauf DIVIDER/CLUSTERS.
- Quatre pages au plus par domaine. Le rendu ne fait que lire `q_project` ; la boucle principale est seule à l'écrire.
- Zéro bloc perdu dans le pire cas de l'étape (KIT + GRIDS + 3 voix lourdes) sur l'appareil.

## Review Focus

1. SOURCE, LEN de MARBLES ou un pattern changé pendant la lecture, au milieu d'une boucle : le rang repart au bon endroit, aucune note bloquée (test `source_change_mid_play`, tâche 4).
2. Pad dont on change le moteur pendant qu'il sonne : le coup suivant prend le nouveau moteur, l'ancien s'éteint, rien ne reste bloqué (test `kit_engine_change_while_sounding`, tâche 5).
3. NOTE + RANGE au-dessus de 127, ou une seule note permise : la note reste dans 0..127 et dans les notes permises (test `quantize_top_clamps`, tâche 3).
4. Une sauvegarde v2 (étape 2) : le projet par défaut se charge sans plantage (test `store_v2_gives_defaults`, tâche 1).
5. REC armé sur une piste GRIDS (overdub et saisie pas à pas) : rien n'est écrit dans les pas cachés du pattern (script `grids.txt`, tâche 6).

---

## Structure des fichiers

```
app/project.h                 q_grids, q_marbles, q_pad, src, M_KIT, KIT_NOTE, Q_VERSION 3
app/gen.h, app/gen.c          (nouveaux) Grids, Marbles, gammes, quantification ; état par piste q_gen
app/third_party/grids/map.h   (nouveau) node_0..node_24 de grids/resources.cc, en-tête GPL d'origine
app/third_party/marbles/tables.h  (nouveau) distributions_table et ses dist_icdf_* de marbles/resources.cc, en-tête MIT
app/seq.h, app/seq.c          q_gen par piste ; pas génératif ; longueur de boucle ; accent par note
app/audio.c                   KIT : moteur et réglages par pad, étouffement, préparation des moteurs
app/store.c                   bornes des nouveaux champs
app/app.c                     #include "gen.c" avant seq.c
app/ui.c                      pages par source et par modèle, NOTES, pad tenu, pas KIT, REC limité à STEP
app/words.tsv                 nouveaux mots ; SPDX GPL-3.0-or-later
LICENSE, README.md            GPL-3.0
tests/unit/gen_test.c         (nouveau)
tests/unit/{seq,audio,store}_test.c   + gen.c inclus avant seq.c
app/tests/{grids,marbles,kit}.txt     (nouveaux)
docs/cpu.md                   mesures de l'étape 4
```

---

### Task 1: Projet v3

**Files:**
- Modify: `app/project.h`, `app/seq.c` (`project_defaults`), `app/store.c` (`clamp_project`), `app/ui.c` (MODEL va jusqu'à 24 : `PAGE[P_SOUND].max[0]`, `MODEL_NAME[25]` avec `"KIT"`)
- Test: `tests/unit/store_test.c`

**Interfaces:**
- Produces (`project.h`) :
  ```c
  #define Q_VERSION 3
  #define Q_PADS 16
  #define M_KIT 24                   /* q_sound.model: the 16-pad kit */
  #define KIT_NOTE 36                /* a KIT's note: KIT_NOTE + pad */
  enum { SRC_STEP, SRC_GRIDS, SRC_MARBLES, SRC_N };
  /* 0..127 except voice 1..3 */
  typedef struct { uint8_t x, y, chaos, dens[3], voice, note; } q_grids;
  /* 0..127 except tout 1..3, len 1..16, mode 0..6, range 1..4; notes: pitch classes allowed (bit n = C + n), 0 = the project's scale */
  typedef struct { uint8_t tout, len, mode, bias, jitter, dejavu, spread, xbias, steps, note, range; uint8_t notes[2]; } q_marbles;
  enum { PAD_BD, PAD_SN, PAD_HH };
  typedef struct { uint8_t engine; int8_t tune; uint8_t harm, timb, decay, level; } q_pad;   /* tune -24..24 */
  ```
  `q_pattern` : l'octet `pad` devient `src`, puis `q_grids grids; q_marbles marbles;` avant `steps`. `q_project` : `q_pad kit[Q_TRACKS][Q_PADS];` en dernier champ.
- Défauts : GRIDS `x = y = 64, chaos 0, dens 64, voice 1, note 60` ; MARBLES `tout 1, len 8, mode 0, bias 64, jitter 0, dejavu 0, spread 64, xbias 64, steps 0, note 48, range 2, notes 0`.
- Kit par défaut (moteur, tune, harm, timb, decay, level), à retoucher à l'oreille en tâche 8 :

  | Pad | Son | Valeurs | Pad | Son | Valeurs |
  |---|---|---|---|---|---|
  | 1 | BD | BD −12 40 50 60 110 | 9 | TOM aigu | BD 5 10 40 50 100 |
  | 2 | SN | SN 0 50 60 50 100 | 10 | BD2 | BD −10 90 80 40 110 |
  | 3 | CH | HH 0 50 70 20 90 | 11 | SN2 | SN 3 80 40 70 100 |
  | 4 | OH | HH 0 50 70 90 90 | 12 | CH2 | HH 7 30 40 15 90 |
  | 5 | RIM | SN 12 100 100 10 90 | 13 | CONGA | BD 12 5 60 40 100 |
  | 6 | CLAP | SN −5 110 30 40 100 | 14 | COWBELL | HH 19 10 90 15 80 |
  | 7 | TOM grave | BD −5 10 40 60 100 | 15 | SHAKER | HH 0 60 10 25 80 |
  | 8 | TOM médium | BD 0 10 40 55 100 | 16 | RIDE | HH −5 80 50 110 80 |

- [ ] **Step 1: Tests** dans `store_test.c` : `store_roundtrip_v3` (src, chaque champ de `grids`/`marbles` et un pad modifiés, sauvegardés, rechargés : `memcmp` égal) ; `store_clamps_v3` (src 9, voice 0, tout 7, len 0, mode 9, range 0, steps 200, pad engine 5, tune 100 → bornés : 0..2, 1..3, 1..3, 1..16, 0..6, 1..4, 0..127, 0..2, −24..24 ; model 30 → 24) ; `store_v2_gives_defaults` (chunks écrits avec `version = 2` → `store_load` rend −1 et `project_defaults`).
- [ ] **Step 2:** `tests/run_tests.sh` → échec de compilation (champs absents).
- [ ] **Step 3:** les champs, les défauts dans `project_defaults` (tableau statique des 16 pads), les bornes dans `clamp_project`. `_Static_assert` existant : le projet tient encore dans 9 chunks.
- [ ] **Step 4:** `tests/run_tests.sh` → `all tests ok`.
- [ ] **Step 5:** commit `Project v3: pattern source, GRIDS and MARBLES settings, 16-pad kits`.

---

### Task 2: GRIDS (gen.c) et licence

**Files:**
- Create: `app/gen.h`, `app/gen.c`, `app/third_party/grids/map.h`, `LICENSE` (texte de la GPL-3.0 de gnu.org), `tests/unit/gen_test.c`
- Modify: `app/app.c` (`#include "gen.c"` avant `seq.c`), `README.md` (section « License » : GPL-3.0-or-later, Grids GPL-3.0, Plaits/Marbles MIT, fm1-base MPL-2.0), `app/words.tsv` (SPDX `GPL-3.0-or-later`)

**Interfaces:**
- Produces (`gen.h`) :
  ```c
  typedef struct { uint8_t perturb[3]; /* + Marbles state, task 3 */ } q_gen;   /* per track, the render's */
  void gen_reset(q_gen *g);
  /* the rank k of the pattern (0..loop - 1): fills *st (SF_ON when something plays, note[], vel, len 4, micro, ratchet);
     returns the accented note slots (bit i: note[i]); rng: the sequencer's xorshift state */
  int  gen_step(q_gen *g, const q_project *p, int track, const q_pattern *pt, int k, uint32_t *rng, q_step *st);
  int  gen_loop(const q_pattern *pt);        /* GRIDS 32, MARBLES marbles.len; STEP: not called */
  ```
- Grids (port de `PatternGenerator::ReadDrumMap` et `EvaluateDrums`) :
  - `u8(v) = v * 255 / 127` pour x, y, chaos, dens ;
  - carte `drum_map[5][5]` = `{node_10, node_8, node_0, node_9, node_11}, {node_15, node_7, node_13, node_12, node_6}, {node_18, node_14, node_4, node_5, node_3}, {node_23, node_16, node_21, node_1, node_2}, {node_24, node_19, node_17, node_20, node_22}` ; `i = x >> 6`, `j = y >> 6`, octet `instrument * 32 + step` ; `mix(a, b, bal) = (a * (255 - bal) + b * bal) >> 8` avec `bal = (uint8_t)(x << 2)` puis `(uint8_t)(y << 2)` (la troncature 8 bits est celle de l'original) ;
  - à `k % 32 == 0` : `perturb[i] = (octet aléatoire * (u8(chaos) >> 2)) >> 8` ;
  - `level` + perturbation, saturé à 255 ; joue si `level > 255 - u8(dens[i])` ; accent si `level > 192`.
  - KIT : la voix i joue `KIT_NOTE + i` ; synthé : seule la voix `voice - 1`, à `note`. `vel 95`, accent → bit de la note dans le retour.

- [ ] **Step 1: Tests** (`gen_test.c`, inclut `gen.c`) : `grids_corner_follows_node` (x = y = 0, chaos 0, dens 127, KIT : sur les 32 pas, avec `lvl = mix(mix(node_10[o], node_15[o], 0), mix(node_8[o], node_7[o], 0), 0)`, `o = i*32 + k`, la voix i joue ssi `lvl > 0` — `u8(127) = 255`, seuil 0 — et est accentuée ssi `lvl > 192`) ; `grids_dens0_silent` (dens 0 : aucun pas joué) ; `grids_interpolates` (x = 16, soit `u8 = 32`, cellule 0, `bal = 128` ; y = 0 : à un pas où `node_10` et `node_15` diffèrent, le niveau attendu `mix(mix(a, b, 128), mix(c, d, 128), 0)` est vérifié en réglant dens juste sous puis au-dessus du seuil) ; `grids_synth_voice_note` (piste synthé, voice 2, note 67 : seules les notes 67 aux pas où la voix 2 de la carte joue) ; `grids_chaos_perturbs_at_loop_start` (chaos 127 : `perturb` ne change qu'à k = 0, 32, 64…).
- [ ] **Step 2:** `tests/run_tests.sh` → échec (gen.c absent).
- [ ] **Step 3:** `map.h` = les 25 tableaux `node_*` de `grids/resources.cc` (`static const uint8_t`, `PROGMEM` retiré) sous l'en-tête GPL d'origine ; `gen.c` (en-tête GPL-3.0-or-later qui cite Grids et Marbles) ; `LICENSE`, `README.md`, `words.tsv`.
- [ ] **Step 4:** `tests/run_tests.sh` → `all tests ok` ; `./build.sh` → image construite.
- [ ] **Step 5:** commit `GRIDS: Mutable's drum map generator, per step; QUARTZ is GPL-3.0`.

---

### Task 3: MARBLES (gen.c)

**Files:**
- Modify: `app/gen.h`, `app/gen.c`
- Create: `app/third_party/marbles/tables.h`
- Test: `tests/unit/gen_test.c`

**Interfaces:**
- Produces (`gen.h`) :
  ```c
  extern const uint16_t SCALE_MASK[11];   /* bit n: n semitones above the tonic; CHROM..MIN PENTA, the order of ui.c MODE_NAME */
  uint16_t gen_allowed(const q_project *p, const q_pattern *pt);  /* absolute pitch classes; notes 0: the scale from the tonic */
  int gen_quantize(int note, uint16_t allowed, int tonic, int steps);  /* 0..127, in the notes STEPS keeps */
  ```
  `SCALE_MASK` : CHROM `0xFFF` ; MAJOR {0,2,4,5,7,9,11} ; MINOR {0,2,3,5,7,8,10} ; DORIAN {0,2,3,5,7,9,10} ; PHRYG {0,1,3,5,7,8,10} ; LYDIAN {0,2,4,6,7,9,11} ; MIXO {0,2,4,5,7,9,10} ; LOCRIAN {0,1,3,5,6,8,10} ; HARM MIN {0,2,3,5,7,8,11} ; PENTA {0,2,4,7,9} ; MIN PENTA {0,3,5,7,10}.
- `q_gen` reçoit l'état Marbles : deux séquences de déjà vu (T et X) `{ float loop[16]; int head, step; }`, `markov[16]`, `markov_ptr`, `streak[2]`, `drum_step`, `drum_index`, `div_left`, `div_pat`, `div_n` (rang dans le motif de division). `gen_reset` remplit `loop` de hasard et remet le reste à zéro (`drum_step = 8` : le premier pas tire un motif).
- Déjà vu (port de `RandomSequence::NextValue` hors mode registre ; `loop` contient des valeurs 0..1) : `p = (2d − 1)²`, `d = dejavu / 127` ; mutation si `u < p` ; mutation et `d ≤ 0.5` : écrire une valeur neuve à `head`, `head = (head + 1) % 16`, `step = len − 1` ; mutation et `d > 0.5` : `step` tiré dans `0..len−1` ; sinon `step = (step + 1) % len`. Résultat `loop[(head + 16 − len + step) % 16]`. Vecteur T : la valeur donne la graine d'un LCG `w = w * 1664525 + 1013904223` qui fournit `u[0], u[1], p, jitter` (comme `NextVector`).
- Modèles T (port de `TGenerator`, `bias = bias / 127`) : bit 0 = t1, bit 1 = t3, t2 joue à chaque pas.
  - `COMPLEMENTARY_BERNOULLI`, `INDEPENDENT_BERNOULLI`, `THREE_STATES`, `DRUMS` (table `drum_patterns[18][8]`), `MARKOV` (probabilité `1 / (1 + expf(−logit))` au lieu de `lut_logit`) : comme l'original.
  - `DIVIDER` : motif `fixed_divider_patterns[min(16, (int)(bias * 17))]` ; `CLUSTERS` : `divider_patterns[(int)(u * 17)]` avec le calcul de `u` de l'original, ratios échangés si `bias < 0.5`. Un ratio `p/q` donne au pas n du motif `⌊(n+1)p/q⌋ − ⌊np/q⌋` impulsions : 0 pas joué, sinon ce nombre en ratchet (borné à 4). Le motif se relit quand `div_left` tombe à 0.
  - JITTER : `micro = round((2 · jitter_u − 1) · J⁴ · 11)`, `J = jitter / 127`.
- Section X (port de `OutputChannel::GenerateNewVoltage` hors mode registre) : `u` de la séquence X ; `v = BetaDistributionSample(u, spread, xbias)` puis les mélanges « degenerate » et « bernoulli » de l'original, `spread = spread / 127`, `xbias = xbias / 127` ; note = `gen_quantize(note + round(v * range * 12), gen_allowed(...), tonic, steps)`.
- `gen_quantize` : notes gardées = les permises triées par intervalle depuis la tonique dans l'ordre 0, 7, 4, 3, 10, 11, 2, 5, 9, 8, 1, 6, les `n − steps * (n − 1) / 127` premières ; la plus proche (à égalité, la plus grave), dans 0..127.
- Sorties : KIT : t1, t2, t3 jouent `KIT_NOTE + 0, 1, 2` ; synthé : la sortie `tout` seule, note X tirée seulement quand elle joue. `vel 100`.

- [ ] **Step 1: Tests** : `marbles_dejavu_mid_loops` (dejavu 64, len 5 : à partir du pas 5, la suite des notes et des déclenchements se répète tous les 5 pas) ; `marbles_dejavu0_varies` (dejavu 0 : sur 64 pas, au moins 2 boucles de 5 diffèrent) ; `marbles_notes_in_allowed` (notes = {C, E, G} : toute note jouée ∈ {C, E, G} et dans `[note, note + range·12]` à une note près) ; `quantize_steps_keeps_strongest` (MAJOR, tonique C : `steps 127` → seule la tonique ; `steps 0` → 7 notes) ; `quantize_top_clamps` (note 120, range 4, une seule note permise : résultat ≤ 127 et dans la note permise) ; `marbles_t2_every_step` (KIT : `KIT_NOTE + 1` à chaque pas) ; `marbles_drums_pattern` (MODE DRUMS, bias 64 : t1/t3 suivent `drum_patterns[0]`) ; `marbles_jitter_bounds` (jitter 127 : `|micro| ≤ 11`, jitter 0 : `micro == 0`) ; `marbles_divider_ratchets` (DIVIDER, bias 0 → `{8,1}/{1,8}` : t1 ratchet 4 à chaque pas, t3 un pas sur 8).
- [ ] **Step 2:** `tests/run_tests.sh` → échec.
- [ ] **Step 3:** `tables.h` = `distributions_table` et les `dist_icdf_*` dont il dépend (y compris `dist_icdf_4_3`), en `static const float`, sous l'en-tête MIT. L'image grossit d'environ 90 Ko (281 Ko aujourd'hui, limite 568 Ko) : noter la taille après `./build.sh`.
- [ ] **Step 4:** `tests/run_tests.sh` → `all tests ok`.
- [ ] **Step 5:** commit `MARBLES: T models, X section and deja vu from Mutable's Marbles, per step`.

---

### Task 4: Le séquenceur joue les générateurs

**Files:**
- Modify: `app/seq.h` (`#include "gen.h"`, `q_gen g[Q_TRACKS]` dans `q_seq`), `app/seq.c`
- Test: `tests/unit/seq_test.c` (inclut `gen.c` avant `seq.c` ; `audio_test.c` aussi)

**Interfaces:**
- Consumes : `gen_step`, `gen_loop`, `gen_reset` (tâches 2–3).
- `seq.c` :
  - `static int loop_len(const q_seq_track *tr, const q_pattern *pt)` : STEP → `xlen` si XY sinon `len` ; sinon `gen_loop(pt)`. Utilisée par le test de fin de boucle de `plan` et par la durée d'une entrée de chanson (`pt->len * u[div]` devient `loop_len`).
  - Dans `plan`, pattern génératif : `gen_step(&s->g[t], p, t, pt, tr->k, &s->rng, &g)` (appelé même muet, pour que l'état avance), le pas joué si `g.flags & SF_ON` et non muet, sans condition ni probabilité ; `h[1].step = tr->k`.
  - `plan_notes(..., int acc)` : la note `i` prend `vel + 32` (borné à 127) si `SF_ACCENT` ou `acc >> i & 1`. STEP passe 0.
  - `gen_reset(&s->g[t])` à `seq_start`, dans `reanchor`, et à la boucle quand le pattern change.

- [ ] **Step 1: Tests** : `grids_steps_on_grid` (piste GRIDS, dens 127, div 1/16 : chaque note-on tombe à l'échantillon d'un pas STEP de même rang, swing 60 compris) ; `grids_accent_velocity` (une voix accentuée → 127, les autres → 95, au même pas) ; `generator_pattern_change_at_loop_end` (pattern STEP → pattern GRIDS demandé au pas 3 : le changement a lieu au pas 16 du STEP ; GRIDS → STEP : au pas 32) ; `marbles_len_sets_loop` (len 5 : `seq_pos` 0..4 en boucle) ; `muted_generator_advances` (une piste GRIDS muette pendant 16 pas puis démutée joue le pas 16 de la carte) ; `source_change_mid_play` (src passe de STEP à MARBLES len 3 au rang 10 : rang suivant 0, chaque note-on a son note-off) ; `song_entry_counts_loop` (scène avec un pattern GRIDS 1/16 : l'entrée dure 2 mesures).
- [ ] **Step 2:** `tests/run_tests.sh` → échec.
- [ ] **Step 3:** l'intégration ci-dessus.
- [ ] **Step 4:** `tests/run_tests.sh` → `all tests ok`.
- [ ] **Step 5:** commit `SEQ: GRIDS and MARBLES patterns planned like STEP steps`.

---

### Task 5: KIT dans le rendu

**Files:**
- Modify: `app/audio.c`
- Test: `tests/unit/audio_test.c`

**Interfaces:**
- `audio.c` :
  - `static int note_model(int track, int note)` : KIT → `21 + kit[track][note − KIT_NOTE].engine`, sinon `sound[track].model` ; `static int note_poly(int track)` : KIT → 1, sinon `poly`. `needs_switch`, `note_on`, `start_later` passent par elles (la note est connue à chaque appel).
  - `wanted()` et `spare_slot()` : une piste KIT veut 1 voix de chaque moteur utilisé par ses pads (`want[21..23]`), aucune pour le modèle 24.
  - `set_patch` d'une voix KIT : `harm`, `timb` du pad ; `morph = decay` du pad ; `decay 0.5`, `glide 0`. Hauteur `48 + pad.tune + sound.tune`. Gain : `level/127 · pad.level/127 · 0.25`.
  - Une note de KIT hors 36..51 est ignorée.

- [ ] **Step 1: Tests** : `kit_choke_same_engine` (pads CH puis OH : même voix, note-on puis la voix porte OH) ; `kit_three_engines_together` (BD, SN, CH au même bloc : 3 voix distinctes, moteurs 21, 22, 23) ; `kit_pad_patch` (pad 5 RIM : `pq_set_patch` reçoit `100/127, 100/127, 10/127`, note `48 + 12`) ; `kit_engine_change_while_sounding` (pad 1 sonne, son moteur passe à SN, pad 1 rejoué : la voix prend le moteur 22, chaque note-on a son note-off, aucune voix V_ON après le note-off) ; `kit_prepares_its_engines` (piste 0 en KIT au démarrage : `audio_prepare` prépare 21, 22, 23 avant le premier PLAY ; aucun changement de moteur dans le rendu au premier pas).
- [ ] **Step 2:** `tests/run_tests.sh` → échec.
- [ ] **Step 3:** les fonctions ci-dessus.
- [ ] **Step 4:** `tests/run_tests.sh` → `all tests ok`.
- [ ] **Step 5:** commit `Audio: the KIT's pads on their Plaits drum engines, one voice per engine`.

---

### Task 6: Pages SEQ par source

**Files:**
- Modify: `app/ui.c`, `app/words.tsv`
- Create: `app/tests/grids.txt`, `app/tests/marbles.txt`

**Interfaces:**
- `ui.c` :
  - Pages nouvelles : `P_GRIDS (DIV, VOICE, NOTE, SOURCE)`, `P_MAP (X, Y, CHAOS)`, `P_DENS (DENS 1, DENS 2, DENS 3)`, `P_MARBLES (DIV, T OUT, LEN, SOURCE)`, `P_T (MODE, BIAS, JITTER, DEJA VU)`, `P_X (SPREAD, X BIAS, STEPS)`, `P_NOTES (NOTE, RANGE)`. `P_STEPS` gagne `SOURCE` en K4. Le texte affiché est sans accents (`DEJA VU`).
  - `DOMAIN_PAGES` devient `static int pages(int d, uint8_t *out)` (nombre de pages, ids dans `out`) : SEQ selon `pat()->src` (STEP : `P_STEPS`, `P_XY` si XY ; GRIDS : 3 ; MARBLES : 4 sur une piste synthé, `P_MARBLES, P_T` sur un KIT) ; EDIT : inchangé ici (tâche 7). `domain_n` et `page_id` passent par elle.
  - `static int knob_hidden(int pid, int k)` : VOICE, NOTE (GRIDS) et T OUT sur un KIT. `knob_turn` les ignore, le pied ne les dessine pas.
  - SOURCE change `pat()->src` (les réglages et les pas restent).
  - Sur un pattern génératif, dans SEQ : les blanches jouent la piste (pas de pas tenu) ; SEQ maintenu + blanches 1–8 garde les patterns ; sur `P_NOTES`, une touche bascule le bit `(note % 12)` de `marbles.notes`, partant de `gen_allowed` si `notes` vaut 0, la dernière note permise restant. 
  - REC : `rec_write`, la saisie pas à pas et `rec_lock` ne font rien quand le pattern visé n'est pas STEP.
  - Écran : sur une page de générateur, la scène montre le nom de la source et le rang joué (`pos + 1 / boucle`) ; sur `P_NOTES`, les 12 notes de l'octave (nom selon `note_name`), permises en clair, les autres en gris. `q_view` reçoit ce qu'il faut pour redessiner à un changement.
  - `ui_expect` : `src`, `gx`, `gdens1`, `mlen`, `mnotes` (le masque), `mdejavu`.

- [ ] **Step 1: Scripts.** `grids.txt` : SEQ, `turn K4 1` → `expect src 1`, `expect page 1`, SELECT → `expect page 2`, `turn K1 10` → `expect gx 74` ; page 3, `turn K1 -64` → `expect gdens1 0` ; PLAY, `expect pos` ≥ 0 ; REC armé, une blanche tenue puis relâchée : `expect stepon_1 0` (le pas 1 reste éteint) ; PLAY stop. `marbles.txt` : `turn K4 2` → `expect src 2` ; 4 pages (`turn SELECT 3` → `expect page 4`) ; sur NOTES, touche D (`key` de D) → `expect mnotes 2737` (C majeur `0xAB5` sans D) ; toutes les notes coupées une à une → `mnotes` garde une note ; `turn K4 -2` sur la page 1 → `expect src 0`, les pas d'avant sont là (`expect stepon_1 1` si allumé avant).
- [ ] **Step 2:** `tests/run_tests.sh` → échec sur `grids.txt`.
- [ ] **Step 3:** l'interface ci-dessus ; les mots dans `words.tsv`.
- [ ] **Step 4:** `tests/run_tests.sh` → `all tests ok` ; capture `shot` des pages GRIDS et NOTES vérifiée à l'œil.
- [ ] **Step 5:** commit `UI: SEQ pages per source, MARBLES allowed notes, REC on STEP patterns only`.

---

### Task 7: Pages et clavier du KIT

**Files:**
- Modify: `app/ui.c`, `app/words.tsv`
- Create: `app/tests/kit.txt`

**Interfaces:**
- `ui.c` :
  - Pages `P_PAD (TUNE, HARM, TIMB, DECAY)` et `P_KIT (MODEL, ENGINE, LEVEL)` ; `pages(D_EDIT)` les donne quand le modèle est `M_KIT`. Elles règlent `kit[track][ui.pad]` ; l'en-tête affiche `PAD n`.
  - `ui.pad` : le dernier pad joué ou tenu (0 par défaut). `ui.padheld` : la touche du pad tenu + 1, 0 aucun.
  - Clavier sur un KIT : blanche w → `audio_live_note(track, KIT_NOTE + w, …)`, `ui.pad = w`, `last_note = KIT_NOTE + w` ; les noires ne jouent rien.
  - Dans EDIT, pad tenu : K1–K4 règlent `P_PAD` de ce pad, PRESET règle son moteur (0..2) ; sans pad tenu, PRESET reste MODEL.
  - SEQ, pas tenu + blanche w sur un KIT : ajoute ou retire `KIT_NOTE + w` des notes du pas (4 au plus ; retirer la dernière éteint le pas) ; un pas vide activé d'un appui prend `last_note` (pad 1 par défaut, `KIT_NOTE`). Les p-locks d'un pas KIT sont refusés (K1–K4 n'ont pas d'effet).
  - `ui_expect` : `pad` (1..16), `padtune_N`, `padengine_N` (N = pad 1..16), `stepnotes_N` (nombre de notes du pas N).
  - Un pad touché dans un pas tenu devient aussi `ui.pad` et `last_note`.
  - Changer le modèle vers ou depuis KIT passe par `audio_track_model`, comme aujourd'hui.

- [ ] **Step 1: Script `kit.txt`** : EDIT, PRESET jusqu'à KIT → `expect model 24`, `expect page 1` ; blanche 4 tenue + `turn K1 3` → `expect padtune_4 3`, `expect pad 4` ; blanche 4 tenue + `turn PRESET -2` → `expect padengine_4 0` ; relâchée, `turn K2 1` règle le HARM du pad 4 ; SEQ, pas 1 tenu + blanches 1 et 3 → `expect stepnotes_1 2` ; blanche 1 de nouveau → `expect stepnotes_1 1` ; appui simple sur le pas 2 vide → `expect stepon_2 1`, `expect stepnote_2 36` (dernier pad joué : 1) ; PLAY : `expect active` ≤ 3 sur 2 mesures.
- [ ] **Step 2:** `tests/run_tests.sh` → échec sur `kit.txt`.
- [ ] **Step 3:** l'interface ci-dessus ; les mots (`PAD`, `KIT`, `ENGINE`, `LEVEL`, `BD`, `SN`, `HH`).
- [ ] **Step 4:** `tests/run_tests.sh` → `all tests ok` ; capture des pages PAD et KIT.
- [ ] **Step 5:** commit `UI: KIT pages, pads edited while held, pads in steps`.

---

### Task 8: Appareil et mesures

**Files:**
- Modify: `docs/cpu.md`, `app/seq.c` (valeurs du kit par défaut, si l'écoute le demande)

- [ ] **Step 1:** `./build.sh`, installer (`fm1_install --yes --classic`). Noter la taille de l'image.
- [ ] **Step 2 (utilisateur) :** écouter le kit par défaut, pad par pad ; retoucher les valeurs du tableau de la tâche 1 au besoin.
- [ ] **Step 3 (utilisateur) :** pire cas 60 s : piste 1 KIT en GRIDS (dens hautes, chaos), piste 2 MARBLES synthé, pistes 3–4 MODAL/ADDITIVE POLY ; page CPU : LOST BLOCKS reste 0, charges des deux cœurs relevées.
- [ ] **Step 4:** `docs/cpu.md` : section « Étape 4 » avec ces chiffres et la taille de l'image.
- [ ] **Step 5:** commit `Step 4: device measurements`.

---

## Hors périmètre

Scène 3D GRIDS (étape 5) ; p-locks sur les réglages des générateurs et sur un KIT ; enregistrement sur une piste générative ; modes euclidien et bandes de Grids ; MARBLES polyphonique ; accent sur ENV AMT (étape 3) ; 4e voix de KIT ; voyants des notes permises sur le clavier.
