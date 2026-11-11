# QUARTZ — étape 3c : punch-ins

Précise la dernière partie de l'étape 3 de la spec globale (`2026-11-10-quartz-groovebox-design.md`, sections 4.4,
4.5, 5.4, 7). Part de `main` (étapes 1, 2, 3a, 3b et 4). Remplace la liste des effets de la section 4.4 : RING MOD
et KILL LOWS y cèdent leur place à deux STUTTER.

## 1. But

Jouer le mix en live avec 16 effets momentanés sur le master, et les enregistrer dans les patterns.
Critères : un projet sans punch-in sonne comme avant l'étape ; chaque punch-in entre et sort sans clic ; un punch-in
enregistré rejoue sur ses pas ; le pire cas de l'étape (section 6) tourne sans bloc perdu.

## 2. Jeu

- **FX maintenu** : les 16 touches blanches sont les punch-ins, quelle que soit la page, sur une piste synthé comme
  sur un KIT. Les noires ne font rien. Un punch-in est actif tant que sa touche est tenue ; plusieurs peuvent l'être
  ensemble.
- Entrée et sortie lissées sur un bloc (5,8 ms) : pas de clic.
- Ils agissent sur tout le mix, retours du delay et de la reverb compris, avant le compresseur et le limiteur. Le clic
  du métronome n'est pas touché.
- Ils marchent aussi à l'arrêt (clavier joué en direct) : la grille des effets calés sur le tempo part alors de
  l'appui.
- Le nom du dernier punch-in appuyé s'affiche dans la ligne de message.

## 3. Les 16 effets

Réglages fixes, sans encodeur. Les valeurs sont des points de départ, à ajuster après écoute sur l'appareil.

| # | Effet | Comportement |
|---|---|---|
| 1–4 | LOOP 1/8, 1/12, 1/16, 1/32 | Répète la tranche de sa division commencée au dernier trait de la grille (temps du séquenceur) : à l'appui, le son continue sans saut, puis la tranche se répète. |
| 5 | TAPE STOP | Lecture ralentie de la vitesse 1 à 0 en un temps (au plus 0,7 s, la limite du tampon), la hauteur descend avec ; silence ensuite tant que la touche est tenue. |
| 6 | LOW-PASS | Passe-bas 2 pôles, 400 Hz, légère résonance. |
| 7 | HIGH-PASS | Passe-haut 2 pôles, 1,5 kHz. |
| 8 | SWEEP | Passe-bas très résonant, coupure en triangle de 8 kHz à 200 Hz et retour, une période par mesure, en partant du haut à l'appui. |
| 9 | BITCRUSH | 6 bits. |
| 10 | DOWNSAMPLE | 4 kHz, sans filtre d'anti-repliement. |
| 11 | GATE 1/16 | Son coupé une double-croche sur deux (la seconde), sur la grille. |
| 12 | AUTO-PAN | Balance gauche-droite sinus, un aller-retour par temps. |
| 13 | DELAY THROW | Tout le mix entre dans le delay ; feedback d'au moins 0,6 pendant l'appui. Les répétitions continuent après le relâché, au feedback du projet. |
| 14 | REVERB FREEZE | Le mix entre dans la reverb pendant une double-croche après l'appui, puis l'entrée se ferme et la queue est tenue (décroissance maximale) ; au relâché, elle s'éteint au réglage du projet. |
| 15 | STUTTER | Répétition accélérée : tranche de 1/8, puis 1/16, 1/32 et 1/64, un cran par temps depuis l'appui ; reste à 1/64. |
| 16 | STUTTER GATE | Tranche de 1/64 répétée, chaque répétition coupée sur sa seconde moitié. |

- **Tampon de boucle** (LOOP, TAPE STOP, STUTTER) : un seul effet le lit à la fois. Priorité : TAPE STOP, puis
  STUTTER GATE, puis STUTTER, puis le plus court des LOOP tenus (ajouter un LOOP plus court donne un roll).
- Le tampon est stéréo, 16 bits, échantillonné à 22 kHz, 0,37 s ; il enregistre le mix en continu et cesse d'écrire
  pendant qu'un effet en relit une tranche. Une tranche plus longue que le tampon est divisée par deux jusqu'à y tenir.
- **Ordre** : tampon de boucle → GATE → BITCRUSH → DOWNSAMPLE → LOW-PASS → HIGH-PASS → SWEEP → AUTO-PAN →
  compresseur. DELAY THROW et REVERB FREEZE agissent en amont, sur les envois et la reverb.

## 4. Enregistrement

- REC + PLAY sur un pattern STEP (hors décompte) : chaque pas de la piste courante traversé pendant qu'un punch-in est
  tenu le retient, pas vide compris, dans le pattern en cours de cette piste s'il est celui affiché (même règle que les
  p-locks d'encodeur).
- Stockage : deux p-locks spéciaux par pas, `LK_PUNCH_LO` (touches 1–8) et `LK_PUNCH_HI` (9–16), dont la valeur est
  un masque de bits. Ils ne sont pas des champs de `q_sound` : leurs numéros suivent `LK_N`, et les lectures de locks
  de voix et d'effets de piste les ignorent. Les repasses ajoutent des bits (OU).
- Ils partagent les 128 locks du pattern ; plein : rien n'est écrit et le message des locks pleins s'affiche, comme
  pour un encodeur.
- « Effacer » le pas (noire 9) les retire ; activer ou couper le pas les garde.
- Relecture : à chaque bloc, le masque actif est l'OU des touches tenues et des locks de punch du pas en cours
  (`seq_pos`) du pattern joué de chaque piste non mutée. Un punch-in enregistré vaut du début de son pas au début du
  pas suivant, à la résolution d'un bloc.
- Le projet ne change pas de format : `Q_VERSION` reste 5.

## 5. Architecture

- **`ui.c`** : FX maintenu + blanche tient ou relâche un bit du masque et l'envoie au rendu ; relâcher FX relâche
  tout. En overdub, le masque tenu est écrit (OU) au pas en cours dans la boucle principale.
- **`audio.c`** : `audio_punch(mask)` (un mot lu par le rendu au début du bloc) ; le masque actif du bloc (section 4) ;
  la position dans le temps (images depuis le dernier temps, images par temps) prise de l'horloge des temps du
  séquenceur pendant la lecture, sinon d'une phase libre partie à l'appui. Le tout entre dans `qfx_master`.
- **`qfx.cc`** : les punch-ins dans `qfx_block`. Filtres SVF de stmlib, réducteurs de Plaits déjà utilisés par
  BITCRUSH ; aucune fonction de libm par échantillon (fastmath). Tampon de boucle dans le pool (32 Ko), mis à zéro par
  `qfx_init`.
- **Mémoire** : 32 Ko de pool pour le tampon, au budget de la spec globale ; quelques octets d'état en RAM principale.

## 6. Tests

- **PC** :
  - aucun punch-in : sortie identique à celle d'avant l'étape ;
  - chaque effet change le signal, sans NaN ni emballement sur un signal plein ; entrée et sortie sans saut ;
  - LOOP : pas de saut à l'appui, la tranche se répète à sa division depuis le dernier trait de la grille ; division
    trop longue divisée par deux ;
  - priorités du tampon ; STUTTER passe de 1/8 à 1/64 en trois temps ;
  - DELAY THROW : répétitions après le relâché ; REVERB FREEZE : queue tenue tant que la touche est tenue ;
  - enregistrement d'un punch-in sur des pas vides, relecture sans touche tenue, effacement par la noire 9 ;
    piste mutée sans punch-in ; locks pleins ;
  - navigation : FX maintenu + blanches, aucune note jouée.
- **Appareil** : le pire cas de 3b (6 MODAL, 2 PHASER, COMP, delay, reverb) avec LOOP 1/16, SWEEP, GATE et REVERB
  FREEZE tenus, sans bloc perdu. Chiffres dans `docs/cpu.md`.

## 7. Hors périmètre

- Intensité réglable d'un punch-in, encodeurs pendant l'appui.
- Écrire ou retirer un punch-in au pas sans l'enregistrer (pas maintenu + FX), affichage des punch-ins dans SEQ.
- Punch-ins par piste (ils agissent sur le master seul).
- Résolution plus fine que le pas pour les punch-ins enregistrés.
