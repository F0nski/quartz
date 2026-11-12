# QUARTZ — étape 5c : scènes KIT, MODAL, SPEECH

Troisième partie de l'étape 5 de la spec globale (`2026-11-10-quartz-groovebox-design.md`, section 9). Part de
`step5b-seq`. Réutilise le moteur de l'étape 5a (`2026-11-11-quartz-step5a-gfx-design.md`). L'étape 5 est découpée en :

- 5a : moteur 3D et planète FM (fait) ;
- 5b : plateau SEQ, scènes MARBLES et GRIDS (fait) ;
- **5c** (ce document) : les scènes KIT (et BASS DRUM, SNARE, HI-HAT), MODAL, SPEECH ;
- 5d : les scènes des autres familles, en deux lots (5d1 : survol de crêtes, bowl, jardin de cristal, plateformes ;
  5d2 : chorale, funambule, dancefloor) ;
- 5e : HOME, page TRACKS (les emblèmes).

## 1. But

Donner leur scène aux modèles percussifs et aux deux modèles qui restent des premières maquettes. Critères : sur une
piste KIT, BASS DRUM, SNARE, HI-HAT, MODAL ou SPEECH, la scène remplace les barres sur les pages de la section 2 ;
chaque encodeur change un élément visible ; chaque note fait réagir la scène (pour un KIT, le polyèdre du moteur du pad
joué) ; le pire cas (section 7) tourne sans bloc perdu ; les autres modèles et pages s'affichent comme avant.

## 2. Où les scènes s'affichent

Zone de scène 240×166 ; en-tête et pied de page inchangés, les valeurs des encodeurs restent dans le pied.

| Modèle | Pages | Encodeurs lus par la scène |
|---|---|---|
| KIT | PAD (un KIT n'a pas de page SOUND) | ceux de PAD pour le pad édité (`ui.pad`, ou le pad tenu) |
| BASS DRUM, SNARE, HI-HAT | ENGINE, SOUND | ceux d'ENGINE, sur les deux pages |
| MODAL | ENGINE, SOUND | ceux d'ENGINE, sur les deux pages |
| SPEECH | ENGINE, SOUND | ceux d'ENGINE, sur les deux pages |

- La page KIT (grille des 16 pads et de leurs moteurs) garde son affichage : une page qui sert à éditer reste lisible
  d'abord.
- Sur SOUND, tourner MODEL vers un de ces modèles fait apparaître sa scène, vers un modèle sans scène la retire.
  Passer une piste en KIT (MODEL de SOUND) mène à ses pages PAD et KIT.
- Changer de pad édité ou de modèle relance la scène sans flash des notes d'avant (`scene_enter`).

## 3. La scène KIT : trois polyèdres dans un tunnel

Fidèle à la maquette `docs/screens/scenes.html` (« Kit »), dans la direction visuelle de la section 9.2.

- **Décor** : fond `#0c0820` ; un tunnel de 7 rectangles en fil de fer centrés en (120, 83) dans la zone, qui grandissent
  vers le spectateur (taille en carré de leur avancée), `#22184a` au loin, `#3a2470` de près ; quatre lignes de fuite du
  centre aux coins. Sur une frappe de grosse caisse, le rectangle le plus proche passe en rose et les proches en 2 px.
- **Polyèdres**, à hauteur d'œil, chacun tournant lentement sur lui-même, avec son nom dessous (`BD`, `SN`, `HH`, en
  `#8b84a8`) :
  - **BD** : un octaèdre bleu (x = 60) ; une frappe le fait gonfler puis revenir ;
  - **SN** : un cube vert (x = 128) ; une frappe le fait éclater, ses 6 faces s'écartent le long de leur normale puis
    se recollent ;
  - **HH** : un tétraèdre orange (x = 194) ; chaque frappe le fait basculer d'un quart de tour.
- **Une frappe** passe le polyèdre dans les trois tons roses, le temps réglé par DECAY ; une nouvelle frappe relance.
- **Le pad édité** : un trait de 2 px `#5fe0ff` sous le nom du polyèdre de son moteur.

| Encodeur (PAD, pad édité) | Élément du polyèdre de son moteur | Plage |
|---|---|---|
| TUNE | taille | grave gros, aigu petit (±25 %) |
| HARM | ampleur de la réaction : gonflement, écart des éclats, hauteur du saut pendant la bascule | faible à forte |
| TIMB | vitesse de rotation | lente à vive |
| DECAY | durée du flash rose | court à long |

Les deux autres polyèdres gardent des valeurs moyennes.

- **BASS DRUM, SNARE, HI-HAT** : le même tunnel avec leur seul polyèdre, centré (x = 120) et 1,6 fois plus grand,
  réagissant à chaque note de la piste.

| Encodeur (ENGINE) | Élément | Plage |
|---|---|---|
| HARM | ampleur de la réaction | faible à forte |
| TIMB | vitesse de rotation | lente à vive |
| MORPH | durée de la réaction (la décroissance du moteur) | courte à longue |
| DECAY | durée du flash rose | court à long |

- Environ 30 faces.

## 4. La scène MODAL : Chladni en relief

Fidèle à la maquette `docs/screens/scenes.html` (« Modal »).

- **Décor** : fond `#0c0820` ; cinq ellipses concentriques `#1a1438` au sol, centrées en (120, 94).
- **Relief** : une plaque de 11 × 11 quads (242 triangles) dont la hauteur suit la figure de Chladni
  `cos(n·πx)·cos(m·πy) − cos(m·πx)·cos(n·πy)`, vue en plongée (inclinaison 0,95 rad), qui pivote lentement de ±0,25 rad.
  Tons d'une face selon la valeur en son centre : près de zéro (lignes nodales) en clair `#8ff5c8`, positive en bleu,
  négative en vert sombre. Fil `#0a0f2e` sur les faces.
- **Percuteur** : un losange orange au-dessus de la plaque, qui descend de 14 px et remonte à chaque note.
- **Une note** : la plaque frémit (sa hauteur oscille vite autour du relief) puis s'apaise ; ses lignes nodales passent
  en rose.

| Encodeur (ENGINE) | Élément | Plage |
|---|---|---|
| HARM (matériau) | la figure : le mode (m, n), m de 1 à 3, n de m + 1 à m + 3 | 9 figures |
| TIMB (excitation) | force du frémissement et taille du percuteur | faible à forte |
| MORPH (amortissement) | durée du frémissement | courte à longue |
| DECAY | durée du rose sur les lignes nodales | court à long |

- Le relief n'est recalculé que lorsque la figure ou le frémissement change ; au repos, seule la rotation bouge.

## 5. La scène SPEECH : la tête qui parle

Fidèle à `docs/screens/speech.html` (« En direct »).

- **Décor** : fond `#0c0820` ; une grille de scan `#1e1640` (pas de 12 px) et une ligne de scan vert sombre qui descend
  en boucle (60 px/s).
- **Tête** : une sphère 10 × 14 déformée (crâne, arcade, orbites, nez, pommettes, mâchoire qui se rétrécit, menton),
  qui flotte sans cou et tourne lentement de ±0,6 rad. Fil de fer sur les faces, couleur de la teinte éclaircie de 15 %,
  rose quand la bouche s'ouvre. Faces des yeux en bandeau sombre (`#0b2a1f #06201a #03140f`), deux petites pupilles en
  losange néon `#8ff5ff` posées dessus quand la tête est de face, roses quand la bouche est grande ouverte. Faces de la
  bouche sombres, roses quand elle s'ouvre. La mâchoire descend pour ouvrir.
- **Barres de phonèmes** : 9 barres en bas de la zone, qui montent avec l'ouverture de la bouche.
- **Une note** : la bouche articule deux syllabes.

| Encodeur (ENGINE) | Élément | Plage |
|---|---|---|
| HARM (modèle de voix) | teinte de la tête | vert (formants), bleu (SAM), violet (LPC) |
| TIMB (espèce) | taille de la tête | 0,85 à 1,15 |
| MORPH (phonème) | ouverture de la bouche | 0,4 à 1 |
| DECAY | durée des deux syllabes | 150 à 500 ms |

- Environ 250 triangles, la moitié cachée.

## 6. Architecture

| Fichier | Rôle |
|---|---|
| `app/scene.c`, `scene.h` | Les scènes KIT, MODAL et SPEECH à côté de la planète. `scene_has` couvre MODAL, SPEECH, BASS DRUM, SNARE, HI-HAT ; `scene_frame` reçoit le modèle et, pour un KIT, les compteurs de frappes par moteur et le moteur du pad édité. |
| `app/p3d.c`, `p3d.h` | Des tons par face (un indice de partie par face) et un contour optionnel des faces, dessiné avec elles dans l'ordre du tri, qui ne trace pas les arêtes marquées comme diagonales de quad. |
| `app/audio.c`, `audio.h` | `audio_kit_hits(track, engine)` : les note-ons d'un KIT par moteur de pad (BD, SN, HH), comptés par le rendu au note-on à côté de `hits`. |
| `app/ui.c` | Les pages de la section 2 donnent la scène ; la scène KIT reçoit les encodeurs de PAD du pad édité. |

- La scène est calculée une fois par image puis dessinée en deux bandes, comme en 5a. Animations au temps (`plat_ms`).
- Calcul en simple précision, sinus et cosinus par `fastmath.h`, aucun appel à libm dans les boucles.
- Les maillages fixes (tête, polyèdres) sont construits une fois au démarrage ; la plaque de MODAL est recalculée en
  place.

## 7. Tests

- **PC** (tests C) :
  - une frappe sur un pad SN fait avancer le compteur SN de sa piste, et ni BD, ni HH, ni une autre piste ;
  - la scène s'affiche sur les pages de la section 2 pour les 6 modèles, pas sur la page KIT, pas pour les autres
    modèles ;
  - p3d : une face d'une partie prend les tons de cette partie ; le contour d'un quad trace ses 4 côtés et pas sa
    diagonale ;
  - MODAL : HARM à 0 et à 127 donne deux figures différentes, la plaque reste sous 300 triangles.
- **Simulateur** : captures des trois scènes au repos et sur une note, comparées aux maquettes.
- **Appareil** : section 8.

## 8. Mesure sur l'appareil

- Pire cas de l'étape 5a (deux pistes POLY 3, MODAL et 6-OP A, delay, reverb, deux PHASER, COMP), la piste MODAL
  affichée sur ENGINE pendant qu'elle joue : zéro bloc perdu, SCENE FPS relevé. Puis la piste passée en SPEECH : même
  relevé.
- Les chiffres vont dans `docs/cpu.md`.

## 9. Hors périmètre

- Les scènes de 5d, HOME (5e).
- Une scène sur la page KIT.
- La mesure sur l'appareil de 5b.
- Un rôle des encodeurs de SOUND dans les scènes.
