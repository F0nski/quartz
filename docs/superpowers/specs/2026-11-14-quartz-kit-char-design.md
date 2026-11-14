# QUARTZ — KIT : CHAR, le dosage analogique / synthétique par pad

Un deuxième caractère pour les pads du KIT, sans nouveau DSP. Spec globale
`2026-11-10-quartz-groovebox-design.md`, section 3 (KIT). Part de la branche `kit-tune` (kit par défaut retouché).

## 1. But

Chaque moteur de batterie de Plaits calcule deux sons à chaque bloc : la sortie principale (`out`) et la sortie
auxiliaire (`aux`), que QUARTZ calcule, post-traite (`qvoice.cc`) puis jette (`pq.cc` ne lit que `.out`) :

| Moteur | `out` (aujourd'hui) | `aux` (inutilisé) |
| --- | --- | --- |
| BD | grosse caisse analogique, type 808 | grosse caisse synthétique, plus sèche, type 909 |
| SN | caisse claire analogique | caisse claire synthétique |
| HH | bruit de carrés métalliques, type 808 | bruit en modulation en anneau, plus dur |

CHAR, un réglage par pad de 0 à 127, mélange les deux : 0 le son d'aujourd'hui, 127 le son synthétique, entre les
deux un hybride.

Critères :
- un pad à CHAR 0 sonne exactement comme aujourd'hui ; à 127, il ne fait entendre que `aux` ;
- CHAR se règle sur la page KIT (K4), pour le pad réglé, et la grille le montre pour chaque pad ;
- un nouveau projet fait entendre les deux caractères : pads 10 à 12 synthétiques ;
- aucune RAM de voix ni aucun moteur en plus ; les modèles synthé et les modèles seuls de batterie ne changent pas.

Hors périmètre : CHAR sur les modèles seuls BASS DRUM, SNARE et HI-HAT (leurs 8 boutons sont pris) ; un p-lock de
CHAR (les locks ne s'appliquent pas aux pads) ; ne plus post-traiter `aux` dans les voix qui ne l'utilisent pas (un
gain CPU possible, à part) ; LEVEL qui ne change pas visiblement la grille (écart existant, signalé seulement) ; un
choix de pads pour ATLAS et FLEET (ils jouent les pads 1, 2 et 3 : pour les entendre synthétiques, on monte le CHAR de
ces pads, ou on prend une deuxième piste KIT).

## 2. Le son

- `pq` garde par voix un `chr` (0..1), réglé par un nouvel appel `pq_set_char(int v, float chr)`, avec les mêmes
  règles de fil d'exécution que `pq_set_patch` (`pq.h`). `pq_set_engine` le remet à 0.
- Le rendu de la voix (`pq.cc`) lit `x = out·(1 − chr) + aux·chr` à la place de `out`, dans les deux branches (filtre
  ouvert et filtre calculé). Mélange linéaire : les deux sons partent du même déclenchement, ils sont corrélés.
- `audio.c` `set_patch` : une voix de KIT reçoit le CHAR de son pad (`chr / 127`), toute autre voix 0. Comme HARM,
  TIMB et DECAY, il passe par `set_patch` : à la note du pad et, tant que la note est tenue, à chaque bloc.
- Si les deux sons n'ont pas le même niveau, LEVEL du pad compense ; la loi de mélange se juge à l'oreille sur
  l'appareil.
- Coût : une multiplication-addition par échantillon et par voix. Le code audio bouge dans la flash : mesure CPU en fin
  de cycle (`docs/cpu.md`), pire cas sans LOST BLOCKS.

## 3. Données

- `q_pad` : `typedef struct { uint8_t engine; int8_t tune; uint8_t harm, timb, decay, level, chr; } q_pad;`
  (`chr` 0..127). `Q_VERSION` 12 → 13 : les anciens projets sont écartés.
- Pool : 4 pistes × 16 pads × 1 o, à vérifier au build sous la marge de 4096 o (`fm1.toml` `pool_spare`).
- `KIT_DEFAULT` (`seq.c`) : `chr` 127 pour les pads 10, 11 et 12 (BD2, SN2, CH2), 0 pour les autres.

## 4. L'interface

- Page KIT : K4 = **CHAR**, 0–127, le pad réglé (le dernier joué), comme LEVEL sur K3 ; un pad tenu fait passer K1–K4 sur la page PAD.
- La grille 4 × 4 de la page KIT : dans chaque case, une barre de 2 px en bas, de largeur proportionnelle au CHAR du
  pad, de la couleur de K4 ; rien à CHAR 0. La case du pad réglé garde sa couleur pleine, la barre y reste visible.
  `scene.eng[]` se double d'un `scene.chr[]` rempli au même endroit (`ui.c`).
- `words.tsv` : `CHAR`.
- Lecteur d'état du simulateur (`ui.c`) : `padchar_<n>`, comme `padlevel_<n>`.

## 5. Manuel

`MANUAL.md`, section KIT : la ligne KIT du tableau (K4 CHAR, 0–127), une phrase sur CHAR (0 le son analogique, 127
le son synthétique de chaque moteur, entre les deux un mélange), le tableau des pads par défaut (pads 10–12 :
« Synth bass drum », « Synth snare », « Synth hat »), la description de la scène KIT (la barre). Capture
`sound-kit.png` refaite.

## 6. Tests

- `tests/unit/pq_test.cc` : une voix BD, même patch et même note : `chr` 0 rend la même suite qu'avant (pas de
  `pq_set_char`) ; `chr` 1 rend une suite différente et non silencieuse ; `pq_set_engine` remet `chr` à 0.
- `tests/unit/seq_test.c` (ou `store_test.c`) : `project_defaults` met `chr` 127 aux pads 10–12, 0 aux autres.
- Script simulateur `app/tests/kit_char.txt` : page KIT, `turn` K4, `expect padchar_1` ; un pad tenu, K4 règle ce
  pad ; `shot` de la grille.
- `tests/run_tests.sh` jusqu'à « all tests ok » ; build appareil ; mesure CPU en fin de cycle.
