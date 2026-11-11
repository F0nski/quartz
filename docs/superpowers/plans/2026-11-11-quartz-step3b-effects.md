# QUARTZ étape 3b (inserts, envois, pan, compresseur) — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** un firmware QUARTZ installable où chaque piste a un insert, un niveau, un pan et deux envois (delay ping-pong, reverb Clouds), avec un compresseur-limiteur et un ducking sur la BD du KIT au master, tout verrouillable par pas sauf le type d'insert et GLO.

**Architecture :** les voix ne s'additionnent plus en un mix mono : chaque cœur rend les siennes dans 4 bus de piste. Après le dernier segment du bloc, `audio.c` résout les réglages d'effet de chaque piste (valeurs de la piste ou locks du pas en cours) et appelle `qfx_block`, qui fait insert, LEVEL, PAN, envois, delay, reverb, ducking, compresseur et limiteur, et rend un stéréo flottant ; `audio.c` y ajoute le clic puis MASTER. Tout le DSP des effets est dans `app/dsp/qfx.cc`, en C++ derrière une interface C, comme `pq`.

**Tech Stack :** C (gnu11, sans libc dans l'app), C++ pour `qfx` ; Plaits, stmlib et la reverb de Clouds (MIT) ; fm1-base (simulateur hôte) ; tests `tests/run_tests.sh`.

**Spec :** `docs/superpowers/specs/2026-11-11-quartz-step3b-effects-design.md` (et la spec globale, sections 4.3, 4.5, 5.2, 7).

## Global Constraints

- Identité git `f0nski <13540044+F0nski@users.noreply.github.com>`, branche `step3b-effects` (partie de `main`) ; pas de ligne `Co-Authored-By`.
- Code, commentaires et commits en anglais ; pas de libc dans le code C de l'app (builtins `__builtin_expf`, `__builtin_sinf`, `__builtin_memset`…).
- Valeurs 0..127 sauf : PAN −64..63 ; type d'insert 0..7 ; DLY TIME un `DIV_*`.
- Défauts piste : type OFF, K1–K3 64, MIX 127, DELAY 0, REVERB 0, PAN 0, LEVEL 100. Défauts projet : DLY TIME `DIV_8`, DLY FB 40, REV SIZE 80, REV TONE 64, COMP 0, DUCK 0.
- Un projet neuf rend, sous le seuil du limiteur, exactement le signal d'avant sur L et sur R (centre de PAN = plein niveau des deux côtés).
- Le rendu ne fait que lire `q_project` ; la boucle principale est seule à l'écrire. Le DSP ne produit jamais de NaN.
- Tampons d'effet dans le pool (`.pool`) : reverb 32 Ko (`FxEngine<16384, FORMAT_12_BIT>`), delay `int16_t[2][11264]` (0,51 s à 22 058 Hz, 44 Ko), insert `float[4][1024]` (16 Ko).
- `Q_VERSION` 5, sans conversion des anciens projets.
- Zéro bloc perdu dans le pire cas de l'étape sur l'appareil (tâche 6).

## Review Focus

1. Type d'insert changé (PRESET) pendant qu'une note tient, y compris vers et depuis COMB ou CHORUS dont le tampon contient l'ancien son : pas de NaN ni de claquement fort, l'état de l'insert repart de zéro (test `type_switch_resets_state`, tâche 2).
2. DLY TIME ou tempo changés pendant que des répétitions sonnent, et tempo 40 avec DLY TIME 1/1 : la lecture reste dans le tampon et le temps est divisé par deux jusqu'à tenir (tests `delay_time_halves_to_fit`, `delay_time_change_stays_finite`, tâche 3).
3. Tout au maximum (4 pistes pleines, envois 127, DLY FB 127, REV SIZE 127, COMP 127, DUCK 127) pendant 10 s : sortie finie, |x| ≤ 1 (test `master_extremes_bounded`, tâche 3).
4. STOP pendant qu'un lock d'effet est actif, ou note d'un pas en micro-timing négatif (avant sa grille) : au STOP le lock disparaît ; en micro négatif il tient jusqu'au pas suivant (tests `stop_clears_fx_locks`, `fx_lock_negative_micro`, tâche 4).
5. Piste passée en KIT ou en sortant pendant que DUCK est actif : elle quitte ou rejoint les pistes baissées au bloc suivant ; DUCK 0 ne change rien au signal (tests `duck_follows_kit_membership` tâche 4, `duck_zero_is_bypass` tâche 3).

---

## Structure des fichiers

```
app/project.h               q_sound + 8 octets, q_project + 6 octets, IT_*, LK_* (LK_N 30), Q_VERSION 5
app/seq.c, app/store.c      défauts et bornes des nouveaux champs
app/dsp/qfx.h, qfx.cc       (nouveaux) inserts, delay, reverb, ducking, compresseur, limiteur
app/third_party/clouds/dsp/fx/reverb.h   (nouveau) la reverb de Clouds, sur le FxEngine de Plaits
app/audio.c                 bus de piste par cœur, gain de voix sans LEVEL, locks d'effet par piste, ducking, qfx_block, stéréo
app/ui.c, app/words.tsv     domaine FX, GLO · TEMPO et GLO · FX, pages de p-lock INSERT et MIX
tests/unit/qfx_test.cc      (nouveau)
tests/unit/{store,audio}_test.c
app/tests/fx.txt (nouveau), glo.txt, plocks.txt
README.md                   Clouds parmi le code vendorisé
docs/cpu.md                 mesures (3a et 3b)
```

---

### Task 1: Projet v5

**Files:**
- Modify: `app/project.h`, `app/seq.c` (`project_defaults`), `app/store.c` (`clamp_project`)
- Test: `tests/unit/store_test.c`

**Interfaces:**
- Produces (`project.h`) :
  ```c
  #define Q_VERSION 5
  enum { LK_HARM, LK_TIMB, LK_MORPH, LK_DECAY, LK_TUNE, LK_GLIDE,
         LK_ATK, LK_DEC, LK_SUS, LK_REL, LK_FTYPE, LK_CUT, LK_RES, LK_ENVAMT,
         LK_SHAPE, LK_RATE, LK_DEPTH, LK_DEST, LK_AMODE, LK_AOCT, LK_ADIV, LK_AGATE,
         LK_I1, LK_I2, LK_I3, LK_IMIX, LK_DSEND, LK_RSEND, LK_PAN, LK_LEVEL, LK_N };
  enum { IT_OFF, IT_DRIVE, IT_FOLD, IT_CRUSH, IT_CHORUS, IT_PHASER, IT_RING, IT_COMB, IT_N };
  ```
  `q_sound` gagne à la fin : `uint8_t itype, i1, i2, i3, imix, dsend, rsend; int8_t pan;`.
  `q_project` gagne après `nsong` : `uint8_t dtime, dfb, rsize, rtone, comp, duck;`.
  `LK_PAN` est rangé en `uint8_t`, relu en `int8_t`.

- [ ] **Step 1: Tests** dans `store_test.c` :
  - `store_roundtrip_v5` : chaque nouveau champ modifié (piste 3 et projet), sauvegardé, rechargé : `memcmp` égal ;
  - `store_clamps_v5` : itype 9 → 7 ; i1 200 → 127 ; pan −100 → −64 ; dtime 20 → 8 ; comp 200 → 127 ;
  - `store_v4_gives_defaults` : chunks de `version = 4` → `store_load` rend −1 et les défauts de la Global Constraints ;
  - le test existant des défauts couvre les nouveaux champs.
- [ ] **Step 2:** `tests/run_tests.sh` → échec.
- [ ] **Step 3:** les champs, défauts et bornes ci-dessus.
- [ ] **Step 4:** `tests/run_tests.sh` → `all tests ok`.
- [ ] **Step 5:** commit `Project v5: track effect settings and the master's`.

---

### Task 2: qfx — les inserts

**Files:**
- Create: `app/dsp/qfx.h`, `app/dsp/qfx.cc`
- Test: `tests/unit/qfx_test.cc` (compilé par `run_tests.sh` comme `pq_test.cc` : il prend déjà tous les `app/dsp/*.cc`)

**Interfaces:**
- Produces (`qfx.h`, `extern "C"`) :
  ```c
  #define QFX_TRACKS 4
  void qfx_init(void);    /* every state cleared, the pool's buffers zeroed */
  /* track t's insert on n mono samples in place; type IT_*, p1..p3 and mix 0..127. A type other than the
     track's last one clears its state first. IT_OFF or mix 0: buf untouched */
  void qfx_insert(int t, int type, int p1, int p2, int p3, int mix, float *buf, unsigned n);
  ```
- Valeurs (SR = 44 117,647 Hz ; `e(a, b, x) = a · (b/a)^(x/127)`) :
  - TONE (DRIVE K2, FOLD K3) : passe-bas un pôle à `e(500, 20000, x)` Hz ;
  - DRIVE : `plaits::Overdrive::Process(p1 / 127, …)` puis TONE ;
  - FOLD : gain `e(1, 16, p1)`, biais `(p2 − 64) / 64`, `lut_fold` lue comme le moteur WAVESHAPING de Plaits, puis TONE ;
  - CRUSH : bits `16 − round(14 · p1 / 127)`, quantification à `2^(bits−1)` pas ; puis `SampleRateReducer::Process<false>(e(44100, 500, p2) / SR, …)` ;
  - CHORUS : ligne de 10 ms ± `8 ms · p2 / 127`, sinus à `e(0.05, 5, p1)` Hz, lecture interpolée ;
  - PHASER : 4 passe-tout du premier ordre, fréquence balayée de 200 Hz à `200 · 2^(5 · p2 / 127)` Hz par un sinus à `e(0.05, 5, p1)` Hz, réinjection `0.9 · p3 / 127` ;
  - RING : `x · sin(2π f t)`, `f = e(20, 5000, p1)` ;
  - COMB : `y = x + fb · LP(y[n − D])`, `D = SR / f(max(p1, 29))` interpolé, `fb = 0.95 · p2 / 127`, LP un pôle dans la boucle de `e(20000, 500, p3)` Hz ;
  - MIX : `out = dry · (1 − m) + wet · m`, `m = mix / 127`.
  - Tampon par piste (CHORUS et COMB) : `static float ibuf[4][1024] __attribute__((section(".pool")))`.

- [ ] **Step 1: Tests** (`qfx_test.cc`) :
  - `off_is_identity` : IT_OFF, et chaque type à MIX 0 : `memcmp` égal sur une scie à 220 Hz ;
  - `each_type_changes_signal` : chaque type à K 100/100/100, MIX 127 : différence RMS > 0,01 ;
  - `inserts_stay_finite` : chaque type à K 127/127/127, puis 0/0/0, sur un bruit plein échelle pendant 2 s : fini, |x| ≤ 4 ;
  - `comb_tunes_to_note` : COMB NOTE 57 (220 Hz), FEEDBK 120, une impulsion : le pic d'autocorrélation tombe à `SR / 220` ± 2 échantillons ;
  - `crush_bits` : BITS 127 (2 bits) sur une rampe : au plus 4 valeurs distinctes en sortie (RATE 0) ;
  - `type_switch_resets_state` : COMB FEEDBK 127 nourri 1 s, passage à CHORUS puis retour à COMB sur du silence : sortie nulle.
- [ ] **Step 2:** `tests/run_tests.sh` → échec.
- [ ] **Step 3:** `qfx_init`, `qfx_insert` selon les valeurs ci-dessus ; les réglages sont pris une fois par appel.
- [ ] **Step 4:** `tests/run_tests.sh` → `all tests ok` ; `./build.sh` → image construite.
- [ ] **Step 5:** commit `QFX: the seven track inserts`.

---

### Task 3: qfx — mix, delay, reverb, ducking, compresseur

**Files:**
- Create: `app/third_party/clouds/dsp/fx/reverb.h` (de `pichenettes/eurorack`, `clouds/dsp/fx/reverb.h`, MIT : en-tête de licence gardé ; seuls le namespace de l'`FxEngine` et le type de trame changent pour le `plaits::FxEngine` déjà vendorisé)
- Modify: `app/dsp/qfx.h`, `app/dsp/qfx.cc`, `README.md` (Clouds dans la liste du code vendorisé)
- Test: `tests/unit/qfx_test.cc`

**Interfaces:**
- Consumes : `qfx_insert` (tâche 2).
- Produces (`qfx.h`) :
  ```c
  /* a track's settings for the block, locks applied; duck 1: the track is ducked (not a KIT) */
  typedef struct { uint8_t type, p1, p2, p3, mix, dsend, rsend, level, duck; int8_t pan; } qfx_track;
  /* dly: the delay's time in frames (seq_step_ratio of tempo and dtime), halved by qfx to fit */
  typedef struct { float dly; uint8_t dfb, rsize, rtone, comp, duck; } qfx_master;
  /* bus[t]: track t's voices, n mono samples (overwritten). duck_at: the frame of a KIT BD's note in the
     block, -1 none. l, r: the mix before MASTER, n samples each */
  void qfx_block(float *const bus[QFX_TRACKS], const qfx_track tr[QFX_TRACKS], const qfx_master *m,
                 int duck_at, float *l, float *r, unsigned n);
  float qfx_delay_fit(float frames);   /* frames halved until <= the buffer's length, in 44.1 kHz frames */
  ```
- Valeurs :
  - par piste : insert, puis `g = level / 127` ; PAN `L = pan > 0 ? 1 − pan/63 : 1`, `R = pan < 0 ? 1 + pan/64 : 1` ; envois `dsend / 127`, `rsend / 127` sur le signal après `g` ; LEVEL, PAN, envois et MIX rampés linéairement du bloc précédent au bloc courant ;
  - delay : entrée mono décimée par 2 (moyenne de paires) dans `int16_t dly[2][11264]` du pool, lecture interpolée, sortie remontée par interpolation linéaire ; ping-pong (l'entrée part à gauche, la réinjection croise les canaux) ; réinjection `0.95 · dfb / 127` à travers un passe-bas un pôle à 4 kHz ; retour à gain 1 ;
  - reverb : la `Reverb` de Clouds, `FxEngine<16384, FORMAT_12_BIT>` dans le pool, `set_amount(1)` (humide seul), `set_input_gain(0.2)`, `set_time(0.5 + 0.48 · rsize / 127)`, `set_lp(0.3 + 0.65 · rtone / 127)`, `set_diffusion(0.625)` ; entrée mono sur L et R ; retour à gain 1 ;
  - ducking : à `duck_at`, gain des pistes `duck` et des retours ramené à `−24 · duck / 127` dB, puis remontée linéaire en dB sur 150 ms ; DUCK 0 : gain 1 exact ;
  - compresseur (COMP > 0) : détecteur crête stéréo lié, attaque 5 ms, relâche 100 ms ; seuil `T = −30 · comp / 127` dB, ratio 4:1, compensation `+0.5 · (−T) · (1 − 1/4)` dB ; COMP 0 : contourné ;
  - limiteur, toujours : crête stéréo liée, plafond 1,0, attaque immédiate, relâche 50 ms ; sous 1,0, gain 1 exact.

- [ ] **Step 1: Tests** (`qfx_test.cc`) :
  - `defaults_are_transparent` : 4 bus de sinus d'amplitude 0,2, réglages par défaut : `L == R == Σ bus · 100/127` à 1e-6 près ;
  - `pan_law` : PAN 0 → L = R = x ; PAN −64 → R = 0, L = x ; PAN 63 → L = 0, R = x ; PAN 32 → L ≈ 0,49 x, R = x ;
  - `delay_lands_on_division` : impulsion, DELAY 127, `dly` 11029 (1/8 à 120 BPM), DLY FB 64 : 1er écho à L à 11029 ± 2, 2e à R à 22059 ± 2 ;
  - `delay_time_halves_to_fit` : `qfx_delay_fit(88235)` (1/1 à 120) → 22059 ; `qfx_delay_fit(264706)` (1/1 à 40) → 16544 ;
  - `delay_time_change_stays_finite` : `dly` alterné entre 2000 et 22000 chaque bloc pendant 2 s, DLY FB 127 : fini ;
  - `reverb_tail` : impulsion, REVERB 127 : énergie non nulle après 500 ms ; REV SIZE 127 garde plus d'énergie à 1 s que REV SIZE 0 ;
  - `duck_dips_and_recovers` : DUCK 127, `duck_at` 0, piste 0 `duck` 1, piste 1 `duck` 0 : piste 0 à −24 dB (± 0,5) au début, à 0 dB (± 0,5) après 150 ms ; piste 1 inchangée ;
  - `duck_zero_is_bypass` : DUCK 0, `duck_at` 10 : identique à `duck_at` −1 ;
  - `compressor_reduces_loud` : sinus 0,9, COMP 127 : crête de sortie plus basse qu'en entrée après 50 ms ; sinus 0,01 : gain ≈ +11 dB ± 1 ;
  - `master_extremes_bounded` : 4 bus de bruit plein échelle, envois 127, DLY FB 127, REV SIZE 127, COMP 127, DUCK 127, `duck_at` chaque 20 blocs, 10 s : fini, |x| ≤ 1.
- [ ] **Step 2:** `tests/run_tests.sh` → échec.
- [ ] **Step 3:** `qfx_block`, `qfx_delay_fit` ; la reverb vendorisée ; README.
- [ ] **Step 4:** `tests/run_tests.sh` → `all tests ok` ; `./build.sh` → image construite (le pool tient).
- [ ] **Step 5:** commit `QFX: track mix, ping-pong delay, Clouds reverb, ducking, compressor and limiter`.

---

### Task 4: Rendu — bus de piste, locks d'effet, ducking, stéréo

**Files:**
- Modify: `app/audio.c`
- Test: `tests/unit/audio_test.c` (double de `qfx` : `qfx_init` et `qfx_block` journalisés, avec copie des `qfx_track`, du `qfx_master`, de `duck_at` et des bus ; le double écrit `l = r = Σ bus`)

**Interfaces:**
- Consumes : `qfx_init`, `qfx_block`, `qfx_track`, `qfx_master` (tâche 3) ; `seq_pos`, `seq_step_ratio`, `lock_find`.
- `audio.c` :
  - `mix1`, `mix2` remplacés par `static float bus[2][Q_TRACKS][APP_BLOCK]` : le cœur 1 rend ses voix dans `bus[0]`, le cœur 2 (ou le cœur 1 à sa place) dans `bus[1]`, chacune dans le bus de sa piste. Après le dernier segment, `bus[0][t] += bus[1][t]`.
  - `gain[v]` = `0.25` (× niveau du pad pour un KIT) : LEVEL passe dans `qfx_track.level`.
  - Locks d'effet par piste : `static uint8_t fxl_val[Q_TRACKS][8]; static uint8_t fxl_mask[Q_TRACKS], fxl_step[Q_TRACKS], fxl_seen[Q_TRACKS];`. Une note-on d'un pas (step ≥ 0, arp compris) prend les locks `LK_I1..LK_LEVEL` de ce pas, `fxl_step = step`, `fxl_seen = 0`. Au début de chaque bloc : si `seq_pos == fxl_step`, `fxl_seen = 1` ; sinon, si `fxl_seen`, le masque tombe à 0. `seq_stop` met tous les masques à 0. Les notes du clavier ne touchent pas aux locks.
  - `qfx_track` de la piste t : `prj->sound[t]` puis ses locks ; `duck = rmodel[t] != M_KIT`.
  - `duck_at` : l'offset de la première note-on du bloc d'un pad dont `prj->kit[t][pad].engine == PAD_BD` sur une piste KIT, −1 sinon.
  - `qfx_master` : `dly` de `seq_step_ratio(prj->tempo, prj->dtime)` (num / den) ; le reste de `prj`.
  - Le clic du métronome est rendu dans `static float clk[APP_BLOCK]`, ajouté à `l` et `r` après `qfx_block`.
  - Sortie : `lr[2i] = L · g`, `lr[2i+1] = R · g`, même bornage qu'avant. `qfx_init()` dans `audio_setup`.

- [ ] **Step 1: Tests** (`audio_test.c`) :
  - `voices_reach_their_track_bus` : piste 0 sur la voix 0, piste 2 sur la voix 4 (cœur 2) : les bus 0 et 2 reçus par `qfx_block` portent leur signal, les autres sont nuls ;
  - `level_moves_to_fx` : LEVEL 50 : le bus reçu ne dépend pas de LEVEL, `tr[t].level == 50` ;
  - `fx_lock_on_its_step` : lock `LK_DSEND` 90 et `LK_PAN` −20 au pas 1 : pendant le pas 1, `tr[0].dsend == 90`, `tr[0].pan == −20` ; au pas 2 (sans lock), les valeurs de la piste ;
  - `fx_lock_negative_micro` : même lock, pas 1 en micro −11 : le lock vaut jusqu'au pas 2 ;
  - `stop_clears_fx_locks` : STOP pendant le pas 1 locké, puis note au clavier : valeurs de la piste ;
  - `kit_bd_sets_duck_at` : piste 0 en KIT, un pas sur le pad 0 (BD) à l'offset 100 du bloc : `duck_at == 100` ; pad SN : −1 ;
  - `duck_follows_kit_membership` : `tr[t].duck` vaut 0 pour une piste KIT, 1 sinon, et suit un changement de modèle au bloc suivant ;
  - `click_after_fx` : métronome armé : le clic est dans `LR` mais pas dans les bus reçus par `qfx_block` ;
  - `stereo_out` : le double rend `l` ≠ `r` : `LR` pair et impair diffèrent.
- [ ] **Step 2:** `tests/run_tests.sh` → échec.
- [ ] **Step 3:** les changements ci-dessus.
- [ ] **Step 4:** `tests/run_tests.sh` → `all tests ok` ; `./build.sh` → image construite. Si la RAM principale déborde, `bus` passe dans `.pool`.
- [ ] **Step 5:** commit `Audio: per-track buses on both cores, track effects and their locks, stereo out`.

---

### Task 5: Interface — FX, GLO et p-locks

**Files:**
- Modify: `app/ui.c`, `app/words.tsv`
- Create: `app/tests/fx.txt`
- Modify: `app/tests/glo.txt`, `app/tests/plocks.txt`

**Interfaces:**
- `ui.c` :
  - **Domaine** `D_FX` sur `B_FX` : pages `P_INSERT`, `P_MIX`.
  - **P_INSERT** : libellés par type, K4 toujours MIX, les autres vides si sans rôle :

    | Type | Nom de page | K1 | K2 | K3 |
    |---|---|---|---|---|
    | OFF | `INSERT OFF` | — | — | — |
    | DRIVE | `OVERDRIVE` | DRIVE | TONE | — |
    | FOLD | `WAVEFOLD` | FOLD | BIAS | TONE |
    | CRUSH | `BITCRUSH` | BITS | RATE | — |
    | CHORUS | `CHORUS` | RATE | DEPTH | — |
    | PHASER | `PHASER` | RATE | DEPTH | FEEDBK |
    | RING | `RING MOD` | FREQ | — | — |
    | COMB | `COMB` | NOTE | FEEDBK | DAMP |

    PRESET y change `itype`, borné 0..7. Valeurs affichées : BITS `16`..`2` ; RATE et FREQ en Hz, 2 chiffres significatifs ; NOTE en nom de note (`F1` au moins) ; le reste en nombre.
  - **P_MIX** : DELAY, REVERB, PAN (−64..63), LEVEL.
  - **GLO** : `P_TEMPO` gagne COMP et DUCK (K3, K4) ; `P_GFX` (nom `FX` : DLY TIME en `DIV_NAME`, DLY FB, REV SIZE, REV TONE) entre `P_SCALE` et `P_CPU` ; `DOMAIN_N[D_GLO]` 4.
  - **Pages de p-lock** : `LPAGE[8] = {P_ENGINE, P_SOUND, P_ADSR, P_FILTER, P_LFO, P_INSERT, P_MIX, P_ARP}`, lignes de `LOCK_OF` `{LK_I1, LK_I2, LK_I3, LK_IMIX}` et `{LK_DSEND, LK_RSEND, LK_PAN, LK_LEVEL}` ; `lpage_n` 8, ou 5 sur un KIT (lignes 2..6). L'overdub passe par la même table.
  - **`ui_expect`** : `itype i1 i2 i3 imix dsend rsend pan level dtime dfb rsize rtone comp duck`.

- [ ] **Step 1: Scripts.**
  - `fx.txt` :
    - `tap FX` → `expect domain` FX ; `turn PRESET 7` → `expect itype 7` ; `turn PRESET 5` → `expect itype 7` ; `shot fx_insert.png` ;
    - `turn K1 -10` → `expect i1 54` ; `turn K4 -127` → `expect imix 0` ;
    - SELECT → page 2 ; `turn K3 -100` → `expect pan -64` ; `turn K4 10` → `expect level 110` ; `shot fx_mix.png`.
  - `glo.txt` (ajout) : `tap GLO`, `turn K3 30` → `expect comp 30`, `turn K4 5` → `expect duck 5` ; SELECT ×2 → page 3, `turn K1 -2` → `expect dtime 2`, `shot glo_fx.png` ; SELECT → page 4 est CPU.
  - `plocks.txt` (ajout et mise à jour des numéros de page) : pas 1 tenu, SELECT ×5 → `expect lpage 6` (INSERT), `turn K1 7` → `expect lock_22_1 71` ; SELECT → MIX, `turn K3 -5` → `expect lock_28_1 -5` ; SELECT → `expect lpage 8` (ARP) ; sur un KIT, SELECT ×4 → `expect lpage 5` (MIX).
- [ ] **Step 2:** `tests/run_tests.sh` → échec sur `fx.txt`.
- [ ] **Step 3:** l'interface ci-dessus ; les mots dans `words.tsv`.
- [ ] **Step 4:** `tests/run_tests.sh` → `all tests ok` ; les `shot` vérifiées à l'œil.
- [ ] **Step 5:** commit `UI: FX domain, GLO compressor and FX page, insert and mix p-locks`.

---

### Task 6: Appareil et mesures

**Files:**
- Modify: `docs/cpu.md`

- [ ] **Step 1:** `./build.sh`, installer (`fm1_install --yes --classic`). Noter la taille de l'image.
- [ ] **Step 2 (utilisateur) :** un projet neuf sonne comme avant, au centre, sans souffle de reverb.
- [ ] **Step 3 (utilisateur) :** écouter chaque insert sur un synthé tenu ; PAN ; delay ping-pong à 1/8 puis 1/4 ; reverb ; un pas locké sur DELAY 127 (delay throw) ; COMP et DUCK avec un KIT sur GRIDS et une basse.
- [ ] **Step 4 (utilisateur) :** pire cas 60 s : 6 voix MODAL (deux pistes POLY 3), filtre résonant et LFO CUTOFF, arp 1/32 sur deux pistes, les 4 inserts sur PHASER, envois delay et reverb à 100, COMP et DUCK à 100. LOST BLOCKS doit rester à 0 ; relever les charges des deux cœurs. Puis le même sans effets (pire cas de 3a).
- [ ] **Step 5:** `docs/cpu.md` : lignes « Step 3a worst case » et « Step 3b worst case ». Commit `Step 3b: device measurements`.
- [ ] **Step 6 (seulement si des blocs sont perdus) :** s'arrêter et le signaler ; la suite est la variante de la spec (reverb sur le cœur 2, un bloc de retard), en tâche à part.

---

## Hors périmètre

Punch-ins (3c) ; LFO vers PAN ou vers les effets ; insert stéréo, second insert ; niveau des retours ; réglages complets du compresseur ; reverb sur le cœur 2 sauf si la tâche 6 l'exige ; bug XY de l'étape 2.
