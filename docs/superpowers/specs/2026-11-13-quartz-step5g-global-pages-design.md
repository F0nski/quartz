# QUARTZ — étape 5g : les pages globales

Deuxième lot des pages encore sans design (spec globale `2026-11-10-quartz-groovebox-design.md`, section 9). Part de
l'étape 5f. Maquette : `docs/screens/global.html` (direction « très claire »). Lot suivant : grilles d'édition (KIT,
NOTES).

## 1. But

Les pages TEMPO, SCALE, KEYS et FX de GLO, et SONG de HOME, montrent ce qu'on règle de façon immédiatement lisible :
un grand titre, un dessin plat et simple par réglage, chacun légendé. Pas de ruban ni de relief : ces pages règlent le
projet, on doit les comprendre d'un coup d'œil. Critères : chaque encodeur pilote un élément visible, légendé dans sa
couleur ; ce qui joue (pas, touche, note) passe en rose ; zéro bloc perdu.

## 2. Principe commun

- Dessin plat : rectangles, triangles et texte (`cv_rect`, `cv_text`, triangles pleins), sur fond noir. Polices de
  `ui.c` : le grand titre en `FONT_L` ou `FONT_M`, les légendes en `FONT_S` / `FONT_XS`.
- **Couleurs** : les tons de base des encodeurs (bleu `#3a6dff`, vert `#22d58e`, blanc `#c9c3e4`, orange `#ff7a2e`),
  leurs tons clair et d'ombre de la spec 5f pour les variantes ; rose `#ff2f8e` (ombre `#8a1050`) pour ce qui joue ;
  fil éteint `#2e2850` pour les rails et les fonds de jauge ; gris `#8b84a8` pour les légendes secondaires ; touches
  blanches `#d8d3ec`, touches noires `#1d1838`.
- **Rangée d'options** : une légende dans la couleur de l'encodeur, puis les valeurs possibles côte à côte en gris ;
  la valeur choisie sur un pavé de la couleur de l'encodeur, en noir.
- L'en-tête et le pied de page ne changent pas.

## 3. Les pages

| Page | Ce qu'on voit | Ce qui joue |
|---|---|---|
| **TEMPO** | Le tempo en grand au centre, « BPM » dessous. Une mesure de 16 cases (temps hauts, croches moyennes, contretemps bas ; temps et croches en bleu, contretemps en vert), les contretemps décalés vers la droite selon SWING (de 0 à la moitié d'un pas), « SWING 62 % » en vert sous la mesure. Deux jauges légendées : COMP (blanc) et DUCK (orange), longues selon leur valeur, remplies depuis la droite. | En lecture, la case du pas de la piste passe en rose ; à chaque temps, les deux jauges reculent un instant puis reviennent (0,25 s ; figuré, non mesuré). |
| **SCALE** | La gamme en grand (« A LOCRIAN »). Une octave de piano à plat : la tonique en bleu, les notes du mode en vert, les autres en touches ordinaires ; nom de note sur chaque touche blanche, « # » sur une noire allumée. Deux rangées d'options : QUANT (OFF, 50 %, 100 %) en blanc, CLICK (OFF, METRO, COUNT, BOTH) en orange. | Les touches tenues au clavier passent en rose (leur classe de note). |
| **KEYS** | Une rangée d'options IN SCALE (OFF, MARBLES, ALL) en bleu ; une phrase dessous : « CHROMATIC KEYBOARD », « SCALE ON MARBLES TRACKS » ou « SCALE ON EVERY TRACK », et la gamme en gris quand elle s'applique. Les 16 touches blanches du FM-1, chacune avec la note qu'elle joue sur la piste choisie : en gamme, bleu sombre, la tonique en bleu ; sinon, touches ordinaires. Les noires sont dessinées à leur place ; en gamme, noires et barrées d'une croix en fil. | La touche tenue passe en rose. |
| **FX** | Deux blocs légendés. DELAY (bleu), « 1/4 · FB 86 » à droite : sur une ligne de temps, la note en barre blanche puis ses échos en barres bleues, espacés selon DLY TIME (de 8 px pour 1/32 à 90 px pour 1/1), chaque écho plus bas selon DLY FB, jusqu'au bord ou sous 2 px. REVERB (blanc), « SIZE 91 · TONE 70 » à droite : la queue en triangle plein, longue selon REV SIZE (30 à 210 px), de couleur orange plus ou moins sombre selon REV TONE (× 0,35 à × 1 du ton clair orange). | À chaque note d'une des pistes, le rose passe d'écho en écho (0,12 s chacun) et balaie la queue de la reverb en 1,2 s. |
| **SONG** | « N ENTRIES » en haut à gauche, « SONG ON » (orange) ou « SONG OFF » (gris) à droite. Dessous, toute la chanson en bande : un segment par entrée, aussi long que ses répétitions, verts alternés, celui du curseur en bleu. Puis 5 rangées autour du curseur : numéro, scène sur un pavé vert (« S3 »), une case par répétition (16 au plus), « x4 » à droite ; la rangée du curseur sur fond sombre avec un trait bleu à gauche. Sans entrée : « EMPTY » en gris. | En lecture de la chanson, le segment et le pavé de l'entrée jouée passent en rose. |

## 4. Architecture

| Fichier | Rôle |
|---|---|
| `app/scene_glo.c` (nouveau) | `glo_band(const q_gview *v, uint32_t ms, int32_t oy)` dessine la page dans la bande : rien n'est construit d'avance, le dessin plat se refait par bande. |
| `app/scene.h` | `q_gview` : la page, les 4 valeurs, ce que la page écrit (gamme, entrées de la chanson), le pas joué, les classes de notes tenues, les touches tenues, l'instant de la dernière note et de l'entrée jouée. |
| `app/ui.c` | Ces cinq pages deviennent animées (`anim` 7, redessinées à 30 images par seconde au plus) ; `ui_frame` remplit le `q_gview` ; leur dessin actuel dans `draw_scene` disparaît. |
| `app/app_ui.c` | Inclut `scene_glo.c`. |

- Aucune nouvelle fonction audio : le pas joué vient de `audio_pos`, les notes de `audio_hits` (toutes pistes pour
  FX), l'entrée jouée de `audio_song_entry`, les touches tenues de `ui.c`.
- Une nouvelle arrivée sur la page ne fait réagir aucune note d'avant.

## 5. Tests

- **PC** (`tests/unit/scene_test.c`) : une bande dessinée par page, avec les valeurs extrêmes, sans sortir de la
  bande ; TEMPO en lecture : du rose dans la mesure, aucun à l'arrêt ; SCALE avec une touche tenue : du rose ; KEYS
  OFF : aucune croix ; FX après une note : du rose ; SONG vide : « EMPTY », aucune rangée.
- **Simulateur** : un script qui parcourt TEMPO, SCALE, KEYS, FX et SONG (`expect anim 7`), tient une touche sur
  SCALE et KEYS, lance la lecture sur TEMPO, et prend une capture par page.
- **Appareil** : le pire cas de `docs/cpu.md` affiché sur TEMPO 45 s : zéro bloc perdu, SCENE FPS relevé.

## 6. Hors périmètre

- KIT et NOTES : lot suivant. CPU reste du texte.
- La progression des répétitions de l'entrée jouée (la maquette remplit ses cases) : le séquenceur ne l'expose pas.
- La mesure réelle du compresseur : les jauges de TEMPO montrent le réglage.
- L'en-tête, le pied de page (dont le nom de mode long qui déborde sur QUANT dans le pied de SCALE), le son.
