# QUARTZ — étape 5d2 : chorale d'androïdes, funambule, dancefloor

Cinquième partie de l'étape 5 de la spec globale (`2026-11-10-quartz-groovebox-design.md`, section 9.3). Part de
`step5d1-scenes`. Réutilise la caméra en perspective de 5d1 (`2026-11-12-quartz-step5d1-scenes-design.md`, section 2) et
ses conventions (scènes dans leur fichier, inclus par `app_ui.c` ; tampon partagé ; `sc.fresh`). Ensuite : 5e (HOME).

## 1. But

Donner leur scène aux six derniers moteurs, fidèle aux maquettes validées, sous 300 primitives et sans perte de son.
Après 5d2, chaque modèle a sa scène sur ENGINE et SOUND. Critères : chaque encodeur change un élément visible ; chaque
note fait réagir la scène ; le pire cas tourne sans bloc perdu.

| Scène | Modèles (n°) | Maquette (`docs/screens/`) |
|---|---|---|
| Chorale d'androïdes | STRING MACH (6), CHORDS (14) | `chords.html`, choir0/choir1 (`choir`) |
| Funambule | STRING (19) | `string.html`, walker (`walker`, `vib`) |
| Dancefloor | SWARM (16), NOISE (17), PARTICLE (18) | `noise.html`, dance0..2 (`dance`, `grains`) |

Les valeurs que la spec ne fixe pas sont celles des maquettes (y de la maquette − 30 = zone de scène).

## 2. Chorale d'androïdes

- **Caméra** yaw 0, pitch 0,2, dist 3,5, h 1,5, f 215, cy 100 ; lumière `(−0,5, 0,75, −0,45)` ; brouillard `#0d0824`,
  far 12.
- **Fond (dans la bande)** : ciel `#05031a` puis `#0a0624` à y = 120 ; 30 étoiles ; 13 tuyaux d'orgue (`#1f1a4a`,
  silhouettes projetées) et leurs filaments néon (`#7a5cff` / `#3fa8ff`) ; grille de la scène `#2a1f78` ; deux spots
  au sol (ellipses `#7a5cff`) ; après la 3D, les rideaux d'hologramme (5 barres de chaque côté, qui pulsent) et le
  liseré du haut `#3a2a8a`.
- **3D** : la scène (dessus et avant, `#1a1440`), son bord avant néon `#5fe0ff` ; une **voix par note de l'accord** :
  gradin aussi haut que sa note (fil `#3fa8ff`), androïde dessus (jambes, corps à la couleur de sa voix, voyant de
  poitrine, partition blanche, tête blanche, yeux cyan qui clignent, bouche, antenne et sa boule) ; le **chef**, de dos
  sur son podium, en queue-de-pie, qui bat la mesure avec sa baguette.
- **Allègements** : yeux, bouche, voyant et partition en une face ; antennes, baguette et bras des choristes en traits ;
  ~260 primitives à 4 voix.
- **Encodeurs** : HARM l'accord (11 : OCT, 5, SUS4, m, m7, m9, m11, 69, M9, M7, M), donc le nombre de voix (2 à 4) et
  les marches ; TIMB le renversement (0 à 3) et, sur STRING MACH, le balancement des choristes ; MORPH la forme des
  têtes (larges et plates à 127) ; DECAY la durée du rose. Le nom de l'accord en haut à droite.
- **Note** : bouches ouvertes et roses, voyants, boules d'antenne et baguette roses ; le chef bat (la baguette descend
  en 0,12 s et remonte en 0,3 s) ; une note de musique rose s'envole d'un choriste sur deux, en alternant d'une note à
  l'autre (0,45 s, 50 px/s).

## 3. Funambule

- **Caméra** yaw 0, pitch 0,14, dist 3,5, h 1,7, f 200, cy 90 ; lumière `(−0,5, 0,75, −0,45)` ; brouillard `#0a0626`,
  far 14.
- **Fond (dans la bande)** : ciel `#05031a` puis `#0a0626` à y = 90 ; 30 étoiles ; chapiteau filaire (arêtes `#4a36b0`,
  deux anneaux `#352a8a`, la moitié arrière) ; grille du sol `#22186a` ; disque de la piste `#0e0a26` ; après la 3D, le
  cercle de la poursuite `#3fa8ff` sur l'acrobate et le public au premier plan (11 silhouettes, visières `#3fa8ff`).
- **3D** : le bord de la piste (16 quads violets et cyan alternés) ; deux mâts et leurs plateformes (fil `#5fe0ff`) ;
  la **corde laser** cyan `#5fe0ff`, 30 segments de 2 px ; l'**acrobate** (jambes bleues, torse rayé cyan et blanc, bras,
  tête blanche, visière cyan, balancier orange) qui avance et recule sur la corde (`u = 0,5 + 0,36 sin 0,35t`) en
  balançant son balancier. ~165 primitives.
- **Corde** : flèche statique `0,05 sin(πq)` plus la vibration de la maquette (`vib`) : 6 modes, amplitude
  `k^(−2,2 + 1,9 TIMB) sin(kπp)` (p : où l'acrobate l'a pincée), pulsation `22k (1 + 0,12 HARM k²)`, amortissement
  `(0,12 + 1,6 (1 − MORPH)) / √k` s.
- **Encodeurs** : HARM la raideur (les modes aigus s'écartent), TIMB la brillance (plus de modes aigus), MORPH
  l'amortissement, DECAY la durée du rose.
- **Note** : l'acrobate fait un petit saut (0,12, 0,2 s), la corde est pincée sous lui et repart en vibration ; les
  segments pincés sont roses 0,25 s.

## 4. Dancefloor

- **Caméra** yaw 0, pitch 0,36, dist 3,1, h 2,2, f 215, cy 102 ; lumière `(−0,5, 0,75, −0,45)` ; brouillard `#0a0626`,
  far 12.
- **Fond (dans la bande)** : ciel `#05031a` puis `#0a0626` à y = 120 ; le **mur de LED** (14 × 6 cellules projetées, une
  onde violette et bleue qui le parcourt) et son cadre cyan ; néons latéraux ; la poutre et ses 5 projecteurs ; les
  faisceaux qui balaient ; les **dalles éteintes** (8 × 6, métal, fil `#2c3a90`) ; après la 3D, 16 points cyan qui
  dérivent.
- **3D** : les **dalles allumées** (leur dessus seul, violet, cyan ou vert selon la hauteur du grain, fil de leur ton
  clair) ; enceintes (fil `#7a5cff`) et leurs woofers ; le **DJ** derrière sa console (platines qui tournent, tête qui
  hoche, casque, visière cyan) ; **trois danseurs** (bleu, vert, orange) qui sautillent et se balancent, bras en
  traits ; la **boule à facettes** à gauche (icosaèdre, fil `#3fa8ff`), qui tourne. ~290 primitives au pire.
- **Grains** (`grains` de la maquette), une dalle allumée par grain (le plus fort s'il y en a plusieurs) :
  - NOISE : une neige qui se renouvelle de 3 à 28 fois par seconde ; HARM la hauteur de la bande (la couleur), TIMB le
    nombre (10 à 60) et la cadence, MORPH la largeur de la bande ;
  - PARTICLE : des éclats épars, chacun s'allume puis s'éteint ; HARM leur étalement en hauteur, TIMB leur nombre ;
    **MORPH leur durée de vie** (longue à 0, brève à 127 : décroissance `exp(−a (3 + 12 MORPH))`) ;
  - SWARM : un essaim qui se promène ; HARM son rayon, TIMB le nombre (8 à 32) ; **MORPH sa traîne** : les dalles où
    l'essaim était `0,05 + 0,3 MORPH` s plus tôt restent allumées, plus sombres (ton de base).
- **Note** : éclair rose de 0,08 s sur les dalles allumées ; jusqu'à 6 rayons roses de la boule vers elles (0,2 s) ;
  les danseurs lèvent les bras et les woofers gonflent, le temps réglé par DECAY.

## 5. Architecture

| Fichier | Rôle |
|---|---|
| `app/scene_choir.c`, `scene_rope.c`, `scene_dance.c` (nouveaux) | Une scène chacun : `*_frame(const q_sview *v, uint32_t ms)` et `*_band(int32_t oy)`. |
| `app/scene.c`, `scene.h` | `scene_has` vrai pour tous les modèles ; aiguillage ; tampons de fond dans `sb`. |
| `app/app_ui.c` | Inclut les trois fichiers. |
| `app/tests/scene.txt`, `plateau.txt` | Plus de modèle sans scène : la page sans scène est prise ailleurs (ENV). |

- Les fonds projetés (silhouettes, lignes, cellules, dalles éteintes) sont calculés dans `*_frame` et dessinés dans la
  bande, sans primitive ; leurs tampons vont dans `sb`.

## 6. Tests

- **PC** : par scène et par moteur, encodeurs croisés (0, 64, 127 en toutes combinaisons) et une note : ≤ 300
  primitives, pas de NaN ; une note fait réagir la scène (image différente de la même image sans la note), plus rien
  après 2 s ; un encodeur change l'image ; chorale : 4 voix pour M, 2 pour OCT ; funambule : la corde revient au repos ;
  dancefloor : PARTICLE et SWARM, MORPH change l'image.
- **Simulateur** : `scenes5d2.txt`, les six modèles sur ENGINE et SOUND, captures.
- **Appareil** (par USB, seul) : le pire cas (`tools/device`), la piste 1 sur CHORDS, STRING, NOISE puis SWARM, 45 s
  chacune : zéro bloc perdu, SCENE FPS au-dessus de 15 ; chiffres dans `docs/cpu.md`.

## 7. Hors périmètre

- 5e (HOME).
- Les mineurs de 5d1 (facette rose manquée des crêtes, `gd_seg` / `wv_seg`, table des fils au-delà de 32).
