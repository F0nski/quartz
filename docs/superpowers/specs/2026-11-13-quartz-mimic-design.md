# QUARTZ — source MIMIC : il improvise d'après ce que tu joues

Une neuvième source de pattern, après STEP, GRIDS, MARBLES, LIFE, LANES, CHORDS, TONNETZ et ORBITS (spec globale
`2026-11-10-quartz-groovebox-design.md`, sections 3 et 6). Elle part de la branche `orbits`. Maquettes : idées
`docs/screens/sequencers.html`, scène `docs/screens/mimic.html` (tête de lecture et quarts de couleur).

## 1. But

Un partenaire de jeu : tu joues une phrase au clavier, MIMIC la retient (notes et rythme) puis improvise en recollant
des morceaux de ta phrase, à la manière du Continuator : ça ressemble à ce que tu as joué sans le répéter.

Critères :
- un pattern MIMIC apprend une phrase du clavier (REC court, LISTEN) et l'improvise, sur une piste synthé comme sur
  un KIT, à la division du pattern ;
- l'impro garde les notes et le rythme de la phrase ; ORDER, DRIFT et LOOP règlent sa fidélité, sa liberté et sa
  répétition ; PRESET fait un nouveau tirage ;
- une variation trouvée avec LOOP revient à l'identique, même après un rechargement du projet ;
- chaque encodeur change un élément visible de la scène ; la scène tient dans 300 primitives ;
- les autres sources jouent et s'affichent comme avant.

Hors périmètre : apprendre la vélocité ou la durée des notes, un ordre 3, un apprentissage permanent (en glissant
pendant le jeu), l'apprentissage depuis le MIDI externe, la fusion de deux phrases, l'édition note à note de la
phrase, un mode GRILLE.

## 2. Le jeu

### Apprendre

- Sur un pattern MIMIC, en SEQ, un **REC court** passe en **LISTEN** ; un nouveau REC court le quitte. Changer de
  pattern, de piste ou de source le quitte aussi.
- En LISTEN, le générateur de la piste se tait. Chaque note jouée au clavier sur la piste sonne comme d'habitude et
  s'ajoute à la phrase, transport en marche ou à l'arrêt. La première note de LISTEN efface l'ancienne phrase ; quitter
  LISTEN sans avoir joué la garde.
- Un **événement** : la note jouée (1..127, telle que le clavier l'envoie, pad compris sur un KIT) et son **écart**
  jusqu'à la note suivante, 0..7 pas. L'écart est mesuré au temps (`plat_ms`) et ramené en pas de la DIV du pattern au
  tempo du projet (un pas = `60000 / tempo × DIV96[div] / 24` ms), arrondi, borné à 7. Un écart de 0 : des notes jouées
  ensemble.
- L'écart de la dernière note est le temps jusqu'au REC qui quitte LISTEN, borné à 1..7.
- La phrase garde 32 événements ; au-delà, le plus ancien sort.

### Improviser

- Le rendu compte les pas depuis le départ du pattern (`n`), remis à zéro par `gen_reset` comme LANES et ORBITS.
- Il garde l'événement joué (`cur`) et celui d'avant (`prev`), et l'attente avant le prochain (`wait`). Quand `wait`
  arrive à 0, il choisit le suivant :
  - avec la chance **DRIFT / 127**, un événement quelconque de la phrase (un saut) ;
  - sinon, les places `i` de la phrase dont la note est celle de `cur` et, en **ORDER** 2, dont la place d'avant
    (`i − 1`, la phrase est circulaire) porte la note de `prev`. Sans place en ORDER 2, l'ordre 1. Une place tirée au
    hasard ; l'événement joué est le suivant de cette place (`i + 1` modulo la longueur) ;
  - au départ (pas encore de `cur`), le premier événement de la phrase.
- L'événement joué sonne ; `wait` prend son écart. Un écart de 0 enchaîne le suivant au même pas, 4 notes au plus.
- Le hasard est un xorshift propre au rendu MIMIC, tiré de **SEED** : il repart de SEED au reset, à chaque changement
  de SEED, et en **LOOP** (1..16) tous les LOOP pas, où `cur`, `prev` et `wait` repartent aussi (la même variation à
  chaque tour). LOOP FREE : jamais.
- **Note jouée** : la note de l'événement ; sur un KIT, le pad `(note − KIT_NOTE) mod 16`. Durée un demi-pas (`len`
  4), vélocité 100, `prob` 20, comme les autres générateurs. Régler SOURCE sur MIMIC (piste synthé) monte POLY à 3 s'il
  est plus bas, pour les accords.
- Phrase vide (`n` 0) ou LISTEN : rien ne joue.

## 3. Données

- `SRC_MIMIC` après `SRC_ORBITS`, `SRC_NAME` « MIMIC ».
- Dans `q_pattern`, à côté de `q_orbits` :
  `typedef struct { uint8_t n, order, drift, loop, seed; uint8_t ev[32][2]; } q_mimic;` (n 0..32, order 1..2, drift
  0..127, loop 0 FREE ou 1..16, seed 0..255 ; `ev[i]` : note 1..127, écart 0..7). 69 octets par pattern, 2208 pour les
  32. Si la marge du pool passe sous son plancher au build, la phrase descend à 24 événements (53 octets).
- Valeurs par défaut : la phrase C4 D4 E4 G4 E4 D4 C4 (60 62 64 67 64 62 60), écarts 2 1 1 2 1 1 4 ; ORDER 1, DRIFT 16,
  LOOP FREE, SEED 0.
- Au chargement, les valeurs hors bornes sont ramenées dans leurs bornes (une note 0 devient 60).
- Dans `q_gen` : `q_mimic_run` : `n`, `rng`, `seed` (celui tiré), `cur`, `prev`, `wait`, `listen`, et pour la scène
  le dernier pas joué : `at` (son `n`), `nlast`, `last[4]` (les places jouées), `jumps` (bit i : `last[i]` vient de
  DRIFT), `from` (la place quittée avant `last[0]`). `audio_mimic(track, out)` le rend à l'écran.
- `CMD_LISTEN` (piste, 0/1) : l'interface met le rendu en LISTEN ou l'en sort ; en sortant, le rendu repart comme au
  reset (la phrase a changé).
- `Q_VERSION` passe à 12, sans migration.

## 4. Pages SEQ et clavier

| Page | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| MIMIC | DIV | ORDER (1..2) | DRIFT (0..127) | SOURCE |
| LOOP | LOOP (FREE, 1..16) | — | — | — |

- **REC court** : LISTEN on/off (au lieu de rien sur un générateur). C'est un nouvel état `R_LISTEN` de `ui.rec` ; le
  badge de l'en-tête affiche `LISTEN` et clignote comme ARM.
- **PRESET** : SEED + 1 (− 1 à l'envers, modulo 256) sur les deux pages : un nouveau tirage, à l'instant.
- **LED** : en lecture, la blanche `1 + last[0] × 8 / n` (la place lue dans la phrase, en huitièmes) ; en LISTEN, les
  blanches 1..8 suivent le remplissage de la phrase (`n × 8 / 32`).
- **Pas de mode GRILLE** : le clavier joue la piste (et apprend en LISTEN).

## 5. La scène

Direction « tête de lecture en couleurs » de la maquette. Coordonnées de la zone de scène 240 × 166 ; l'en-tête et les
encodeurs restent ceux de `ui.c`. Les textes de l'écran sont en anglais. Les quarts de la phrase : bleu, vert, orange,
blanc (`KBLUE`, `KGREEN`, `ORANGE`, `MB_WHITE`) ; la place `i` est dans le quart `i × 4 / n`.

- **Bande du haut** (y 4 à 16) : `<n> NOTES` ; `FREE` ou `LOOP <pas>/<LOOP>` ; DRIFT en 8 losanges, allumés en rose au
  prorata (`(DRIFT + 8) / 16`) ; `#<SEED>` à droite, en rose 300 ms après un changement.
- **Lignes de notes** : une ligne par note distincte de la phrase, la plus grave en bas, communes aux deux rouleaux ;
  le nom (`C4`, ou le pad `BD`, `SN`, `HH`, `P4`… sur un KIT) à gauche (x < 30) quand une ligne fait 7 px au moins.
- **Rouleau YOU** (y 24 à 76) : la phrase en cases plates (face et liseré clair, hauteur `min(6, ligne − 1)`), de
  x 30 à 232, chacune large au prorata de son écart (0 compte pour 1) ; sous elle, un trait de 2 px par quart à sa
  couleur.
- **Tête de lecture** : un trait rose de 2 px sur la hauteur du rouleau et un triangle au-dessus, dans la case de
  `last[0]`, qui avance avec le temps de son écart. Sur un saut (`last[0]` n'est pas la place suivant `from`), une
  flèche courbe au-dessus du rouleau de la fin de `from` au début de `last[0]` (12 segments, pointe), pendant 600 ms :
  claire (`#c9c3e4`) pour un lien appris, rose pour DRIFT.
- **Rouleau MIMIC** (y 94 à 158) : la ligne NOW à x 196 ; chaque événement joué naît sur NOW à la ligne de sa note, à
  la couleur du quart de sa place, large de son écart × 8 px, et défile vers la gauche de 8 px par pas ; rose pendant
  160 ms quand il sonne ; un cran rose au-dessus s'il vient de DRIFT. La scène garde les 24 derniers (relevés quand
  `at` change). En LOOP, un pointillé vertical à chaque redépart. Un crochet blanc entoure les ORDER derniers
  événements : le contexte cherché dans la phrase.
- **LISTEN** : pas de tête ni d'impro ; la phrase se construit case par case, la dernière en rose.

| Réglage | Effet visible |
|---|---|
| DIV | la vitesse du défilement et de la tête |
| ORDER | le crochet sur 1 ou 2 événements |
| DRIFT | les losanges ; les flèches et crans roses |
| LOOP | `LOOP n/N`, les pointillés de redépart |
| PRESET | `#SEED` en rose, un autre trajet |
| REC (LISTEN) | le badge, la phrase qui se reconstruit |

Le code va dans `app/scene_mimic.c` (`mimic_enter`, `mimic_frame`), sa vue dans `scene.h` ; `ui.c` la choisit sur les
pages MIMIC et LOOP. Budget : 32 cases (64), 24 événements (48), la flèche (13), les crochets et la tête (8) : environ
140 primitives ; lignes, textes, losanges et pointillés en dessin direct.

## 6. Tests

- **`gen_test.c`** : défauts et `sizeof(q_mimic) == 69` ; une phrase aux notes toutes différentes est rejouée dans
  l'ordre, aux bons écarts (DRIFT 0) ; un écart 0 joue 2 notes au même pas, 4 au plus ; ORDER 2 lève l'ambiguïté (phrase
  A B A C : après B A, toujours C ; en ORDER 1, B et C sortent) ; repli sur l'ordre 1 sans place en ORDER 2 ; DRIFT 127
  atteint toutes les places ; LOOP 8 : les pas 8..15 rejouent les pas 0..7 ; la même SEED, la même suite ; une autre
  SEED, une autre ; LISTEN et phrase vide : rien ; KIT : pad `(note − 36) mod 16` ; bornes au chargement.
- **`scene_test.c`** : budget ; une case par événement ; la tête dans la case de `last[0]` ; une flèche rose sur un saut
  DRIFT, claire sur un lien, aucune sur une suite ; chaque réglage du tableau change un pixel.
- **Simulateur** : `mimic.txt` (pages, bornes, REC court entre et sort de LISTEN, la phrase apprise avec ses écarts, la
  première note efface, 33 notes gardent les 32 dernières, PRESET change SEED), `mimic_scene.txt` (lecture, LISTEN,
  KIT).
- **Appareil** : avec les autres mesures CPU en fin de chantier.

## 7. Mises à jour de la spec globale

- Section 6, Générateurs : une ligne MIMIC.
- Section 7, Contrôles : REC court sur un pattern MIMIC : LISTEN ; PRESET : un nouveau tirage (SEED).
