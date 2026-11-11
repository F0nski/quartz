# QUARTZ — étape 5a : moteur 3D et scène planète

Première partie de l'étape 5 de la spec globale (`2026-11-10-quartz-groovebox-design.md`, sections 9 et 12).
Part de `step3c-punchins`. L'étape 5 est découpée en trois :

- **5a** (ce document) : le moteur 3D en aplats, une scène (la planète FM), la mesure sur l'appareil ;
- **5b** : les autres scènes validées (KIT, MARBLES, MODAL, SPEECH) et le plateau SEQ H2 ;
- **5c** : la conception et la réalisation des scènes manquantes (GRIDS, autres familles) et les animations de HOME.

## 1. But

Prouver que les scènes animées en low poly tiennent sur le FM-1 sans toucher au son.
Critères : sur une piste FM, la planète remplace les barres sur ENGINE et SOUND ; chaque encodeur d'ENGINE change un
élément visible ; chaque note fait réagir la scène ; le pire cas (section 6) tourne sans bloc perdu ; les autres
modèles et pages s'affichent comme avant.

## 2. Où la scène s'affiche

- Pages **ENGINE** et **SOUND** d'une piste dont le modèle est 6-OP A, 6-OP B, 6-OP C ou 2-OP FM : la planète
  occupe la zone de scène (240×166). En-tête et pied de page inchangés : les valeurs des encodeurs restent dans le pied.
- Sur SOUND, tourner MODEL vers un modèle FM fait apparaître la scène, vers un autre la retire.
- Les autres pages s'affichent comme avant. Les autres modèles gardent les barres jusqu'en 5b ou 5c.

## 3. La scène planète

Fidèle à la maquette `docs/screens/scenes.html` (6-OP FM), dans la direction visuelle de la section 9.2.

- **Décor** : fond violet nuit ; étoiles qui défilent lentement de droite à gauche, en deux vitesses (petites
  lentes, grosses plus rapides) ; sous l'horizon (y = 120 dans la zone), un sol violet quadrillé en fil de fer, dont
  les lignes de fuite partent du centre et dont les lignes horizontales avancent vers le spectateur.
- **Planète** : une sphère taillée (5 bandes × 10 secteurs, environ 50 facettes) en orange, trois tons selon la
  lumière venue d'en haut à gauche ; elle tourne lentement sur son axe vertical. Une ombre plate (trapèze sombre) au
  sol sous elle.
- **Anneau** : un trait de 4 px en 12 segments droits autour de la planète ; la moitié arrière (ton ombre) est
  dessinée avant la planète, la moitié avant (ton clair) après.
- **Vaisseaux** : flèches de 3 triangles (deux flancs bleu ou vert en alternance, un réacteur rose), en orbite
  elliptique autour de la planète, chacune à sa vitesse ; ceux de la moitié arrière de leur orbite passent derrière
  la planète et l'anneau.

| Encodeur (ENGINE) | Élément | Plage |
|---|---|---|
| HARM | nombre de vaisseaux | 2 à 5 |
| TIMB | écart entre les orbites | serrées à larges |
| MORPH | inclinaison de l'anneau (et du plan des orbites) | presque plat à très penché |
| DECAY | durée du flash et des tirs | court à long |

- **Note jouée** par la piste (séquenceur, clavier, arpégiateur, générateur) : la planète passe dans les trois tons
  roses et chaque vaisseau tire un trait rose de 2 px vers elle. L'effet décroît sur la durée réglée par DECAY ; une
  nouvelle note le relance.
- L'animation suit le temps (`plat_ms`), pas le nombre d'images : une image moins fluide ne ralentit rien.
- Environ 70 faces en tout, sous la limite de 300 de la section 9.4.

## 4. Architecture

| Fichier | Rôle |
|---|---|
| `app/p3d.c` (nouveau) | Le module `gfx` de la spec : rotation, projection, tri des faces de l'arrière vers l'avant, triangles pleins, traits. Nommé ainsi pour ne pas se confondre avec `gfx.c` de fm1-base. |
| `app/scene.c` (nouveau) | La scène planète : l'état de la piste (4 encodeurs, notes jouées, temps) donne une image. |
| `app/audio.c` | Un compteur de notes par piste, incrémenté par le rendu à chaque note-on ; lu par l'interface. |
| `app/ui.c` | Sur ENGINE et SOUND d'une piste FM, appelle la scène au lieu des barres et redessine la zone à chaque image. Une ligne FPS sur la page CPU. |

- Les deux nouveaux fichiers entrent dans le build unitaire par `app.c`, comme les autres.
- **Dessin en deux bandes** : la mémoire ne permet pas une image entière (240×166 en 16 bits, 80 Ko). La scène est
  calculée une fois par image (sommets transformés et projetés, faces triées), puis chaque bande de 83 lignes
  dessine les faces et les traits qui la touchent, coupés à ses lignes. Un triangle à cheval sur les deux bandes
  donne exactement les mêmes pixels que s'il était dessiné d'un coup.
- **Calcul** en virgule flottante (le FPU du FM-1 est simple précision), sinus et cosinus par `fastmath.h`, aucun
  appel à libm dans les boucles. Le remplissage des triangles se fait par lignes horizontales.
- **Ton d'une face** : sa normale comparée à la lumière (haut-gauche-devant) choisit clair, base ou ombre ; pas de
  dégradé. Les faces tournées vers l'arrière ne sont pas dessinées.
- **Le son d'abord** : le rendu audio tourne en interruption et passe avant l'écran. Une scène lourde fait baisser
  la fluidité, jamais le son ; la mesure de la section 6 le vérifie.
- **FPS** : l'interface compte les images dessinées par seconde ; la page CPU l'affiche sur une 8e ligne.

## 5. Tests

- **PC** (tests C du simulateur hôte) :
  - un triangle remplit exactement les pixels attendus, coupé ou non entre les deux bandes, et rien hors de lui ;
  - les faces sont dessinées de l'arrière vers l'avant, les faces arrière écartées ;
  - la scène s'affiche sur ENGINE et SOUND pour les 4 modèles FM et pas pour les autres ;
  - une note jouée (séquenceur ou clavier) fait avancer le compteur de sa piste et de nulle autre.
- **Émulateur** (fm1-emu) : captures de la scène, comparées à la maquette.
- **Appareil**, avec l'utilisateur (section 6).

## 6. Mesure sur l'appareil

- Pire cas : celui de l'étape 3b (deux pistes POLY 3, 6 voix, delay, reverb, deux PHASER, COMP), la seconde piste
  passée de MODAL à 6-OP A et affichée sur ENGINE pendant qu'elle joue. Critère : zéro bloc perdu. Relevés : charge des deux cœurs,
  FPS.
- Le test appareil de l'étape 3c (punch-ins) se fait au même passage.
- Les chiffres vont dans `docs/cpu.md`.

## 7. Hors périmètre

- Toutes les autres scènes, le plateau SEQ H2, HOME (5b et 5c).
- Une scène sur les pages ENV, LFO, ARP, FX.
- Un rôle des encodeurs de SOUND dans la scène.
- Transparence, textures, dégradés, formes rondes (section 9.2 et 9.4).
