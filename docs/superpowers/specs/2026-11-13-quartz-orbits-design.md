# QUARTZ — source ORBITS : des lunes qui jouent en passant

Une huitième source de pattern, après STEP, GRIDS, MARBLES, LIFE, LANES, CHORDS et TONNETZ (spec globale
`2026-11-10-quartz-groovebox-design.md`, sections 3 et 6). Elle part de la branche `tonnetz`. Maquettes : idées
`docs/screens/sequencers.html`, scène `docs/screens/orbits.html`, direction V2 « arcs en pointillés ».

## 1. But

Des polyrythmes qu'on lit d'un coup d'œil : 1 à 4 lunes tournent autour d'une planète, chacune à sa période, et
chaque passage devant la planète joue la note de la lune. Mélodie sur une piste synthé, batterie sur un KIT.

Critères :
- un pattern ORBITS joue sur une piste synthé (degrés de la gamme de GLO) comme sur un KIT (pads), à la division du
  pattern ;
- chaque lune se règle en période, phase et note sur une page, la lune choisie par PRESET ;
- toutes les lunes repassent ensemble au PPCM de leurs périodes, et la scène le montre ;
- chaque encodeur change un élément visible de la scène ; la scène tient dans 300 primitives ;
- les autres sources jouent et s'affichent comme avant.

Hors périmètre : une probabilité, une vélocité ou une durée par lune, des orbites elliptiques ou à vitesse variable,
plus de 4 lunes, une saisie au clavier de la note d'une lune, MIMIC.

## 2. Le jeu

- **MOONS** (1..4) lunes actives ; chacune a **PERIOD** (1..32 pas), **PHASE** (0..31) et **NOTE** (0..15).
- Le rendu compte les pas depuis le départ du pattern (`n`) ; il ne repart pas au tour, mais au STOP puis PLAY, au
  changement de pattern et au changement de source (`gen_reset`), comme LANES. Un tour du pattern fait 16 pas.
- La lune `m` passe au pas `n` quand `n mod PERIOD = PHASE mod PERIOD`. Chaque lune qui passe joue sa note : jusqu'à
  4 notes au même pas (`note[0..3]` du pas, dans l'ordre des lunes).
- **Note** : sur une piste synthé, NOTE de la page ORBITS ramenée dans la gamme et montée de NOTE de la lune degrés
  de la gamme (`gen_life_note`) ; sur un KIT, le pad NOTE + 1.
- **Conjonction** : 2 lunes ou plus au même pas : toutes leurs notes accentuées.
- Durée un demi-pas (`len` 4), vélocité 100, `prob` 20, comme les autres générateurs. Régler SOURCE sur ORBITS (piste
  synthé) monte POLY à 3 s'il est plus bas, pour les conjonctions.

## 3. Données

- `SRC_ORBITS` après `SRC_TONNETZ`, `SRC_NAME` « ORBITS ».
- Dans `q_pattern`, à côté de `q_tonnetz` :
  `typedef struct { uint8_t note, moons, per[4], pha[4], val[4]; } q_orbits;` (note 1..127, moons 1..4, per 1..32,
  pha 0..31, val 0..15). 14 octets par pattern, 448 pour les 32.
- Valeurs par défaut : 3 lunes, périodes 8, 6, 5, 3, phases 0, notes 0, 2, 4, 7, NOTE 48 (C E G sur un synthé ; pads
  1, 3, 5 sur un KIT ; réalignement en 120 pas).
- Au chargement, les valeurs hors bornes sont ramenées dans leurs bornes.
- Dans `q_gen` : le compteur `orbits_n` ; `audio_orbits(track)` le rend à l'écran.
- `Q_VERSION` passe à 11, sans migration.

## 4. Pages SEQ et clavier

| Page | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| ORBITS | DIV | NOTE (masqué sur un KIT) | MOONS | SOURCE |
| MOON | PERIOD | PHASE | NOTE (degré 0..15) / PAD (1..16, sur un KIT) | — |

- **PRESET**, sur la page MOON, choisit la lune réglée (1..MOONS ; non sauvegardé, 1 au démarrage, ramené dans
  MOONS). Sur la page ORBITS, il change le pattern comme ailleurs.
- **Pas de mode GRILLE** : REC court ne fait rien (comme GRIDS) ; le clavier joue la piste.
- **LED** : les blanches 1..MOONS à mi-niveau, pleine au passage de leur lune (si le pattern affiché joue).

## 5. La scène

Direction V2 de la maquette. Coordonnées de la zone de scène 240 × 166 ; l'en-tête et les encodeurs restent ceux de
`ui.c`. Les textes de l'écran sont en anglais. Couleurs des lunes 1 à 4 : orange, bleu, vert, blanc (`ORANGE`,
`KBLUE`, `KGREEN`, `MB_WHITE`).

- **Décor** : 40 étoiles fixes d'un pixel (`#1c1340`, une sur quatre `#3a2f63`) ; une grande planète lointaine en
  sphère à facettes, rayon 46, centrée en (222, 146), coupée par le coin, en tons très sombres (`#2a2148` à `#120c2a`),
  qui tourne d'un tour en 56 s.
- **Bande du haut** (y 4 à 16) : par lune, une étiquette de 30 × 12 px à partir de x 8 (pas de 34 px) : une gemme à
  sa couleur et sa période ; en page MOON, l'étiquette de la lune réglée en clair. À droite, `SYNC <pas>` : les pas
  avant que toutes les lunes repassent ensemble (cherchés une fois par pas, jusqu'à 999 ; `-` au-delà ; 0 à l'arrêt quand
  elles partent ensemble), en rose au moment du réalignement.
- **Radar** (centre (104, 94)) : une orbite ronde par lune, rayon 22 + 15 m (m = 0..3 : la porte de la 4e reste sous la
  bande du haut), `#2a2148`, plus claire pour
  la lune réglée en page MOON ; un cran (losange de 2 px) par pas de sa période, le cran de la porte (en haut) en rose ;
  le chemin parcouru depuis la porte en points de 2 px tous les 5 px, à la couleur sombre de la lune, effacé au
  passage. La lune : une gemme de 6 px à sa couleur, au point `(n − PHASE) mod PERIOD` (fraction du pas comprise) de son
  orbite ; rose et de 8 px pendant 160 ms à son passage.
- **Notes** : à droite (x 200), à la hauteur de la porte de chaque orbite, le nom de la note (`C3`) ou du pad
  (`BD`, `SN`, `HH`, `P4`…), à la couleur de la lune ; un trait de la porte au nom, rose au passage.
- **Planète** : une sphère à facettes (8 méridiens, 5 bandes, inclinée de 0,35 rad), rayon 15, en bleus éclairés du
  haut à gauche, qui tourne d'un tour en 25 s. Quand toutes les lunes (au moins 2) passent au même pas, elle clignote
  en blanc deux fois en 300 ms.

| Encodeur | Effet visible |
|---|---|
| DIV | la vitesse des lunes |
| NOTE | les noms des notes |
| MOONS | le nombre d'orbites, de lunes et d'étiquettes |
| PERIOD | le nombre de crans de l'orbite, la vitesse de sa lune, l'étiquette, SYNC |
| PHASE | la place de la lune sur son orbite |
| NOTE / PAD | le nom de sa note |
| PRESET | l'étiquette et l'orbite claires |

Le code va dans `app/scene_orbits.c` (`orbits_enter`, `orbits_frame`, `orbits_band`), sa vue dans `scene.h` ; `ui.c`
la choisit (anim 12) sur les pages ORBITS et MOON. Budget : orbites en 32 segments (au plus 128 lignes), deux sphères
(environ 40 et 20 facettes visibles), 4 gemmes (16) : environ 230 primitives ; crans, points, étoiles et textes en
dessin direct.

## 6. Tests

- **`gen_test.c`** : défauts et `sizeof(q_orbits) == 14` ; passages de chaque lune aux bons pas (périodes 8, 6, 5,
  phase 0 ; phase 3 décale) ; au pas 0 les trois passent ensemble, accentuées ; pas 24 : périodes 8 et 6, accent ;
  pas 1 : rien ; réalignement au pas 120 ; notes C E G sur un synthé (C majeur, NOTE 48), pads 1, 3, 5 sur un KIT ;
  MOONS 1 : seule la lune 1 ; PERIOD 1 : à chaque pas ; le compteur ne repart pas au tour ; bornes au chargement.
- **`scene_test.c`** : budget ; une gemme rose à la porte au passage ; clignotement blanc de la planète quand toutes
  passent ensemble, pas pour 2 sur 3 ; crans d'une orbite = PERIOD ; chaque réglage du tableau change un pixel.
- **Simulateur** : `orbits.txt` (pages, bornes, PRESET choisit la lune, NOTE et PAD selon la piste),
  `orbits_scene.txt` (lecture, page MOON, KIT).
- **Appareil** : avec les autres mesures CPU en fin de chantier.

## 7. Mises à jour de la spec globale

- Section 6, Générateurs : une ligne ORBITS.
- Section 7, Contrôles : PRESET choisit la lune réglée d'un pattern ORBITS (page MOON).
