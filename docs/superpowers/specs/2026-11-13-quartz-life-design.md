# QUARTZ — source LIFE : un séquenceur jeu de la vie

Une quatrième source de pattern, après STEP, GRIDS et MARBLES (spec globale `2026-11-10-quartz-groovebox-design.md`,
sections 3 et 6). C'est le premier d'une série de nouveaux séquenceurs ; chacun aura sa propre spec. Maquette de la
scène : `docs/screens/life.html`, direction M3 « Valence ».

## 1. But

Un séquenceur génératif qui se réécrit tout seul de boucle en boucle : une grille du jeu de la vie lue colonne par
colonne. Critères : un pattern dont la source est LIFE joue sur une piste synthé comme sur un KIT, quantifié dans les
notes permises, à la division du pattern ; il ne s'éteint pas tant qu'AUTO est actif ; il repart de sa graine au
STOP puis PLAY ; chaque encodeur change un élément visible de la scène ; une grille pleine tient dans le budget de 300
primitives ; les autres sources jouent et s'affichent comme avant.

Hors périmètre : une longueur LEN de 1 à 16 colonnes (plus tard, comme le LEN de GRIDS), une graine dessinée à la
main, d'autres règles que B3/S23 (HighLife…), les autres séquenceurs.

## 2. Le jeu

- Grille torique de 16 colonnes × 8 lignes, règle de Conway (B3/S23). La ligne 0 est la plus grave.
- La tête lit une colonne par pas, à la division du pattern (DIV) ; un tour fait 16 pas.
- Une génération est calculée au pas 0 d'un tour, tous les GEN tours (1, 2, 4 ou 8), puis MUT ajoute des cellules au
  hasard (de 0 à 6, proportionnel à MUT).
- **Graine** : SEED (0..127) et DENS (0..127) déterminent la grille de départ, par un tirage reproductible (une
  cellule vit avec la probabilité DENS / 127 × 0,55). La même paire redonne toujours la même grille.
- **Resemis manuel** : tourner SEED ou DENS ; la nouvelle grille part au tour suivant.
- **Grille morte ou figée** : après une génération, si la grille est vide ou identique à l'une des deux précédentes
  (période 1 ou 2), et seulement si MUT vaut 0 :
  - AUTO actif : une nouvelle grille est tirée (même DENS, tirage suivant du générateur), SEED ne change pas ;
  - AUTO inactif : la grille reste telle quelle ; morte, elle ne joue plus rien jusqu'au prochain resemis.
- **STOP puis PLAY** : la grille repart de SEED et DENS, le compteur de tours à zéro.

## 3. Les notes jouées

Les cellules vivantes de la colonne lue, de bas en haut, au plus POLY (1..4) ; 3 au plus sur un KIT (sa limite de
voix).

| Piste | Ce que joue la ligne r |
|---|---|
| Synthé | NOTE ramené dans la gamme du projet (`gen_scale`, `gen_quantize`), monté de r degrés de la gamme |
| KIT, MAP = PADS | le pad r + 1 |
| KIT, MAP = BANDS | lignes 0–2 : pad 1 ; 3–5 : pad 2 ; 6–7 : pad 3. Une bande joue si l'une de ses cellules vit ; POLY compte les bandes |

Une cellule née à la dernière génération (ou au resemis) joue accentuée, comme un accent de GRIDS. Vélocité, longueur
et micro-décalage : ceux des autres générateurs (`gen_step` remplit `q_step`, longueur 4).

## 4. Données

- `SRC_LIFE` après `SRC_MARBLES` (`project.h`), `SRC_NAME` « LIFE ».
- Dans `q_pattern`, à côté de `q_grids` et `q_marbles` :
  `typedef struct { uint8_t poly, note, map, seed, dens, autos, gen, mut; } q_life;`
  (poly 1..4, note 1..127, map 0 PADS / 1 BANDS, seed et dens 0..127, autos 0..1, gen 0..3 pour 1/2/4/8, mut 0..127).
  Valeurs par défaut : POLY 2, NOTE 48, PADS, SEED 0, DENS 70, AUTO actif, GEN 2, MUT 0.
- `Q_VERSION` passe à 7, sans migration.
- Dans `q_gen` (état du rendu, non sauvegardé) : la grille et les deux précédentes (`uint16_t [8]` chacune, bit c =
  colonne c), les naissances de la dernière génération, le compteur de tours, la paire SEED/DENS de la grille en cours
  (pour voir qu'elle a changé) et l'état du tirage.
- `gen_loop` rend 16 pour LIFE.

## 5. Pages SEQ

| Page | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| LIFE | DIV | POLY | NOTE (synthé) / MAP (KIT) | SOURCE |
| SEED | SEED | DENS | AUTO | — |
| EVOL | GEN | MUT | — | — |

`GEN_PAGES` gagne la ligne de LIFE ; NOTE est masqué sur un KIT et MAP sur une piste synthé (`knob_hidden`, comme
VOICE et NOTE de GRIDS). Les touches blanches 1–8 gardent le choix du pattern.

## 6. La scène

Direction M3 de la maquette : des molécules vues de face, la grille sur toute la zone de scène.

- **Disposition** (coordonnées de la zone de scène 240 × 166 ; l'en-tête reste celui de `ui.c`) : une bande du haut
  (y 0 à 13) pour la graine, GEN et AUTO ; les crans POLY au-dessus de la colonne lue (y 14 à 17) ; 16 × 8 cases de
  13,5 × 18 px, de x = 22 à 238 et de y = 18 à 162 ; les noms des lignes à gauche (notes en synthé, pads ou bandes en
  KIT) ; la colonne lue en bande plus claire.
- **Atomes** : un hexagone plat à facettes par cellule vivante, 4 triangles en 3 tons ; ses sommets tournent
  lentement, les facettes changent de ton. Bleu sur une piste synthé ; sur un KIT, la couleur du pad ou de la bande.
- **Liaisons** vers les voisins vivants (droite, haut et les deux diagonales du dessus, sans traverser le bord du
  tore) : double en ligne droite (2 lignes), simple en diagonale (1 ligne de largeur 2).
- **Valence** : un atome qui va mourir (moins de 2 ou plus de 3 voisins) tremble et prend des tons sombres ; une case
  vide qui va naître (3 voisins) montre un fantôme filaire (losange de 4 lignes).
- **Notes** : un atome qui joue grossit, passe au rose et émet 6 photons ; ses liaisons rosissent.
- **Naissances** : l'atome grandit en rose, 3 électrons tournent autour ; **morts** : l'atome rétrécit en gris.
- **Mort ou figée, AUTO inactif** : atomes gris et « FROZEN » ou « EXTINCT » au centre
  (les textes de l'écran sont en anglais).

Ce que règle chaque encodeur à l'écran :

| Encodeur | Effet visible |
|---|---|
| DIV | la vitesse de la bande de lecture |
| POLY | des crans au-dessus de la colonne lue ; le nombre d'atomes qui s'excitent |
| NOTE | les noms de notes à gauche |
| MAP | couleurs et étiquettes par pad ou par bande |
| SEED, DENS | la pluie d'atomes au resemis ; le numéro « S017 » à droite de la bande du haut |
| AUTO | deux flèches qui tournent dans la bande du haut (actif) ou un carré éteint |
| GEN | des losanges dans la bande du haut, un allumé par tour écoulé |
| MUT | des étincelles roses qui tombent sur les atomes ajoutés |

**Données de la scène** : le rendu écrit à chaque génération et à chaque resemis un instantané lu par
`audio_life(track, …)` (grilles en cours et précédente, naissances, tours, drapeau mort ou figé). Une lecture pendant
une écriture fausse au plus une image : pas de verrou. La colonne et les notes jouées viennent de `audio_pos` et
`audio_hits`, comme pour GRIDS. Le code va dans `app/scene_life.c` (`life_enter`, `life_frame`, `life_band`), sa vue
`q_lview` dans `scene.h` ; `ui.c` la choisit sur les pages SEQ d'un pattern LIFE.

**Budget** : atome 4 primitives, liaison 1 ou 2, fantôme 4, photons 6 par atome joué, électrons 3 par naissance. À une
densité courante (30 à 45 atomes), de 200 à 280 primitives. Au-delà, l'ordre d'envoi fait la priorité, et ce qui
dépasse 300 est abandonné : atomes de la colonne lue, autres atomes, liaisons, effets, fantômes.

## 7. Tests

- **Unitaires, `tests/unit/gen_test.c`** : clignotant (période 2), planeur qui traverse le bord, bloc immobile ; la
  même paire SEED/DENS redonne la même grille, DENS 0 une grille vide ; une génération tous les GEN tours, au pas 0 ;
  grille morte ou figée resemée avec AUTO, gardée sans AUTO (morte : rien ne joue), jamais resemée avec MUT > 0 ;
  changer SEED ou DENS resème au tour suivant ; POLY garde les cellules les plus basses, 3 au plus sur un KIT ; degrés
  depuis NOTE ; PADS et BANDS ; naissances accentuées ; STOP puis PLAY repart de SEED.
- **Unitaires, `tests/unit/scene_test.c`** : une grille pleine reste sous 300 primitives ; les atomes de la colonne
  lue sont toujours dessinés.
- **Simulateur** : `app/tests/life.txt` (SOURCE sur LIFE, les 3 pages, bornes de chaque encodeur, MAP seulement sur un
  KIT, une capture par page) ; `app/tests/life_scene.txt` (la scène en lecture, une capture après une génération et
  une après un resemis).
- **Appareil** : pire cas (grille dense, POLY 4, MUT élevé), charge des cœurs et images par seconde, avec les autres
  mesures CPU en fin de chantier.
