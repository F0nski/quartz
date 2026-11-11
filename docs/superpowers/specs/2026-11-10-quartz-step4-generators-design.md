# QUARTZ — étape 4 : générateurs et KIT

Précise l'étape 4 de la spec globale (`2026-11-10-quartz-groovebox-design.md`, sections 3, 6, 7, 12). Part de la
branche `step2-sequencer` ; l'étape 3 (son) viendra après et ne change rien ici.

## 1. But

Des pistes génératives : une piste joue un pattern STEP, GRIDS ou MARBLES, et le moteur KIT donne une batterie de
16 pads. Critères : les pistes génératives se comportent comme une piste STEP vis-à-vis du swing, des mutes, des
changements de pattern et des scènes ; un projet qui les utilise se recharge à l'identique ; le pire cas de
l'étape (KIT + piste GRIDS + 3 voix lourdes) tourne sans bloc perdu.

Licence : QUARTZ est sous GPL-3.0, pour porter Grids (code AVR de Mutable, GPL-3.0) tel quel, carte comprise.
Les fichiers Mutable gardent leur en-tête d'origine (GPL-3.0 pour Grids, MIT pour Marbles et Plaits).

## 2. KIT

- Modèle 24, après les 24 de Plaits ; le nom affiché est KIT.
- **16 pads**, un par touche blanche. Un pad : un moteur (BD, SNARE ou HI-HAT, modèles 21–23 de Plaits) et TUNE
  (−24..24), HARM, TIMB, DECAY, LEVEL (0..127). DECAY pilote le MORPH de Plaits, qui règle la décroissance des
  percussions ; HARM et TIMB vont à HARMONICS et TIMBRE.
- Kit par défaut : 1 BD · 2 SN · 3 CH · 4 OH · 5 RIM · 6 CLAP · 7 TOM grave · 8 TOM médium · 9 TOM aigu · 10 BD2 ·
  11 SN2 · 12 CH2 · 13 CONGA (BD aiguë, HARM bas) · 14 COWBELL (HH aigu, DECAY court) · 15 SHAKER (HH, TIMB bas) ·
  16 RIDE (HH, DECAY long). Les valeurs exactes sont réglées à l'oreille pendant l'implémentation.
- **Voix** : un pad joue sur une voix qui tient son moteur ; une piste KIT a au plus une voix par moteur, donc 3.
  Un pad coupe le pad du même moteur qui sonne (CH coupe OH, un TOM coupe la BD).
- La hauteur jouée est 48 + TUNE du pad (TUNE de la piste en plus) ; la vélocité est celle de la touche ou du pas.
- **Clavier** : hors SEQ, une blanche joue son pad ; les noires ne jouent rien sur un KIT.
- **Pages EDIT** (quand le modèle est KIT) :
  - PAD : TUNE, HARM, TIMB, DECAY du dernier pad joué ou tenu ;
  - KIT : MODEL, moteur du dernier pad (BD/SN/HH), LEVEL du dernier pad.
  L'en-tête affiche le numéro du pad.
- **Pad tenu**, dans le domaine EDIT : K1–K4 règlent TUNE, HARM, TIMB, DECAY de ce pad, PRESET change son moteur.
  Le pad sonne à l'appui. Ailleurs qu'EDIT, une touche ne fait que jouer.
- **Pas** : la note d'un pas KIT est `36 + pad` (pad 0..15), affichée comme le numéro du pad (1..16). Pas maintenu + blanche n : ajoute ou retire le pad n
  (4 pads au plus par pas). Un appui simple sur un pas vide l'active avec le dernier pad joué (pad 1 par défaut).
- Les p-locks d'un pas KIT sont hors périmètre : la page des p-locks est étendue à l'étape 3.

## 3. Source par pattern

- Chaque pattern a sa source : STEP (défaut), GRIDS ou MARBLES, et les réglages de son générateur. Une scène peut
  donc faire passer une piste de STEP à GRIDS.
- Changer de source garde les pas STEP en mémoire.
- Une piste générative suit la division du pattern (DIV). Un changement de pattern a lieu au bout de la boucle :
  32 pas pour GRIDS, LEN pas pour MARBLES. La durée d'une entrée de chanson compte cette longueur.
- Chaque pas d'une piste générative produit un pas ordinaire (notes, vélocité, accent, micro-timing, durée d'un
  demi-pas), planifié comme un pas STEP : swing, mutes, scènes et dates exactes ne changent pas.
- REC (overdub et saisie pas à pas) n'écrit que dans un pattern STEP ; sur une piste générative, les touches jouent
  sans enregistrer.

### 3.1 Pages SEQ

| Source | Page 1 | Page 2 | Page 3 | Page 4 |
|---|---|---|---|---|
| STEP | pas, LEN, DIV, PATH, SOURCE | XY si PATH = XY | — | — |
| GRIDS | DIV, VOICE\*, NOTE\*, SOURCE | X, Y, CHAOS | DENS 1, DENS 2, DENS 3 | — |
| MARBLES | DIV, T OUT\*, LEN, SOURCE | T : MODE, BIAS, JITTER, DÉJÀ VU | X\* : SPREAD, X BIAS, STEPS | NOTES\* : NOTE, RANGE |

\* masqués sur une piste KIT (les pages X et NOTES aussi : un KIT n'utilise pas la section X). SOURCE est toujours
K4 de la page 1 : on change de source par le même encodeur.
Plages : X, Y, CHAOS, DENS, BIAS, JITTER, SPREAD, X BIAS, STEPS, DÉJÀ VU 0..127 ; VOICE et T OUT 1..3 ;
NOTE 0..127 ; MODE 0..6 ; LEN 1..16 ; RANGE 1..4 octaves.

MARBLES sur une piste synthé est le seul cas à 4 pages ; la limite de la spec globale (section 8) passe à quatre
pages au plus par domaine.

Sur une piste générative, la page 1 n'affiche pas de pas ; le pied montre les 4 réglages. Dans SEQ, les blanches
ne servent plus à éditer des pas ; elles jouent la piste.

## 4. GRIDS

- Port en C du `PatternGenerator` de Grids (mode carte), carte `node_0`..`node_24` comprise : rythme de 32 pas à
  3 voix, interpolé dans la carte 5×5 selon X et Y, seuils de densité par voix, perturbation tirée au début de
  chaque boucle selon CHAOS, accents.
- Sur un KIT, les voix 1, 2, 3 jouent les pads 1, 2, 3. Sur une piste synthé, seule la voix VOICE joue, à la note
  NOTE.
- Vélocité : 95, et 127 pour une voix accentuée (l'accent de chaque voix est le sien).
- Hors périmètre : les modes euclidien et séquenceur de bandes de Grids, son horloge propre (swing de Grids).

## 5. MARBLES

- Port en C, au pas, des parties de Marbles (MIT) qui décident des notes : les modèles T de `t_generator.cc`, le
  tirage X de `x_generator.cc` (SPREAD, BIAS, STEPS) et la `RandomSequence` du DÉJÀ VU.
- Section T, évaluée une fois par pas : t2 part à chaque pas (l'horloge), t1 et t3 selon MODE (les 7 modèles de
  Marbles : COMPLEMENTARY BERNOULLI, CLUSTERS, DRUMS, INDEPENDENT BERNOULLI, DIVIDER, THREE STATES, MARKOV) et BIAS.
  JITTER tire un micro-timing jusqu'à ±11/24 de pas.
- Section X : une valeur 0..1 par déclenchement, répartie selon SPREAD et X BIAS ; la note est NOTE + valeur ×
  RANGE × 12 demi-tons, quantifiée dans les **notes permises**.
- Notes permises : 12 notes de l'octave, propres au pattern. Par défaut, celles de la gamme du projet (et elles la
  suivent tant qu'aucune n'a été changée). Sur la page NOTES, une touche (blanche ou noire) active ou coupe sa note,
  quelle que soit l'octave ; la dernière note permise ne se coupe pas. La page montre les 12 notes et celles
  permises.
- STEPS bas : toutes les notes permises ; en montant, il ne garde que les plus fortes, par intervalle depuis la
  tonique du projet dans l'ordre 0, 7, 4, 3, 10, 11, 2, 5, 9, 8, 1, 6, jusqu'à une seule.
- DÉJÀ VU, commun à T et X, comme le potentiomètre de Marbles : 0 hasard neuf à chaque pas, au milieu boucle figée
  de LEN pas, au-delà permutations de la boucle.
- Sur un KIT, t1, t2, t3 jouent les pads 1, 2, 3. Sur une piste synthé, seule la sortie T OUT joue, à la note de X.
- Vélocité 100. Hors périmètre : Marbles polyphonique (t1–t3 sur X1–X3), les rampes et le mode d'horloge externe
  de Marbles.

## 6. Code

| Fichier | Changement |
|---|---|
| `project.h` | `q_pattern.src` et les réglages des deux générateurs (~21 octets, gardés quand on change de source, dont le masque des notes permises de MARBLES) ; `q_pad` et `q_project.kit[4][16]` ; modèle `M_KIT 24` ; `Q_VERSION 3` |
| `gen.c`, `gen.h` (nouveaux, module `gen` de la spec) | état par piste ; `gen_step()` remplit un `q_step` pour le rang k du pattern ; carte Grids dans `gen_grids_map.h` |
| `seq.c` | au lieu de lire `pt->steps`, un pattern génératif demande son pas à `gen_step` puis passe par `plan_notes` ; longueur de boucle selon la source |
| `audio.c` | KIT : la note d'une voix est le pad, ses réglages viennent du pad ; note-on avec le moteur du pad et poly 1 ; `wanted` et `spare_slot` comptent les moteurs des pads |
| `ui.c` | pages SEQ par source, pages PAD et KIT, pad tenu + K / PRESET, pas KIT, REC limité à STEP |
| `store.c` | bornes des nouveaux champs ; une sauvegarde v2 redonne le projet par défaut (pas de migration) |

Le projet passe de ~31,4 à ~32,5 Ko et tient dans les 9 chunks (34,5 Ko).

## 7. Tests

- Unitaires (PC) : Grids à CHAOS 0 suit la carte (cases de coin et interpolation) ; DENS 0 ne joue rien, DENS 127
  joue tout pas non nul de la carte ; Marbles à DÉJÀ VU au milieu rejoue la même boucle de LEN pas ; ses notes
  restent dans les notes permises et dans RANGE ; dates exactes des pas d'une piste générative avec swing ; changement STEP ↔ GRIDS au bout
  de la boucle ; KIT : CH coupe OH, BD et SN sonnent ensemble, 3 voix au plus ; sauvegarde et rechargement à
  l'identique (src, réglages, pads).
- Simulateur : pages SEQ selon la source, notes permises sur la page NOTES, pages PAD/KIT, édition pad tenu, pas KIT, REC ignoré sur GRIDS.
- Appareil : pire cas KIT + GRIDS + 3 voix lourdes sans bloc perdu ; CPU relevée dans `docs/cpu.md`.

## 8. Hors périmètre

Scène 3D GRIDS (étape 5) ; p-locks sur les réglages des générateurs et sur un KIT ; enregistrement sur une piste
générative ; modes euclidien et bandes de Grids ; Marbles polyphonique ; accent sur ENV AMT (étape 3) ; une 4e voix
de KIT.
