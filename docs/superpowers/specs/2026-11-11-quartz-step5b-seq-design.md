# QUARTZ — étape 5b : plateau SEQ et scène MARBLES

Deuxième partie de l'étape 5 de la spec globale (`2026-11-10-quartz-groovebox-design.md`, section 9). Part de `main`
(étapes 1 à 4, 5a). Réutilise le moteur de l'étape 5a (`2026-11-11-quartz-step5a-gfx-design.md`). L'étape 5 est
découpée en :

- 5a : moteur 3D et planète FM (fait) ;
- **5b** (ce document) : le côté séquenceur, plateau SEQ H2 et scène MARBLES ;
- 5c : les scènes KIT (et BD, SNARE, HI-HAT), MODAL, SPEECH ;
- 5d : les scènes à concevoir (GRIDS, autres familles) et HOME.

## 1. But

Donner au séquenceur l'univers visuel des scènes, sans perdre en lisibilité, et faire voir les notes que MARBLES joue
vraiment. Critères : les pages STEPS et XY montrent le plateau de la maquette H2 (`docs/screens/sequencer.html`,
« Plateau 4×4 » et « Pas tenu ») avec tout ce que la page montre aujourd'hui ; chaque note d'une piste MARBLES fait
tomber une bille dans la case de sa note ; le pire cas (section 6) tourne sans bloc perdu. Les réglages fins se feront
à l'usage.

## 2. Le plateau SEQ

Pages STEPS et XY d'un pattern STEP, zone de scène 240×166. Les pages d'un pattern GRIDS gardent leur affichage
actuel jusqu'en 5d.

- **Décor** : fond `#07041a` ; 17 lignes de fuite `#160f36` depuis le point (120, 4) ; 6 bandes horizontales qui
  glissent vers le spectateur ; deux cubes en fil de fer `#2a1d55` qui tournent lentement dans les coins hauts.
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

## 3. La scène MARBLES

Pages MARBLES, T et X d'un pattern MARBLES (la page NOTES garde son clavier).

- **Décor** : fond `#0c0820`. En haut à gauche la position dans la boucle (`5/16`, `-/16` à l'arrêt), à droite la
  gamme du projet (`D DORIAN`).
- **Plots** : 5 rangées de petites croix `#3a2c64` (6 px), en quinconce, de y = 22 à y = 98. Écart horizontal
  `22 + 8 · SPREAD / 127` px ; chaque rangée décalée de `(X BIAS − 64) / 64 · 7 · rangée / 4` px.
- **Cases** : une par note permise (les classes de hauteur de `gen_allowed`, jusqu'à 12), réparties sur 224 px en
  bas. Sous chacune, son nom ; au-dessus, une colonne en prisme low poly (face et dessus) dont la hauteur suit
  l'historique : +1 à chaque bille reçue, plafonné à 6, et qui redescend d'une unité toutes les 4 s.
- **Billes** : chaque note jouée par la piste lâche un icosaèdre (20 faces) blanc-gris de rayon 7 px depuis le haut
  au centre. Il descend en 0,5 s ; à chaque rangée de plots il dévie d'une demi-case, toujours vers la case de sa
  note, et atterrit dedans. Une note hors des notes permises va à la case la plus proche. À l'arrivée, la case et son
  nom flashent en rose 150 ms. DEJA VU > 64 : les billes en vol sont orange. 6 billes en vol au plus ; au-delà, la plus
  ancienne est retirée.
- Les encodeurs agissent par le son : SPREAD, X BIAS, STEPS, DEJA VU changent les notes jouées, donc les cases où les
  billes tombent et la forme de l'historique. SPREAD et X BIAS changent aussi les plots.

## 4. Architecture

| Fichier | Rôle |
|---|---|
| `app/plateau.c`, `plateau.h` (nouveaux) | Le plateau SEQ : décor, cases, blocs, repères, viseur, chemin, pas tenu. Dessiné directement dans chaque bande, du fond vers l'avant (ordre `gx + gy`), pour que le texte posé sur un bloc soit caché par le bloc de devant. |
| `app/scene.c`, `scene.h` | La scène MARBLES à côté de la planète : billes (`p3d_mesh` de l'icosaèdre), plots, cases. |
| `app/audio.c`, `audio.h` | Les dernières notes jouées, par piste : un anneau de 16 notes et un compteur ; l'interface lit celles qu'elle n'a pas encore vues. |
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
  - MARBLES : une note lâche une bille qui finit dans la case de sa note, la colonne monte, puis redescend avec le
    temps ; une note hors des notes permises va à la case la plus proche.
- **Simulateur** : script avec captures du plateau en lecture, pas tenu, chemin SNAKE, page XY, et de MARBLES en jeu.
- **Appareil** : section 6.

## 6. Mesure sur l'appareil

- Pire cas de l'étape 5a (deux pistes POLY 3, MODAL et 6-OP A, delay, reverb, deux PHASER, COMP), la piste 1 en
  pattern STEP de 16 pas tous actifs avec ratchets, affichée sur STEPS : zéro bloc perdu, SCENE FPS relevé. Puis une
  piste MARBLES à T OUT 3 affichée sur MARBLES : même relevé.
- Les chiffres vont dans `docs/cpu.md`.

## 7. Hors périmètre

- Les scènes KIT, MODAL, SPEECH (5c) ; GRIDS et HOME (5d).
- La variante 2×8 du plateau (maquette H3).
- Les autres points mineurs de 5a (traits non découpés hors écran, tirs de 1 px à l'horizontale, lignes du sol hors
  bande, tableau de rotation).
