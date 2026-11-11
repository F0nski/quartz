# QUARTZ — étape 5b : plateau SEQ et scène MARBLES

Deuxième partie de l'étape 5 de la spec globale (`2026-11-10-quartz-groovebox-design.md`, section 9). Part de `main`
(étapes 1 à 4, 5a). Réutilise le moteur de l'étape 5a (`2026-11-11-quartz-step5a-gfx-design.md`). L'étape 5 est
découpée en :

- 5a : moteur 3D et planète FM (fait) ;
- **5b** (ce document) : le côté séquenceur, plateau SEQ H2, scènes MARBLES et GRIDS ;
- 5c : les scènes KIT (et BD, SNARE, HI-HAT), MODAL, SPEECH ;
- 5d : les scènes à concevoir (autres familles de moteurs) et HOME.

## 1. But

Donner au séquenceur l'univers visuel des scènes, sans perdre en lisibilité, et faire voir les notes que MARBLES joue
vraiment. Critères : les pages STEPS et XY montrent le plateau de la maquette H2 (`docs/screens/sequencer.html`,
« Plateau 4×4 » et « Pas tenu ») avec tout ce que la page montre aujourd'hui ; chaque note d'une piste MARBLES fait
tirer un vaisseau sur le cristal de sa note, qu'il touche quand elle sonne ; le pire cas (section 6) tourne sans bloc perdu. Les réglages fins se feront
à l'usage.

## 2. Le plateau SEQ

Pages STEPS et XY d'un pattern STEP, zone de scène 240×166. Les pages d'un pattern GRIDS gardent leur affichage
actuel jusqu'en 5d.

- **Décor, une ville immobile** (`docs/screens/backdrops.html`, « 1, Ville immobile », tours un peu moins visibles) :
  ciel `#07041a` aux étoiles dispersées au hasard, fixes ; horizon à y = 48 dans la zone ; deux rangées de tours en fil
  de fer discrètes (rectangles sombres bordés, antennes) qui ne bougent pas ; quelques fenêtres allumées, qui changent à
  chaque temps pendant la lecture ; sous l'horizon un sol `#0a0620` en lignes de fuite `#1c1340` dont les lignes
  horizontales avancent.
- **Plateau** : les 16 pas de la page en 4×4 isométrique (pas 1–4 sur la première rangée), sans perspective. Une
  case `(gx, gy)` a ses coins en `(120 + (gx − gy)·25, 34 + (gx + gy)·12,5)`. Sol : chaque case remplie `#0e0a26`,
  bordée `#2a1d55` ; au-delà de LEN, ou hors de X LEN × Y LEN sur la page XY, `#06031a` bordée `#120c2c`. Quand la
  boucle finit dans la page, un trait orange de 2 px borde la case qui suit le dernier pas.
- **Pas actif** : un bloc posé sur sa case (marge de 18 % de la case), haut de `5 + 22 · vel / 127` px, dessus clair,
  face gauche base, face droite ombre, en blanc-gris (`#ffffff #c9c3e4 #6f6890`), arête du dessus soulignée. Sur le
  dessus, sa note (nom dans la gamme, octave non affichée) ou, sur un KIT, le numéro de son premier pad.
- **Pas éteint** : sa case seule, un peu plus claire (`#16112f`).
- **Repères** : un petit triangle orange au coin du dessus quand le pas a d'autres attributs (ratchet > 1,
  probabilité < 100 %, condition, slide, accent, micro-timing non nul, au moins un p-lock) ; une bande verte au sol,
  vers la droite, quand la note dure plus d'un pas (`len > 4`), longue de `len / 8` cases au plus jusqu'au bord du
  plateau.
- **Lecture** (le pas en cours est sur la page affichée) :
  - un viseur en crochets de 2 px (vert clair, rose quand il joue une note) encadre sa case ;
  - quand il joue une note : son bloc passe en rose et saute de 6 px puis retombe, une onde carrée rose part de sa
    case sur le sol, trois éclats roses s'envolent du dessus ; le tout dure un pas, ou 150 ms au plus ;
  - à chaque temps, les bordures des cases proches du pas en cours s'éclairent (`#5b2f8c`) puis s'éteignent ;
  - en haut : `STEP nn` à gauche (vert clair, rose sur une note), la note du pas à droite (`--` s'il est éteint).
- **À l'arrêt** : pas de viseur, sauf en REC où il se pose en blanc sur le curseur d'écriture. En haut à gauche, la
  plage de la page (`17-32`).
- **Réglage de PATH** (comme aujourd'hui, 1,5 s après un changement) : le chemin se dessine sur le plateau, un trait
  violet `#8a6ad8` d'un centre de case au suivant, avec une flèche `#b89cff` au milieu de chaque saut ; RANDOM et XY
  n'en ont pas.
- **Pas tenu** : le plateau rétrécit à gauche (demi-case 16 × 8, origine (64, 48)), le bloc tenu se soulève de 10 px
  en orange, les autres blocs passent en gris éteint ; à droite, les 9 attributs (ordre actuel `ATTR_ROW`), une ligne de
  17 px chacun de x = 128 à 234, celui que règle PRESET surligné en orange ; sous le plateau, la note tenue en grand,
  en orange.
- Les notes affichées suivent la gamme du projet (section 9.2).

## 3. La scène MARBLES : l'escadrille

Pages MARBLES, T et X d'un pattern MARBLES (la page NOTES garde son clavier). Maquette de référence :
`docs/screens/marbles.html` (« Escadrille, canyon de rochers taillés »).

- **Synchronisation** : le séquenceur décide chaque pas d'une piste MARBLES un pas avant son heure ; l'écran connaît
  chaque note et l'instant où elle sonnera. Un tir part de façon à toucher sa cible quand la note sonne (trajet d'un
  pas, 500 ms au plus).
- **Décor, un canyon** : ciel nuit `#0c0820` et étoiles dispersées au hasard (positions fixes, trois tailles, les
  grosses rares et plus rapides) ; horizon à y = 66 dans la zone ; au loin une crête de montagnes en facettes
  (`#2a1c52`, `#1c1240`) qui glisse lentement ; fond du canyon `#120a28` avec 5 lignes qui avancent ; de chaque côté
  deux rangées de rochers low poly (blocs cabossés, un sur trois environ une aiguille), tailles, formes et
  orientations tirées au hasard et gardées tout le trajet, avec des trouées ; des pierres éparses au milieu du fond.
  Tout avance vers le spectateur au rythme de DIV ; un rocher sort par le côté avant de disparaître.
- **Les cibles** : un cristal (octaèdre étiré) par note permise (jusqu'à 12), en rangée en bas, sur un socle en bloc
  isométrique ; nom de la note dessous. Le socle et le cristal grandissent avec l'historique (+1 par note reçue, 6 au
  plus, −1 toutes les 4 s). Une note écartée par STEPS : cristal en simple contour, socle bas et sombre.
- **Un tir qui touche** : le cristal passe au rose, éclate en éclats qui retombent, une onde en losange court au sol,
  puis il repousse en 350 ms.
- **L'escadrille** : 1 à 3 vaisseaux (flèches en trois tons, cockpit, flamme qui scintille), qui s'inclinent dans les
  virages et passent au blanc quand ils tirent. Ils volent en boucle au-dessus du canyon. Les tirs sont des traits roses
  avec une traînée en paliers plus sombres, de la position du vaisseau au moment du tir jusqu'au cristal.

| Encodeur | Effet visible |
|---|---|
| DIV | vitesse du décor, durée du trajet des tirs (un pas) |
| T OUT | nombre de vaisseaux (1 à 3) |
| LEN | rangée de témoins de boucle en haut, le pas en cours allumé |
| MODE | formation : ligne, V, colonne, diagonale, vague, triangle, ronde |
| BIAS | taille du vaisseau de tête par rapport aux autres |
| JITTER | balancement lent de l'escadrille (1 à 2 px) |
| DEJA VU | > 64 : vaisseaux, tirs et cristaux à l'orange, traînée orange derrière les vaisseaux, trajet répété tracé |
| SPREAD | écart entre les vaisseaux |
| X BIAS | position de la boucle de vol, de gauche à droite |
| STEPS | cristaux des notes écartées éteints |

- Aucune transparence ni dégradé. Les étoiles de la planète FM (5a) prennent le même tirage au hasard (leur motif en
  diagonale disparaît).
- **Budget** : au plus 300 faces visibles ; les rochers les plus lointains (moins de 15 % de la profondeur) sont dessinés
  en un seul triangle. Si le budget ou la fluidité l'exigent, on dessine moins de rochers (accord de l'utilisateur).

## 3 bis. La scène GRIDS : les piles

Pages GRIDS, MAP et DENS d'un pattern GRIDS. Maquette : `docs/screens/backdrops.html` (« GRIDS en piles », G2).

- **Plateau** : les 32 pas de la boucle sur un plateau isométrique de 8 × 4 cases (pas 1–8 sur la première rangée),
  toute la largeur, épais (deux faces de côté) et posé sur une ombre ; les temps (pas 1, 5) marqués d'un trait sur le
  bord arrière, les rangées numérotées 1, 9, 17, 25 à gauche, la rangée en cours en vert.
- **Piles** : chaque pas porte une pile de blocs dans l'ordre du kit, BD à la base, SN, HH au sommet. Un bloc est là
  quand le niveau de la carte à X/Y dépasse le seuil de DENS de sa voix ; sa hauteur suit ce niveau (l'accent). Les
  couleurs : BD orange, SN vert, HH bleu ; la voix de VOICE dans ses trois tons, les autres plus sombres. Une frappe
  ajoutée par CHAOS apparaît en contour de losange au-dessus de la pile, jusqu'à la fin du passage.
- **Lecture** : viseur sur le pas en cours ; une frappe y fait sauter le bloc de sa voix en rose, avec l'onde carrée et
  les éclats du plateau SEQ. En haut à gauche `STEP nn` et, dessous, la voix et la note jouées par une piste synthé
  (`HH G#2`).
- **Densités** : trois lignes fines en haut à droite (BD, SN, HH), longues comme DENS, roses sur une frappe.
- **Carte** : une mini-carte de 5 × 5 points en bas à gauche, le curseur X/Y en losange blanc qui tremble avec CHAOS.
- **Décor, un sol filaire** : fond `#07041a`, un quadrillage isométrique `#120c2a` très sombre qui prolonge le plateau
  dans tout l'écran.

| Encodeur | Effet visible |
|---|---|
| DIV | vitesse du viseur |
| VOICE | la voix mise en couleur vive dans les piles ; le texte sous `STEP` |
| NOTE | la note du texte sous `STEP` |
| X, Y | les piles changent (motif et hauteurs) ; le curseur de la mini-carte |
| CHAOS | frappes en plus en contour ; le curseur de la mini-carte tremble |
| DENS 1–3 | les blocs de la voix apparaissent ou s'éteignent ; la ligne de densité |

- Le niveau d'un pas vient de la carte de Grids (`grids_read`), calculé par l'interface à partir du pattern ; les
  frappes réellement jouées (CHAOS compris) passent par l'anneau des notes, comme pour MARBLES.

## 4. Architecture

| Fichier | Rôle |
|---|---|
| `app/plateau.c`, `plateau.h` (nouveaux) | Le plateau SEQ (décor ville) et les piles GRIDS : cases, blocs, repères, viseur, chemin, pas tenu. Dessinés directement dans chaque bande, du fond vers l'avant (ordre `gx + gy`), pour que le texte posé sur un bloc soit caché par le bloc de devant. |
| `app/scene.c`, `scene.h` | La scène MARBLES (escadrille, canyon, cristaux) à côté de la planète. |
| `app/gen.c`, `gen.h` | `gen_grids_level` : le niveau 0..255 d'un pas et d'une voix pour le pattern, sans CHAOS. |
| `app/seq.c`, `seq.h`, `app/audio.c`, `audio.h` | Un pas MARBLES est décidé un pas à l'avance ; ses notes sont gardées avec leur instant (16 par piste) ; l'interface lit celles qu'elle n'a pas encore vues, avec le délai avant qu'elles sonnent. |
| `app/p3d.c` | Règle de remplissage aux bords horizontaux corrigée : lignes dont le centre est dans [y haut, y bas), plus de 0/0 quand un sommet tombe sur un centre de ligne. |
| `app/ui.c` | STEPS et XY → plateau ; MARBLES, T, X d'un pattern MARBLES → scène MARBLES ; tous deux animés, redessinés au plus toutes les 30 ms comme la planète. SCENE FPS compte aussi ces pages. |

- Le plateau reçoit de `ui.c` tout ce que la page montre déjà (`q_view.scene` : pas de la page, curseur, position,
  LEN, X/Y LEN, chemin, p-locks, pas tenu et attribut) plus l'heure et le compteur de notes de la piste ; il ne lit
  pas le projet lui-même.
- Le déclenchement « le pas en cours joue une note » vient de `audio_hits` (étape 5a) : le compteur change pendant
  que la position est sur ce pas.
- Les animations suivent `plat_ms`.

## 5. Tests

- **PC** (tests C) :
  - plateau : hauteur d'un bloc selon la vélocité (vel 0 et 127), case sombre au-delà de LEN et hors X/Y LEN, triangle
    orange sur un pas avec p-lock et pas sur un pas simple, bloc tenu en orange et les autres en gris ;
  - p3d : un triangle à base plate dont les sommets tombent sur des centres de ligne remplit les bonnes lignes, sans
    NaN ;
  - audio : chaque note jouée entre dans l'anneau de sa piste, dans l'ordre, ratchets compris ; l'interface qui lit
    en retard de moins de 16 notes n'en perd aucune ;
  - MARBLES : une note décidée lance un tir qui touche le cristal de sa note à l'instant où elle sonne ; le socle monte, puis redescend avec le temps ; une note hors des notes permises vise le cristal le plus proche ; T OUT règle le nombre de vaisseaux, MODE la formation ;
- **Simulateur** : script avec captures du plateau en lecture, pas tenu, chemin SNAKE, page XY, et de MARBLES en jeu.
- **Appareil** : section 6.

## 6. Mesure sur l'appareil

- Pire cas de l'étape 5a (deux pistes POLY 3, MODAL et 6-OP A, delay, reverb, deux PHASER, COMP), la piste 1 en
  pattern STEP de 16 pas tous actifs avec ratchets, affichée sur STEPS : zéro bloc perdu, SCENE FPS relevé. Puis une
  piste MARBLES à T OUT 3 affichée sur MARBLES : même relevé.
- Les chiffres vont dans `docs/cpu.md`.

## 7. Hors périmètre

- Les scènes KIT, MODAL, SPEECH (5c) ; HOME (5d).
- LEN pour GRIDS (étape ultérieure, spec globale section 6).
- La variante 2×8 du plateau (maquette H3).
- Les autres points mineurs de 5a (traits non découpés hors écran, tirs de 1 px à l'horizontale, lignes du sol hors
  bande, tableau de rotation).
