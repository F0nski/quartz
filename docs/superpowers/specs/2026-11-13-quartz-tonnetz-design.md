# QUARTZ — source TONNETZ : une marche d'accords sur le Tonnetz

Une septième source de pattern, après STEP, GRIDS, MARBLES, LIFE, LANES et CHORDS (spec globale
`2026-11-10-quartz-groovebox-design.md`, sections 3 et 6). Elle part de la branche `chords`, dont elle reprend la façon
de jouer un accord (RATE, STRUM, VOICE, accent, POLY) et la case tenue. Maquettes : idées
`docs/screens/sequencers.html`, scène `docs/screens/tonnetz.html`, direction T1 « Carte qui suit ».

## 1. But

Des enchaînements d'accords qu'on n'aurait pas écrits, toujours fluides : à chaque mesure, l'accord bascule sur un
voisin du Tonnetz par une transformation P, L ou R, qui garde deux notes communes.

Critères :
- un pattern TONNETZ joue sur une piste synthé, à la division du pattern ;
- la séquence de transformations s'écrit d'un geste par pas, et le pas tenu montre l'accord qu'il donnera ;
- avec RETURN, la progression est une boucle fixe ; avec CHAOS 0, elle est entièrement prévisible ;
- en IN SCALE, aucun accord ne sort de la gamme de GLO ;
- chaque encodeur change un élément visible de la scène ; la scène tient dans 300 primitives ;
- les autres sources jouent et s'affichent comme avant.

Hors périmètre : les accords de 7e, une durée d'accord autre qu'une mesure, les autres transformations (N, S, H…), un
départ sur un autre accord que celui de NOTE, l'enregistrement en live, les autres séquenceurs (ORBITS, MIMIC).

## 2. Le jeu

- **Accords** : des triades majeures ou mineures, repérées sur le Tonnetz par un triangle `(m, i, j)` : le nœud
  `(i, j)` vaut la classe `7i + 4j` (mod 12) ; majeur `(i, j)` = nœuds `(i, j) (i+1, j) (i, j+1)` ; mineur `(i, j)` =
  `(i+1, j) (i, j+1) (i+1, j+1)`.
- **Transformations** (chacune garde deux notes) :

| | depuis majeur `(i, j)` | depuis mineur `(i, j)` | exemple |
|---|---|---|---|
| P | mineur `(i, j−1)` | majeur `(i, j+1)` | C ↔ Cm |
| L | mineur `(i, j)` | majeur `(i, j)` | C ↔ Em |
| R | mineur `(i−1, j)` | majeur `(i+1, j)` | C ↔ Am |

- **Accord de départ** : la triade de la gamme de GLO posée sur NOTE ramenée dans la gamme (`gen_life_note(note, scale,
  tonic, 0)` et ses degrés +2 et +4) ; majeure ou mineure selon sa tierce ; une triade diminuée part mineure ; sans
  tierce dans la gamme (pentatoniques), la qualité qui a le plus de notes dans la gamme. En IN SCALE, un départ hors de
  la gamme cède la place à son premier voisin dans la gamme (P, L, R) ; aucun : il reste. Il est
  placé au triangle `(0, 0)` ; les classes du Tonnetz sont comptées depuis sa fondamentale.
- **Un accord par mesure** (16 pas à la division du pattern) : le rendu compte les pas depuis le départ (`n`, aligné
  sur le tour comme CHORDS) ; la mesure 0 joue l'accord de départ ; chaque mesure `b ≥ 1` applique un pas de la
  séquence à l'accord en cours.
- **La séquence** : LEN (1..8) pas, chacun `—` (garder), P, L ou R. Sans RETURN, la mesure `b` applique le pas
  `(b − 1) mod LEN`. Avec RETURN, un tour fait LEN + 1 mesures : les mesures 1..LEN appliquent les pas 0..LEN−1, la
  suivante revient à l'accord de départ, et ainsi de suite.
- **CHAOS** (0..127) : à chaque pas appliqué, avec la probabilité CHAOS / 128, la transformation est remplacée par
  l'une des deux autres (P, L, R), tirée au hasard ; un `—` remplacé devient P, L ou R. Le tirage utilise le
  générateur du séquenceur. La tuile du pas ne passe en orange que si l'accord obtenu diffère de celui du pas écrit.
- Une marche FREE très longue ramène `(i, j)` près de l'origine par 12 colonnes ou 3 rangées : mêmes classes de hauteur.
- **SCALE** :
  - FREE : la transformation s'applique toujours ;
  - IN SCALE : si l'accord obtenu a une note hors de la gamme, on essaie les deux autres dans l'ordre P → L → R → P à
    partir de la transformation demandée ; la première dont l'accord reste dans la gamme l'emporte ; aucune : on garde
    l'accord.
- **Jouer l'accord**, comme CHORDS :
  - notes : fondamentale, tierce, quinte, au plus POLY (gardées dans cet ordre) ; en ROOT, la fondamentale dans
    l'octave qui part de NOTE, les autres au-dessus ; en SMOOTH (défaut), le renversement et l'octave les plus proches
    de la frappe précédente, la basse à une quinte au plus de celle de ROOT (même règle et même code que CHORDS) ;
  - RATE (1, 2, 4, 8, 16 frappes par mesure), STRUM (0..7, 1/24 de pas), la première frappe de chaque mesure accentuée
    sur toutes ses notes, vélocité 100 ;
  - régler SOURCE sur TONNETZ monte POLY à 3 s'il est plus bas ; un KIT ne joue rien.

## 3. Données

- `SRC_TONNETZ` après `SRC_CHORDS`, `SRC_NAME` « TONNETZ ».
- Dans `q_pattern`, à côté de `q_chords` :
  `typedef struct { uint8_t note, len, rate, strum, voice, chaos, scale, ret; uint16_t seq; } q_tonnetz;` (note 1..127,
  len 1..8, rate 0..4, strum 0..7, voice 0..1, chaos 0..127, scale 0 FREE / 1 IN SCALE, ret 0..1 ; `seq` : le pas i
  dans les bits `2i`, `2i+1`, 0 `—`, 1 P, 2 L, 3 R). 10 octets par pattern, 320 pour les 32.
- Valeurs par défaut : séquence R L R L (LEN 4), NOTE 48, RATE 2, STRUM 0, SMOOTH, CHAOS 0, IN SCALE, RETURN.
- Au chargement, les valeurs hors bornes sont ramenées dans leurs bornes.
- Dans `q_gen` : `n`, l'accord en cours `(m, i, j)` (entiers 16 bits), sa fondamentale de départ, le pas appliqué à la
  dernière mesure (−1 : départ ou retour), s'il a été remplacé par CHAOS, et la dernière frappe (notes, nombre) ;
  `audio_tonnetz(track, …)` les rend à l'écran.
- `Q_VERSION` passe à 10, sans migration.
- Le SMOOTH de CHORDS devient une fonction partagée de `gen.c` (deux usages).

## 4. Pages SEQ

| Page | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| TONNETZ | DIV | NOTE | RATE | SOURCE |
| WALK | LEN | CHAOS | SCALE (`FREE` / `IN SCALE`) | RETURN (`OFF` / `ON`) |
| PLAY | STRUM | VOICE (`ROOT` / `SMOOTH`) | — | — |

## 5. La saisie

REC, appui court, dans SEQ sur un pattern TONNETZ : désarmé ↔ GRILLE, comme CHORDS.

- En GRILLE, les **blanches 1–8** sont les pas de la séquence ; au-delà de LEN et 9–16 : rien.
- **Appui court** : la transformation suivante, `—` → P → L → R → `—`.
- **Pas tenu + noire 1, 2, 3, 4** : P, L, R, `—`. **Pas tenu + K1** : la transformation (0..3).
- Une autre touche pendant le maintien annule l'appui court ; un appui long ne change rien. Pas de couche d'aide : la
  scène montre le pas tenu (section 6).
- **LED** : les pas dans LEN à mi-niveau, le pas appliqué à la dernière mesure à plein niveau ; pas tenu : les noires
  1–4.
- L'état de la case tenue est celui de LANES et CHORDS ; PRESET change le pattern ; désarmé, le clavier joue la piste.

## 6. La scène

Direction T1 de la maquette. Coordonnées de la zone de scène 240 × 166 ; l'en-tête et les encodeurs restent ceux de
`ui.c`. Les textes de l'écran sont en anglais.

- **Bande du haut** (y 4 à 16) : 8 tuiles de 13 × 12 px à partir de x 6, pas de 15 px, la lettre du pas (`-`, `P`,
  `L`, `R`) en blanc ; bleues dans LEN, sombres au-delà ; le pas appliqué à la dernière mesure en rose, en orange s'il a
  été remplacé par CHAOS ; RETURN : un losange orange après la tuile LEN. À droite (x 170 à 232), les crans de RATE
  comme CHORDS.
- **Carte** (y 20 à 166, sans clavier : la carte montre les notes jouées) : la grille du Tonnetz, maille de 46 px (nœud
  `(i, j)` à `(46 i + 23 j, −40 j)` du centre de la carte), arêtes `#1c1340` ; un losange de 4 px par nœud et le nom de
  sa note au-dessus ; en IN SCALE, les nœuds hors gamme plus sombres. L'accord en cours : son triangle en trois
  facettes roses depuis son centre (blanches 140 ms à chaque frappe), son nom au centre (noir sur blanc) ; ses trois
  nœuds en blanc, chacun avec la note jouée et son octave (`E3`), qui grossit et blanchit 140 ms à son départ (STRUM :
  l'un après l'autre, du grave à l'aigu) ; un petit triangle blanc sous le nœud de la note la plus grave (VOICE). Les
  trois accords précédents : leurs triangles pleins en bleus de plus en plus sombres. La carte glisse pour garder
  l'accord au centre (x 120, y 92), en 250 ms.
- **Pas tenu** : un bandeau clair à la place des crans, `STEP 3 - R > Dm` (le pas, sa transformation, l'accord qu'elle
  donnerait depuis l'accord en cours, dans le mode SCALE) ; sa tuile en blanc.
- À l'arrêt : l'accord de départ, sans rose ; **KIT** : `NO CHORDS ON A KIT`.

| Encodeur | Effet visible |
|---|---|
| DIV | la vitesse des crans et des changements d'accord |
| NOTE | les noms des nœuds |
| RATE | le nombre de crans |
| LEN | les tuiles allumées, la place du losange RETURN |
| CHAOS | une tuile orange quand un pas a été remplacé |
| SCALE | les nœuds hors gamme sombres ou non |
| RETURN | le losange orange |
| STRUM | les nœuds de l'accord qui s'allument l'un après l'autre |
| VOICE | les octaves écrites sur les nœuds de l'accord, le repère de la basse |

Le code va dans `app/scene_tonnetz.c` (`tonnetz_enter`, `tonnetz_frame`, `tonnetz_band`), sa vue dans `scene.h` ;
`ui.c` la choisit (anim 11) sur les pages TONNETZ, WALK et PLAY. Budget : une trentaine d'arêtes et de nœuds visibles, 3 facettes, 3 traces :
environ 150 primitives (300 au plus).

## 7. Tests

- **`gen_test.c`** : P, L, R depuis C et depuis Am (C ↔ Cm, C ↔ Em, C ↔ Am ; Am ↔ A, Am ↔ F, Am ↔ C) ; deux notes
  communes à chaque transformation ; départ en C majeur sur NOTE 48 = C, sur NOTE 50 = Dm ; séquence R L R L sans
  RETURN : C Am F Dm B♭ (FREE) ; avec RETURN : C Am F Dm B♭/F puis C ; IN SCALE : jamais de note hors de C majeur sur
  16 tours ; P depuis C en IN SCALE donne L ou R ; CHAOS 0 : identique sur deux départs, CHAOS 127 : chaque pas
  différent de la séquence ; RATE, STRUM, accent, POLY, KIT comme CHORDS ; marche FREE sur 1000 mesures sans
  débordement ; bornes au chargement (`store_test.c`).
- **`scene_test.c`** : budget ; triangle rose sur les nœuds de l'accord ; nœuds hors gamme plus sombres en IN SCALE ;
  tuile orange après un remplacement CHAOS ; bandeau du pas tenu ; octaves des notes jouées sur les nœuds et
  repère de la basse ; nœuds allumés dans l'ordre avec STRUM ; chaque réglage du tableau change au moins un pixel.
- **Simulateur** : `tonnetz.txt` (pages, bornes, POLY monté à 3), `tonnetz_entry.txt` (appui court, tenu + noires,
  tenu + K1, au-delà de LEN), `tonnetz_scene.txt` (lecture, pas tenu, KIT).
- **Appareil** : avec les autres mesures CPU en fin de chantier.

## 8. Mises à jour de la spec globale

- Section 6, Générateurs : une ligne TONNETZ.
