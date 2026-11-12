# QUARTZ — étape 5d1 : survol de crêtes, bowl, jardin de cristal, plateformes

Quatrième partie de l'étape 5 de la spec globale (`2026-11-10-quartz-groovebox-design.md`, section 9.3). Part de
`step5c-scenes`. L'étape 5 est découpée en : 5a (moteur, planète, fait), 5b (plateau SEQ, MARBLES, GRIDS, fait), 5c
(KIT, MODAL, SPEECH, fait), **5d1** (ce document), 5d2 (chorale, funambule, dancefloor), 5e (HOME).

## 1. But

Donner leur scène à neuf moteurs de plus, fidèle aux maquettes validées, sous le budget de 300 primitives et sans
perte de son. Critères : sur ENGINE et SOUND d'une piste de ces modèles, la scène remplace les barres ; chaque
encodeur change un élément visible ; chaque note fait réagir la scène ; le pire cas (section 8) tourne sans bloc perdu.

| Scène | Modèles (n°) | Maquette (`docs/screens/`) |
|---|---|---|
| Survol de crêtes | VA VCF (0), PHASE DIST (1), VA (8), WAVESHAPE (9) | `analog.html`, canvas a0..a3 (`crests`) |
| Bowl en hoverboard | WAVE TERRAIN (5), WAVETABLE (13) | `wavetable.html`, bowl0/bowl1 (`bowl`) |
| Jardin de cristal | GRAIN FORM (11), ADDITIVE (12) | `spectral.html`, garden0/garden1 (`garden`) |
| Plateformes | CHIPTUNE (7) | `chiptune.html`, platforms (`platforms`) |

Les valeurs que la spec ne fixe pas (positions, tailles, tons, vitesses) sont celles des maquettes ; les y de la
maquette moins 30 donnent la zone de scène.

## 2. Le moteur : une caméra en perspective (`p3d`)

Les quatre maquettes partagent la même caméra, qu'on ajoute à `p3d` à côté des objets posés de 5a :

- **Caméra** `{yaw, pitch, dist, h, f, cy}` : `X = x cos yaw − z sin yaw`, `Z = x sin yaw + z cos yaw + dist`,
  `Y = y − h`, puis `(X, Y cos p + Z sin p, Z cos p − Y sin p)` ; écran `(120 + X f / Z, cy − Y f / Z)`. Un élément
  dont un sommet a `Z < 0,3` n'est pas dessiné.
- **Primitives en coordonnées du monde** : triangle, quad (deux triangles, diagonale cachée), boîte, poutre (boîte entre
  deux points), trait (1 ou 2 px). Chacune avec trois tons, un fil optionnel et un biais de profondeur (profondeur triée
  = Z moyen − biais). Les faces tournées vers l'arrière sont écartées.
- **Lumière dans le repère de la caméra** : normale de la face comparée à `L` ; ton clair si > 0,6, base si > 0,2,
  ombre sinon. Un ton imposé (« plat ») remplace ce choix.
- **Brouillard** : `fog = clamp((z − 0,35 far) / (0,65 far), 0, 0,85)`, arrondi au huitième ; faces, fils et traits
  mélangés vers la couleur de brouillard (mélange RGB565 par canal).
- La table des couleurs de fil passe de 8 à 32 entrées (bits 3–7 de `emask`), le brouillard multipliant les teintes.

Tout passe par la liste de 300 primitives existante, triée une fois et dessinée dans les deux bandes.

## 3. Survol de crêtes

- **Décor (dans la bande)** : ciel en 4 bandes jusqu'à l'horizon y = 37 (`#06031a`, `#0b0624`, `#120a30`,
  `#1b0f3d`) ; 30 étoiles au-dessus de y = 23, qui glissent à gauche (grosses blanches 2 px, petites `#6c5fa0`) ; lune
  taillée (8 triangles, cratère `#8a7bcc`) en (184, 16) ; deux chaînes de crêtes lointaines de 19 hauteurs, qui glissent
  à 2 et 4 px/s ; sol `#170f38` sous l'horizon, ligne `#4b2f8a`.
- **Relief** : bandes de crêtes (rangées paires, hautes) et de vallées (impaires, basses) qui avancent (`scroll = 2t`,
  rangée r à `z = 1,4 + 1,5 r − scroll`) ; caméra yaw 0, pitch 0,5, dist 0, h 4,2, f 130, cy 106 ; lumière
  `(−0,8, 0,45, 0,1)`, brouillard `#170f38`, far 18. **16 cellules × 8 bandes** sur la partie utile `|x| ≤ 5,4` (les
  cellules plates des bords ne sont pas dessinées : le sol les remplace). Hauteur :
  `env · (0,15 + 1,15 u)` en crête, `env · (0,04 + 0,22 u)` en vallée, `u = 0,5 + 0,5 · wave(x / 3,6 + 0,25 + 0,2 r)`,
  `env = clamp((5,4 − |x|) / 1,6, 0, 1)`, le tout × `grow = min(1, (far − z) / 3 + 0,05)` (le relief naît au loin).
  Faces basses (y moyen < 0,3) violettes `#6b4bc4 #3d2a85 #22164f`, hautes bleues `#8fb4ff #3a6dff #1c3590`. Fil de
  crête `#8fb4ff` sur chaque rangée paire.
- **Formes d'onde** (encodeurs, p de 0 à 1) : celles de `crests` dans la maquette, par moteur :
  - VA VCF : source scie/carré (MORPH) filtrée par un SVF (TIMB coupure, HARM résonance), table de 64 recalculée quand
    un encodeur change ;
  - VA : deux oscillateurs (TIMB, MORPH) décalés de rangée en rangée de `0,18 r (0,1 + 1,4 HARM)` ;
  - WAVESHAPE : triangle asymétrique (MORPH), saturé (HARM), replié (TIMB) ;
  - PHASE DIST : distorsion de phase (MORPH), modulée (TIMB, HARM le rapport).
- **Vaisseau** (2D, devant) : flèche blanche/gris lavande à 3,2 devant, qui serpente (`x = 1,4 sin 0,8t`) en suivant
  la crête, inclinée dans les virages ; flamme rose.
- **Note** : la crête visée devant le vaisseau prend 1 à 2 facettes roses, son fil vire au rose et s'éteint, un tir rose
  de 2 px part du vaisseau ; DECAY règle la durée (`0,05 + 0,4 DECAY` s).

## 4. Bowl en hoverboard

- **Décor (dans la bande)** : ciel `#06041a`, `#0b0726`, `#120b33` ; 30 étoiles au-dessus de y = 50 ; ville de 12
  blocs `#120b30`/`#160e38` aux fenêtres cyan et violettes ; dalles du deck `#3b2a90` et clôture (poteaux `#5a3fc0`,
  lisse `#9a7cff`) dessinées en fond, sous tout le reste.
- **3D** : caméra yaw `0,3 + 0,12 sin 0,2t`, pitch 0,85, dist 4,6, h 5, f 215, cy 120 ; lumière `(−0,5, 0,8, −0,3)`,
  brouillard `#0b0726`, far 16.
  - Cuvette **8 × 8 quads**, demi-largeurs 2 × 1,4 (WAVETABLE) ou 2 × 2 (WAVE TERRAIN), un ton par quad (`#232863` en
    haut, `#1b1f50` au fond), fil `#2c6fd8`. Hauteur : half-pipe `1,3 u²` dont les parois portent la forme d'onde
    (WAVETABLE : 4 banques mélangées par HARM, TIMB leur paramètre, MORPH la déformation de phase), ou bowl
    `1,3 min(1, 1,1 (u² + w²))` déformé par le terrain (WAVE TERRAIN : 4 terrains mélangés par HARM).
  - Parois verticales jusqu'au deck (y = 1,3) seulement là où elles ont une hauteur (bords en w du half-pipe) ;
    deck `#140f30` ; coping cyan `#5fe0ff` 2 px ; deux pylônes (trait `#7a5cff` et barre lumineuse cyan).
  - Lignes peintes : WAVETABLE 3 profils `#8fe8ff` de 8 segments ; WAVE TERRAIN le cercle lu en pointillés `#ffc196`
    (10 segments).
- **Rider** : planche orange, corps, casque, visière cyan (une dizaine de boîtes et poutres), orienté sur la normale
  de la surface ; WAVETABLE : kick turns d'un bord à l'autre ; WAVE TERRAIN : il roule sur le cercle lu (TIMB son
  rayon, MORPH son décalage). Ombre en losange.
- **Note** : traînée rose derrière le rider, qui pâlit selon DECAY ; une note sur deux, petit kickflip (saut de 0,12,
  un tour de planche en 0,2 s) et 4 étincelles roses à l'atterrissage.

## 5. Jardin de cristal

- **Décor (dans la bande)** : ciel `#05031a`, `#0b0624`, `#120a2c` ; 30 étoiles ; lune taillée en (196, −4) ; arches
  du dôme `#4a36b0`, mur du fond `#352a8a`, faîte `#6a4ee0`, pont `#0e0a26` et sa grille `#2a1f78`, dessinés en fond.
- **3D** : caméra yaw 0,12, pitch 0,26, dist 4, h 1,75, f 200, cy 120 ; lumière `(−0,5, 0,75, −0,45)`, brouillard
  `#120a2c`, far 12. Jardinière bleue (fil `#3fa8ff`), 11 éclats de cristal cyan.
- **16 fleurs**, une par harmonique, réparties sur la jardinière ; `a` = la valeur du spectre pour la fleur :
  - hauteur `0,25 + 1,45 a`, balancement léger ;
  - tige : un trait vert `#22a06a` de 2 px ; deux feuilles : un triangle chacune ;
  - 6 pétales, **un triangle chacun**, tournés vers la caméra, ouverture `(a − 0,25) / 0,45`, tons bleu, lavande ou
    pervenche selon la fleur ;
  - cœur : une face orange ; reflet au sol (losange plat) pour `a > 0,45`.
- **Spectre** (gaussiennes) : ADDITIVE : 1 à 4 bosses (HARM), position du pic (TIMB), largeur (MORPH) ; GRAIN FORM :
  deux formants, f1 (TIMB), écart du second (HARM), largeur (MORPH).
- **Note** : les fleurs s'ouvrent davantage, les cœurs des fleurs ouvertes passent au rose (durée selon DECAY) ;
  du pollen rose monte des fleurs ouvertes (3 grains chacune, 2D) ; 10 lucioles vertes (2D).

## 6. Plateformes

- **Décor (dans la bande)** : ciel `#05031a`, `#0a0626` ; 26 étoiles qui scintillent ; **lune et nuages en voxels
  dessinés en 2D** : leurs cubes projetés une fois en carrés à trois tons (lune lavande, nuages violets `#3a2c90`), les
  nuages glissant à droite ; sol en grille `#22186a`.
- **3D** : caméra yaw `0,08 sin 0,3t`, pitch 0,2, dist 3,4, h 1,6, f 210, cy 96 ; lumière `(−0,5, 0,75, −0,45)`,
  brouillard `#0a0626`, far 12.
  - **Blocs** : un par note de l'accord (HARM : 11 accords, de l'octave à la majeure), à la hauteur de sa note, socle
    violet (fil `#7a5cff`), dessus vert néon, joints `#5a48a0`, pilier `#2a1f78` ; demi-largeur `0,1 + 0,3 (1 − MORPH)`
    (l'impulsion qui s'affine).
  - **Robot** cube cyan (fil blanc), jambes, yeux, antenne au bout cyan ; squash et stretch à l'atterrissage.
- **Note** : le robot saute (0,22 s, arc de 0,5) du bloc de la note d'avant à celui de la note jouée, pris dans l'ordre
  de l'arpège (TIMB : montant, descendant, aller-retour, au hasard) ; le bloc visé et le bout de l'antenne passent au
  rose ; une pièce rose monte en tournant (0,4 s) ; traînée de 4 petits cubes en l'air ; score `10 × notes` sur 5
  chiffres en haut à gauche (`#ffd25a`), le nom de l'accord et du mode en haut à droite.

## 7. Architecture

| Fichier | Rôle |
|---|---|
| `app/p3d.c`, `p3d.h` | La caméra en perspective, les primitives du monde, le brouillard, 32 couleurs de fil. |
| `app/scene_va.c`, `scene_wave.c`, `scene_garden.c`, `scene_chip.c` (nouveaux) | Une scène chacun : `*_frame(const q_sview *v, uint32_t ms)` et `*_band(int32_t oy)`. |
| `app/scene.c`, `scene.h` | `scene_has` et l'aiguillage vers les nouvelles scènes. |
| `app/app_ui.c` | Inclut les quatre fichiers (code de l'écran, en fin de flash). |

- Les notes : `hits[0]` de la vue (5c). Les sauts, la cible de l'arpège, le kickflip une note sur deux et le score
  avancent avec le compteur de notes ; `scene_enter` : pas de flash ni de saut pour les notes d'avant.
- Les maillages variables (relief, cuvette) sont dans le tampon partagé des scènes (5c), reconstruits quand leur scène
  revient. Les tables (VA VCF) sont recalculées quand un encodeur change, pas à chaque image.
- Calcul en simple précision, `fastmath.h`, pas de libm.

## 8. Tests

- **PC** : par scène, aux encodeurs 0 et 127 et pendant une note : moins de 300 primitives, aucun NaN (ASan et UBSan) ;
  une note fait apparaître du rose, qui disparaît après DECAY ; pas de rose à l'arrivée sur la scène ; un encodeur
  change l'image (HARM, TIMB, MORPH) ; p3d : un quad du monde se projette à l'endroit attendu, une face arrière est
  écartée, le brouillard assombrit une face lointaine.
- **Simulateur** : `scenes5d1.txt`, les neuf modèles sur ENGINE et SOUND, captures comparées aux maquettes.
- **Appareil** (sans l'utilisateur, par USB) : le pire cas de 5c (montage scripté), chaque scène affichée 45 s : zéro
  bloc perdu, SCENE FPS relevé ; chiffres dans `docs/cpu.md`. Les outils de mesure (montage, capture, relevé de la page
  CPU) entrent dans le dépôt, sous `tools/device/`.

## 9. Hors périmètre

- 5d2 (chorale, funambule, dancefloor), 5e (HOME).
- Le code audio en RAM.
- Les rôles des encodeurs de SOUND dans les scènes.
