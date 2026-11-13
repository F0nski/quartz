# QUARTZ — source LANES : un séquenceur à lignes polymétriques

Une cinquième source de pattern, après STEP, GRIDS, MARBLES et LIFE (spec globale `2026-11-10-quartz-groovebox-design.md`,
sections 3 et 6). Elle s'inspire de Kria (monome) : chaque propriété d'une note a sa propre boucle. Maquettes : idées
`docs/screens/sequencers.html`, scène `docs/screens/lanes.html`, direction C3 « Focus ».

## 1. But

Des mélodies longues et vivantes à partir de peu de cases, sans hasard. Chaque propriété (déclenchement, note,
octave, durée, vélocité) boucle sur sa propre longueur ; les boucles se décalent et la phrase ne se répète à
l'identique qu'au bout du PPCM des longueurs.

Critères :
- un pattern LANES joue sur une piste synthé comme sur un KIT, dans la gamme du projet, à la division du pattern ;
- le même pattern redonne toujours la même suite de notes depuis PLAY ;
- les cinq lignes et leurs longueurs se règlent sur l'appareil, et chaque case se voit et s'édite d'un geste ;
- chaque encodeur change un élément visible de la scène ;
- la scène tient dans le budget de 300 primitives ;
- les autres sources jouent et s'affichent comme avant.

Hors périmètre :
- une division ou un sens de lecture par ligne ;
- des lignes RATCH ou PROB ;
- l'enregistrement en live dans les lignes ;
- le presse-papier, la sélection et les p-locks de STEP ;
- un geste qui règle la longueur sur la grille ;
- les autres nouveaux séquenceurs (ORBITS, TONNETZ, CHORDS, MIMIC), qui auront chacun leur spec.

## 2. Le jeu

- Cinq lignes : **TRIG, NOTE, OCT, DUR, VEL**. Chacune est une boucle de 1 à 16 cases, avec sa longueur LEN et sa
  propre tête.
- Le rendu compte les pas joués depuis le départ du pattern : `n`. La tête de la ligne l est la case
  `n mod LEN[l]`. Toutes les têtes avancent d'une case par pas, à la division du pattern (DIV).
- Le compteur ne repart pas à zéro au tour du pattern. Il repart à zéro au STOP puis PLAY et quand la piste change de
  pattern, puisque l'état du générateur est remis à zéro dans ces deux cas (`gen_reset`).
- Un tour du pattern fait 16 pas (`gen_loop`), comme LIFE : le changement de pattern se fait tous les 16 pas.
- À chaque pas, si la case TRIG de la tête est allumée, une note part avec :

| Ligne | Valeurs | Effet |
|---|---|---|
| TRIG | 0, 1 | une note part ou non |
| NOTE | 0..15 | synthé : NOTE ramené dans la gamme du projet, monté de v degrés de la gamme (`gen_life_note`) ; KIT : le pad v + 1 |
| OCT | 0..4 | −2 à +2 octaves (synthé ; ignoré sur un KIT), le résultat borné entre 1 et 127 |
| DUR | 0..4 | ¼, ½, 1, 2, 4 pas (`len` 2, 4, 8, 16, 32) |
| VEL | 0..3 | vélocité 40, 70, 100, 100 avec accent |

La gamme est celle de GLO (tonique et mode, `gen_scale`). Le micro-décalage est nul ; le swing s'applique comme pour
les autres générateurs.

## 3. Données

- `SRC_LANES` après `SRC_LIFE` (`project.h`), `SRC_NAME` « LANES ».
- Dans `q_pattern`, à côté de `q_life` :
  `typedef struct { uint8_t note, len[5], v[5][16]; } q_lanes;` (note 1..127, len 1..16, v selon la ligne,
  section 2). 86 octets par pattern ; `store.c` vérifie déjà que `q_project` tient dans ses blocs.
- Valeurs par défaut (celles de la maquette, pour qu'un pattern neuf joue tout de suite) : NOTE 48 ;
  TRIG 1 0 1 1 0 1 0 (LEN 7) ; NOTE 0 4 2 5 3 (LEN 5) ; OCT 2 2 3 (LEN 3) ; DUR 1 3 1 2 (LEN 4) ; VEL 3 1 2 1 3 2
  (LEN 6) ; les cases au-delà valent 0 (OCT 2).
- Au chargement, les valeurs hors bornes sont ramenées dans leurs bornes, comme pour LIFE.
- Dans `q_gen` (état du rendu, non sauvegardé) : `uint32_t lanes_n`, le compteur de pas.
- `audio_lanes(track)` rend ce compteur à la scène.
- `Q_VERSION` passe à 8, sans migration.

## 4. Pages SEQ

| Page | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| LANES | DIV | NOTE | TRIG LEN | SOURCE |
| LENS | NOTE LEN | OCT LEN | DUR LEN | VEL LEN |

`GEN_PAGES` gagne la ligne de LANES. Sur un KIT, NOTE et OCT LEN sont masqués (`knob_hidden`). Les touches blanches
1–8, SEQ tenu, gardent le choix du pattern.

## 5. La saisie

REC, appui court, dans SEQ : désarmé → GRILLE → désarmé. Un pattern LANES n'a pas de mode NOTES. REC tenu + PLAY
passe en LIVE pour jouer sans rien écrire, comme pour les autres générateurs.

- **PRESET** choisit la ligne éditée : TRIG, NOTE, OCT, DUR, VEL, sans OCT sur un KIT. Ce choix appartient à l'UI,
  n'est pas sauvegardé et vaut TRIG au démarrage.
- En GRILLE, les **16 blanches** sont les cases de la ligne éditée.
- **LED** : les cases non nulles s'allument à mi-niveau, la tête de la ligne à plein niveau, les cases au-delà de LEN
  restent éteintes. Désarmé, les LED montrent la même chose ; le clavier joue la piste.
- **Appui court sur une case** : sur TRIG, la case s'allume ou s'éteint ; sur les autres lignes, la valeur monte d'un
  cran et revient à 0 après le maximum.
- **Case tenue + K1** : règle la valeur de la case, quelle que soit la ligne.
- **Case NOTE tenue + une autre touche** :
  - synthé : écrit le degré de gamme le plus proche de la note jouée (octave du clavier comprise), compté depuis NOTE
    et borné entre 0 et 15 ;
  - KIT : écrit le pad que joue cette touche.
- Une case au-delà de LEN ne fait rien.
- L'aide des boutons tenus (spec `2026-11-12-quartz-step-entry-design.md`, section 7) montre ces gestes pour une case
  tenue.

## 6. La scène

Direction C3 « Focus » de la maquette, variante F2 (sol filaire). Coordonnées de la zone de scène 240 × 166 ;
l'en-tête et les encodeurs restent ceux de `ui.c`. Les textes de l'écran sont en anglais.

- **Vue d'ensemble** (y 0 à 70) : une rangée de 13 px par ligne, base du bloc à y = 14 + 13 r (16 px sur un KIT, qui
  n'a que quatre lignes).
  - À gauche, le nom de la ligne, en rose pour la ligne éditée.
  - 16 cases de 12,5 px à partir de x = 34. Chaque bloc fait 10 px et a deux facettes (face avant et dessus, en biais
    de 2 px). Sa hauteur, 2 + valeur × 7, donne la valeur. Une case TRIG éteinte est un trait.
  - La tête est un bloc rose. La ligne éditée est en bleu vif, les autres en tons sombres.
  - Un mur rose de 2 × 11 px ferme la boucle après la case LEN. Au-delà, les cases ne sont plus qu'un trait au sol.
- **Ligne éditée** (y 72 à 166), sous un filet à y 71 : `EDIT <LIGNE>` à gauche et `LEN <n>` à droite (y 82), puis
  deux rangs de huit blocs, bases à y 120 et 162, qui correspondent aux blanches 1–8 et 9–16.
  - Blocs de 24 px à x = 6 + 29 (c mod 8), trois facettes (biais 4 px), hauteur 6 + valeur × 20 (TRIG allumée 22).
  - Le numéro de la touche est écrit au pied du bloc ; la valeur au-dessus : nom de note (`P1`…`P16` sur un KIT),
    `-2`…`+2`, `1/4` `1/2` `1` `2` `4`, `pp` `mf` `f` `ff`. TRIG n'a pas de texte.
  - Les cases au-delà de LEN sont un trait et un numéro sombres ; un mur rose de 30 px suit la case LEN.
- **Sol filaire** (décor), derrière la ligne éditée, en `#140e30` : 21 lignes qui fuient d'un point à (120, 78) vers
  le bas de la zone (écart 10 px à l'horizon, 44 px en bas) et 7 lignes horizontales à
  y = 80 + ((j + f) / 7)² × 86, où f est la fraction du pas en cours. Les lignes avancent d'un cran par pas ; à
  l'arrêt, le sol est immobile.
- **Case tenue ou touchée** : elle clignote en blanc 300 ms, en haut comme en bas.

Ce que règle chaque encodeur à l'écran :

| Encodeur | Effet visible |
|---|---|
| DIV | la vitesse des têtes et du sol |
| NOTE | les noms de notes écrits sur les blocs NOTE |
| TRIG LEN … VEL LEN | la longueur de la rangée et la place du mur rose, en haut comme en bas |
| PRESET | la ligne affichée en bas et le nom en rose en haut |

**Données de la scène** :
- la scène lit le pattern (le projet de l'UI) et `audio_lanes(track)`, et en déduit chaque tête ;
- à l'arrêt, les têtes sont sur la case 1 ;
- le pas joué et les notes viennent de `audio_pos` et `audio_hits`, comme pour LIFE ;
- le code va dans `app/scene_lanes.c` (`lanes_enter`, `lanes_frame`), sa vue dans `scene.h` ; `ui.c` la choisit sur
  les pages SEQ d'un pattern LANES.

**Budget** : vue d'ensemble 80 blocs × 2 primitives ; ligne éditée 16 × 3 ; murs 7 ; sol 28 lignes ; textes environ
25. Environ 270 primitives au pire. Le sol est envoyé en dernier : au-delà de 300, c'est lui qui est abandonné.

## 7. Tests

- **Unitaires, `tests/unit/gen_test.c`** :
  - les têtes suivent chacune leur LEN, et la suite de notes se répète au bout du PPCM (7, 5, 3, 4, 6 → 420 pas) ;
  - le compteur ne repart pas au tour du pattern, mais repart au STOP puis PLAY et au changement de pattern ;
  - degrés depuis NOTE dans la gamme de GLO, OCT et bornes 1..127 ;
  - pads sur un KIT, OCT ignoré ;
  - DUR vers `len`, VEL vers la vélocité et l'accent ;
  - une case TRIG éteinte ne joue rien ;
  - les valeurs hors bornes sont ramenées au chargement.
- **Unitaires, `tests/unit/scene_test.c`** : cinq lignes de 16 cases restent sous 300 primitives.
- **Simulateur** :
  - `app/tests/lanes.txt` : SOURCE sur LANES, les 2 pages, les bornes de chaque encodeur, NOTE et OCT LEN masqués sur
    un KIT, PRESET qui change de ligne, appui court, case tenue + K1, case NOTE tenue + touche, une case au-delà de
    LEN, une capture par page ;
  - `app/tests/lanes_scene.txt` : la scène en lecture, une capture après quelques pas.
- **Appareil** : pire cas (cinq lignes de 16 cases, DIV 1/32), charge des cœurs et images par seconde, avec les autres
  mesures CPU en fin de chantier.

## 8. Mises à jour de la spec globale

- Section 6, Générateurs : une ligne LANES qui renvoie à cette spec.
- Section 7, Contrôles : PRESET choisit la ligne éditée d'un pattern LANES.
