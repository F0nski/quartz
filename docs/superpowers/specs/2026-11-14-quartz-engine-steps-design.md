# QUARTZ — ENGINE : des crans sur les potards qui choisissent

Sur certains modèles, un potard de la page ENGINE choisit parmi quelques valeurs (un preset, un accord, un motif
d'arpège, une banque de mots), mais il se tourne de 0 à 127 et affiche un nombre : on cherche le choix suivant à
l'oreille. Ces potards passent d'un choix au suivant à chaque cran et affichent le nom du choix.

## 1. But

Critères :
- sur un potard à crans, un cran de l'encodeur joue le choix suivant (ou le précédent), jamais le même ;
- le pied de page affiche le nom du choix (`M7`, `UP 2`, `WORDS 3`, `E.PIANO 1`), pas un nombre ;
- le reste ne change pas : la valeur reste 0–127 dans le projet, les p-locks et le LFO sur HARM gardent leur sens.

Potards concernés (la piste synthé, page ENGINE) :

| Modèle | Potard | Choix |
|---|---|---|
| HEX A, HEX B, HEX C | HARM | 32 presets |
| CHORALE, ENSEMBLE, ARCADE | HARM | 11 accords |
| ARCADE | TIMB | 12 motifs d'arpège |
| ORATOR | HARM | de 0 à 42 continu (FORMANT, SAM, LPC), puis 6 crans (PHONEMES, WORDS 1–5) |

Hors périmètre : PIPE (son HARM fond les banques en continu sur sa première moitié) ; le MORPH d'ORATOR (le nombre de
mots dépend de la banque) ; les pages PAD du KIT (les pads de batterie n'ont pas de choix discret) ; les scènes
animées ; le LFO (il module toujours HARM en continu, à travers les choix).

## 2. Données

Aucune : `harm` et `timb` restent des `uint8_t` 0–127 dans `q_sound` et dans les p-locks. Pas de changement de
`Q_VERSION`.

## 3. Les zones

Plaits choisit par des `HysteresisQuantizer2` (`stmlib/dsp/hysteresis_quantizer.h`). Une zone à crans se décrit par
son début `start`, sa largeur `width` (en unités 0–127) et son nombre de crans `n` ; le cran `k` vaut
`round(start + (k + 0.5) * width)`, le milieu de sa zone :

| Cas | Code Plaits | n | start | width | Milieux |
|---|---|---|---|---|---|
| HEX HARM | `six_op_engine.cc` : `Init(32, 0.005)`, `harmonics * 1.02` | 32 | 0 | 127 / 32 / 1.02 | 2, 6, 10 … 119, 123 |
| Accords HARM | `chord_bank` : `Init(11, 0.075)`, `parameter * 1.02` | 11 | 0 | 127 / 11 / 1.02 | 6, 17, 28 … 108, 119 |
| ARCADE TIMB | `chiptune_engine.cc` : `Init(12, 0.075)`, `timbre` | 12 | 0 | 127 / 12 | 5, 16, 26 … 111, 122 |
| ORATOR HARM | `speech_engine.cc` : `group = harmonics * 6`, au-delà de 2 `Init(6, 0.1)` sur `(group - 2) * 0.275` | 6 | 127 / 3 | 127 / 6 / 1.65 | 49, 62, 74, 87, 100, 113 |

Chaque milieu donne le choix attendu quel que soit le choix précédent de Plaits (hystérésis comprise), vérifié pour
toutes les valeurs.

Le cran d'une valeur `v` : `floor((v - start) / width)`, borné à 0..n-1.

Tourner de `d` crans :
- dans une zone à crans, la valeur va au milieu du cran `k + d`, borné à 0..n-1 ; une valeur qui n'est pas un milieu
  (un projet fait avant) part de son cran ;
- au-delà d'un bout (le cran borné est déjà celui de la valeur), la valeur ne bouge pas : 127 sur HEX reste 127 ;
- ORATOR HARM, de 0 à 42 : `v + d`, comme aujourd'hui ; au-delà de 42, la valeur va au milieu du premier cran (49) ;
  depuis le premier cran, un cran vers le bas ramène à 42 ;
- les autres potards ne changent pas.

La règle vaut pour `knob_turn` (le potard) et pour `lock_turn` (un pas tenu, le p-lock).

## 4. Les noms

Le pied de page (`knob_text`) affiche, pour la page ENGINE et le modèle de la piste :

- accords : `OCTAVE`, `FIFTH`, `SUS4`, `m`, `m7`, `m9`, `m11`, `6/9`, `M9`, `M7`, `M` (l'ordre de `chord_bank.cc` ;
  `m` mineur, `M` majeur, comme les scènes CHORALE et ARCADE les écrivent au-dessus du pied de page ; `OCTAVE`,
  `FIFTH`, `6/9` plutôt que `OCT`, `5`, `69`, que `words.tsv` lit déjà autrement ou qui seraient lus ainsi partout) ;
- arpèges ARCADE : motif `p`, sens `p / 3` (`UP`, `DOWN`, `UP-DN`, `RAND`) et étendue `1 << p % 3` octaves :
  `UP 1`, `UP 2`, `UP 4`, `DOWN 1` … `RAND 4` ;
- ORATOR : `FORMANT` à 0, `SAM` à 21, `LPC` à 42, la valeur entre deux ; puis `PHONEMES`, `WORDS 1` … `WORDS 5` ;
- HEX : le nom du preset, d'une table `HEX_NAME[3][32]` dans `ui.c` (pas de lecture des banks DX7 à l'exécution).

Les noms HEX sont ceux des banks DX7 de Plaits (`syx_bank_0..2`, octets 118–127 de chaque voix), en capitales, sans
`*` ni `'`, espaces multiples réduits à un, espaces de fin retirés. Ceux qui citent une marque, un modèle ou un artiste
sont renommés :

| Bank | DX7 | QUARTZ |
|---|---|---|
| A | Mooger Low | DEEP LOW |
| A | MORHOL TB1 | ACID LINE |
| A | CROMA 2 | PRISMA 2 |
| A | CS 80 | BIG POLY |
| A | DX-TROTT | TROTTER |
| A | FAIRLIGHT | VINTAGE 1 |
| A | *Fairl. 3 | VINTAGE 3 |
| A | *PPG*Vol.1 | WAVE VOL 1 |
| A | *PPG*Vol.2 | WAVE VOL 2 |
| B | FENDER 1 | TINES 1 |
| B | WINTRHODES | WINTER EP |
| B | *Mark III | EP DELUXE |
| B | CLAVINET | CLAV |
| B | STEINWAY | CONCERT |
| C | *Hammond 1 | TONEWHEEL |
| C | JX-33-P | POLY 33 |
| C | M1 PADS | WAVE PADS |
| C | CARLOS 2 | CLASSIC 2 |
| C | VANGELIS 1 | CINEMA 1 |
| C | Etherial5a | ETHEREAL 5 |

Un nom plus large qu'une colonne du pied de page (54 px, même en `FONT_XS`) est raccourci : SOLID BAS, LEADTAPE,
RESONANT, SYNBASS 2, PRC SYN 1, WINE GLAS, WAVE V1, WAVE V2 ; CLAV EP, GRAND 1, AC GUITAR, HARPSI 1, XYLO, GLOCKEN,
LATIN 4, SYN DM 25, BD-SNARE ; DRAWBAR, 60S ORG, SCORE, WAVE PAD, SOFT TCH, TEXTURE 6, ETHER 5, FULL STR, TRUMPET.

Les scènes CHORALE/ENSEMBLE et ARCADE prennent l'accord comme Plaits et le pied de page (`harm * 1.02` en 11 zones).

L'instantané du pied de page (`q_view.foot`) porte le modèle de la piste, pour que `knob_text` choisisse le nom. Un nom
trop large pour sa colonne passe déjà en police plus petite (`draw_foot`). La barre sous la valeur suit toujours `v`.

Chaque nouveau mot a sa ligne dans `app/words.tsv`.

## 5. Manuel

`app/docs/MANUAL.md` : la ligne ENGINE dit que HARM (et TIMB sur ARCADE) avance d'un choix par cran sur ces modèles et
affiche son nom ; les cellules HARM et TIMB du tableau des modèles nomment les choix ; une capture de CHORALE montre un
nom d'accord dans le pied de page (ajoutée à `app/tests/manual_shots.txt`).

## 6. Tests

- Test unitaire C++ (`tests/unit/engine_steps_test.cc`) : chaque milieu de la section 3, passé dans un
  `HysteresisQuantizer2` initialisé comme dans Plaits, donne son cran quel que soit le cran précédent.
- Script simulateur `app/tests/engine_steps.txt` : `ui.c` pose bien ces milieux. CHORALE, HARM un cran à la fois :
  6, 17, 28 … 119 ; ORATOR, HARM de 42 monte à 49 puis redescend à 42, puis 41 ; un p-lock de HARM sur un pas tenu
  avance aussi d'un accord par cran ; HEX A et ARCADE TIMB jusqu'à leurs bornes. Les noms (`OCTAVE`, `FIFTH`, `LPC`,
  `PHONEMES`, `SOLID BASS`, `DEEP LOW`, `UP 2`) se vérifient sur ses captures : `expect` ne lit que des nombres.
- `tests/run_tests.sh` au complet ; écoute sur le FM-1 au déploiement.
