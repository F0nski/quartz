# QUARTZ étape 3c (punch-ins) — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** un firmware QUARTZ installable où FX maintenu + les 16 blanches jouent 16 effets momentanés sur le master, enregistrés au pas en overdub et rejoués avec le pattern.

**Architecture :** `qfx_block` reçoit un masque de 16 bits et la position dans la mesure, et applique les punch-ins entre les retours d'effets et le compresseur (DELAY THROW et REVERB FREEZE en amont, sur les envois). `audio.c` forme le masque à chaque bloc : touches tenues (`audio_punch`) OU locks de punch du pas en cours de chaque piste non mutée. `ui.c` tient le masque sous FX et l'écrit dans deux locks spéciaux, `LK_PUNCH_LO` et `LK_PUNCH_HI`, en overdub.

**Tech Stack :** C (gnu11, sans libc dans l'app), C++ pour `qfx` ; stmlib (`Svf`), Plaits (`SampleRateReducer`) ; fm1-base (simulateur hôte) ; tests `tests/run_tests.sh`.

**Spec :** `docs/superpowers/specs/2026-11-11-quartz-step3c-punchins-design.md` (et la spec globale, section 4.4).

## Global Constraints

- Identité git `f0nski <13540044+F0nski@users.noreply.github.com>`, branche `step3c-punchins` ; pas de ligne `Co-Authored-By`.
- Code, commentaires et commits en anglais ; pas de libc dans le code C de l'app ; aucune fonction de libm par échantillon dans `qfx` (fastmath de fm1-base : `fm_expf`, `fm_exp2f`, `fm_sinf`…), coefficients de filtre recalculés au plus tous les 16 échantillons.
- Masque : bit `k` = touche blanche `k + 1` (0..15). Numéros d'effet de la spec : bit 0..3 LOOP 1/8, 1/12, 1/16, 1/32 ; 4 TAPE STOP ; 5 LOW-PASS ; 6 HIGH-PASS ; 7 SWEEP ; 8 BITCRUSH ; 9 DOWNSAMPLE ; 10 GATE ; 11 AUTO-PAN ; 12 DELAY THROW ; 13 REVERB FREEZE ; 14 STUTTER ; 15 STUTTER GATE.
- Masque nul : `qfx_block` rend exactement le signal d'avant l'étape.
- Entrée et sortie de chaque effet en fondu linéaire sur un bloc ; coupures du GATE et du STUTTER GATE avec des rampes de 2 ms.
- Tampon de boucle : `int16_t lbuf[2][8192]` dans `.pool` (32 Ko, 0,371 s à 22 058 Hz), même échelle que le delay (`kDlyScale`).
- `Q_VERSION` reste 5 ; `LK_PUNCH_LO = LK_N`, `LK_PUNCH_HI = LK_N + 1`.
- Le rendu ne fait que lire `q_project` ; la boucle principale est seule à l'écrire.
- Zéro bloc perdu dans le pire cas de l'étape sur l'appareil (tâche 5).

## Review Focus

1. Tempo changé ou STOP pendant qu'un LOOP ou un STUTTER est tenu : la lecture reste dans le tampon, sortie finie, pas de saut fort ; un STOP en lecture passe à la phase libre (test `loop_survives_tempo_and_stop`, tâche 2).
2. Toutes les touches tenues ensemble, 10 s, sur un bruit plein échelle avec envois et FB au maximum : sortie finie, |x| ≤ 1 (test `all_punches_bounded`, tâche 2).
3. Appuis et relâchés à chaque bloc (doigts qui tremblent) sur LOOP et FREEZE : pas de NaN, la reverb n'est pas laissée figée après le relâché (test `punch_chatter`, tâche 2).
4. FX relâché avant les touches, ou touche relâchée après FX : aucun punch-in ne reste collé, aucune note ne part au relâché de la touche (script `punch.txt`, tâche 4).
5. Pattern changé ou scène lancée pendant la relecture d'un punch-in enregistré : il s'arrête avec le pas de son pattern ; locks pleins : rien d'écrit, message affiché (tests `recorded_punch_follows_pattern` tâche 3, script `punch.txt` tâche 4).

---

## Structure des fichiers

```
app/project.h               LK_PUNCH_LO, LK_PUNCH_HI
app/seq.c, app/seq.h        seq_bar_frames
app/dsp/qfx.h, qfx.cc       qfx_master + punch, fpb, bpos ; les 16 punch-ins
app/audio.c, app/audio.h    audio_punch, masque du bloc, position dans la mesure
app/ui.c, app/words.tsv     FX maintenu + blanches, enregistrement, noms
tests/unit/qfx_test.cc, audio_test.c, seq_test.c
app/tests/punch.txt         (nouveau)
docs/cpu.md                 mesures
```

---

### Task 1: qfx — interface, fondus et les effets sans tampon

**Files:**
- Modify: `app/dsp/qfx.h`, `app/dsp/qfx.cc`
- Test: `tests/unit/qfx_test.cc`

**Interfaces:**
- Produces (`qfx.h`) :
  ```c
  /* punch: the punch-ins held, bit k = white key k + 1. fpb: frames per beat; bpos: frames since the bar's
     first beat at the block's start, 0 <= bpos < 4 * fpb (the grid of LOOP, GATE, AUTO-PAN, the STUTTERs) */
  typedef struct { float dly; uint8_t dfb, rsize, rtone, comp, duck; uint16_t punch; float fpb, bpos; } qfx_master;
  ```
  `audio.c` passe `punch = 0`, `fpb` et `bpos` à 0 dans cette tâche (son initialiseur positionnel est complété pour compiler).
- Valeurs :
  - fondu : un gain `w[k]` par effet, rampé de sa valeur de fin du bloc précédent à 0 ou 1 sur le bloc ; `out = x + w · (fx(x) − x)` ; un effet à `w` nul sur tout le bloc n'est pas calculé ;
  - LOW-PASS : `stmlib::Svf` passe-bas, 400 Hz, Q 1,2 ; HIGH-PASS : passe-haut, 1,5 kHz, Q 0,8 (un filtre par canal) ;
  - SWEEP : `Svf` passe-bas Q 6, coupure `200 · 40^|2u − 1|` Hz (8 kHz à `u = 0`, 200 Hz à `u = 0,5`, 8 kHz à `u = 1`), avec `u` = frames depuis l'appui / `4 · fpb`, modulo 1 (interpolation exponentielle) ;
  - BITCRUSH : `round(x · 32) / 32` (6 bits) ;
  - DOWNSAMPLE : `plaits::SampleRateReducer` par canal, fréquence `4000 / SR` ;
  - GATE : gain 0 sur la seconde moitié de chaque 1/16 de la grille (`bpos`, `fpb / 4`), rampes de 2 ms ;
  - AUTO-PAN : `p = sin(2π · beatpos)` où `beatpos` = position dans le temps ; `L ×= p > 0 ? 1 − p : 1`, `R ×= p < 0 ? 1 + p : 1` ;
  - DELAY THROW : `dsum += 0.5 · (l + r)` (le mix des pistes avant les retours) pondéré par `w` ; réinjection `max(0.95 · dfb / 127, 0.6)` pendant `w > 0` ;
  - REVERB FREEZE : pendant les `fpb / 4` premières frames après l'appui, envoi reverb `+= 0.5 · (l + r)` ; ensuite envoi reverb nul et `set_time(1.0)` dans `qfx_reverb` ; au relâché, entrée et temps du projet ; `qfx_reverb` ne se met pas en sommeil tant que le gel est actif ;
  - ordre : envois (THROW, FREEZE) → delay et retours → [tampon, tâche 2] → GATE → BITCRUSH → DOWNSAMPLE → LOW-PASS → HIGH-PASS → SWEEP → AUTO-PAN → compresseur → limiteur.
  - « frames depuis l'appui » : compteur par effet, remis à 0 quand son bit passe de 0 à 1.

- [ ] **Step 1: Tests** (`qfx_test.cc`) :
  - `no_punch_is_identity` : réglages par défaut, `punch = 0` : `L` et `R` identiques bit à bit à ceux d'un `qfx_master` sans les nouveaux champs (signal de référence enregistré avant la modification, ou même calcul avec `punch = 0` et `fpb` quelconque : `memcmp` égal) ;
  - `each_punch_changes_signal` : pour les bits 5–13, tenus 0,5 s sur une scie à 220 Hz + bruit (fpb 22059) : différence RMS avec la référence > 0,01 (THROW et FREEZE : envois à 0, le changement vient du punch-in) ;
  - `punch_fades_in` : LOW-PASS appuyé : sur le premier bloc, |out − ref| croît de 0 au premier échantillon ; aucun saut entre échantillons > 0,05 sur un sinus 0,5 à 100 Hz à l'appui et au relâché, pour chaque bit 5–13 ;
  - `gate_on_grid` : GATE, fpb 22059, bpos 0 : RMS de `[0, fpb/8)` > 0,1, RMS de `[fpb/8 + 100, fpb/4 − 100)` < 1e-4 ;
  - `delay_throw_tails` : THROW tenu 0,25 s puis relâché, entrée nulle ensuite : énergie non nulle à 0,6 s (DLY FB 0) ;
  - `freeze_holds_tail` : FREEZE tenu 2 s, entrée coupée à 0,2 s : énergie de la reverb à 1,9 s > 0,5 × celle à 0,3 s ; relâché : sous −60 dB 4 s plus tard (REV SIZE 0).
- [ ] **Step 2:** `tests/run_tests.sh` → échec.
- [ ] **Step 3:** les champs et les effets ci-dessus dans `qfx_block` et `qfx_reverb`.
- [ ] **Step 4:** `tests/run_tests.sh` → `all tests ok` ; `./build.sh` → image construite.
- [ ] **Step 5:** commit `QFX: punch-in mask, fades, filters, crush, gate, auto-pan, delay throw, reverb freeze`.

---

### Task 2: qfx — tampon de boucle : LOOP, TAPE STOP, STUTTER

**Files:**
- Modify: `app/dsp/qfx.cc`
- Test: `tests/unit/qfx_test.cc`

**Interfaces:**
- Consumes : `qfx_master.punch`, `fpb`, `bpos`, les fondus (tâche 1).
- Produces : rien de nouveau dans `qfx.h` ; `qfx_init` met `lbuf` à zéro.
- Comportement :
  - le mix (après retours, avant GATE) est écrit en continu dans `lbuf` à demi-cadence (moyenne de paires), sauf pendant qu'un LOOP ou un STUTTER relit une tranche dont l'écriture est terminée ;
  - effet lecteur du bloc, par priorité : bit 4 TAPE STOP, puis 15, puis 14, puis le plus haut des bits 0–3 tenus ; un seul à la fois ;
  - LOOP : longueur `L` : 1/8 = `fpb / 2`, 1/12 = `fpb / 3`, 1/16 = `fpb / 4`, 1/32 = `fpb / 8`, divisée par deux tant que `L > 2 · 8192 − 2` ; début `g` = dernier trait de la grille de `L` (depuis `bpos`) à l'appui ; tête de lecture `g + ((t − g) mod L)` : à l'appui elle est sur le direct, puis la tranche se répète ;
  - passage d'un lecteur à un autre pendant que l'écriture est arrêtée : même début `g`, nouvelle longueur (roll) ; sinon nouveau `g` ;
  - STUTTER (bit 14) : `g` comme un LOOP 1/8 ; longueur `fpb / 2`, `/4`, `/8`, `/16` selon le nombre de temps entiers depuis l'appui (0, 1, 2, ≥ 3) ;
  - STUTTER GATE (bit 15) : `g` sur la grille de `fpb / 16` ; longueur `fpb / 16` ; seconde moitié de chaque répétition à 0, rampes de 2 ms ;
  - TAPE STOP (bit 4) : l'écriture continue ; vitesse de lecture de 1 à 0 linéairement en `T = min(fpb, 0,7 s)`, lecture interpolée ; silence ensuite ;
  - lecture interpolée linéairement à 44,1 kHz ; fondu d'un bloc entre direct et lecteur à l'appui, au relâché et au changement de lecteur.

- [ ] **Step 1: Tests** (`qfx_test.cc`), entrée une rampe ou un compteur reconnaissable, fpb 22059 (120 BPM) :
  - `loop_no_jump_on_press` : LOOP 1/16 appuyé à `bpos = fpb/8` : pendant `fpb/8` après l'appui, la sortie suit l'entrée (à la précision de la demi-cadence, erreur < 0,02) ;
  - `loop_repeats_grid_slice` : même appui ; après `fpb/4 − fpb/8`, la sortie reprend le contenu de `g = 0` : corrélation entre `out[g + L + i]` et `in[g + i]` > 0,95 ;
  - `loop_halves_to_fit` : LOOP 1/8 à 40 BPM (fpb 66176) : longueur relue `≤ 16382` frames et égale à `33088 / 2^k` ;
  - `roll_keeps_start` : LOOP 1/8 tenu, puis LOOP 1/32 ajouté : la tranche répétée commence au même `g` ;
  - `priority` : bits 0, 4, 14 tenus : TAPE STOP joue (sortie qui ralentit puis silence) ; bits 0 et 14 : STUTTER ;
  - `stutter_accelerates` : STUTTER tenu 4 temps : périodes mesurées par autocorrélation ≈ fpb/2, fpb/4, fpb/8, fpb/16 (± 2 %) sur les temps 1 à 4 ;
  - `stutter_gate_chops` : STUTTER GATE : RMS de la seconde moitié de chaque `fpb/16` < 1e-3 ;
  - `tape_stop_to_silence` : sinus 1 kHz, TAPE STOP : fréquence apparente < 500 Hz à `T/2 + 10 %`, silence après `T` ;
  - `loop_survives_tempo_and_stop` : LOOP 1/8 tenu, fpb alterné entre 22059 et 66176 chaque bloc, puis `fpb` de la phase libre : fini, |x| ≤ 1 ;
  - `all_punches_bounded` : `punch = 0xFFFF`, bruit plein échelle, envois 127, DLY FB 127, REV SIZE 127, 10 s : fini, |x| ≤ 1 ;
  - `punch_chatter` : bits 0 et 13 basculés à chaque bloc pendant 2 s puis relâchés : fini ; 4 s après, la reverb est sous −60 dB (REV SIZE 0).
- [ ] **Step 2:** `tests/run_tests.sh` → échec.
- [ ] **Step 3:** le tampon et les quatre lecteurs.
- [ ] **Step 4:** `tests/run_tests.sh` → `all tests ok` ; `./build.sh` → image construite (le pool tient : noter la marge restante).
- [ ] **Step 5:** commit `QFX: loop buffer, LOOP, TAPE STOP, STUTTER and STUTTER GATE`.

---

### Task 3: Rendu — masque du bloc et position dans la mesure

**Files:**
- Modify: `app/project.h`, `app/seq.h`, `app/seq.c`, `app/audio.h`, `app/audio.c`
- Test: `tests/unit/seq_test.c`, `tests/unit/audio_test.c` (le double de `qfx_block` copie aussi le `qfx_master`)

**Interfaces:**
- Consumes : `qfx_master.punch`, `fpb`, `bpos` (tâche 1).
- Produces :
  ```c
  /* project.h */
  enum { LK_PUNCH_LO = LK_N, LK_PUNCH_HI };   /* a step's punch-ins, not q_sound fields: white keys 1-8 and 9-16, bit k = key k + 1 */
  /* seq.h: frames since the bar's first beat at the block's start, -1 when stopped or counting in */
  float seq_bar_frames(const q_seq *s);
  /* audio.h: the punch-ins held (bit k = white key k + 1), from the next block on */
  void audio_punch(uint16_t mask);
  ```
- `audio.c` :
  - masque du bloc = `punch_held` OU, pour chaque piste `t` non mutée (`seq.mute`) avec `seq_pos(&seq, t) >= 0`, les locks `LK_PUNCH_LO` (bits 0–7) et `LK_PUNCH_HI` (bits 8–15) du pas `seq_pos` du pattern `seq_pattern` ;
  - `fpb` : `seq_step_ratio(prj->tempo, DIV_4)` (num / den) ;
  - `bpos` : `seq_bar_frames` en lecture ; sinon une phase libre : remise à 0 quand le masque passe de 0 à non nul, avancée de `n` à chaque bloc, modulo `4 · fpb`.
  - `take_locks` et `fx_take` ignorent déjà les paramètres ≥ `LK_N` (vérifier, sans changer leur comportement).

- [ ] **Step 1: Tests**
  - `seq_test.c` · `bar_frames` : 120 BPM, après 3 blocs de 256 : `seq_bar_frames` = 768 ± 1 ; après 4 temps exacts, revient près de 0 ; à l'arrêt et pendant un décompte : −1 ;
  - `audio_test.c` :
    - `held_punch_reaches_qfx` : `audio_punch(0x0021)` : `m.punch == 0x0021` au bloc suivant ; `audio_punch(0)` : 0 ;
    - `recorded_punch_on_its_step` : lock `LK_PUNCH_HI` = 0x04 au pas 2 (pas vide) de la piste 1 en lecture : `m.punch == 0x0400` pendant le pas 2 seulement ;
    - `muted_track_no_punch` : même lock, piste 1 mutée : 0 ;
    - `recorded_punch_follows_pattern` : lock au pattern 1 ; passage au pattern 2 au début de la mesure : plus de punch ;
    - `held_and_recorded_or` : `audio_punch(0x0001)` + lock 0x04 : `0x0401` ;
    - `free_phase_when_stopped` : arrêt, `audio_punch(1)` : `bpos` 0 au premier bloc, puis 256, 512… ; `fpb` = 22058,8 ± 0,5 à 120 BPM.
- [ ] **Step 2:** `tests/run_tests.sh` → échec.
- [ ] **Step 3:** les changements ci-dessus.
- [ ] **Step 4:** `tests/run_tests.sh` → `all tests ok`.
- [ ] **Step 5:** commit `Audio: the punch-ins held and recorded reach qfx with the bar's position`.

---

### Task 4: Interface — FX maintenu + blanches, enregistrement

**Files:**
- Modify: `app/ui.c`, `app/words.tsv`
- Create: `app/tests/punch.txt`

**Interfaces:**
- Consumes : `audio_punch`, `LK_PUNCH_LO`, `LK_PUNCH_HI` (tâche 3) ; `lock_set`, `lock_find`.
- `ui.c` :
  - `ui.fxdown` (comme `seqdown`) et `ui.punch` (masque tenu) ;
  - FX maintenu : une blanche `w` met le bit `w`, son relâché l'enlève ; aucune note ne part ni ne s'arrête (une touche appuyée hors FX puis relâchée sous FX garde sa note-off normale) ; relâcher FX met `ui.punch` à 0 ; chaque changement appelle `audio_punch` ; les noires ne font rien sous FX ;
  - nom du dernier punch-in appuyé dans la ligne de message (`msg_until`) : `LOOP 1/8`, `LOOP 1/12`, `LOOP 1/16`, `LOOP 1/32`, `TAPE STOP`, `LOW-PASS`, `HIGH-PASS`, `SWEEP`, `BITCRUSH`, `DOWNSAMPLE`, `GATE 1/16`, `AUTO-PAN`, `DELAY THROW`, `FREEZE`, `STUTTER`, `STUT GATE` ;
  - enregistrement, à chaque tour de boucle : si REC armé, lecture, `ui.punch` non nul, `pos = audio_pos(ui.track) >= 0`, pattern STEP, `audio_pattern(ui.track) == ui.p->pattern[ui.track]` (règles de `rec_lock`, sans exiger `SF_ON`) : OU des bits dans `LK_PUNCH_LO` / `LK_PUNCH_HI` du pas `pos`, seulement pour la moitié non nulle et seulement si la valeur change ; échec de `lock_set` : `locks_until` comme `rec_lock` ;
  - `ui_expect` : `punch` (masque tenu) ; `lock_P_N` accepte `P` jusqu'à `LK_PUNCH_HI`.
- `words.tsv` : les 16 noms ci-dessus.

- [ ] **Step 1: Script** `punch.txt` (blanche 1 = `key 0`, 6 = `key 9`, 13 = `key 21`, 16 = `key 26`) :
  - `press FX`, `key 0 down` → `expect punch 1` ; `key 9 down` → `expect punch 33` ; `key 0 up` → `expect punch 32` ; `release FX` → `expect punch 0` ; `key 9 up` → `expect punch 0`, aucune note (`expect voices 0` ou l'équivalent existant) ;
  - `press FX`, `key 21 down`, `shot punch_msg.png`, `key 21 up`, `release FX` ;
  - overdub : tempo 60, `tap REC`, `tap PLAY`, après le décompte, `press FX`, `key 26 down` pendant ~1 pas, `key 26 up`, `release FX` : `expect lock_31_N 128` sur le pas traversé, et le pas reste éteint (`expect stepon_N 0`) ;
  - effacement : stop, `tap SEQ`, pas N tenu + noire 9 : `expect lock_31_N -1` ;
  - locks pleins : pattern rempli de 128 locks, overdub d'un punch-in : aucun lock écrit, `expect locks_full 1` (ou le nom existant du message).
- [ ] **Step 2:** `tests/run_tests.sh` → échec sur `punch.txt`.
- [ ] **Step 3:** l'interface ci-dessus.
- [ ] **Step 4:** `tests/run_tests.sh` → `all tests ok` ; capture vérifiée à l'œil.
- [ ] **Step 5:** commit `UI: punch-ins under FX, recorded on the steps in overdub`.

---

### Task 5: Appareil et mesures

**Files:**
- Modify: `docs/cpu.md`

- [ ] **Step 1:** `./build.sh`, installer (`fm1_install --yes --classic`). Noter la taille de l'image et la marge du pool.
- [ ] **Step 2 (utilisateur) :** chaque punch-in sur un beat KIT + basse, en lecture ; LOOP 1/8 puis 1/32 ajouté (roll) ; STUTTER sur 4 temps ; TAPE STOP ; FREEZE après un accord avec REVERB ouvert ; à l'arrêt, punch-ins sur le clavier joué.
- [ ] **Step 3 (utilisateur) :** overdub d'un punch-in sur une mesure, relecture sans toucher, effacement d'un pas.
- [ ] **Step 4 (utilisateur) :** pire cas 60 s : le pire cas de 3b (6 MODAL, 2 PHASER, COMP, delay, reverb) avec LOOP 1/16, SWEEP, GATE et REVERB FREEZE tenus (enregistrés sur une mesure pour ne pas tenir les touches). LOST BLOCKS doit rester à 0 ; relever les charges des deux cœurs.
- [ ] **Step 5:** `docs/cpu.md` : ligne « Step 3c worst case ». Commit `Step 3c: device measurements`.
- [ ] **Step 6 (seulement si des blocs sont perdus) :** s'arrêter et le signaler avec les chiffres.

---

## Hors périmètre

Intensité réglable, encodeurs pendant l'appui ; écrire un punch-in au pas sans l'enregistrer, affichage dans SEQ ; punch-ins par piste ; résolution plus fine que le pas pour les punch-ins enregistrés.
