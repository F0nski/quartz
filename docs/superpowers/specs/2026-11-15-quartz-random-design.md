# QUARTZ — RANDOM : tirer des valeurs au hasard

Pour explorer vite, un geste disponible sur toutes les pages tire au hasard les valeurs des encodeurs : la page
affichée, ou toutes les pages du domaine affiché. En SEQ, le second tire aussi le contenu de la source (un pattern
STEP neuf, les accords de CHORDS…). MODEL a son propre geste, pour le changer ou le garder.

## 1. But

Critères :
- REC tenu + noire 6, 7 ou 8 tire au hasard sur n'importe quelle page, sans changer de page ni de domaine ;
- aucune fonction existante ne se perd : les noires 6–8 sous REC tenu ne font rien aujourd'hui (la touche ne joue
  même pas), REC tenu + noires 1–2, l'appui court de REC et REC + PLAY restent tels quels ;
- un tirage ne rend jamais la piste muette ni ne change sa nature : SOURCE, MODEL (hors noire 8), LEVEL, POLY et TEMPO
  ne sont jamais tirés.

Hors périmètre : une annulation ; une variation autour de la valeur actuelle (le tirage est toujours uniforme entre
les bornes) ; ratchets, accents, slides et accords dans le pattern STEP tiré ; les p-locks d'un pas tenu ; HOME.

## 2. Le geste

REC tenu, une noire du deuxième groupe de trois (`BLACK[5..7]`, fa#, sol#, la# de la deuxième octave) :

| Touche | Nom (carte de REC) | Message d'en-tête | Ce qui est tiré |
|---|---|---|---|
| noire 6 | `RND PAGE` | `RANDOM PAGE` | les encodeurs de la page affichée ; sur CHORD, STEP (TONNETZ) et MOON, l'élément sous le curseur de PRESET ; sur STEPS et XY d'un pattern STEP, les 16 pas de la page de grille affichée (OCT-/OCT+), rien d'autre |
| noire 7 | `RND ALL` | `RANDOM ALL` | les encodeurs de toutes les pages du domaine affiché ; en SEQ, en plus, tout le contenu de la source (section 5) |
| noire 8 | `RND MODEL` | `RANDOM MODEL` | un MODEL, sur EDIT seulement (synthé : SOUND ; KIT : KIT) |

Le message reste 1,5 s (`MSG_MS`), comme les autres. Pour changer de moteur et de réglages : noire 8 puis noire 7.

Le dé ne fait rien, et ses touches sont éteintes sur la carte :
- en LIVE : REC tenu y efface les notes jouées, ce rôle reste entier (la garde LIVE de `key_down` passe avant) ;
- tant qu'un pas est tenu, qu'une sélection est en cours (SEL) ou qu'une cellule LANES, un accord ou un pas TONNETZ est
  tenu : les encodeurs y règlent des p-locks ou la cellule, pas la page ;
- sur HOME (noires 6 et 7) et hors EDIT (noire 8).

En armement (`R_ARM`), REC tenu ne fait rien aujourd'hui : le dé y marche.

Un tirage marque le projet modifié (`ui.dirty`), comme un tour d'encodeur. Il porte sur la piste en cours ; sur un KIT,
la page PAD tire le pad sélectionné (`ui.pad`).

## 3. Ce qui n'est jamais tiré

| Page | Encodeur | Pourquoi |
|---|---|---|
| STEPS, ATLAS, FLEET, COLONY (deux pages), LOOM, CADENCE, LATTICE, MOONS, IMPROV | SOURCE | changerait la source, donc les pages qu'on tire |
| SOUND, KIT | MODEL | a son geste, noire 8 |
| SOUND | POLY | le nombre de voix : la charge CPU et la RAM |
| MIX | LEVEL | piste muette ou saturée |
| KIT | LEVEL | idem |
| TEMPO | TEMPO | le projet entier change de vitesse |
| STEPS | LEN, PATH (et la page XY) | LEN : la noire 7 seule, parmi 16/32/48/64 ; PATH : XY figerait le pattern |
| TRACKS, SONG (HOME) | tous | le mixage et l'arrangement, pas du son |
| CPU | — | rien à tirer |

Restent tirables, entre autres : SWING, COMP, DUCK (TEMPO), toute la page SCALE, KEYS, le FX global.

Les encodeurs sans libellé ou cachés (`knob_hidden` : les K1–K3 sans rôle d'INSERT, les voix et notes d'un KIT…) sont
sautés.

## 4. Le tirage d'un encodeur

Valeur uniforme dans `[PAGE[pid].min[k], PAGE[pid].max[k]]`. Un encodeur qui choisit (`knob_zone` : presets HEX,
accords, arpèges ARCADE, banques ORATOR) prend le milieu de la zone de sa valeur, comme un cran de `knob_step`
(ORATOR : la partie continue 0–42 reste continue). La valeur s'écrit par `knob_set`, qui fait déjà le reste
(`audio_track_model` pour MODEL, le pas du curseur pour SN1–SN3, l'accord, le pas ou la lune sous le curseur).

Le hasard : un xorshift local à `ui.c`, mêlé à `plat_ms()` à chaque appui (`seq_rand` et `gen_rand` sont `static` dans
leurs fichiers).

## 5. Le contenu des sources (noire 7 en SEQ)

Les encodeurs des pages d'abord, puis le contenu :

- **STEP** : DIV tiré, LEN parmi 16, 32, 48 et 64 (des pages de grille pleines), puis les 64 pas vidés avec leurs p-locks
  (`step_clear`, comme REC + noire 1). PATH et la page XY ne sont pas tirés : XY avec X LEN et Y LEN à 1 figerait le
  pattern sur un pas.
  Une densité est tirée entre 25 % et 75 % ; chaque pas de 1 à LEN (noire 6 : de la page de grille, dans LEN) s'allume avec cette probabilité, aux valeurs d'un
  pas posé à la main (`step_default` : durée d'un demi-pas, probabilité 100 %, pas de condition, ratchet, slide ni
  accent), avec :
  - une note : synthé, une note de la gamme du projet (`gen_scale`) entre 48 et 72 ; KIT, `KIT_NOTE` + un pad 0–15 ;
  - une vélocité entre 60 et 127.
- **CHORDS** : les 8 accords, degré et taille (le curseur `chcur` passe sur chacun, puis revient).
- **TONNETZ** : les 8 pas, P, L, R ou stay (`tzcur`, idem).
- **MOONS** : chaque lune, PERIOD, PHASE, NOTE ou PAD (`moon`, idem), sur le nombre de lunes tiré.
- **LANES** : chaque cellule des cinq pistes, entre 0 et `LANE_MAX[lane]` (`lanes_set`) ; un KIT n'a pas de piste OCT.

Les autres sources (ATLAS, FLEET, COLONY, LOOM, MIMIC) n'ont que leurs encodeurs.

## 6. Données, CPU

Aucune donnée nouvelle dans `q_project` : pas de changement de `Q_VERSION`. Le dé est du code d'interface lancé sur
un appui, dans `ui.c` (lié `APP_LATE`) : rien dans le rendu audio.

## 7. Écran

- La carte de REC tenu (`layer_keys(B_REC)`, `draw_layer`) allume et nomme les noires 6–8 là où elles agissent, à côté
  des noires 1–2.
- Les messages `RANDOM PAGE`, `RANDOM ALL`, `RANDOM MODEL` dans l'en-tête.
- Les nouveaux mots dans `app/words.tsv`.
- La scène réagit d'elle-même aux valeurs changées.

## 8. Tests

Script du simulateur `app/tests/random.txt`, par `expect` (des propriétés, pas des valeurs exactes) :
- noire 6 sur ENGINE : HARM–DECAY dans 0–127, au moins une a changé ; MODEL inchangé ;
- noire 7 sur EDIT : SOUND tiré sauf MODEL et POLY ;
- noire 8 : MODEL change ; hors EDIT, rien ne bouge ;
- noire 7 sur MIX/INSERT : LEVEL inchangé ; sur TEMPO : TEMPO inchangé ; sur HOME : rien ne bouge ;
- noire 7 sur STEP : SOURCE inchangée, des pas allumés, leurs notes dans la gamme, plus de p-locks ;
- noire 7 sur CHORDS, TONNETZ, MOONS, LANES : le contenu reste dans ses bornes, le curseur revient ;
- en LIVE, REC tenu + noire 6 efface la note au lieu de tirer ;
- REC relâché après le dé n'arme pas REC.

Puis `tests/run_tests.sh` en entier.

## 9. Manuel

Une sous-section **Random** (section 8, à côté de REC held), la ligne de REC dans le tableau des boutons, et la capture
de la carte de REC refaite.
