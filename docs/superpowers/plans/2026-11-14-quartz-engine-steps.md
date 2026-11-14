# ENGINE : potards à crans — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** sur la page ENGINE, les potards qui choisissent (HARM des HEX, des accords, d'ORATOR ; TIMB d'ARCADE)
avancent d'un choix par cran et affichent le nom du choix.

**Architecture:** tout est dans `app/ui.c`. Une fonction pure `knob_zone` décrit les zones à crans d'après Plaits ;
`knob_step` en tire la valeur après un tour, utilisée par `knob_turn` et `lock_turn` ; `knob_text` affiche le nom à
partir du modèle porté par l'instantané du pied de page. Aucune donnée du projet ne change.

**Tech Stack:** C (gnu11) pour l'UI, C++ (gnu++11) pour le test unitaire contre `stmlib`, scripts du simulateur hôte.

**Spec:** `docs/superpowers/specs/2026-11-14-quartz-engine-steps-design.md`

## Global Constraints

- Branche `engine-steps` ; commits en anglais, petits, qui disent ce qui change pour le joueur ; auteur
  `f0nski <13540044+F0nski@users.noreply.github.com>` (config locale du dépôt) ; pas de ligne `Co-Authored-By`.
- `harm` et `timb` restent 0–127 ; pas de changement de `q_project` ni de `Q_VERSION`.
- Numéros de modèles (ordre Plaits, `MODEL_NAME`) : HEX A 2, HEX B 3, HEX C 4, ENSEMBLE 6, ARCADE 7, CHORALE 14,
  ORATOR 15 ; HEX A lit `syx_bank_0`, B `syx_bank_1`, C `syx_bank_2`.
- Zones (spec §3), cran `k` au milieu `round(start + (k + 0.5) * width)` :
  HEX HARM n 32, start 0, width 127/32/1.02 ; accords HARM (6, 7, 14) n 11, start 0, width 127/11/1.02 ;
  ARCADE TIMB n 12, start 0, width 127/12 ; ORATOR HARM n 6, start 127/3, width 127/6/1.65.
- Tout texte affiché a sa ligne dans `app/words.tsv` ; tout changement visible met à jour `app/docs/MANUAL.md` dans le
  même commit, captures refaites.
- Avant de conclure : `tests/run_tests.sh` se termine par `all tests ok`.

## Review Focus

- Un projet fait avant, HARM entre deux milieux (64, la valeur par défaut) : le premier cran va au milieu du cran
  suivant ou précédent, jamais plus loin d'un cran. Testé en Task 1 (départ de 64 sur CHORALE et ORATOR).
- Un tour rapide (`d` de plusieurs crans) au-delà des bornes : bloqué au premier / dernier milieu, ORATOR bloqué à 42 vers
  le bas depuis la zone à crans. Testé en Task 1 (`spin K1 -20`, `spin K1 20`).
- Un KIT, ou un modèle sans crans (CREST, PIPE) : HARM tourne de 1 en 1 comme avant. Testé en Task 1 (CREST).
- Le p-lock d'un pas tenu sur un modèle à crans : même saut qu'au potard. Testé en Task 1 (`lockharm_1`).
- La page de p-lock affiche le nom du choix verrouillé (instantané `foot` rempli par `lock_value`). Vérifié en Task 2
  par une capture d'un pas tenu.

---

### Task 1: les crans

**Files:**
- Modify: `app/ui.c` (près de `knob_turn`, l.630 ; `lock_turn`, l.871)
- Create: `tests/unit/engine_steps_test.cc`
- Create: `app/tests/engine_steps.txt`

**Interfaces:**
- Produces :
  - `static int knob_zone(int model, int pid, int k, float *start, float *width)` : le nombre de crans du potard `k`
    de la page `pid` pour ce modèle (0 : potard continu), et `*start`, `*width` d'après les contraintes globales ;
    seul `pid == P_ENGINE` en a.
  - `static int knob_step(int model, int pid, int k, int v, int d)` : la valeur après `d` crans depuis `v`, non bornée
    pour un potard continu (`v + d`, l'appelant borne comme aujourd'hui).
  - `static int knob_choice(int v, float start, float width, int n)` : le cran de `v`, `floor((v - start) / width)`
    borné à 0..n-1 (aussi utilisé par Task 2 pour les noms).

- [ ] **Step 1: test unitaire des milieux contre Plaits**

`tests/unit/engine_steps_test.cc`, comme `pq_test.cc` (inclut `stmlib/dsp/hysteresis_quantizer.h`). Pour chaque cas,
les milieux écrits en dur, et pour chaque milieu `m` et chaque état précédent `p` du quantizer (on l'amène à `p` en le
nourrissant de la valeur qui le pose à `p`), `Process` rend `k` :

```cpp
static const int HEX[32] = {2, 6, 10, 14, 18, 21, 25, 29, 33, 37, 41, 45, 49, 53, 56, 60, 64, 68, 72, 76, 80, 84,
                            88, 91, 95, 99, 103, 107, 111, 115, 119, 123};   // Init(32, 0.005f, false), harm * 1.02
static const int CHORD[11] = {6, 17, 28, 40, 51, 62, 74, 85, 96, 108, 119};  // Init(11, 0.075f, false), harm * 1.02
static const int ARP[12] = {5, 16, 26, 37, 48, 58, 69, 79, 90, 101, 111, 122};  // Init(12, 0.075f, false), timb
static const int WORDS[6] = {49, 62, 74, 87, 100, 113};  // Init(6, 0.1f, false), (harm * 6 - 2) * 0.275
// for each table: for p in 0..n-1, for k in 0..n-1: q.Process(input(mid of p)); assert(q.Process(input(T[k])) == k)
```

ORATOR : vérifier aussi que 42 donne `group <= 2` (`42 / 127.f * 6 <= 2`) et 49 `group > 2`.

- [ ] **Step 2: lancer le test**

Run: `tests/run_tests.sh` (ou la commande `c++` de la boucle `*_test.cc` du script, pour ce seul fichier)
Expected: `run tests/unit/engine_steps_test.cc` sans assertion. Ce test valide les milieux du spec : il passe dès
l'écriture ; s'il échoue, c'est le spec qui est faux, s'arrêter et le signaler.

- [ ] **Step 3: script simulateur (échoue)**

`app/tests/engine_steps.txt`. Les modèles se choisissent par `spin PRESET` sur EDIT (rang dans `MODEL_ORDER` :
CHORALE 12, ARCADE 14, ORATOR 15, HEX A 5) ; `expect model N` après chaque choix. Vérifier :

```
# CHORALE (14): HARM from 64 one chord a detent
expect harm 64 → spin K1 1 → harm 74 ; spin K1 -20 → harm 6 ; spin K1 1 → 17 ; spin K1 1 → 28 ; spin K1 20 → 119
# ARCADE (7): TIMB  spin K2 -20 → timb 5 ; spin K2 1 → 16 ; spin K2 20 → 122
# ORATOR (15): HARM from 119 → spin K1 -1 → 100 ; spin K1 -20 → 42 ; spin K1 -1 → 41 ; spin K1 1 → 42 ;
#   spin K1 1 → 49 ; spin K1 -1 → 42 ; spin K1 20 → 113
# HEX A (2): spin K1 -40 → 2 ; spin K1 1 → 6 ; spin K1 40 → 123
# CREST (8, rang 0): spin K1 1 from its value v → v + 1
# p-lock: CHORALE, tap SEQ, tap REC, key 0 down, spin K1 1 → lockharm_1 = next chord's middle from the track's
#   HARM ; key 0 up
```

`timb` n'a peut-être pas de clé `expect` : l'ajouter dans `ui_expect` à côté de `harm` (`s->timb`). Le modèle reste
par piste ; le HARM d'une piste change avec le modèle, noter les valeurs de départ réelles avec un premier `expect`.

Run: `./build.sh --host && app/build/host/quartz_host app/tests/engine_steps.txt app/build/host`
Expected: FAIL au premier `expect harm 74` (aujourd'hui 65).

- [ ] **Step 4: `knob_zone`, `knob_choice`, `knob_step` dans `ui.c`, branchés dans `knob_turn` et `lock_turn`**

`knob_step` : potard à crans hors ORATOR : cran de `v` plus `d`, borné, milieu. ORATOR HARM :

```c
if (v <= 42 && v + d <= 42) return v + d;          /* the blend of the three synths: plain */
if (v <= 42) return middle(0);                     /* up out of the blend: the first word bank */
c = knob_choice(v, start, width, n) + d;
return c < 0 ? 42 : middle(c > n - 1 ? n - 1 : c); /* down out of the banks: the blend's top, LPC */
```

`knob_turn` et `lock_turn` passent par `knob_step(ui.p->sound[ui.track].model, pid, k, cur, d)` puis bornent comme
aujourd'hui.

- [ ] **Step 5: lancer le script et tous les tests**

Run: `tests/run_tests.sh`
Expected: `all tests ok`.

- [ ] **Step 6: manuel**

`app/docs/MANUAL.md`, après la puce **HARM, TIMB, MORPH** : sur HEX A–C, CHORALE, ENSEMBLE, ARCADE et ORATOR (au-delà
de son fondu de synthèses, HARM 0–42), HARM — et TIMB sur ARCADE — passe d'un choix au suivant à chaque cran. (Les
noms viennent en Task 2.) Pas de capture changée par cette tâche.

- [ ] **Step 7: commit**

```bash
git add app/ui.c app/docs/MANUAL.md tests/unit/engine_steps_test.cc app/tests/engine_steps.txt
git commit -m "ENGINE: HARM on HEX, CHORALE, ENSEMBLE, ARCADE, ORATOR's word banks and ARCADE's TIMB move one choice a detent"
```

### Task 2: les noms

**Files:**
- Modify: `app/ui.c` (tables de noms après `MODEL_ORDER` l.~96 ; `q_view.foot` l.151 ; remplissage l.~2279 ;
  `knob_text` l.2049 ; `draw_foot` l.2802)
- Modify: `app/words.tsv`, `app/docs/MANUAL.md`, `app/tests/manual_shots.txt`, `app/tests/engine_steps.txt`
- Modify: `app/docs/img/*.png` (captures refaites)

**Interfaces:**
- Consumes : `knob_zone`, `knob_choice` (Task 1).
- Produces : `q_view.foot.model` (`uint8_t`, le modèle de la piste) ; `knob_text(int pid, int k, int v, int model,
  char *b)`.

- [ ] **Step 1: les tables, en copie exacte**

```c
static const char *const CHORD_NAME[11] = {"OCTAVE", "FIFTH", "SUS4", "M", "M7", "M9", "M11", "6/9", "MAJ9", "MAJ7", "MAJ"};
/* ARCADE's arpeggios: Plaits' pattern p, direction p / 3, 1 << p % 3 octaves */
static const char *const ARP_NAME[12] = {"UP 1", "UP 2", "UP 4", "DOWN 1", "DOWN 2", "DOWN 4", "UP-DN 1", "UP-DN 2",
                                         "UP-DN 4", "RAND 1", "RAND 2", "RAND 4"};
static const char *const WORDS_NAME[6] = {"PHONEMES", "WORDS 1", "WORDS 2", "WORDS 3", "WORDS 4", "WORDS 5"};
/* HEX A-C's presets: the DX7 banks' names (syx_bank_0..2), cleaned; brands and artists renamed */
static const char *const HEX_NAME[3][32] = {
{"SOLID BASS", "DEEP LOW", "LEADERTAPE", "ACID LINE", "BASS 3", "BILL BASS", "BASS 1", "ELEC BASS", "S.BAS 27.7", "RESONANCES", "SYN-BASS 2", "PRC SYNTH1", "PRISMA 2", "ANALOG 4", "ANALOG A", "ANALOG 6", "BIG POLY", "INSERT 1", "SPIRAL", "TROTTER", "GASHAUS", "RING DING", "PAPAGAYO", "WINEGLASS", "AMYTAL", "VINTAGE 1", "WAVE VOL 1", "WAVE VOL 2", "VINTAGE 3", "VOCODER 2", "SEQUENCE", "BOUNCE 4"},
{"E.PIANO 1", "TINES 1", "WINTER EP", "RS-EP C", "EP DELUXE", "CLAV-E.PNO", "SYN-CLAV", "CLAV", "PIANO 5", "GRD PIANO1", "CONCERT", "GUIT ACOUS", "SITAR", "KOTO", "HARPSICH 1", "CLAV 3", "XYLOPHONE", "MARIMBA", "VIBE 1", "GLOKENSPL", "BELL C", "BELLS", "TUB BELLS", "GONG 2", "KETTLE 6", "MID DRM 3", "ORI DRUM 1", "WOOD 6", "LATN DRM 4", "CIMBAL", "SYNDM 25.8", "B.DRM-SNAR"},
{"CLICK 124", "TONEWHEEL", "E.ORGAN 3", "60-S ORGAN", "OPTIC 28", "PIPES 1", "PIPES 3", "PIPES 2", "POLY 33", "SOUNDTRACK", "ICE PAD 2", "WAVE PADS", "CLASSIC 2", "SOFT TOUCH", "PLANETS", "CIRRUS", "ENTRIX", "MAL POLY", "TEXTURES 6", "ETHEREAL 5", "AIRY", "BORON A", "CINEMA 1", "STRINGS C", "STRINGS 3", "STRINGS 2", "STRINGS 7", "FULL STRIN", "SYN-ORCH", "BRASS 1", "BRASS 6 BC", "BR TRUMPET"}};
```

(À réindenter au style du fichier, lignes ≤ 120 colonnes.)

- [ ] **Step 2: `knob_text` affiche le nom**

`foot` gagne `uint8_t model`, rempli avec `ui.p->sound[ui.track].model` à côté de `v->foot.pid` ; `draw_foot` passe
`v->foot.model`. Dans `knob_text`, pour `pid == P_ENGINE` et `knob_zone(model, …) > 0` : ORATOR, `v <= 42` →
`FORMANT` (0), `SAM` (21), `LPC` (42), sinon le nombre ; au-delà `WORDS_NAME[choice]` ; HEX `HEX_NAME[model - 2]`,
accords `CHORD_NAME`, ARCADE TIMB `ARP_NAME`.

- [ ] **Step 3: `words.tsv`**

Une ligne par texte nouveau, à la suite des noms de modèles, dite en minuscules avec les abréviations développées :
`M	minor`, `M7	minor 7`, `MAJ	major`, `MAJ7	major 7`, `6/9	six nine`, `FIFTH	fifth`, `OCTAVE	octave`,
`UP 1	up 1 octave`, `UP 2	up 2 octaves` …, `UP-DN 1	up and down 1 octave` …, `RAND 4	random 4 octaves`,
`FORMANT	formant`, `SAM	sam`, `LPC	L P C`, `PHONEMES	phonemes`, `WORDS 1	words 1` …, et les 96 noms HEX
(`E.PIANO 1	electric piano 1`, `GRD PIANO1	grand piano 1`, `GUIT ACOUS	acoustic guitar`, `GLOKENSPL	glockenspiel`,
`HARPSICH 1	harpsichord 1`, `LATN DRM 4	latin drum 4`, `B.DRM-SNAR	bass drum snare`, `S.BAS 27.7	synth bass 27.7`,
`BR TRUMPET	brass trumpet`, `FULL STRIN	full strings`…). Ne pas redéfinir une clé déjà présente (`SUS4`, `UP`…) : vérifier
avec `cut -f1 app/words.tsv | sort | uniq -d` (aucune sortie).

- [ ] **Step 4: captures de contrôle**

À la fin de `app/tests/engine_steps.txt`, des `shot` : CHORALE HARM 6 (`es-chord.png`, `OCTAVE`), ORATOR à 42 et 49
(`es-lpc.png`, `es-phonemes.png`), HEX A à 6 (`es-hex.png`, `DEEP LOW`), ARCADE TIMB 16 (`es-arp.png`, `UP 2`), le
pas tenu de la Task 1 (`es-lock.png`, nom de l'accord verrouillé). Lancer le script, ouvrir chaque PNG et vérifier le
nom dans la colonne du potard, sans débordement.

- [ ] **Step 5: manuel**

`app/docs/MANUAL.md` : la puce de la Task 1 dit aussi que le pied de page affiche le nom du choix. Le tableau des modèles : HEX `Preset, 1 of 32, by name` ; CHORALE / ENSEMBLE / ARCADE HARM
`Chord, 1 of 11 (OCTAVE … MAJ)` ; ARCADE TIMB `Arpeggio: UP, DOWN, UP-DN, RAND over 1, 2 or 4 octaves` ; ORATOR HARM
`FORMANT, SAM, LPC blend, then PHONEMES, WORDS 1–5`. Une capture CHORALE (`sound-chorale.png`) ajoutée à
`manual_shots.txt` près de `sound-orator.png` et citée sous le tableau. Refaire les captures (commande du
`CLAUDE.md`) ; `sound-hex.png` et `sound-orator.png` changent aussi.

- [ ] **Step 6: tous les tests**

Run: `tests/run_tests.sh` puis `python3 base/tools/manual_html.py app/docs/MANUAL.md app/build/manual.html --name QUARTZ`
Expected: `all tests ok` ; le manuel se génère sans erreur.

- [ ] **Step 7: commit**

```bash
git add app/ui.c app/words.tsv app/docs/MANUAL.md app/docs/img app/tests/manual_shots.txt app/tests/engine_steps.txt
git commit -m "ENGINE: the stepped knobs show their choice by name: presets, chords, arpeggios, ORATOR's synths and word banks"
```
