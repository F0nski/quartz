# QUARTZ étape 2 (Séquenceur complet) — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** un firmware QUARTZ installable où l'on compose un morceau : 8 patterns par piste, pas riches (durée, micro-timing, probabilité, conditions, ratchets, slide, accent, p-locks), swing, mutes, scènes, chanson, overdub quantisé avec décompte et métronome.

**Architecture :** le séquenceur (`seq.c`) planifie chaque pas un demi-pas à l'avance dans une file d'événements par piste, datés à l'échantillon ; l'horloge de mesure du même module lance scènes et entrées de chanson. Le rendu (`audio.c`) applique les p-locks par voix et renvoie les notes enregistrées à la boucle principale par une seconde file : la boucle principale reste le seul écrivain du projet. Le projet (≈ 33 Ko) vit dans `.pool` et se sauvegarde en 9 objets du pool de stockage de fm1-base.

**Tech Stack :** C (gnu11), C++ (gnu++11) pour `pq` ; fm1-base `b0976f2` (simulateur hôte, `storage.c` avec `APP_STORE_POOL`) ; tests `tests/run_tests.sh`.

**Spec :** `docs/superpowers/specs/2026-11-10-quartz-groovebox-design.md` (sections 5, 7, 8, 11, 12 « 2. Séquenceur complet », 13).

## Choix à valider (la spec ne tranche pas ou ne tient pas)

1. **Un seul projet sauvegardé.** La section 11 (8 emplacements) ne tient pas : un objet du pool prend un secteur de 4 Ko par copie, un projet en prend 9, et la zone de données en a 73. Les 8 emplacements sont hors périmètre ; il faudra revoir le format (patterns vides non écrits) dans une étape ultérieure.
2. **Un pas fait 10 octets** (la spec en estime 6) : 4 notes pour POLY + 6 attributs. Un pattern fait 1 028 octets, les 32 patterns 32,9 Ko (budget du pool : 32 Ko, pris sur la marge de 48 Ko).
3. **Scènes :** HOME maintenu + blanche 9–16 lance la scène (à la mesure suivante en lecture, tout de suite à l'arrêt) ; une scène vide mémorise l'état courant (patterns demandés + mutes) ; HOME + SEL maintenus + blanche la remplace.
4. **Chanson :** page SONG dans HOME (entrée, scène, répétitions, mode PATTERN/SONG ; SEL efface l'entrée). Une répétition = le plus long des 4 patterns de la scène, arrondi à la mesure supérieure. La chanson boucle à la fin.
5. **Édition d'un pas** (SEQ) : appui = pas tenu ; relâché sans rien modifier = bascule on/off. Pendant le maintien : les autres blanches écrivent la note (accord sur une piste POLY), les noires choisissent l'attribut, PRESET le règle (vélocité par défaut), SELECT choisit la page de p-locks (ENGINE, SOUND), K1–K4 posent les p-locks.
6. **Métronome** pendant l'enregistrement seulement ; **décompte** au PLAY quand REC est armé.
7. **Accent** = vélocité + 32 (ENV AMT viendra à l'étape 3). **Slide** sur la première note du pas, en legato.
8. **Ratchet :** répétitions à `k / r` pas, chacune longue de `min(durée, 1 pas) / r`.
9. **Overdub :** la note est écrite au relâchement de la touche, vélocité 100 (le capteur de vélocité `APP_KEY_SENSE` est hors périmètre).

## Hors périmètre

8 emplacements de projet ; enregistrement des punch-ins et p-locks d'enveloppe, filtre, LFO, insert, envois (étape 3) ; usage de la gamme par Marbles et l'affichage des notes (étapes 4 et 5 : ici elle est réglée et sauvegardée) ; pas de KIT (étape 4) ; vélocité des touches ; copier-coller et FILL (spec 14) ; saisie d'accords en saisie pas à pas (REC à l'arrêt reste mono : les accords passent par le pas tenu ou l'overdub).

## Global Constraints

- Identité git `f0nski <13540044+F0nski@users.noreply.github.com>`, remote `git@github-f0nski:F0nski/quartz.git`, branche `step2-sequencer` ; pas de ligne `Co-Authored-By`.
- Code, commentaires et commits en anglais ; pas de libc dans le code de l'app (builtins, comme `seq.c`).
- Longueur 1–64 pas ; divisions `1/32 … 1/1` ; durée de 1/8 de pas à 8 pas ; micro-timing −11/24 à +11/24 par 1/24 ; probabilité 0–100 % par 5 % ; conditions A:B (B 2–8), FIRST, !FIRST, PRE, !PRE ; ratchet 1–4 ; 128 p-locks par pattern.
- Tempo 40–240 BPM, swing 50–75 % ; scènes à la mesure suivante ; chanson ≤ 32 entrées, 1–16 répétitions ; changement de pattern à la fin du pattern en cours.
- Quantisation OFF, 50 %, 100 % ; décompte d'une mesure ; mesure = 4 temps.
- Une seule écriture du projet : la boucle principale. Le rendu ne fait que lire `q_project`.
- La sauvegarde se fait sur SAVE, jamais pendant la lecture. Une étape n'est validée qu'avec zéro bloc perdu dans le pire cas sur l'appareil.

## Review Focus

1. Note longue (8 pas, slide) en cours quand la piste change de pattern, de longueur, de scène ou s'arrête : aucune note bloquée (test `every_on_gets_its_off`, tâche 3 ; `stop_during_tie_releases`, tâche 3 ; `scene_launch_keeps_offs`, tâche 4).
2. Même note rejouée alors que son note-off d'une note longue est encore en file : la nouvelle note n'est pas coupée (test `same_note_retrigger_not_cut`, tâche 2).
3. Tempo changé avec des événements déjà en file (micro-timing, ratchets) : les suivants tombent sur la nouvelle grille, pas de doublon ni de pas sauté (test `tempo_change_with_queue`, tâche 2).
4. Table de p-locks pleine (128), en édition comme en overdub : refus visible (« LOCKS FULL »), rien d'écrasé (test `lock_full_refused`, tâche 1 ; script `plocks.txt`, tâche 6).
5. Sauvegarde interrompue (morceaux de générations différentes), morceau manquant, ou ancien objet v1 : le projet par défaut se charge sans plantage (tests `store_mixed_generations`, `store_missing_chunk`, tâche 1).

---

## Structure des fichiers

Rien de nouveau : tout va dans les modules de l'étape 1.

```
app/project.h    modèle v2 (patterns, pas, p-locks, scènes, chanson, réglages)
app/seq.h/.c     + p-locks, file d'événements, conditions, horloge de mesure, scènes, chanson, localisation
app/store.c      projet en 9 morceaux du pool de stockage
app/app.h        objets OBJ_CHUNK0.., APP_STORE_POOL*
app/app.c        projet en .pool
app/voices.h/.c  + voices_retarget (legato)
app/dsp/pq.*     + pq_glide_to
app/audio.h/.c   p-locks par voix, legato, métronome, overdub, scènes
app/ui.c         SEQ (pas tenu, patterns), HOME (mutes, scènes, SONG), SAVE (couche), GLO (swing, gamme, REC)
app/words.tsv    les mots affichés
tests/unit/*     seq_test.c, store_test.c, audio_test.c, voices_test.c, pq_test.cc
app/tests/*.txt  step_edit, plocks, patterns, home, scenes, song, glo, overdub
docs/cpu.md      mesures de l'étape 2
```

---

### Task 1: Projet v2 et sauvegarde en morceaux

**Files:**
- Modify: `app/project.h`, `app/seq.c` (`project_defaults`, p-locks, lecture du pattern), `app/store.c`, `app/app.h`, `app/app.c`, `app/ui.c` (adaptation aux nouveaux champs, comportement inchangé)
- Test: `tests/unit/seq_test.c`, `tests/unit/store_test.c`

**Interfaces:**
- Produces (`project.h`) :
  ```c
  #define Q_PATTERNS 8
  #define Q_LOCKS 128
  #define Q_SCENES 8
  #define Q_SONG 32
  #define Q_VERSION 2
  enum { SF_ON = 1, SF_SLIDE = 2, SF_ACCENT = 4 };          /* q_step.flags; bits 3-4: ratchet - 1 */
  #define STEP_RATCHET(s) ((((s)->flags >> 3) & 3) + 1)
  enum { CD_NONE, CD_FIRST, CD_NFIRST, CD_PRE, CD_NPRE };   /* q_step.cond < 16; else A:B: A = cond & 15, B = cond >> 4 */
  enum { LK_HARM, LK_TIMB, LK_MORPH, LK_DECAY, LK_TUNE, LK_GLIDE, LK_N };   /* lockable q_sound fields */
  /* note[1..3] 0 = none; vel 1..127; len 1..64 eighths of a step; prob 0..20 (x 5 %); micro -11..11 (24ths) */
  typedef struct { uint8_t note[4], vel, len, prob, cond, flags; int8_t micro; } q_step;
  typedef struct { uint8_t step, param, value; } q_lock;    /* value: the q_sound field's byte (tune as uint8_t) */
  enum { PATH_FWD, PATH_REV, PATH_PINGPONG, PATH_SNAKE, PATH_COLUMNS, PATH_SPIRAL, PATH_RANDOM, PATH_XY, PATH_N };   /* spec 5.1 */
  /* XY: xlen, ylen 1..4, ydiv a DIV_* (the X clock is div) */
  typedef struct { uint8_t len, div, nlocks, path, xlen, ylen, ydiv, pad; q_step steps[Q_STEPS]; q_lock locks[Q_LOCKS]; } q_pattern;
  typedef struct { uint8_t used, mute, pat[Q_TRACKS]; } q_scene;
  typedef struct { uint8_t scene, reps; } q_entry;          /* scene 0..7, reps 1..16 */
  typedef struct {
      uint8_t version; uint16_t tempo;
      uint8_t swing, tonic, mode, quant, click, mute, song_on, nsong;   /* swing 50..75; tonic 0..11; mode 0..10;
                                                         quant 0 OFF 1 50 % 2 100 %; click bit 0 METRO, bit 1 COUNT;
                                                         mute bit per track */
      uint8_t pattern[Q_TRACKS];                        /* the pattern each track is asked to play (and is edited) */
      q_sound sound[Q_TRACKS];
      q_pattern pat[Q_TRACKS][Q_PATTERNS];
      q_scene scene[Q_SCENES];
      q_entry song[Q_SONG];
  } q_project;
  ```
  Défauts : version 2, 120 BPM, swing 50, tonique 0, mode 1 (MAJOR), quant 2, click 0 ; chaque pas : note `{60,0,0,0}`, vel 100, len 4, prob 20, cond 0, flags 0, micro 0 ; patterns len 16, `DIV_16`, `PATH_FWD`, xlen 4, ylen 4, ydiv `DIV_4`, 0 lock.
- Produces (`seq.h`, écrivain : boucle principale ; le rendu lit) :
  ```c
  int  lock_find(const q_pattern *pt, int step, int param);              /* index, -1 none */
  int  lock_set(q_pattern *pt, int step, int param, int value);          /* 0; -1 full (nothing written) */
  void lock_clear_step(q_pattern *pt, int step);
  ```
  `lock_set` écrit l'entrée puis incrémente `nlocks` (barrière entre les deux) ; `lock_clear_step` retire en remplaçant par la dernière entrée puis décrémente : le rendu peut lire un lock en double un instant, jamais un lock à moitié écrit.
- Produces (`app.h`, `store.c`) : `Q_CHUNKS 9` ; `enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_PROJECT, OBJ_CHUNK0, OBJ_NOBJ = OBJ_CHUNK0 + Q_CHUNKS }` (OBJ_PROJECT réservé : l'objet v1, plus lu) ; `APP_STORE_POOL` avec `APP_STORE_POOL_BASE 0x99000u`, `APP_STORE_POOL_N 20`, `APP_STORE_POOL_OBJ0 OBJ_CHUNK0`, `APP_STORE_POOL_OBJN Q_CHUNKS`, `APP_STORE_POOL_OLD 0` (lire l'en-tête de `base/device/storage.c` pour la forme exacte d'`APP_STORE_MAP` avec un pool). `store_load` / `store_save` gardent leur signature.

- [ ] **Step 1 : créer la branche** `git switch -c step2-sequencer`.
- [ ] **Step 2 : tests.**
  - `seq_test.c` : `lock_set_find_clear` (trois locks sur deux pas, `lock_find` les retrouve, `lock_clear_step` retire ceux d'un pas et garde l'autre) ; `lock_set_overwrites` (même pas et paramètre : `nlocks` inchangé, valeur nouvelle) ; `lock_full_refused` (128 locks, le 129e renvoie -1, `nlocks == 128`, les 128 intacts) ; les tests existants passent en lisant `pat[t][pattern[t]]` et `flags & SF_ON`.
  - `store_test.c` (doubles : 9 objets en mémoire, indexés par `obj - OBJ_CHUNK0`) : `roundtrip_full_project` (tous les patterns, scènes et chanson remplis d'octets pseudo-aléatoires dans les bornes → rechargé identique au `memcmp`) ; `store_mixed_generations` (un morceau d'une sauvegarde plus ancienne → défauts, -1) ; `store_missing_chunk` (un morceau absent → défauts, -1) ; `store_rejects_bad_object` repris (version 1, champ hors bornes ramené dans les bornes) ; `save_refused_while_playing` repris.
- [ ] **Step 3 : lancer** `tests/run_tests.sh` → échec de compilation.
- [ ] **Step 4 : implémenter.** `project.h` et `project_defaults` comme ci-dessus ; `seq.c` lit le pattern `p->pat[t][p->pattern[t]]` et `flags & SF_ON` / `note[0]` (gate toujours d'un demi-pas : la durée arrive en tâche 2). `store.c` : chaque morceau = en-tête `{ uint16_t magic 0x515A; uint8_t version, index; uint32_t gen; }` + jusqu'à 3 832 octets du `q_project` vu comme octets (`_Static_assert(sizeof(q_project) <= Q_CHUNKS * 3832)`) ; chargement direct dans le projet par un tampon statique d'un morceau (pas de `q_project` sur la pile) ; tous les morceaux doivent avoir la taille attendue, la version 2 et la `gen` du morceau 0, sinon défauts ; `clamp_project` couvre les nouveaux champs (dont `nlocks ≤ 128`, `step < 64`, `param < LK_N`, `nsong ≤ 32`, `scene < 8`, `reps` 1–16, `pattern < 8`, `path < PATH_N`, `xlen` et `ylen` 1–4, `ydiv < DIV_N`) ; la sauvegarde écrit les 9 morceaux avec `gen + 1`. `app.c` : `static q_project project PLAT_POOL;`. `ui.c` : mêmes adaptations de champs, rien d'autre.
- [ ] **Step 5 : lancer** `tests/run_tests.sh` → tout passe, y compris `save.txt`.
- [ ] **Step 6 : commit** `"Project v2: eight patterns per track, rich steps, p-locks, saved in nine chunks"`

### Task 2: Séquenceur — file d'événements, durée, micro-timing, swing, ratchets

**Files:**
- Modify: `app/seq.h`, `app/seq.c`, `app/audio.c` (appels de `seq_start`)
- Test: `tests/unit/seq_test.c`

**Interfaces:**
- Consumes : `q_project` v2 (tâche 1).
- Produces (`seq.h`) :
  ```c
  #define Q_CLICK_TRACK Q_TRACKS          /* q_event.track of a metronome click: note 1 on a bar's first beat */
  #define SEQ_QUEUE 64                    /* per track */
  enum { EV_LEGATO = 1 };
  typedef struct { uint32_t offset; uint8_t track, note, vel, on, pat, step, flags; } q_event;
  void seq_start(q_seq *s, const q_project *p, int count_in);   /* count_in: the tracks start one bar later */
  void seq_click(q_seq *s, int on);                             /* metronome events while on (and during a count-in) */
  void seq_stop(q_seq *s, q_event *out, int *n);                /* out holds Q_TRACKS * SEQ_QUEUE */
  int  seq_block(q_seq *s, const q_project *p, uint32_t nframes, q_event *out, int max);
  int  seq_pos(const q_seq *s, int track);                      /* step whose grid span holds the block's start; -1 stopped or counting in */
  int  seq_pattern(const q_seq *s, int track);                  /* the pattern playing */
  int  seq_locate(const q_seq *s, const q_project *p, int track, int *micro);  /* nearest step to the block's start, micro -11..11; -1 if none */
  int  seq_quantize(int micro, int quant);                      /* quant 0 OFF, 1 50 %, 2 100 %: micro scaled by 1, 1/2, 0, rounded toward 0 */
  ```
  Les champs de `q_seq` restent privés ; une horloge `{next_i, next_r, num, den, tempo, div}` sert aux pistes et à la mesure (`DIV_1` = 4 temps).

Règles (toutes les dates sont exactes puis arrondies à l'échantillon supérieur, comme à l'étape 1) :
- Un pas est **planifié** quand le début de sa grille moins un demi-pas tombe avant la fin du bloc ; ses note-ons vont dans la file de la piste ; le note-off d'une note entre dans la file quand son note-on est émis. Un événement daté avant le bloc courant (pas 0 au départ, micro négatif) part au début du bloc.
- Date d'une répétition `j` d'un pas `k` : `début(k) + spf × F / 4800` avec `F = 200 × micro + (k impair ? 96 × (swing − 50) : 0) + 4800 × j / r` (pas de `double`) ; gate = `spf × min(len, 8 si r > 1 sinon len) / (8 × r)`.
- Tout le pas : ses notes `note[0..3]` non nulles (dans cet ordre).
- Sens de lecture : la piste avance d'un **rang** `k` (0 à LEN − 1) par pas de grille ; le pas joué est `16 × ⌊k' / 16⌋ + CHEMIN[path][k' mod 16]`, avec `k' = k` (FWD, SNAKE, COLUMNS, SPIRAL), `LEN − 1 − k` (REV), le rang d'un aller-retour de période `2 × LEN − 2` (PING-PONG, `LEN = 1` : toujours 0), ou un rang tiré au hasard dans `0..LEN − 1` (RANDOM). `CHEMIN` : FWD `0..15` ; SNAKE `0,1,2,3,7,6,5,4,8,9,10,11,15,14,13,12` ; COLUMNS `0,4,8,12,1,5,9,13,2,6,10,14,3,7,11,15` ; SPIRAL `0,1,2,3,7,11,15,14,13,12,8,4,5,6,10,9`. Le swing, les conditions et le bouclage suivent le rang ; `seq_pos` et `seq_locate` renvoient le pas. Le `path` est lu au bouclage, comme le pattern.
- PATH_XY : deux horloges ancrées au départ (ou à la scène), X sur la grille de la piste (`div`), Y sur sa propre grille (`ydiv`, même calcul exact que les pistes). Le pas joué est `4 × y + x` (première page) ; `x` avance à chaque pas de X modulo `xlen`, `y` à chaque pas de Y modulo `ylen` ; seule X planifie des notes (Y ne fait que changer de rangée, à son instant exact). Un pas de Y qui tombe au même instant qu'un pas de X est appliqué avant lui. Les conditions, le bouclage et le changement de pattern suivent X (un passage = `xlen` pas de X) ; le swing suit le rang de X.
- Changement de `p->pattern[t]` : pris en compte au bouclage de la piste (fin du pattern en cours) ; à l'arrêt, `seq_start` prend `p->pattern[t]`.
- Un note-on d'une note dont un note-off est en file plus tard avance ce note-off juste avant lui.
- `seq_block` émet par date croissante, les note-offs avant les note-ons à la même date ; ce qui dépasse `max` reste en file pour le bloc suivant. `EV_MAX` de `audio.c` passe à 64.
- Métronome : un clic par temps à partir de l'ancre de mesure (le départ), `note = 1` au premier temps.
- `seq_locate` : par rapport au début swingué du pas ; à mi-chemin, le pas suivant.

- [ ] **Step 1 : tests** (`seq_test.c`, adapter `setup` à `seq_start(&S, &P, 0)`) :
  - `length_sets_gate` : len 1, 4, 8, 64 → note-off à `début + spf × len / 8` (len 64 : 8 pas plus tard, après 7 pas sans note-on sur une piste à un seul pas actif).
  - `micro_shifts_note` : micro −11 et +11 au pas 4 → note-on à `début(4) ∓ spf × 11 / 24` ; le pas 0 à micro −5 au départ tombe au premier échantillon.
  - `swing_delays_odd_steps` : swing 75 → pas 1 à `début(1) + spf / 2`, pas 2 à l'heure ; swing 50 → aucun décalage.
  - `ratchet_repeats` : ratchet 4, len 4 → 4 note-ons à `k × spf / 4`, chacun suivi de son note-off `spf / 8` plus tard ; ratchet 3, len 16 → gates de `spf / 3`.
  - `poly_step_plays_all_notes` : `note = {60, 64, 67, 0}` → 3 note-ons à la même date, 3 note-offs.
  - `same_note_retrigger_not_cut` : pas 0 note 60 len 64, pas 2 note 60 len 4 → le note-off du pas 0 sort juste avant le note-on du pas 2, et le seul autre note-off de 60 est celui du pas 2.
  - `tempo_change_with_queue` : micro +11 sur tous les pas, tempo 120 → 60 pendant le pas 3 : chaque pas joue une fois, dans l'ordre, et le pas 5 tombe à `début(4) + spf(60) + spf(60) × 11 / 24`.
  - `pattern_switch_at_wrap` : `pattern[0]` passé de 0 à 1 au pas 5 d'un pattern de 16 pas : les pas 6–15 viennent du pattern 0, puis le pas 0 du pattern 1 ; `seq_pattern` vaut 1 dès ce pas.
  - `path_fwd_default` : un pattern par défaut joue les pas 0, 1, 2… dans l'ordre (les tests de l'étape 1 le couvrent déjà).
  - `path_orders` : LEN 16 en SNAKE → pas joués `0,1,2,3,7,6,5,4,8,…` ; LEN 14 en SNAKE → les 14 premiers de cet ordre puis retour au pas 0 ; LEN 32 en COLUMNS → `0,4,8,12,…,15` puis `16,20,24,…` ; REV LEN 5 → `4,3,2,1,0` ; PING-PONG LEN 4 → `0,1,2,3,2,1,0,1…`.
  - `path_random_stays_in_length` : RANDOM LEN 6 sur 600 pas → que des pas < 6, chacun joué entre 70 et 130 fois.
  - `path_change_at_wrap` : `path` passé à REV au milieu d'un passage → pris au bouclage suivant.
  - `swing_follows_rank` : SNAKE et swing 75 → le décalage tombe sur les rangs impairs (pas 1, 3, 6, 4…), pas sur les numéros de pas impairs.
  - `xy_two_clocks` : XY, X en `DIV_8T` (1/6 de temps) et Y en `DIV_4T` (2/3 de temps, soit 4 pas de X), `xlen 4`, `ylen 3` → pas joués `0,1,2,3, 4,5,6,7, 8,9,10,11, 0,…` ; avec `xlen 3` → `0,1,2,0, 5,6,4,5, 10,8,9,10, 0,…` (Y change de rangée tous les 4 pas de X pendant que X boucle sur 3, au même instant que le pas de X et avant lui).
  - `xy_y_is_silent` : un pas de Y seul n'émet aucun événement.
  - `xy_lengths_clamp` : `xlen 1`, `ylen 1` → toujours le pas 0.
  - `overflow_stays_queued` : `max = 4` avec 4 pistes en poly 4 sur le même pas → les événements restants sortent au bloc suivant, aucun perdu.
  - `count_in_delays_tracks` : `seq_start(…, 1)` → 4 clics (le premier `note = 1`) puis le pas 0 à `ceil(spf(DIV_1))` ; `seq_pos` vaut -1 pendant le décompte.
  - `click_follows_flag` : `seq_click(1)` → un clic par temps ; `seq_click(0)` → aucun.
  - `locate_and_quantize` : à `début(3) + spf/3` → pas 3, micro 8 ; à `début(3) + 2 × spf / 3` → pas 4, micro −8 ; `seq_quantize(8, 1) == 4`, `seq_quantize(-7, 1) == -3`, `seq_quantize(8, 2) == 0`.
  - Les tests de l'étape 1 passent tels quels (len 4 = demi-pas).
- [ ] **Step 2 : lancer** → échecs.
- [ ] **Step 3 : implémenter** dans `seq.c` selon les règles ci-dessus ; `audio.c` appelle `seq_start(&seq, prj, 0)` (le décompte arrive en tâche 5).
- [ ] **Step 4 : lancer** `tests/run_tests.sh` → tout passe.
- [ ] **Step 5 : commit** `"Sequencer: per-track event queue, note length, micro-timing, swing, ratchets"`

### Task 3: Séquenceur — conditions, probabilité, mutes, accent, slide

**Files:**
- Modify: `app/seq.c`
- Test: `tests/unit/seq_test.c`

**Interfaces:**
- Consumes : la planification de la tâche 2.
- Produces : les événements portent `pat` et `step` (le pas qui les a produits) et `EV_LEGATO`.

Règles, évaluées à la planification du pas :
- `loops` : passages complets du pattern depuis le départ ou le dernier changement de pattern. A:B joue si `loops % B == A − 1` ; FIRST si `loops == 0` ; !FIRST l'inverse.
- PRE / !PRE : le résultat du dernier pas de la piste qui avait une condition autre que NONE, PRE, !PRE, ou une probabilité < 20 ; faux au départ. Les pas PRE / !PRE ne le modifient pas.
- Probabilité : tirée après la condition (xorshift32 dans `q_seq`, graine = `s->now` au départ) ; le résultat de PRE inclut ce tirage.
- Mute (`p->mute`, bit de la piste) : le pas avance et met à jour `loops` et PRE, mais ne joue rien ; les note-offs en file partent.
- Accent : vélocité `min(127, vel + 32)`.
- Slide : la première note du pas n'a pas de note-off ; si le pas joué suivant de la piste a une note, son premier note-on porte `EV_LEGATO` et remplace la note liée ; s'il ne joue pas, la note liée reçoit son note-off au début de la grille de ce pas. `seq_stop` libère une note liée.

- [ ] **Step 1 : tests :**
  - `cond_ab` : cond 2:4 sur un pattern de 4 pas → le pas joue aux boucles 1, 5, 9 seulement (sur 12) ; `first_and_not_first` (FIRST à la boucle 0 seulement, !FIRST jamais à la 0) ; `pre_follows_last_condition` (pas 0 en 1:2, pas 1 en PRE, pas 2 en !PRE : le pas 1 joue aux boucles paires, le pas 2 aux impaires).
  - `probability_rates` : prob 10 (50 %) sur 1 000 boucles → entre 400 et 600 ; prob 0 → jamais ; prob 20 → toujours.
  - `mute_silences_but_advances` : piste muette 3 pas puis démutée → le pas suivant joué est le bon (pas de décalage), une note longue commencée avant le mute reçoit son note-off.
  - `accent_boosts_velocity` : vel 100 → 127 ; vel 60 → 92.
  - `slide_ties_to_next` : pas 0 slide note 60, pas 1 note 64 → aucun note-off de 60, note-on 64 avec `EV_LEGATO` ; si le pas 1 est éteint : note-off de 60 au début du pas 1.
  - `stop_during_tie_releases` : `seq_stop` pendant une note liée → son note-off.
  - `events_carry_step` : `pat` et `step` des événements = pattern et pas qui les ont produits.
  - `every_on_gets_its_off` : 4 pistes, 2 000 blocs, attributs tirés au hasard (graine fixe) pour chaque pas, changements de pattern, de longueur, de tempo et de mute aléatoires en cours, puis `seq_stop` : chaque note-on a exactement un note-off (ou un `EV_LEGATO` qui la remplace), jamais un note-off sans note-on.
- [ ] **Step 2 : lancer** → échecs.
- [ ] **Step 3 : implémenter.**
- [ ] **Step 4 : lancer** → tout passe.
- [ ] **Step 5 : commit** `"Sequencer: conditions, probability, mutes, accent and slide"`

### Task 4: Séquenceur — scènes et chanson

**Files:**
- Modify: `app/seq.h`, `app/seq.c`
- Test: `tests/unit/seq_test.c`

**Interfaces:**
- Produces :
  ```c
  void seq_launch(q_seq *s, int scene);      /* at the next bar while running */
  int  seq_scene_started(q_seq *s);          /* the scene launched since the last call, -1 none */
  int  seq_song_entry(const q_seq *s);       /* the song entry playing, -1 when not in song mode */
  ```

Règles :
- Le séquenceur garde le dernier `p->pattern[t]` et `p->mute` vus : seul un **changement** de ces champs est une demande (pattern au bouclage, mute tout de suite). Une scène lancée met à jour ces valeurs vues : quand la boucle principale recopie ensuite la scène dans le projet, rien ne change.
- Une scène lancée prend effet au début de la mesure suivante `B` : chaque piste repart au pas 0 de `scene.pat[t]` ancré à `B`, `loops = 0` ; les note-ons en file datés de `B` ou après sont retirés ; les note-offs restent ; le mute effectif devient `scene.mute`. Une scène non utilisée (`used == 0`) ne change rien.
- Mode chanson (`song_on` et `nsong > 0`) : `seq_start` lance l'entrée 0 au départ (après le décompte s'il y en a un). Une entrée dure `reps × ceil(max_t(len_t × u(div_t)) / 96)` mesures, `u = {3, 4, 6, 8, 12, 16, 24, 48, 96}` (vingt-quatrièmes de temps), calculé à son lancement ; à sa fin, l'entrée suivante est lancée ; après la dernière, l'entrée 0.

- [ ] **Step 1 : tests :**
  - `scene_launch_at_next_bar` : lancée au pas 5 d'un 1/16 à 120 BPM → les 4 pistes jouent le pas 0 de leur pattern de scène à `ceil(2 × Q_SR)` (la mesure 1), le mute effectif suit la scène ; `seq_scene_started` renvoie la scène une fois puis -1.
  - `scene_launch_keeps_offs` : une note de 8 pas en cours à `B` reçoit son note-off ; les note-ons planifiés pour `B` et après depuis l'ancien pattern ne sortent pas.
  - `scene_then_project_copy_is_no_request` : après le lancement, recopier la scène dans `p->pattern` et `p->mute` ne programme aucun changement.
  - `song_advances_and_loops` : chanson `{scène 0 ×2, scène 1 ×1}`, scène 0 = patterns de 16 pas en 1/16 (1 mesure), scène 1 = un pattern de 24 pas en 1/16 (2 mesures) → scène 1 lancée à la mesure 2, scène 0 à la mesure 4 ; `seq_song_entry` vaut 0, 1, 0.
  - `song_with_count_in` : l'entrée 0 démarre après la mesure de décompte.
- [ ] **Step 2 : lancer** → échecs.
- [ ] **Step 3 : implémenter.**
- [ ] **Step 4 : lancer** → tout passe.
- [ ] **Step 5 : commit** `"Sequencer: scenes at the next bar and song mode"`

### Task 5: Rendu — p-locks, legato, métronome, overdub, scènes

**Files:**
- Modify: `app/audio.h`, `app/audio.c`, `app/voices.h`, `app/voices.c`, `app/dsp/pq.h`, `app/dsp/pq.cc`
- Test: `tests/unit/audio_test.c`, `tests/unit/voices_test.c`, `tests/unit/pq_test.cc`

**Interfaces:**
- Consumes : `seq_*` (tâches 2–4), `lock_find` (tâche 1).
- Produces :
  ```c
  /* voices.h */
  int  voices_retarget(q_voices *p, int track, int note);   /* the track's newest V_ON slot takes note, -1 none */
  /* pq.h */
  void pq_glide_to(int v, float note);                      /* new pitch, no trigger: slides over the voice's glide */
  /* audio.h */
  typedef struct { uint8_t track, pat, step, note, vel, len; int8_t micro; } q_rec;
  void audio_rec(int on);           /* overdub armed: live notes while playing are recorded; metronome per prj->click */
  void audio_scene(int scene);      /* while playing: at the next bar */
  int  audio_take_rec(q_rec *r);    /* main loop: 1 and one recorded note, 0 none */
  int  audio_scene_started(void);   /* -1 none */
  int  audio_pattern(int track);
  int  audio_song_entry(void);
  ```

Règles :
- Note-on venu du séquenceur : la voix garde les locks du pas (`pat`, `step`) en masque + valeurs ; `set_patch` applique la valeur lockée à la place de celle de la piste tant que la voix joue cette note. Note jouée au clavier : pas de lock.
- `EV_LEGATO` : `voices_retarget` puis `pq_glide_to` (note + tune) ; sans voix, un note-on normal.
- Métronome : sur un événement `Q_CLICK_TRACK`, un clic ajouté au mix depuis son offset : sinus 1 kHz (2 kHz si `note = 1`), amplitude 0,3, décroissance exponentielle de 15 ms. `seq_click` = enregistrement armé et bit METRO.
- `CMD_PLAY` : décompte si l'enregistrement est armé et le bit COUNT est mis.
- Overdub (armé, en lecture, hors décompte) : au note-on du clavier, `seq_locate` donne le pas et le micro ; au note-off, `len = round(durée × 8 / spf)` borné 1–64, micro passé par `seq_quantize(micro, prj->quant)`, et un `q_rec` part vers la boucle principale (file de 32, une note de trop est perdue) ; 8 notes tenues suivies au plus. `audio_play(0)` envoie celles encore tenues avec leur durée jusque-là.
- `audio_scene` passe par la file de commandes (`CMD_SCENE`) ; `audio_scene_started`, `audio_pattern`, `audio_song_entry` lisent l'état du séquenceur.

- [ ] **Step 1 : tests.**
  - `voices_test.c` : `retarget_takes_newest_on` ; `retarget_none_without_on`.
  - `pq_test.cc` : `glide_to_keeps_envelope` (VA, decay court : après `pq_glide_to`, le RMS des 20 ms suivantes ne dépasse pas 1,2 × celui des 20 ms d'avant, et la hauteur atteint la nouvelle note ±1 % en moins de 0,6 s avec glide 1).
  - `audio_test.c` (mock `pq_set_patch` étendu pour journaliser harm ; mock `pq_glide_to`, op `'g'`) : `lock_reaches_its_voice` (pas 0 lock HARM 127 → la voix du pas reçoit harm 1,0, la note jouée au clavier sur une autre piste garde 64/127) ; `lock_ends_with_note` (pas suivant sans lock → harm de la piste) ; `legato_glides_without_retrigger` (slide → `'g'`, pas de `'n'` pour la seconde note) ; `click_on_beats` (REC armé, METRO, voix muettes : échantillons non nuls à chaque temps, nuls entre) ; `count_in_then_play` ; `overdub_records_step` (tempo 60, note au bloc qui contient le début du pas 4 + spf/3, relâchée un pas plus tard → `q_rec` pas 4, micro 8, len 8 avec quant OFF, micro 0 avec quant 100 %) ; `stop_flushes_held_rec` ; `scene_reported_once`.
- [ ] **Step 2 : lancer** → échecs.
- [ ] **Step 3 : implémenter** `voices_retarget`, `pq_glide_to` (pq.cc : cible de la note sans `trigger_patched`), puis `audio.c`.
- [ ] **Step 4 : lancer** `tests/run_tests.sh` → tout passe.
- [ ] **Step 5 : commit** `"Render: per-voice p-locks, legato, metronome, count-in, overdub capture, scenes"`

### Task 6: Interface SEQ — pas tenu, attributs, p-locks, patterns

**Files:**
- Modify: `app/ui.c`, `app/words.tsv`
- Test: `app/tests/step_edit.txt`, `app/tests/plocks.txt`, `app/tests/patterns.txt` (et `seq.txt` inchangé doit passer)

**Interfaces:**
- Consumes : `lock_*` (tâche 1), `audio_pattern` (tâche 5).
- Produces (`ui_expect`) : `held` (pas tenu 1–64, 0 aucun), `attr`, `path`, `xlen`, `ylen`, `ydiv`, `pattern` (demandé, 1–8), `playpat`, `locks` (nombre du pattern édité), `stepvel_N`, `steplen_N`, `stepprob_N`, `stepcond_N`, `stepratchet_N`, `stepslide_N`, `stepaccent_N`, `stepmicro_N`, `stepnote2_N` (`note[1]`), `lockharm_N` (valeur ou -1).

Comportement dans SEQ (on édite toujours `p->pattern[piste]`) :

| Geste | Effet |
|---|---|
| Blanche appuyée | ce pas devient le pas tenu |
| Blanche relâchée sans modification | bascule le pas (allumé : note = dernière note jouée, vel 100) |
| Autre blanche pendant le maintien | joue la note (53 + touche + 12 × octave) et l'écrit : la 1re remplace l'accord, les suivantes remplissent `note[1..poly−1]` ; le pas s'allume |
| Noire pendant le maintien (ordre `PLAT_BLACK_KEYS`) | 0 RATCHET · 1 PROB · 2 COND · 3 SLIDE (bascule) · 4 ACCENT (bascule) · 5 NOTE · 6 LEN · 7 MICRO · 8 CLEAR (pas éteint, valeurs par défaut, locks du pas retirés) · 9–10 rien |
| PRESET pendant le maintien | règle l'attribut choisi (VEL au début du maintien) : VEL 1–127, RATCHET 1–4, PROB 0–20, COND dans l'ordre NONE, FIRST, !FIRST, PRE, !PRE, 1:2, 2:2, 1:3 … 8:8, NOTE ±1 demi-ton sur tout l'accord, LEN 1–64, MICRO −11–11 |
| SELECT pendant le maintien | page de p-locks : ENGINE (HARM TIMB MORPH DECAY) ou SOUND (—, TUNE, GLIDE, —) |
| K1–K4 pendant le maintien | `lock_set` de (valeur lockée ou de la piste) + pas ; le pas s'allume ; table pleine : « LOCKS FULL » 1,5 s dans l'en-tête |
| SEQ maintenu + blanches 1–8 | pattern demandé de la piste (à l'arrêt, il joue au prochain PLAY) |
| PRESET sans pas tenu | pattern demandé ±1 |
| K3 sur la page STEPS | PATH : FWD, REV, PING-PONG, SNAKE, COLUMNS, SPIRAL, RANDOM, XY (la page devient LEN, DIV, PATH) |
| Page XY (SELECT, présente seulement quand PATH = XY) | K1 X LEN 1–4, K2 X DIV (= DIV de la piste), K3 Y LEN 1–4, K4 Y DIV |

Écran : la grille des 16 pas devient 4×4 (pas 1–4 en première rangée, c'est l'ordre FWD) ; pendant qu'on tourne K3, le chemin se dessine en traits entre les cases. Le plateau isométrique de la maquette H2 (`docs/screens/sequencer.html`) arrive avec `gfx` à l'étape 5. En-tête « SEQ P3 » (« P1>P3 » tant que le pattern demandé n'est pas celui qui joue) ; pas tenu → la zone de scène liste NOTE, VEL, LEN, MICRO, PROB, COND, RATCHET, SLIDE, ACCENT, l'attribut choisi en blanc sur fond K4 ; le pied montre la page de p-locks, valeur lockée en couleur de l'encodeur, valeur de la piste en gris ; grille des pas : un petit carré dans le coin d'un pas qui a des locks.

- [ ] **Step 1 : scripts.**
  - `step_edit.txt` : `tap SEQ` ; `key 0 down`, `expect held 1`, `key 1 down` (noire 0), `turn PRESET 2`, `expect stepratchet_1 3`, `key 1 up`, `key 3 down`/`up` (noire 1), `turn PRESET -10`, `expect stepprob_1 10`, `key 5 down`/`up` (noire 2), `turn PRESET 5`, `expect stepcond_1 33` (1:2 = 0x21, 6e de la liste), `key 8 down`/`up` (noire 3), `expect stepslide_1 1`, `key 0 up`, `expect held 0`, `expect stepon_1 0` (rien qui l'allume) ; puis la note : `key 0 down`, `key 4 down`/`up` (blanche 3 = note 57), `key 0 up`, `expect stepon_1 1`, `expect stepnote_1 57` ; accord : `tap EDIT`, `turn K4 2` (poly 3), `tap SEQ`, `key 2 down`, `key 4 down`/`up`, `key 7 down`/`up`, `key 2 up`, `expect stepnote_2 57`, `expect stepnote2_2 60` ; CLEAR : `key 2 down`, `key 20 down`/`up` (noire 8), `key 2 up`, `expect stepon_2 0`, `expect locks 0` ; `shot step_held.png` pendant un maintien.
  - `plocks.txt` : pas tenu + `turn K1 20` → `expect lockharm_1 84`, `expect locks 1`, `expect stepon_1 1` ; `turn SELECT 1` + `turn K2 3` → un lock TUNE ; pas relâché, `turn K1 5` (hors maintien : sur la page STEPS = LEN) ne crée pas de lock ; remplir 128 locks (le geste répété : 16 blanches × 4 pages de pas, HARM et TIMB) puis un 129e : `expect locks 128`, `expect msg_locks_full 1`.
  - `patterns.txt` : `turn K3 3`, `expect path 3` (SNAKE), `turn K3 10`, `expect path 7` (borne : XY), `turn SELECT 1`, `expect page 2` (page XY), `turn K3 -1`, `expect ylen 3`, `turn K4 2`, `expect ydiv 8`, `turn SELECT 1`, `turn K3 -7`, `expect path 0`, `turn SELECT 1`, `expect page 1` (page XY masquée) ; `shot seq_path.png` pendant le réglage ; `press SEQ`, `key 2 down`/`up` (blanche 2), `release SEQ`, `expect pattern 2` ; allumer le pas 1, `expect stepon_1 1` ; `turn PRESET -1`, `expect pattern 1`, `expect stepon_1 0` (un autre pattern) ; en lecture : `tap PLAY`, demander le pattern 2, `expect playpat 1`, attendre la fin du pattern (`wait 2100` à 120 BPM), `expect playpat 2`.
- [ ] **Step 2 : lancer** `tests/run_tests.sh` → les nouveaux scripts échouent.
- [ ] **Step 3 : implémenter** dans `ui.c` ; mots nouveaux dans `words.tsv`.
- [ ] **Step 4 : lancer** → tout passe ; regarder `app/build/host/step_held.png`.
- [ ] **Step 5 : commit** `"SEQ: held step editing, step attributes, p-locks and pattern selection"`

### Task 7: Interface HOME — mutes, scènes, chanson ; couche SAVE

**Files:**
- Modify: `app/ui.c`, `app/words.tsv`
- Test: `app/tests/home.txt`, `app/tests/scenes.txt`, `app/tests/song.txt` (et `save.txt` doit passer)

**Interfaces:**
- Consumes : `audio_scene`, `audio_scene_started`, `audio_song_entry`, `audio_pattern` (tâche 5).
- Produces (`ui_expect`) : `mute` (masque), `scene_used_N`, `song_n`, `song_scene_N`, `song_reps_N`, `song_on`, `entry` (curseur 1–32, 0 aucun), `playentry` (`audio_song_entry() + 1`), `msg_scene` (scène en attente 1–8, 0 aucune).

Comportement :
- Domaine HOME (bouton `B_HOME`), pages TRACKS et SONG.
- TRACKS : 4 lignes (numéro, modèle, « P2 » joué et « >P3 » demandé, mute, barre de position) ; pas de paramètre.
- SONG : K1 ENTRY (= PRESET), K2 SCENE 1–8, K3 REPEAT 1–16, K4 MODE PATTERN / SONG ; SEL (tap, sans HOME maintenu) efface l'entrée du curseur ; la liste affiche jusqu'à 6 entrées autour du curseur, celle qui joue en rose.
- HOME maintenu + blanches 1–4 : bascule le mute de la piste (`p->mute`, effet immédiat).
- HOME maintenu + blanches 9–16 (scène 1–8) : SEL aussi maintenu, ou scène vide → mémorise `{used 1, mute, pattern[]}` ; sinon à l'arrêt → recopie la scène dans `p->pattern` et `p->mute` ; en lecture → `audio_scene`, « SCENE n » dans l'en-tête jusqu'au lancement.
- Chaque image : `audio_scene_started()` ≥ 0 → la scène recopiée dans `p->pattern` et `p->mute`.
- SAVE : sauvegarde au **relâchement** si aucune blanche n'a été appuyée pendant le maintien ; SAVE maintenu + blanches 1–8 : ajoute `{scène, 1}` à la chanson (32 au plus), le curseur dessus.

- [ ] **Step 1 : scripts.**
  - `home.txt` : `press HOME`, `key 0 down`/`up`, `release HOME`, `expect mute 1` ; mémoriser la scène 1 (blanche 9 = touche 14) avec pattern 1 et mute 1 ; changer de pattern et démuter ; relancer la scène 1 à l'arrêt → `expect pattern 1`, `expect mute 1` ; `press SEL` + scène 1 → réécrite avec l'état courant ; `shot home.png`.
  - `scenes.txt` : deux scènes aux patterns différents ; `tap PLAY`, lancer la scène 2, `expect msg_scene 2`, `expect playpat <ancien>` ; `wait 2100` → `expect playpat 2`, `expect msg_scene 0`, `expect pattern 2`.
  - `song.txt` : `press SAVE`, blanches 1 et 2, `release SAVE` → `expect song_n 2`, `expect dirty 1`, `expect store_writes 0` ; page SONG : `turn K3 1` → `expect song_reps_2 2` ; `turn K4 1`, `expect song_on 1` ; `tap PLAY`, `expect playentry 1`, `wait 2100`, `expect playentry 2` ; `tap PLAY` ; `turn K1 -1`, `tap SEL`, `expect song_n 1` ; `shot song.png`.
- [ ] **Step 2 : lancer** → échecs.
- [ ] **Step 3 : implémenter** ; mots dans `words.tsv`.
- [ ] **Step 4 : lancer** → tout passe.
- [ ] **Step 5 : commit** `"HOME: mutes, scenes and song page; SAVE layer appends scenes"`

### Task 8: Interface GLO et enregistrement live

**Files:**
- Modify: `app/ui.c`, `app/words.tsv`
- Test: `app/tests/glo.txt`, `app/tests/overdub.txt` (et `nav.txt`, `bounds.txt` mis à jour pour les pages de GLO)

**Interfaces:**
- Consumes : `audio_rec`, `audio_take_rec`, `audio_pos`, `audio_pattern` (tâche 5), `lock_set` (tâche 1).
- Produces (`ui_expect`) : `swing`, `tonic`, `mode`, `quant`, `click`, `overdub`.

Comportement :
- GLO : TEMPO (K1 TEMPO, K2 SWING 50–75 %) · SCALE (K1 TONIC C…B, K2 MODE CHROM, MAJOR, MINOR, DORIAN, PHRYG, LYDIAN, MIXO, LOCRIAN, HARM MIN, PENTA, MIN PENTA, K3 QUANT OFF / 50 % / 100 %, K4 CLICK OFF / METRO / COUNT / BOTH) · CPU.
- REC bascule l'armement à l'arrêt comme en lecture ; armé en lecture = overdub (`audio_rec(1)`). PLAY qui démarre avec REC armé lance l'overdub (décompte selon CLICK) ; PLAY qui arrête désarme REC.
- Chaque image : chaque `q_rec` reçu écrit dans `p->pat[track][pat]` : pas éteint → `note = {note, 0, 0, 0}`, vel, len, micro, prob 20, cond NONE, flags ON ; pas allumé → piste mono : `note[0]`, vel, len, micro remplacés ; piste POLY : la note ajoutée dans le premier `note[1..poly−1]` libre si elle n'y est pas déjà ; `dirty = 1`.
- Overdub : un paramètre lockable tourné change la piste comme d'habitude et pose aussi un lock sur `audio_pos` si ce pas est allumé et que `audio_pattern == p->pattern` ; table pleine → « LOCKS FULL ».

- [ ] **Step 1 : scripts.**
  - `glo.txt` : `tap GLO`, `turn K2 30` → `expect swing 75` ; `turn SELECT 1` ; `turn K1 -1` → `expect tonic 0` (borne) ; `turn K2 1`, `turn K3 -2`, `turn K4 3` → `expect mode 2`, `expect quant 0`, `expect click 3` ; `shot glo_scale.png` ; sauvegarde et `reboot` → valeurs gardées.
  - `overdub.txt` : tempo 60, QUANT 100 %, `tap SEQ`, `tap REC`, `tap PLAY`, `expect overdub 1` ; `wait 1000` (pas 4), `key 7 down`, `wait 250`, `key 7 up`, `wait 50` → `expect stepon_5 1`, `expect stepnote_5 60`, `expect steplen_5 8`, `expect stepmicro_5 0` ; overdub de p-lock : `tap EDIT`, `wait 3700` (le pas 5 rejoue), `turn K1 10` → `expect lockharm_5 74` ; `tap PLAY` → `expect rec 0` ; décompte : CLICK COUNT, `tap REC`, `tap PLAY`, `wait 500` → `expect pos -1`, `wait 4100` → `expect pos >-1`.
- [ ] **Step 2 : lancer** → échecs.
- [ ] **Step 3 : implémenter** ; mots dans `words.tsv`.
- [ ] **Step 4 : lancer** `tests/run_tests.sh` → tout passe.
- [ ] **Step 5 : commit** `"GLO: swing, scale and REC settings; live overdub of notes and p-locks"`

### Task 9: Vérification sur l'émulateur et sur le FM-1

**Files:**
- Modify: `docs/cpu.md`

- [ ] **Step 1 : émulateur.** `./build.sh`, puis `fm1-emu --fwsc app/build/quartz.fwsc --ms 5000 --keys '3000:PLAY' --snap 4900` : démarre sans faute.
- [ ] **Step 2 : appareil** (fait par l'utilisateur, sauvegarde M-Flasher d'abord) : `.tools/venv/bin/python base/tools/fm1_install.py --yes --classic app/build/quartz.fwsc`. Vérifier d'abord le correctif de l'étape 1 resté en attente : LOST BLOCKS = 0 au premier PLAY et aux changements de modèle.
- [ ] **Step 3 : pire cas.** Les 4 pistes en MODAL, POLY 2 sur les pistes 1 et 2, 16 pas actifs en 1/16 à 240 BPM, ratchet 4 et 8 p-locks par pattern, swing 75, REC armé + METRO en lecture ; puis lancer une scène toutes les mesures pendant 60 s : GLO p3 → LOST BLOCKS = 0. Recommencer en ADDITIVE.
- [ ] **Step 4 : sauvegarde.** Projet rempli (4 pistes × 8 patterns, scènes, chanson) : SAVE, noter la durée de la coupure, éteindre, rallumer : tout est revenu.
- [ ] **Step 5 : `docs/cpu.md`** : section « étape 2 » avec les charges des deux cœurs, les blocs perdus et la durée de la sauvegarde.
- [ ] **Step 6 : commit** `"Record the step 2 measurements"` ; pousser la branche, ouvrir la PR.
