# QUARTZ — étape 3b : inserts, envois, pan, compresseur

Précise la deuxième partie de l'étape 3 de la spec globale (`2026-11-10-quartz-groovebox-design.md`, sections 4.3,
4.5, 5.2 pour les p-locks, 7, 10). Part de `main` (étapes 1, 2, 3a et 4). Les punch-ins sont l'étape 3c.

## 1. But

Donner à chaque piste un insert, une place dans le stéréo et des envois vers un delay et une reverb communs, et
tenir le mix avec un compresseur-limiteur et un ducking sur la BD du KIT.
Critères : un projet neuf sonne comme avant l'étape tant qu'on ne touche à rien ; chaque nouveau paramètre se règle
en direct, se p-locke (sauf le type d'insert et les réglages de GLO) et se recharge à l'identique ; le pire cas de
l'étape (section 8) tourne sans bloc perdu.

## 2. Trajet du son

Pour chaque piste : ses voix (filtre de 3a compris) → bus mono de la piste → insert → LEVEL → PAN → mix stéréo.

- **Envois** : pris après LEVEL, avant PAN ; la somme des envois de toutes les pistes entre dans le delay et dans la
  reverb, dont les retours (stéréo, gain fixe) s'ajoutent au mix.
- **Ducking** : un gain qui retombe à chaque note du pad BD d'une piste KIT (au pas près, à l'échantillon de la note)
  et remonte ensuite. Il s'applique aux pistes qui ne sont pas des KIT et aux retours d'effets ; les pistes KIT ne
  sont jamais baissées.
- **Master** : mix → compresseur → limiteur → MASTER (le potentiomètre, comme aujourd'hui).
- **PAN** : loi de balance. Au centre, les deux canaux sont à plein niveau (le son d'avant, sans perte de 3 dB) ;
  vers un côté, l'autre canal baisse linéairement jusqu'à 0.
- Tout réglage qui change (bouton, lock, ducking) est lissé sur le bloc : pas de clic.

## 3. Paramètres

### 3.1 Par piste (`q_sound`)

| Domaine · page | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| FX · INSERT (PRESET choisit le type) | selon le type | selon le type | selon le type | MIX |
| FX · MIX | DELAY | REVERB | PAN −64..63 | LEVEL |

Valeurs 0..127 sauf mention. Les trois boutons de l'insert sont communs à tous les types : changer de type garde
leurs valeurs. MIX dose le signal traité contre le signal direct (0 : direct seul ; 127 : traité seul).

| Type | K1 | K2 | K3 |
|---|---|---|---|
| OFF | — | — | — |
| OVERDRIVE | DRIVE | TONE | — |
| WAVEFOLD | FOLD | BIAS | TONE |
| BITCRUSH | BITS : 16 à 2 bits | RATE : 44,1 kHz à 500 Hz, exponentiel | — |
| CHORUS | RATE : 0,05 à 5 Hz, exponentiel | DEPTH | — |
| PHASER | RATE : 0,05 à 5 Hz, exponentiel | DEPTH | FEEDBK |
| RING MOD | FREQ : 20 Hz à 5 kHz, exponentiel | — | — |
| COMB | NOTE : note MIDI, au moins 29 (fa 1, la limite du tampon) | FEEDBK | DAMP |

- **TONE** : passe-bas de 500 Hz à 20 kHz, exponentiel.
- **CHORUS** : une ligne à retard de 10 ms modulée par un LFO sinus, DEPTH jusqu'à ±8 ms.
- **PHASER** : 4 passe-tout modulés par un LFO sinus ; FEEDBK jusqu'à 0,9.
- **COMB** : FEEDBK jusqu'à 0,95 ; DAMP, passe-bas dans la boucle.
- **Défauts** : type OFF, K1–K3 64, MIX 127, DELAY 0, REVERB 0, PAN 0, LEVEL 100 (inchangé).
- Le type d'insert se choisit avec PRESET sur la page INSERT ; les boutons sans rôle pour le type sont affichés
  vides.

### 3.2 Projet (`q_project`)

| Domaine · page | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| GLO · TEMPO | TEMPO | SWING | COMP | DUCK |
| GLO · FX (nouvelle, entre SCALE et CPU) | DLY TIME | DLY FB | REV SIZE | REV TONE |

- **DLY TIME** : les 9 divisions du séquenceur (1/32 … 1/1), au tempo du projet. Si la durée dépasse le tampon
  (0,5 s), elle est divisée par deux jusqu'à y tenir.
- **Delay** : ping-pong (l'entrée part à gauche, chaque répétition change de côté) ; DLY FB jusqu'à 0,95 ; un
  passe-bas fixe dans la boucle assombrit les répétitions.
- **Reverb** : celle de Clouds ; REV SIZE règle la durée de la queue, REV TONE l'amortissement des aigus.
- **COMP** : 0 = compresseur coupé. Au-delà, le seuil descend de 0 à −30 dB, ratio 4:1, attaque 5 ms, relâche
  100 ms, gain de compensation automatique (la moitié de la réduction maximale). Le limiteur reste toujours actif.
- **DUCK** : profondeur de la baisse, de 0 (coupé) à −24 dB ; attaque instantanée, remontée en 150 ms.
- **Défauts** : DLY TIME 1/8, DLY FB 40, REV SIZE 80, REV TONE 64, COMP 0, DUCK 0.

## 4. P-locks

- Verrouillables : les trois boutons de l'insert, MIX, DELAY, REVERB, PAN, LEVEL. Pas le type d'insert ni les
  réglages de GLO.
- Ces paramètres sont ceux de la piste, pas d'une voix : un lock vaut du début de son pas jusqu'au début du pas
  suivant de la piste, puis la piste reprend son réglage. Le rendu prend ces locks à la note du pas et suit le pas
  en cours du séquenceur (`seq_pos`) pour les retirer ; un STOP les retire.
- Pas maintenu dans SEQ : les pages de p-lock deviennent ENGINE, SOUND, ADSR, FILTER, LFO, INSERT, MIX, ARP (sur un
  KIT : ADSR, FILTER, LFO, INSERT, MIX).
- En overdub, un mouvement de ces encodeurs devient un p-lock sur le pas joué, comme pour les autres pages.
- PAN est rangé en `uint8_t`, relu en `int8_t`, comme TUNE.

## 5. Interface

- Domaine **FX** (bouton `B_FX`) : pages INSERT et MIX. FX maintenu + touches reste libre jusqu'à 3c (punch-ins).
- GLO : COMP et DUCK sur TEMPO ; nouvelle page FX.
- Le nom de la page INSERT affiche le type choisi.

## 6. Architecture

- **`app/dsp/qfx.cc`, `qfx.h`** (C++ derrière des fonctions C, comme `pq`) : les inserts, le delay, la reverb, le
  compresseur et le limiteur. Réutilise ce qui est déjà là : `Overdrive` et `SampleRateReducer` de Plaits, sa table
  `lut_fold`, `CosineOscillator` de stmlib, le `FxEngine` de Plaits. Ajoute `clouds/dsp/fx/reverb.h` (MIT) dans
  `third_party`. Phaser, comb, chorus, delay et compresseur sont écrits ici.
- **`audio.c`** :
  - chaque cœur rend ses voix dans ses 4 bus de piste (2 cœurs × 4 pistes), à la place de `mix1` et `mix2` ;
  - après le dernier segment du bloc, le cœur 1 additionne les bus des deux cœurs, applique insert, LEVEL, PAN et
    envois par piste, puis delay, reverb, ducking, compresseur, limiteur, MASTER ;
  - la sortie devient stéréo ;
  - le clic du métronome s'ajoute au mix après les effets, sans ducking ni envoi.
- **`ui.c`** : domaine FX, page FX de GLO, COMP et DUCK, pages de p-lock INSERT et MIX.
- **Projet** : `q_sound` gagne 8 octets (type, trois boutons, MIX, DELAY, REVERB, PAN) ; `q_project` gagne 6 octets ;
  `LK_*` est étendu ; `Q_VERSION` passe à 5. Les projets sauvegardés avant ne se rechargent pas.
- **Mémoire** :
  - pool : reverb 32 Ko et delay 43 Ko (au budget de la spec globale), plus un tampon de 4 Ko par piste pour
    chorus, phaser et comb (16 Ko) ; il reste environ 13 Ko de marge, LOOP de 3c comptés ;
  - RAM principale : environ 9 Ko de plus (bus, envois, clic) ; si la place manque, les bus vont dans le pool.
- **CPU** : estimé à environ 15 % du cœur 1 (reverb 5–8 %, delay 2 %, inserts 1–3 % chacun, compresseur 1 %).
  Si le pire cas perd des blocs, la reverb passe sur le cœur 2 avec un bloc de retard (5,8 ms, inaudible).

## 7. Tests

- **PC** :
  - insert OFF ou MIX 0 : sortie identique à l'entrée ;
  - chaque type d'insert change le signal, sans NaN ni emballement sur un signal plein ;
  - loi de pan (centre, extrêmes) ;
  - delay : le retour tombe à 2 échantillons près de sa division (tampon à 22 kHz), alterne gauche et droite ; division trop longue
    divisée par deux ;
  - ducking : déclenché par la BD d'un KIT, la piste KIT n'est pas baissée, les autres et les retours le sont ;
  - lock d'effet appliqué à son pas, retiré au pas suivant ;
  - projet neuf : même signal qu'avant l'étape sur les deux canaux (sous le seuil du limiteur) ;
  - navigation dans FX, GLO · FX, et les pages de p-lock INSERT et MIX.
- **Émulateur** : captures des pages INSERT, MIX et GLO · FX.
- **Appareil** : 6 voix MODAL, les 4 inserts sur PHASER, envois delay et reverb ouverts, COMP et DUCK actifs, sans
  bloc perdu ; on mesure aussi le pire cas de 3a. Les chiffres vont dans `docs/cpu.md`.

## 8. Hors périmètre

- Punch-ins (3c).
- LFO vers PAN ou vers les paramètres d'effet.
- Insert stéréo (le chorus reste mono dans sa piste), second insert.
- Niveau des retours d'effets, réglages complets du compresseur (seuil, ratio, temps).
- Bug XY connu de l'étape 2 (`seq.c` ~470) : à corriger à part.
