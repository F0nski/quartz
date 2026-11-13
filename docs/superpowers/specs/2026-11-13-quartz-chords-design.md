# QUARTZ — source CHORDS : une progression d'accords

Une sixième source de pattern, après STEP, GRIDS, MARBLES, LIFE et LANES (spec globale
`2026-11-10-quartz-groovebox-design.md`, sections 3 et 6). Elle part de la branche `lanes`, dont elle réutilise la case
tenue (`ui.cell`) et la marge du pool (`pool_spare` 4096). Maquettes : idées `docs/screens/sequencers.html`, scène
`docs/screens/chords-source.html`, direction C1 « Heptagone » avec son mode tenu.

## 1. But

Poser vite une harmonie : une progression de 1 à 8 accords, un par mesure, toujours juste dans la gamme du projet,
frappée à un rythme simple et égrenée au besoin ; l'arpège passe par le domaine ARP déjà là.

Critères :
- un pattern CHORDS joue sur une piste synthé, dans la gamme de GLO, à la division du pattern ;
- chaque accord se choisit en un geste (touche tenue + une note de la gamme) et se voit à l'écran pendant ce geste ;
- avec VOICE SMOOTH, deux accords voisins partagent ou rapprochent leurs notes ;
- chaque encodeur change un élément visible de la scène ;
- la scène tient dans le budget de 300 primitives ;
- les autres sources jouent et s'affichent comme avant.

Hors périmètre : des accords de plusieurs mesures (on répète l'accord), des accords hors gamme ou altérés, les 9e et
11e, l'accord imposé aux autres pistes, l'enregistrement d'une progression en live, la saisie d'un accord par ses
notes, un arpège propre à CHORDS, les autres nouveaux séquenceurs (TONNETZ, ORBITS, MIMIC).

## 2. Le jeu

- **La progression** : LEN (1..8) accords, un par mesure de 16 pas à la division du pattern. Le rendu compte les pas
  joués depuis le départ du pattern (`chords_n`, comme `lanes_n`) : l'accord en cours est `(chords_n / 16) mod LEN`, le
  pas dans la mesure `chords_n mod 16`. Le compteur repart à zéro au STOP puis PLAY et au changement de pattern
  (`gen_reset`). Un tour du pattern fait 16 pas (`gen_loop`).
- **Un accord** : un degré 0..6 (I..VII) de la gamme du projet (`gen_scale`) et une taille :

| Taille | Degrés empilés depuis la fondamentale |
|---|---|
| TRIAD | 0, 2, 4 |
| SEVENTH | 0, 2, 4, 6 |
| SUS2 | 0, 1, 4 |
| SUS4 | 0, 3, 4 |

  La fondamentale est NOTE ramenée dans la gamme et montée du degré (`gen_life_note(note, scale, tonic, degré)`), les
  autres notes autant de degrés au-dessus. Majeur, mineur ou diminué découlent de la gamme.
- **Nombre de notes** : au plus le POLY de la piste (`q_sound.poly`, 1..4). On garde dans l'ordre la fondamentale,
  la tierce (ou la seconde / quarte d'un sus), la septième, puis la quinte.
- **VOICE** :
  - ROOT : les notes gardées, en position fondamentale ;
  - SMOOTH (par défaut) : parmi les renversements de ces notes (les k plus graves montées d'une octave, k de 0 au
    nombre de notes − 1) et leurs transpositions de −2 à +1 octave, celui dont la somme, pour chaque note, de l'écart
    à la note la plus proche de l'accord joué juste avant est la plus petite ; à égalité, le plus grave. Seuls
    comptent les candidats dont la note la plus grave reste à une quinte (7 demi-tons) au plus de la fondamentale en
    position fondamentale : sans cette ancre, une progression descendante glisse d'un tour à l'autre jusqu'au grave. Le premier
    accord après un `gen_reset` est en position fondamentale. Les notes restent dans 1..127 (un candidat qui en sort
    est écarté ; s'il n'en reste aucun, ROOT).
- **RATE** (0..4) : l'accord est frappé 1, 2, 4, 8 ou 16 fois par mesure, au pas `i × 16 / rate`. Chaque frappe dure
  jusqu'à la suivante (`len` = 128 / rate huitièmes de pas, soit 16 pas à RATE 1 : l'accord tient toute la mesure).
- **STRUM** (0..7) : les notes d'une frappe partent de la plus grave à la plus aiguë, chacune décalée de STRUM / 24
  de pas sur la précédente. 0 : plaqué. Le séquenceur ne sait aujourd'hui jouer les notes d'un pas qu'ensemble : le
  décalage passe par les bits 5 à 7 de `q_step.flags` (libres), lus par `seq.c` pour les seuls patterns générateurs.
- **Accent** : la première frappe de chaque mesure est accentuée, toutes ses notes ; vélocité 100, `prob` 20, `micro` 0.
- **Arpège** : ARP actif sur la piste arpège l'accord tenu, sans rien de propre à CHORDS.
- **KIT** : un pattern CHORDS sur un KIT ne joue rien.

## 3. Données

- `SRC_CHORDS` après `SRC_LANES` (`project.h`), `SRC_NAME` « CHORDS ».
- Dans `q_pattern`, à côté de `q_lanes` :
  `typedef struct { uint8_t note, len, rate, strum, voice, ch[8]; } q_chords;` (note 1..127, len 1..8, rate 0..4,
  strum 0..7, voice 0 ROOT / 1 SMOOTH ; `ch[i]` : degré dans les bits 0–2 (0..6), taille dans les bits 3–4). 13 octets
  par pattern, 416 pour les 32 : ils tiennent dans la marge du pool (`pool_spare` 4096).
- Valeurs par défaut : I – vi – IV – V en TRIAD (degrés 0, 5, 3, 4), LEN 4, NOTE 48, RATE 2 (4 frappes), STRUM 0,
  SMOOTH.
- Au chargement, les valeurs hors bornes sont ramenées dans leurs bornes (degré > 6 : 6).
- Dans `q_gen` : `uint32_t chords_n` et la dernière frappe jouée (`uint8_t chords_last[4], chords_nlast`), pour SMOOTH
  et pour la scène ; `audio_chords(track, …)` les rend à l'écran.
- `Q_VERSION` passe à 9, sans migration.

## 4. Pages SEQ

| Page | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| CHORDS | DIV | NOTE | RATE (`1/BAR` … `16/BAR`) | SOURCE |
| PROG | LEN | STRUM | VOICE (`ROOT` / `SMOOTH`) | — |

`GEN_PAGES` gagne la ligne de CHORDS. Les touches blanches 1–8, SEQ tenu, gardent le choix du pattern, et PRESET le
change comme ailleurs dans SEQ.

## 5. La saisie

REC, appui court, dans SEQ sur un pattern CHORDS : désarmé ↔ GRILLE, comme LANES.

- En GRILLE, les **blanches 1–8** sont les accords de la progression ; celles au-delà de LEN et les blanches 9–16 ne
  font rien.
- **LED** : les accords dans LEN à mi-niveau, celui qui joue à plein niveau ; un accord tenu : les touches dont la
  note est dans la gamme du projet.
- **Appui court** (relâché avant 0,4 s sans rien éditer) : la taille suivante (TRIAD → SEVENTH → SUS2 → SUS4 →
  TRIAD).
- **Accord tenu + une autre touche** : si sa note est dans la gamme, l'accord prend son degré (l'octave ne compte
  pas) ; une note hors gamme ne fait rien.
- **Accord tenu + K1** : le degré (0..6) ; **+ K2** : la taille ; K3 et K4 ne font rien.
- Tenu plus de 0,4 s puis relâché sans rien éditer : rien ne change. La scène montre l'accord tenu tant qu'il l'est
  (section 6) : c'est son aide, il n'y a pas de couche d'aide à part.
- L'état de la case tenue est celui de LANES (`ui.cell`, `cell_key`, `cell_edited`, `cell_ms`), lâché au changement
  de pattern ou de piste.
- Désarmé, le clavier joue la piste.

## 6. La scène

Direction C1 « Heptagone » de la maquette. Coordonnées de la zone de scène 240 × 166 ; l'en-tête et les encodeurs
restent ceux de `ui.c`. Les textes de l'écran sont en anglais.

- **Heptagone** des sept degrés, centre (120, 78), rayon 46, sommet I en haut : arêtes `#2a2148`, un losange de 6 px
  par sommet (blanc pour les degrés de l'accord montré, bleu sombre sinon), le chiffre romain à 15 px au-delà, rose
  pour les degrés de l'accord.
- **Accord qui joue** : un polygone plein entre ses degrés (au moins un triangle), en facettes roses depuis son
  centre, blanches 140 ms à chaque frappe ; le nom de l'accord (`C`, `Am`, `G7`, `Fsus4`) au centre. L'accord
  précédent : son polygone en fil `#3a2f63`.
- **Progression** : 8 carrés de 9 × 6 px à partir de (8, 8), pas de 12 px : bleus dans LEN, rose pour celui qui joue,
  sombres au-delà.
- **RATE** : autant de crans que de frappes par mesure (au plus 16), de x 170 à 232 à y 14, roses jusqu'à la frappe en
  cours.
- **Clavier** (y 140 à 162) : deux octaves et demie depuis NOTE − 12, touches blanches de 11 px ; les notes de la
  dernière frappe en rose, chacune blanche 140 ms à son départ (STRUM les allume l'une après l'autre) ; C nommé sur
  chaque do.
- **Mode tenu** (un accord 1–8 tenu en GRILLE) : l'heptagone montre l'accord tenu en facettes blanches, sans fantôme ;
  son carré passe en blanc ; un bandeau clair de (124, 3) à (234, 15), `EDIT 3 · IV F` (touche, degré, note, et `7`,
  `SUS2`, `SUS4` selon la taille) remplace les crans RATE ; le clavier montre ses notes en blanc (voicing ROOT, ou
  SMOOTH depuis l'accord qui le précède dans la progression).
- **KIT** : `NO CHORDS ON A KIT` au centre, rien d'autre.

Ce que règle chaque encodeur à l'écran :

| Encodeur | Effet visible |
|---|---|
| DIV | la vitesse des crans RATE et des changements d'accord |
| NOTE | les notes allumées sur le clavier (tout monte ou descend) |
| RATE | le nombre de crans |
| LEN | les carrés allumés de la progression |
| STRUM | les touches du clavier s'allument l'une après l'autre |
| VOICE | les touches allumées (positions différentes) |

**Données de la scène** : la vue lit le pattern (le projet de l'UI), `audio_chords(track, …)` (compteur et dernière
frappe), `audio_pos` (−1 quand le pattern affiché ne joue pas : ni accord en cours ni rose), l'accord tenu et le
moment de la dernière frappe (`audio_hits`, comme LIFE). Les notes d'un accord tenu viennent d'une fonction de `gen.c`
partagée avec le rendu (`gen_chord_notes`). Le code va dans `app/scene_chords.c` (`chords_enter`, `chords_frame`,
`chords_band`), sa vue dans `scene.h` ; `ui.c` la choisit (anim 10) sur les pages CHORDS et PROG.

**Budget** : heptagone 7 lignes, polygone 4 triangles, fantôme 4 lignes, losanges 7 × 2 triangles, carrés 8 × 2,
crans 16, clavier 31 × 2 : environ 130 primitives.

## 7. Tests

- **Unitaires, `tests/unit/gen_test.c`** :
  - défauts (I – vi – IV – V, LEN 4) et taille de `q_chords` (13 octets) ;
  - un accord par mesure, dans l'ordre, sans retour au premier au tour du pattern ; remise à zéro au `gen_reset` ;
  - C majeur, NOTE 48 : I TRIAD → 48 52 55 ; vi → 57 60 64 ; V SEVENTH → 55 59 62 65 ; I SUS2 → 48 50 55 ; I SUS4
    → 48 53 55 ;
  - POLY 2 : fondamentale et tierce ; POLY 3 sur une septième : fondamentale, tierce, septième ;
  - ROOT : position fondamentale ; SMOOTH : I puis IV donne 48 53 57 (C F A) ; premier accord en fondamentale ;
    notes dans 1..127 avec NOTE 127 ;
  - RATE : nombre de frappes par mesure et `len` (128, 64, 32, 16, 8) ; première frappe accentuée, les autres non ;
  - STRUM : les bits 5–7 de `flags` ; KIT : rien.
- **Unitaires, `tests/unit/audio_test.c`** : STRUM 3 sur un pattern CHORDS, les notes d'une frappe partent à 3/24 de
  pas d'écart, de la plus grave à la plus aiguë ; STRUM 0 : ensemble ; un pattern STEP n'est pas touché.
- **Unitaires, `tests/unit/store_test.c`** : bornes au chargement.
- **Unitaires, `tests/unit/scene_test.c`** : budget ; polygone rose sur les degrés de l'accord qui joue ; mode tenu en
  blanc avec bandeau ; à l'arrêt, ni rose ni accord en cours ; clavier allumé sur les notes ; KIT ; chaque réglage du
  tableau change au moins un pixel.
- **Simulateur** : `app/tests/chords.txt` (pages et bornes), `app/tests/chords_entry.txt` (REC GRILLE, appui court,
  tenu + note de la gamme, note hors gamme sans effet, tenu + K1 / K2, au-delà de LEN), `app/tests/chords_scene.txt`
  (lecture, mode tenu, captures).
- **Appareil** : pire cas (POLY 4, SEVENTH, RATE 16, STRUM 7), charge des cœurs et images par seconde, avec les autres
  mesures CPU en fin de chantier.

## 8. Mises à jour de la spec globale

- Section 6, Générateurs : une ligne CHORDS qui renvoie à cette spec.
