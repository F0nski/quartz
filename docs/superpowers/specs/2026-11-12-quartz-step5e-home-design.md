# QUARTZ — étape 5e : la page TRACKS de HOME

Dernière partie de l'étape 5 (spec globale `2026-11-10-quartz-groovebox-design.md`, section 9.3, « Page TRACKS de
HOME »). Part de `step5d2-scenes` ; réutilise la caméra de 5d1. Maquette : `docs/screens/home.html`, direction 1
(« Les emblèmes »).

## 1. But

La page TRACKS montre les 4 pistes d'un coup d'œil, animées : ce qui joue, ce qui est coupé, le pattern et sa position.
Mêmes données qu'aujourd'hui (`q_trow` : modèle, pattern, pattern en attente, muet, position, longueur). Critères :
chaque piste réagit à ses notes ; une piste coupée est éteinte ; la piste sélectionnée se voit ; ≤ 300 primitives ;
zéro bloc perdu.

## 2. La scène

- **Caméra** yaw 0, pitch 0,2, dist 5, h 2,1, f 215, cy 108 ; lumière `(−0,5, 0,75, −0,45)` ; brouillard `#0b0628`,
  far 16.
- **Fond (dans la bande)** : ciel en bandes `#05031a`, `#0b0628`, `#140a3a`, `#1c0d48` jusqu'à l'horizon ; un soleil
  taillé en tranches (orange `#ffb347` vers magenta `#c2207a`, les écarts s'élargissant vers le bas) ; deux crêtes
  `#0d0730` bordées `#5a2fc0` ; le sol `#07041c` et sa grille `#2a1d80`.
- **Socles** : 4 prismes hexagonaux (rayon 0,4, haut 0,62), en léger arc (`x = −2,1 + 1,4 i`, `z = 0,16 x²`), métal
  `#2b3070` filaire `#3a2c90` ; la piste sélectionnée filaire `#2c9fd8`, avec un hexagone cyan `#5fe0ff` au sol.
- **Pas du pattern** sur le bord du dessus : un segment par pas (jusqu'à 32), les pas joués `#3d7bff`, le pas en cours
  rose, les autres `#2a2060` ; tracés dans la bande.
- **Emblèmes** (moins de 20 faces chacun), qui tournent au-dessus de leur socle et flottent doucement :

  | Famille (modèles) | Emblème |
  |---|---|
  | FM (2, 3, 4, 10) | planète orange et son anneau |
  | KIT et percussions (21–24) | porte octogonale bleue du tunnel, sa lumière cyan au fond |
  | MODAL (20) | plaque taillée bleue et verte |
  | SPEECH (15) | tête verte à visière sombre, pupilles cyan |
  | VA (0, 1, 8, 9) | pyramide de crête bleue |
  | WAVETABLE, WAVE TERRAIN (5, 13) | planche de hoverboard orange, dessous cyan |
  | ADDITIVE, GRAIN FORM (11, 12) | fleur de cristal, cœur orange |
  | CHIPTUNE (7) | robot cube cyan, antenne |
  | CHORDS, STRING MACH (6, 14) | tête d'androïde blanche, yeux cyan, antenne |
  | STRING (19) | balancier orange sur une corde cyan |
  | NOISE, PARTICLE, SWARM (16–18) | boule à facettes |

- **Une note de la piste** : l'emblème sautille (0,14, 0,2 s), son fil passe au rose un instant, un anneau rose court
  sur le dessus du socle (0,3 s).
- **Piste coupée** : socle et emblème sombres `#1a1640 #15123a #100d2c`, fil `#3a2c90`, emblème immobile et posé plus
  bas ; ni pas ni réaction.
- **Bas de la zone** (y 140 à 166) : une colonne par piste (60 px) : le numéro sur une pastille (orange pour la piste
  sélectionnée, sinon la couleur de la famille, sombre si coupée), le pattern (`P3`, ou `P3›4` dont le `›4` clignote
  quand un autre est en attente ; `MUTE` si coupée), le nom du modèle en petit, coupé à la largeur de la colonne.

## 3. Architecture

| Fichier | Rôle |
|---|---|
| `app/scene_home.c` (nouveau) | `home_frame(const q_hview *v, uint32_t ms)`, `home_band(int32_t oy)`. |
| `app/scene.h` | `q_hview` : par piste modèle, pattern affiché, pattern en attente, muet, position, longueur, compteur de notes ; la piste sélectionnée. |
| `app/scene_dance.c` | `dn_ball` prend ses tons et son fil (la boule et la planète partagent l'icosaèdre). |
| `app/ui.c` | La page TRACKS devient animée (`anim` 5) ; la clé de scène élargie (l'`anim` sur 3 bits) ; les 4 compteurs `audio_hits`. |
| `app/app_ui.c` | Inclut `scene_home.c`. |

- Comme les autres scènes : fonds projetés dans `home_frame`, dessinés dans la bande ; tampons dans `sb` ; une nouvelle
  arrivée sur la page ne fait réagir aucune note d'avant.

## 4. Tests

- **PC** : ≤ 300 primitives avec 4 pistes, toutes familles, des notes ; une note de la piste 2 fait réagir la piste 2
  seule ; une piste coupée ne réagit pas ; la pastille de la piste sélectionnée est orange ; le pas en cours rose sur
  le bon socle.
- **Simulateur** : `home.txt` étendu : TRACKS animée (`anim 5`), une note, un mute, captures.
- **Appareil** : le pire cas affiché sur TRACKS 45 s : zéro bloc perdu, SCENE FPS ≥ 15.

## 5. Hors périmètre

- La page SONG, le pied de page.
