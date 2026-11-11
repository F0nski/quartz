# QUARTZ étape 3a (enveloppe, filtre, LFO, arpégiateur) — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** un firmware QUARTZ installable où chaque piste a une ADSR, un filtre SVF par voix, un LFO et un arpégiateur, réglables en direct et verrouillables par pas.

**Architecture :** un nouveau module C `mod.c` calcule, voix par voix et bloc Plaits par bloc Plaits (12 échantillons), l'ADSR et le LFO, et rend un `pq_mod` (niveau, fréquence du filtre, décalages) que `pq.cc` applique : niveau de la voix Plaits (donc son low-pass gate), `Svf` de stmlib, gain. L'arpégiateur (dans `mod.c` aussi) se place entre le séquenceur ou le clavier et les voix : `audio.c` lui donne les notes d'une piste et fusionne ses notes avec les autres événements du bloc. Les LFO FREE lisent une horloge globale (temps et temps musical depuis PLAY) : toutes les voix d'une piste partagent la phase sans état par piste.

**Tech Stack :** C (gnu11, sans libc dans l'app), C++ pour `pq` ; fm1-base (simulateur hôte) ; tests `tests/run_tests.sh`.

**Spec :** `docs/superpowers/specs/2026-11-11-quartz-step3a-modulations-design.md` (et la spec globale, sections 4.2, 4.3, 5.2, 7).

## Global Constraints

- Identité git `f0nski <13540044+F0nski@users.noreply.github.com>`, branche `step3a-modulations` (partie de `main`) ; pas de ligne `Co-Authored-By`.
- Code, commentaires et commits en anglais ; pas de libc dans le code de l'app (builtins : `__builtin_expf`, `__builtin_sinf`, `__builtin_memset`…).
- Valeurs 0..127 sauf : ENV AMT et DEPTH −64..63 ; TYPE 0..2 ; SHAPE 0..4 ; RATE 0..75 (0..11 synchro, 12..75 libre) ; DEST 0..6 ; MODE 0..5 ; OCT 1..4 ; DIV `DIV_*` ; GATE 5..100.
- ATTACK, DECAY, RELEASE : `t = 0.001 · 10000^(x/127)` s. CUTOFF : demi-ton `15.5 + x · 119.5/127` (20 Hz..20 kHz). RES : `Q = 0.5 · 60^(x/127)`. ENV AMT ±63 = ±96 demi-tons, × 1,5 sur un accent.
- RATE synchro, périodes en temps : 32, 16, 8, 4, 2, 1, 2/3, 1/2, 1/3, 1/4, 1/6, 1/8. RATE libre i = 0..63 : `0.05 · 600^(i/63)` Hz.
- LFO à DEPTH ±63 : HARM, TIMB, MORPH, RES ±1 (sur la course 0..1) ; PITCH ±12 demi-tons ; CUTOFF ±60 demi-tons ; LEVEL gain `1 − |d| · (1 − lfo) / 2`. `d = clamp(depth / 63, −1, 1)`.
- Défauts : ADSR 0 / 64 / 127 / 20 ; LP, CUTOFF 127, RES 0, ENV AMT 0 ; SIN, FREE, RATE 5 (1/4), DEPTH 0, DEST CUTOFF ; ARP OFF, OCT 1, DIV 1/16, GATE 50.
- Moteurs à enveloppe propre : modèles 2, 3, 4 (6-OP), 19 (STRING), 20 (MODAL), 21–23 (BD, SN, HH, donc tout KIT).
- Le rendu ne fait que lire `q_project` ; la boucle principale est seule à l'écrire. Un note-off n'est jamais perdu.
- `Q_VERSION` 4, sans conversion des anciens projets.
- Zéro bloc perdu dans le pire cas de l'étape (6 voix MODAL, filtre et LFO actifs, arp sur deux pistes) sur l'appareil.

## Review Focus

1. MODE de l'arp changé (encodeur ou p-lock) pendant qu'il joue ou que des notes passent en direct : aucune note bloquée, les notes tenues reprennent dans le nouveau mode (test `arp_mode_change_no_stuck`, tâche 5).
2. STOP, ou passage de la piste en KIT, pendant un arp ou un latch : toutes ses notes s'arrêtent, un latch au clavier survit au STOP mais pas au KIT (test `arp_stop_and_kit_end_notes`, tâche 6).
3. Une voix volée en pleine release est relancée : l'attaque part du niveau courant, sans saut (test `adsr_attack_from_current_level`, tâche 2).
4. RES 127, CUTOFF 127, ENV AMT 63, LFO CUTOFF à fond, accent : la sortie reste finie et bornée (test `filter_extremes_stay_finite`, tâche 3).
5. Tempo changé pendant un arp ou un LFO synchro : la note d'arp suivante tombe au nouveau tempo, sans rafale, et le LFO ne saute pas (tests `arp_tempo_change` tâche 5, `lfo_sync_tempo_change_continuous` tâche 2).

---

## Structure des fichiers

```
app/project.h            q_sound + 17 octets, LK_* étendu (LK_N 22), Q_VERSION 4
app/mod.h, app/mod.c     (nouveaux) ADSR et LFO par voix, horloge des LFO, arpégiateur par piste
app/dsp/pq.h, pq.cc      pq_mod ; mod_voice_block appelé par bloc Plaits ; Svf ; gain ; décalages
app/seq.h, app/seq.c     EV_ACCENT ; défauts des nouveaux champs
app/store.c              bornes des nouveaux champs
app/audio.c              paramètres par voix avec locks, mod_*, arp entre événements et voices, latch
app/audio.h              audio_live_note : on = 3, note verrouillée par l'arp
app/app.c                #include "mod.c" avant audio.c
app/ui.c                 domaines ENV, LFO, ARP ; pages de p-lock ; latch ; indications KIT / enveloppe propre
app/words.tsv            nouveaux mots
tests/run_tests.sh       mod.c compilé en C et lié aux tests C++
tests/unit/mod_test.c    (nouveau)
tests/unit/{store,audio}_test.c, pq_test.cc
app/tests/{env,lfo,arp}.txt (nouveaux), plocks.txt
docs/cpu.md              mesures de l'étape 3a
```

---

### Task 1: Projet v4

**Files:**
- Modify: `app/project.h`, `app/seq.c` (`project_defaults`), `app/store.c` (`clamp_project`)
- Test: `tests/unit/store_test.c`

**Interfaces:**
- Produces (`project.h`) :
  ```c
  #define Q_VERSION 4
  enum { LK_HARM, LK_TIMB, LK_MORPH, LK_DECAY, LK_TUNE, LK_GLIDE,
         LK_ATK, LK_DEC, LK_SUS, LK_REL, LK_FTYPE, LK_CUT, LK_RES, LK_ENVAMT,
         LK_SHAPE, LK_RATE, LK_DEPTH, LK_DEST, LK_AMODE, LK_AOCT, LK_ADIV, LK_AGATE, LK_N };
  enum { FT_LP, FT_HP, FT_BP };
  enum { LS_SIN, LS_TRI, LS_SAW, LS_SQR, LS_SH };
  enum { LD_HARM, LD_TIMB, LD_MORPH, LD_PITCH, LD_CUT, LD_RES, LD_LEVEL };
  enum { AM_OFF, AM_UP, AM_DOWN, AM_UPDN, AM_RAND, AM_ORDER };
  #define LRATE_SYNC 12              /* lrate 0..11: synced; 12..75: free */
  ```
  `q_sound` gagne, après `decay` : `uint8_t atk, dec, sus, rel, ftype, cutoff, res; int8_t envamt; uint8_t lshape, ltrig, lrate; int8_t ldepth; uint8_t ldest, amode, aoct, adiv, agate;` (`ltrig` 0 FREE, 1 TRIG). Un lock signé (`LK_ENVAMT`, `LK_DEPTH`) est rangé en `uint8_t`, relu en `int8_t`.

- [ ] **Step 1: Tests** dans `store_test.c` : `store_roundtrip_v4` (chaque nouveau champ modifié sur la piste 2, sauvegardé, rechargé : `memcmp` égal) ; `store_clamps_v4` (ftype 7, lshape 9, lrate 200, ldest 9, amode 9, aoct 0, adiv 20, agate 0, ltrig 5 → 2, 4, 75, 6, 5, 1, 8, 5, 1 ; envamt et ldepth −128 → −64) ; `store_v3_gives_defaults` (chunks de `version = 3` → `store_load` rend −1, défauts de la Global Constraints sur les 4 pistes) ; le test existant qui vérifie les défauts couvre les nouveaux champs.
- [ ] **Step 2:** `tests/run_tests.sh` → échec de compilation.
- [ ] **Step 3:** les champs, les défauts, les bornes. Le `_Static_assert` de `store.c` doit encore passer.
- [ ] **Step 4:** `tests/run_tests.sh` → `all tests ok`.
- [ ] **Step 5:** commit `Project v4: envelope, filter, LFO and arpeggiator settings per track`.

---

### Task 2: mod.c — ADSR et LFO par voix

**Files:**
- Create: `app/mod.h`, `app/mod.c`, `tests/unit/mod_test.c`
- Modify: `app/dsp/pq.h` (seulement le type `pq_mod` et la déclaration de `mod_voice_block`), `app/app.c` (`#include "mod.c"` avant `audio.c`)

**Interfaces:**
- Produces (`pq.h`, en C) :
  ```c
  /* a voice's modulations for its next Plaits block: env the ADSR 0..1; cutoff f / sample rate, clamped to
   * 0.0001..0.45; res the SVF's Q; pitch semitones; harm, timb, morph offsets; gain 0..1; type FT_* */
  typedef struct { float env, cutoff, res, pitch, harm, timb, morph, gain; int type; } pq_mod;
  void mod_voice_block(int v, pq_mod *m);   /* the app's (mod.c): voice v advanced by one Plaits block, 12 frames */
  ```
- Produces (`mod.h`) :
  ```c
  /* a voice's settings, locks applied (audio.c) */
  typedef struct { uint8_t atk, dec, sus, rel, ftype, cutoff, res; int8_t envamt; uint8_t lshape, ltrig, lrate;
                   int8_t ldepth; uint8_t ldest; } q_vparam;
  void mod_init(void);
  void mod_set(int v, const q_vparam *p, int tempo);   /* core 1, each block before the render, and before a note-on */
  void mod_note_on(int v, int accent);                 /* attack from the current level; TRIG: LFO phase 0 */
  void mod_note_off(int v);                            /* release from the current level */
  void mod_clock(int tempo, uint32_t n);               /* after each block: the FREE LFOs' clock */
  void mod_restart(void);                              /* at PLAY: the FREE LFOs from 0 */
  ```
- ADSR, avancée de 12 frames par appel : attaque linéaire de l'enveloppe courante à 1 en ATTACK (ATTACK 0 : 1 au premier bloc) ; decay exponentiel vers SUSTAIN, à 1 % de l'écart après DECAY ; release exponentielle, sous 0,001 après RELEASE, puis 0 exactement.
- `cutoff` = demi-ton CUTOFF + `env · envamt/63 · 96` (× 1,5 si accent) + LFO, converti en `f / Q_SR`. `res` : Q de RES + LFO RES.
- Horloge FREE : `mod_clock` cumule en `double` les secondes et les temps (`n · tempo / 60 / Q_SR`) depuis `mod_restart`. `mod_set` en tire la phase de la voix au début du bloc (`frac(temps / période)` ou `frac(secondes · Hz)`), et `mod_voice_block` l'avance en `float` de 12 frames par appel. S&H : valeur = hachage du numéro de cycle (FREE : numéro global, donc commun aux voix d'une piste ; TRIG : numéro propre à la voix, plus un compteur de notes).
- Formes sur −1..1 : SIN `sin 2πφ` ; TRI ; SAW montante ; SQR +1 sur la première moitié ; S&H.

- [ ] **Step 1: Tests** (`mod_test.c`, inclut `mod.c`) :
  - `adsr_default_is_gate` : avec les défauts, `env` vaut 1 dès le premier bloc et tombe sous 0,001 en moins de 5 ms après `mod_note_off`.
  - `adsr_times` : ATTACK 64, puis DECAY 64, SUSTAIN 64, RELEASE 64. `env` atteint 1 à `0.001 · 10000^(64/127)` s, à un bloc près ; il est à 1 % de 64/127 après DECAY ; il est sous 0,001 après RELEASE.
  - `adsr_release_from_attack` : un note-off à mi-attaque fait redescendre `env` depuis sa valeur courante, sans saut.
  - `adsr_attack_from_current_level` : une note-on à mi-release fait remonter `env` depuis sa valeur, sans saut supérieur à un pas d'attaque.
  - `filter_env_amount_and_accent` : CUTOFF 64, ENV AMT 63, SUSTAIN 127. Le demi-ton de `cutoff` vaut 64 + 96 sans accent et 64 + 144 avec accent, borné à 0,45.
  - `lfo_shapes` : SIN, TRI, SAW, SQR à RATE 1 temps, 120 BPM, DEST CUTOFF, DEPTH 63. On vérifie le décalage de `cutoff` aux quarts de période.
  - `lfo_free_shared_phase` : deux voix en FREE avec le même RATE et des note-on décalés ont la même phase. Après `mod_restart`, la phase est à 0.
  - `lfo_trig_restarts` : en TRIG, chaque note-on remet la phase à 0.
  - `lfo_sync_tempo_change_continuous` : on passe de 120 à 90 BPM. La phase ne saute pas de plus d'un bloc d'avance.
  - `lfo_level_depth` : DEST LEVEL et DEPTH −63 font varier `gain` dans 0..1 ; avec DEPTH 0, `gain` vaut 1.
  - `lfo_pitch_range` : DEST PITCH et DEPTH 63 donnent un `pitch` dans ±12.
  - `sh_changes_once_per_cycle`.
- [ ] **Step 2:** `tests/run_tests.sh` → échec (mod.c absent).
- [ ] **Step 3:** `mod.c` (état par voix : `q_vparam`, `env`, étape, phase, incrément, cycle, accent).
- [ ] **Step 4:** `tests/run_tests.sh` → `all tests ok`.
- [ ] **Step 5:** commit `MOD: per-voice ADSR and LFO, per Plaits block`.

---

### Task 3: pq applique les modulations

**Files:**
- Modify: `app/dsp/pq.cc`, `app/dsp/pq.h` (commentaires de `pq_note_off`, `pq_silent`), `tests/run_tests.sh` (`cc -c app/mod.c` puis lien avec chaque test `.cc`)
- Test: `tests/unit/pq_test.cc`

**Interfaces:**
- Consumes : `pq_mod`, `mod_voice_block`, `mod_set`, `mod_note_on`, `mod_note_off`, `mod_init` (tâche 2). pq n'appelle que `mod_voice_block` ; le test appelle les autres comme le fera `audio.c`.
- Dans `pq_render`, à chaque bloc Plaits :
  - `mod_voice_block(v, &m)` ;
  - `mod.level` = moteur à enveloppe propre ? (gate ? vel : 0) : `vel · m.env` ;
  - le patch est les valeurs de `pq_set_patch` plus `m.harm`, `m.timb`, `m.morph`, bornées à 0..1 ;
  - `mod.note` = note glissée plus `m.pitch`. L'état du glide reste séparé du décalage ;
  - `stmlib::Svf` : `set_f_q<FREQUENCY_ACCURATE>(m.cutoff, m.res)`, `Process<FILTER_MODE_LOW_PASS | HIGH_PASS | BAND_PASS>` selon `m.type`, sur la sortie en float, puis `× m.gain`.
- `pq_note_off` ne remet plus `mod.level` à 0 : c'est la release qui le fait. `pq_silent` exige en plus `m.env == 0` au dernier bloc.

- [ ] **Step 1: Tests** (`pq_test.cc`) :
  - `lowpass_darkens` : VA, note 48 ; CUTOFF 30 au lieu de 127 fait chuter le taux de passages par zéro et l'énergie haute ;
  - `highpass_thins` : HP, CUTOFF 90 sur une note grave fait baisser le crête ;
  - `release_holds_voice` : RELEASE 100 (~1,5 s), et `pq_silent` reste 0 pendant 1 s après le note-off ;
  - `enveloped_ignores_adsr_level` : MODAL avec ATTACK 127 sonne dès les premières 10 ms ;
  - `filter_extremes_stay_finite` : RES 127, CUTOFF 127, ENV AMT 63, LFO CUTOFF à fond, accent, sur 2 s : sortie finie, |x| ≤ 1 ;
  - `defaults_match_old_level` : avec les défauts, crête à ±10 % de la crête d'avant, mesurée et inscrite dans le test.
- [ ] **Step 2:** `tests/run_tests.sh` → échec.
- [ ] **Step 3:** les changements ci-dessus.
- [ ] **Step 4:** `tests/run_tests.sh` → `all tests ok` ; `./build.sh` → image construite.
- [ ] **Step 5:** commit `PQ: ADSR on the voice's level, per-voice SVF, LFO offsets`.

---

### Task 4: Rendu — paramètres par voix, locks, accent

**Files:**
- Modify: `app/audio.c`, `app/seq.h`, `app/seq.c` (`plan_notes`)
- Test: `tests/unit/audio_test.c` (inclut `mod.c` ; le double de pq ignore `mod_voice_block`), `tests/unit/seq_test.c`

**Interfaces:**
- `seq.h` : `enum { EV_LEGATO = 1, EV_ACCENT = 2 };`. `plan_notes` pose `EV_ACCENT` sur la note dont la vélocité a reçu +32.
- `audio.c` :
  - `lk_mask` passe en `uint32_t`, `lk_val[Q_VOICES][LK_N]` ; `take_locks` lit tous les `LK_*`, KIT compris. Un pad garde ses réglages, mais une voix KIT prend les locks ADSR, FILTER et LFO de son pas.
  - `static void vparam(int v, const q_sound *s, q_vparam *q)` : chaque champ via `param()`, les champs signés relus en `int8_t`.
  - `note_on(…, int accent)` : `vparam`, `mod_set`, `mod_note_on(v, accent)` avant `pq_note_on`. `event()` et `later[]` portent l'accent.
  - Note-off d'une voix : `mod_note_off` avec `pq_note_off`.
  - Dans la boucle qui appelle `set_patch` sur chaque bloc : `vparam` puis `mod_set`.
  - En fin de bloc : `mod_clock(prj->tempo, n)`. Sur `CMD_PLAY` avec on : `mod_restart()`. Dans `audio_setup` : `mod_init()`.

- [ ] **Step 1: Tests** :
  - `accent_flag_on_accented_notes` (seq) : un pas SF_ACCENT a `EV_ACCENT` sur chaque note ; un pas normal, aucune ;
  - `voice_takes_new_locks` (audio) : lock CUTOFF 10 et ENV AMT −20 au pas 1, pas 2 sans lock. La voix du pas 1 reçoit `cutoff 10, envamt −20` ; celle du pas 2 reçoit les valeurs de la piste ;
  - `kit_voice_takes_filter_locks` : même chose sur un pas KIT ;
  - `play_restarts_lfo_clock` : après PLAY, le temps compté par `mod_clock` repart de 0.
- [ ] **Step 2:** `tests/run_tests.sh` → échec.
- [ ] **Step 3:** les changements ci-dessus.
- [ ] **Step 4:** `tests/run_tests.sh` → `all tests ok`.
- [ ] **Step 5:** commit `Audio: voices take their envelope, filter and LFO settings and locks`.

---

### Task 5: mod.c — arpégiateur

**Files:**
- Modify: `app/mod.h`, `app/mod.c`
- Test: `tests/unit/mod_test.c`

**Interfaces:**
- Consumes : `q_event`, `EV_LEGATO`, `EV_ACCENT` (`seq.h`), `seq_step_ratio`.
- Produces (`mod.h`) :
  ```c
  #define ARP_HELD 8                  /* notes held at most; a 9th note-on is ignored */
  #define ARP_IN 32                   /* note events per block; beyond, note-ons dropped, never a note-off */
  #define ARP_OUT 48                  /* what arp_block may write */
  enum { EV_LATCH = 4 };              /* a keyboard note-on with ARP held: kept after its key is released */
  typedef struct { uint8_t mode, oct, div, gate; } q_arpp;
  typedef struct { /* held notes in arrival order (note, vel, pat, step, flags, latched), the events queued for
                      this block, the mode last played, the clock (next step: frame + remainder), the index,
                      the note sounding and its off date, the rng */ } q_arp;
  void arp_reset(q_arp *a);
  void arp_input(q_arp *a, const q_event *e);          /* the track's event, offset in this block */
  void arp_unlatch(q_arp *a);                          /* the latched notes released at the next arp_block */
  /* the arp's events for the block's n frames, sorted by offset; consumes the queued inputs */
  int  arp_block(q_arp *a, const q_arpp *p, int tempo, uint32_t n, q_event *out);
  int  arp_sounding(const q_arp *a);                   /* notes it holds on the voices */
  ```
- Ordre de chaque mode :
  - les notes tenues sont triées par hauteur (ORDER : ordre d'arrivée), puis répétées sur OCT octaves (+12 à chaque fois), en restant ≤ 127 ;
  - UP : la liste dans l'ordre ; DOWN : la liste inversée ;
  - UP-DN : la liste montante puis descendante, sans répéter les extrémités, donc une période de 2L − 2 pour L > 1 ;
  - RAND : une note au hasard dans la liste à chaque pas.
  - Quand l'ensemble change, l'index est gardé modulo la nouvelle période.
- Horloge :
  - un pas d'arp vaut `seq_step_ratio(tempo, div)` frames, compté de façon exacte (entier + reste) ;
  - le premier pas tombe à la note-on qui rend l'ensemble non vide ;
  - une note d'arp dure `gate %` du pas, au moins 1 frame, puis s'éteint même si l'ensemble se vide ;
  - quand l'ensemble est vide, aucun nouveau pas ne part ;
  - un changement de tempo vaut au pas suivant.
- MODE OFF (passage direct) : chaque entrée ressort telle quelle, à la même position.
- Changement de MODE :
  - au début du bloc, la note sonnante s'éteint ;
  - vers OFF : les notes tenues sont rejouées en note-on directs ;
  - depuis OFF : les notes tenues sont éteintes, puis l'arp démarre à la position 0.
- Entrée EV_LEGATO hors OFF : traitée comme un note-off de `from`, puis un note-on de la nouvelle note.
- Une note d'arp porte la vélocité, `pat`, `step` et `EV_ACCENT` de sa note tenue.

- [ ] **Step 1: Tests** (offsets en frames, à 120 BPM, 1/16 = 5514,7 frames) :
  - `arp_up_two_octaves` : C E G tenues, UP, OCT 2. Les notes sont C E G C' E' G' C, aux offsets `⌊k · num / den⌋`, chacune longue de 50 % ;
  - `arp_down`, `arp_updown_no_repeat` (C E G donne C E G E C E…), `arp_order_follows_arrival` (G C E reste G C E) ;
  - `arp_rand_in_set` : 64 pas, toutes les notes appartiennent à l'ensemble ;
  - `arp_single_note_repeats` : une seule note, OCT 1, répétée à chaque pas ;
  - `arp_starts_at_first_note` : un note-on à l'offset 100 joue sa première note à 100 ;
  - `arp_gate_min` : GATE 5 donne des notes de 5 % du pas ;
  - `arp_stops_when_empty` : après le dernier note-off, aucun note-on ; la note sonnante finit son gate ;
  - `arp_latch` : des notes EV_LATCH survivent à leur note-off. `arp_unlatch` les éteint au bloc suivant ;
  - `arp_mode_change_no_stuck` : UP pendant 3 pas, OFF, puis UP. Chaque note-on a son note-off, et les notes tenues sonnent en direct pendant OFF ;
  - `arp_passthrough_keeps_legato` : en OFF, une entrée EV_LEGATO ressort avec `from` ;
  - `arp_tempo_change` : de 120 à 60 BPM entre deux pas, le pas suivant arrive un pas à l'ancien tempo après le précédent, puis l'écart double ; jamais deux note-on au même bloc ;
  - `arp_ninth_note_ignored`.
- [ ] **Step 2:** `tests/run_tests.sh` → échec.
- [ ] **Step 3:** l'arp dans `mod.c`.
- [ ] **Step 4:** `tests/run_tests.sh` → `all tests ok`.
- [ ] **Step 5:** commit `MOD: the arpeggiator, sample-accurate on the project's tempo`.

---

### Task 6: Rendu — l'arp entre les notes et les voix

**Files:**
- Modify: `app/audio.c`, `app/audio.h`
- Test: `tests/unit/audio_test.c`

**Interfaces:**
- Consumes : `arp_*`, `q_arpp`, `EV_LATCH` (tâche 5).
- `audio.h` : `audio_live_note` accepte aussi `on = 3`, une note-on verrouillée par l'arp (non enregistrée, comme 2).
- `audio.c` :
  - `static q_arp arp[Q_TRACKS]` ; une piste « en arp » est une piste dont `rmodel` n'est pas `M_KIT` (MODE OFF compris : passage direct).
  - Clavier, piste en arp : `commands()` appelle `arp_input` à l'offset 0. `on = 3` passe `EV_LATCH`. Une note-on du clavier sans latch appelle d'abord `arp_unlatch`. La note du clavier a pour `step` 255, que le rendu relit comme −1, sans locks.
  - Après `seq_block`, les événements des pistes en arp vont à `arp_input`. Pour chaque piste en arp, on appelle `arp_block` avec les paramètres de la piste ; MODE, OCT, DIV et GATE sont pris dans les locks du pas de la dernière note-on entrée, si elle vient d'un pas. Les sorties sont fusionnées avec les autres événements, par offset (tri stable). `EV_MAX` grandit d'autant (`EV_MAX + Q_TRACKS * ARP_OUT`, statique hors pile).
  - `CMD_MODEL` vers KIT : la piste sort de l'arp, `arp_reset`. Ses voix s'éteignent déjà en fondu.
  - STOP : les note-off du séquenceur passent par l'arp. Un latch du clavier continue.

- [ ] **Step 1: Tests** :
  - `arp_plays_step_chord` : un pas POLY C E G, LEN 1 pas, ARP UP à 1/32. On entend deux notes d'arp dans le pas, sur le même offset que le note-on du pas ;
  - `arp_mode_lock_off` : un lock MODE OFF sur le pas 2. Ce pas joue ses notes en direct ;
  - `arp_keyboard_latch` : `audio_live_note(t, 60, 3)`, puis un note-off. L'arp continue ; un `audio_live_note(t, 62, 1)` arrête le latch ;
  - `arp_stop_and_kit_end_notes` : arp sur un pas plus un latch, puis STOP. Les notes du pas s'arrêtent et le latch continue. Passage de la piste en KIT : plus aucune voix V_ON sur la piste, et aucun note-on d'arp ensuite ;
  - `arp_ignored_on_kit` : sur un KIT avec MODE UP, les pads jouent en direct.
- [ ] **Step 2:** `tests/run_tests.sh` → échec.
- [ ] **Step 3:** les changements ci-dessus.
- [ ] **Step 4:** `tests/run_tests.sh` → `all tests ok`.
- [ ] **Step 5:** commit `Audio: the arpeggiator between a track's notes and its voices, latch from the keyboard`.

---

### Task 7: Interface — ENV, LFO, ARP et p-locks

**Files:**
- Modify: `app/ui.c`, `app/words.tsv`
- Create: `app/tests/env.txt`, `app/tests/lfo.txt`, `app/tests/arp.txt`
- Modify: `app/tests/plocks.txt`

**Interfaces:**
- `ui.c` :
  - **Domaines** `D_ENV` (`P_ADSR`, `P_FILTER`), `D_LFO` (`P_LFO`), `D_ARP` (`P_ARP`), sur `B_ENV`, `B_LFO`, `B_ARP`.
  - **Pages** (nom, libellés, bornes de la Global Constraints) :
    - ADSR : ATTACK, DECAY, SUSTAIN, RELEASE ;
    - FILTER : TYPE, CUTOFF, RES, ENV AMT ;
    - LFO : SHAPE, RATE, DEPTH, DEST ;
    - ARP : MODE, OCT, DIV, GATE.

    `knob_get` et `knob_set` les relient aux champs de `q_sound`.
  - **Valeurs affichées** :
    - TYPE `LP HP BP` ; SHAPE `SIN TRI SAW SQR S&H` ;
    - RATE : `8 BAR, 4 BAR, 2 BAR, 1 BAR, 1/2, 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T, 1/32`, puis les Hz avec 2 chiffres significatifs (`0.05` … `30`) ;
    - DEST `HARM TIMB MORPH PITCH CUTOFF RES LEVEL` ; MODE `OFF UP DOWN UP-DN RAND ORDER` ;
    - DIV `DIV_NAME` ; GATE `n %`.
  - **PRESET sur P_LFO** : bascule `ltrig`. L'en-tête affiche `FREE` ou `TRIG`.
  - **ADSR sur un moteur à enveloppe propre** : le pied affiche `FILTER ONLY`. **ARP sur un KIT** : la page affiche `NO ARP ON KIT`, et ses encodeurs n'ont pas d'effet.
  - **Pages de p-lock** (pas tenu, SELECT) :
    - piste synthé : ENGINE, SOUND, ADSR, FILTER, LFO, ARP ; KIT : ADSR, FILTER, LFO ;
    - la table `static const int8_t LOCK_OF[page][4]` (indice `LK_*`, −1 si le paramètre ne se verrouille pas : MODEL, POLY) remplace `lock_param` ;
    - `lock_value`, `lock_turn`, `rec_lock` et le pied passent par elle ;
    - les p-locks d'un pas KIT ne sont plus refusés.
  - **Latch** : si B_ARP est maintenu, une touche (piste synthé) envoie `audio_live_note(track, note, 3)`. Son relâchement envoie le note-off habituel, que l'arp ignore.
  - **`ui_expect`** : les champs `atk dec sus rel ftype cutoff res envamt lshape ltrig lrate ldepth ldest amode aoct adiv agate` ; `lpage` (page de p-lock + 1) ; `lock_P_N` (valeur du lock `LK_*` P au pas N, −1 si aucun, signée relue en `int8_t`).

- [ ] **Step 1: Scripts.**
  - `env.txt` :
    - `tap ENV` → `expect domain` ENV, `turn K1 10` → `expect atk 10` ;
    - SELECT → page 2, `turn K4 -30` → `expect envamt -30`, `turn K1 5` → `expect ftype 2` (borné) ;
    - modèle MODAL → le pied affiche `FILTER ONLY` (`shot` vérifié à l'œil).
  - `lfo.txt` :
    - `tap LFO`, `turn K2 20` → `expect lrate 25` ;
    - `turn PRESET 1` → `expect ltrig 1` ;
    - `turn K3 -100` → `expect ldepth -64`.
  - `arp.txt` :
    - `tap ARP`, `turn K1 1` → `expect amode 1` ;
    - B_ARP maintenu + touche 60 puis relâchée, PLAY arrêté : `expect active` ≥ 1 après 200 ms (l'arp joue le latch) ;
    - touche 62 sans ARP → le latch s'arrête ;
    - piste en KIT → `turn K1 1` laisse `amode` inchangé.
  - `plocks.txt` (ajout) :
    - pas 1 tenu, SELECT ×2 → `expect lpage 3` (ADSR), `turn K1 7` → `expect lock_6_1 7` ;
    - SELECT ×1 → FILTER, `turn K4 -5` → `expect lock_13_1 -5` ;
    - sur un KIT, pas tenu → `expect lpage 1` et la page est ADSR.
- [ ] **Step 2:** `tests/run_tests.sh` → échec sur `env.txt`.
- [ ] **Step 3:** l'interface ci-dessus ; les mots dans `words.tsv`.
- [ ] **Step 4:** `tests/run_tests.sh` → `all tests ok` ; `shot` des pages ADSR, FILTER, LFO, ARP vérifiées à l'œil.
- [ ] **Step 5:** commit `UI: ENV, LFO and ARP pages, their p-locks, the arpeggiator's latch`.

---

### Task 8: Appareil et mesures

**Files:**
- Modify: `docs/cpu.md`

- [ ] **Step 1:** `./build.sh`, installer (`fm1_install --yes --classic`). Noter la taille de l'image.
- [ ] **Step 2 (utilisateur) :** un projet neuf sonne comme avant (notes courtes et tenues, KIT).
- [ ] **Step 3 (utilisateur) :** écouter :
  - un balayage du filtre en LP, HP et BP, avec la résonance ;
  - l'ENV AMT et l'accent ;
  - un LFO sur CUTOFF et sur PITCH, en FREE puis en TRIG ;
  - l'arp sur un accord tenu, en latch, puis sur un pas POLY.
- [ ] **Step 4 (utilisateur) :** pire cas, pendant 60 s :
  - 6 voix MODAL (deux pistes POLY 3), filtre résonant, LFO CUTOFF ;
  - arp à 1/32 sur deux pistes ;
  - page CPU : LOST BLOCKS doit rester à 0 ; relever les charges des deux cœurs.
- [ ] **Step 5:** `docs/cpu.md` : une ligne « Step 3a worst case » avec ces chiffres et le coût mesuré par voix. Commit `Step 3a: device measurements`.

---

## Hors périmètre

Inserts, envois, pan, compresseur (3b) ; punch-ins (3c) ; second LFO ; LFO vers PAN ; p-locks des réglages de pads ; dessin de l'enveloppe ou du LFO (étape 5) ; enregistrement en overdub des notes verrouillées par l'arp ; bug XY de l'étape 2.
