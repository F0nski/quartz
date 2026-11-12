# QUARTZ — étape 5f : les pages de son

Premier lot des pages encore sans design (spec globale `2026-11-10-quartz-groovebox-design.md`, section 9). Part de
l'étape 5e. Maquette : `docs/screens/sound.html` (FILTER : version « harmoniques », MIX : version « console »).
Lots suivants, chacun sa spec : pages globales (TEMPO, SCALE, KEYS, FX de GLO, SONG), puis grilles d'édition (KIT, NOTES).

## 1. But

Les pages ADSR, FILTER (ENV), LFO, ARP, INSERT et MIX (FX) montrent ce qu'on règle au lieu de quatre barres : la forme
de l'enveloppe, la réponse du filtre, l'onde du LFO, l'arpège, l'effet sur une onde, la tranche de mixage. Critères :
chaque encodeur pilote un élément visible dans sa couleur ; chaque note de la piste fait réagir la page en rose ; une
page reste lisible d'abord (pas de point de fuite) ; ≤ 300 primitives ; zéro bloc perdu.

## 2. Principe commun

- **Le ruban** : une polyligne extrudée en projection oblique, sans fuite : profondeur `(DX, DY) = (12, −9)` px par
  rangée. Pour chaque segment : la face avant, de la courbe jusqu'à sa base, dans le ton d'ombre ; le dessus dans le ton
  clair si le segment monte, le ton de base s'il est plat, le ton de base assombri (× 0,75) s'il descend ; l'arête
  arrière du dessus en trait du ton clair.
- **Le bloc** : une boîte oblique (avant en ton de base, dessus en ton clair, côté droit en ton d'ombre).
- **Le sol** : grille en fil de fer `#1d1838` (8 colonnes de 25 px, 3 rangées en profondeur) sous la zone de tracé
  x 16–216, y 62–172 (coordonnées de l'écran ; la zone de scène commence à y 30).
- **Couleurs** : les tons des encodeurs (bleu `#8fb4ff #3a6dff #1c3590`, vert `#8ff5c8 #22d58e #0f6d48`, blanc
  `#ffffff #c9c3e4 #6f6890`, orange `#ffc196 #ff7a2e #a3410c`) ; rose `#ffb8dc #ff2f8e #8a1050`, réservé aux notes ; fil
  éteint `#2e2850` ; gris `#6f6890 #4a4466 #2c2844`.
- **En haut** : le nom du modèle en blanc à gauche (y 42), comme aujourd'hui ; à droite, une précision par page en
  petit (type de filtre, destination, nombre de pas).
- L'en-tête et le pied de page ne changent pas.

## 3. Les pages

| Page | Ce qu'on voit | Une note |
|---|---|---|
| **ADSR** | Le ruban de l'enveloppe sur 2,3 s : attaque, décroissance, maintien (0,45 s, fixe), relâchement ; un segment par encodeur dans sa couleur. Un moteur à enveloppe propre ajoute « FILTER ONLY » en ambre, en haut à droite. | Le segment que traverse l'enveloppe de la note passe au rose. |
| **FILTER** | Les 16 harmoniques d'une dent de scie en colonnes (hauteur 92 / (1 + 0,35 i)), deux rangées derrière : en fil éteint toute la colonne, en blanc assombri (× 0,75) la part qui passe le filtre. Devant, le ruban vert fin (profondeur × 0,6) de la réponse : forme selon TYPE (LP, BP, HP), coude au CUTOFF, pic selon RES. Un pilier vert plein au CUTOFF, un cristal blanc sur le pic (taille selon RES, absent sous 8 %), un pilier orange en fil à la cible de ENV AMT (CUTOFF + ENV AMT), relié au pilier vert par un trait orange. En haut à droite : LOW-PASS, BAND-PASS ou HIGH-PASS en bleu. | Le coude part de la cible et revient en 0,9 s (carré) ; un trait rose vertical le suit ; pendant 0,4 s, la part qui passe est rose. |
| **LFO** | Le ruban bleu de l'onde : SHAPE sa forme (SIN, TRI, SAW, SQR, S&H, les marches verticales), RATE le nombre de cycles affichés (1, 2, 3, 4, 6), DEPTH l'amplitude autour de l'axe (inversée si négative). En haut à droite : `→ DEST` en orange. Un losange blanc suit la phase réelle du LFO de la piste ; aucune voix ne sonne : il reste immobile au début de l'onde. | Le losange passe au rose 0,15 s. |
| **ARP** | Les notes de l'arpège en blocs bleus, de gauche à droite dans l'ordre du MODE sur OCT octaves (12 au plus ; au-delà, la page de 12 qui contient le pas joué) : hauteur = note, largeur = GATE ; une octave sur deux d'un ton plus sombre (× 0,8). OFF : l'accord en une seule pile. Notes : celles que tient l'arp ; sans note tenue, l'accord de la tonique du projet (1, 3, 5 de la gamme) en fil éteint, pour montrer la forme. En haut à droite : `N STEPS`. Sur un KIT : « NO ARP ON KIT » en gris, sans blocs. | Le bloc joué passe au rose, son nom (`E4`) au-dessus. |
| **INSERT** | Deux cycles de sinus en fil gris (l'entrée), et la sortie en ruban bleu autour de l'axe, déformée par le type : OVERDRIVE écrête (DRIVE), adoucie par TONE ; WAVEFOLD replie (FOLD, BIAS), adoucie par TONE ; BITCRUSH en marches (BITS : niveaux, RATE : largeur des paliers) ; CHORUS double l'onde, décalée selon RATE et DEPTH ; PHASER la mêle à une copie déphasée (RATE, DEPTH, FEEDBK) ; RING MOD la module (FREQ) ; COMB lui ajoute ses échos (NOTE : écart, FEEDBK : amortissement des échos, DAMP : adoucie). MIX fond l'entrée dans la sortie. INSERT OFF : le ruban gris de l'onde intacte. | Une vague rose (8 segments) court le long du ruban en 1 s. |
| **MIX** | Une tranche de console : boutons hexagonaux en relief DELAY (bleu, x 44) et REVERB (vert, x 108) en haut, PAN (blanc, bipolaire, x 76) en dessous, chacun avec 11 graduations allumées jusqu'à la valeur (depuis le centre pour PAN) et un index sombre ; à droite, un rail et le capuchon orange du fader LEVEL ; tout à droite, un vumètre L/R de 14 pavés par côté. | Le vumètre monte en rose à LEVEL, réparti selon PAN, et retombe en 0,9 s. |

## 4. Architecture

| Fichier | Rôle |
|---|---|
| `app/scene_sound.c` (nouveau) | `sound_frame(const q_pview *v, uint32_t ms)` construit la page en primitives 2D de `p3d` (`p3d_tri`, `p3d_line`, la profondeur pour l'ordre) ; `sound_band(int32_t oy)` dessine la bande. |
| `app/scene.h` | `q_pview` : la page, le modèle, les 4 valeurs, le type d'insert, l'enveloppe propre ou non, le compteur de notes, la phase du LFO, les notes tenues par l'arp et la dernière note jouée. |
| `app/ui.c` | Ces six pages deviennent animées (`anim` 6) ; leur dessin actuel dans `draw_scene` disparaît, sauf le texte du KIT sur ARP. |
| `app/audio.c`, `app/audio.h` | `audio_lfo_phase(track)` (phase 0..1 de la première voix de la piste qui sonne, −1 sans voix) ; `audio_arp(track, held, &sounding)` (copie des notes tenues, dans leur ordre d'arrivée, et la note que l'arp fait sonner, −1 aucune). |
| `app/app_ui.c` | Inclut `scene_sound.c`. |

- Comme les autres scènes : une nouvelle arrivée sur la page ne fait réagir aucune note d'avant ; la page est
  redessinée à 30 images par seconde au plus.
- Budget : FILTER est la plus chargée (16 colonnes et leurs fils, le ruban de 40 segments, les piliers) ; ≤ 300
  primitives sur chaque page.

## 5. Tests

- **PC** (`tests/unit/`) : ≤ 300 primitives sur chaque page, avec les valeurs extrêmes ; une note met en rose le
  segment attendu de l'ADSR selon le temps écoulé ; ARP met en rose le bloc de la dernière note ; ARP sans note
  tenue dessine l'accord de la tonique en fil ; INSERT OFF ne contient aucun ton bleu ; aucune note d'avant l'arrivée
  ne fait réagir la page.
- **Simulateur** : un script qui parcourt ENV, LFO, ARP et FX (les deux pages de chacun), joue une note sur chacune,
  change le type d'insert, et prend une capture par page.
- **Appareil** : le pire cas de `docs/cpu.md` affiché sur FILTER 45 s : zéro bloc perdu, SCENE FPS relevé.

## 6. Hors périmètre

- Les pages globales (TEMPO, SCALE, KEYS, FX de GLO, SONG ; CPU reste du texte), KIT et NOTES : lots suivants.
- L'en-tête, le pied de page, le son.
