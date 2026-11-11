# QUARTZ — étape 3a : enveloppe, filtre, LFO, arpégiateur

Précise la première partie de l'étape 3 de la spec globale (`2026-11-10-quartz-groovebox-design.md`, sections 4.2,
4.3 pour le filtre, 5.2 pour les p-locks, 7, 10). Part de `main` (étapes 1, 2 et 4). L'étape 3 est découpée en trois :
3a (ce document), 3b (inserts, envois, pan, compresseur), 3c (punch-ins).

## 1. But

Sculpter le son de chaque piste : une enveloppe ADSR, un filtre SVF par voix, un LFO par piste et un arpégiateur.
Critères : un projet neuf sonne comme avant l'étape tant qu'on ne touche à rien ; chaque nouveau paramètre se
règle en direct, se p-locke et se recharge à l'identique ; le pire cas de l'étape (6 voix MODAL, filtre et LFO
actifs, arp sur deux pistes) tourne sans bloc perdu.

## 2. Trajet du son

Pour chaque voix : moteur Plaits → low-pass gate de Plaits piloté par l'ADSR → filtre SVF → niveau de la piste.

- **ADSR** : son niveau, multiplié par la vélocité, devient le niveau de la voix Plaits (`mod.level`), recalculé
  à chaque bloc Plaits (12 échantillons). Le low-pass gate suit ce niveau : le son s'assombrit en retombant, comme
  aujourd'hui. DECAY de la page ENGINE garde son rôle (queue du low-pass gate, enveloppes internes).
- **Moteurs à enveloppe propre** (6-OP A/B/C, STRING, MODAL, BD, SN, HH, donc tout KIT) : ils gardent leur
  enveloppe et leur niveau suit la vélocité seule, comme aujourd'hui ; l'ADSR ne pilote que le filtre.
- **Filtre** : le `Svf` de stmlib, un par voix, appliqué à chaque bloc Plaits. Sa fréquence vaut
  CUTOFF + ADSR × ENV AMT + LFO ; elle est recalculée à chaque bloc Plaits.
- **Accent** : la vélocité monte déjà de 32 ; en plus, la part de l'ADSR sur le filtre est multipliée par 1,5.
  Le séquenceur le signale par un drapeau `EV_ACCENT` sur l'événement.
- Une voix est libérée quand sa release est finie et que sa sortie est silencieuse (règle actuelle de `pq_silent`).

## 3. Paramètres

Tous sont par piste, dans `q_sound`. Valeurs 0..127 sauf mention.

| Domaine · page | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| ENV · ADSR | ATTACK | DECAY | SUSTAIN | RELEASE |
| ENV · FILTER | TYPE : LP, HP, BP | CUTOFF | RES | ENV AMT −64..63 |
| LFO · LFO | SHAPE | RATE | DEPTH −64..63 | DEST |
| ARP · ARP | MODE | OCT 1..4 | DIV | GATE 5..100 % |

- **ATTACK, DECAY, RELEASE** : de 1 ms à 10 s, en échelle exponentielle. **SUSTAIN** : niveau linéaire.
- **CUTOFF** : de 20 Hz à 20 kHz, en échelle exponentielle (au demi-ton). 127 laisse passer tout le son en LP.
- **RES** : de la résonance nulle jusqu'au bord de l'auto-oscillation.
- **ENV AMT** : ±63 vaut ±8 octaves sur la fréquence du filtre.
- **SHAPE** : SIN, TRI, SAW (montante), SQR, S&H (une valeur au hasard par cycle).
- **RATE** :
  - d'abord 12 vitesses synchronisées au tempo, avec la période : 8 mesures, 4, 2, 1 mesure, 1/2, 1/4, 1/4T, 1/8,
    1/8T, 1/16, 1/16T, 1/32 ;
  - puis 64 vitesses libres, de 0,05 Hz à 30 Hz en échelle exponentielle.
- **PRESET sur la page LFO** : bascule FREE / TRIG. FREE : une phase par piste, qui tourne en continu ; un RATE
  synchronisé repart de zéro à chaque PLAY, calé sur la mesure. TRIG : chaque note repart du début de la forme.
- **DEST** : HARM, TIMB, MORPH, PITCH, CUTOFF, RES, LEVEL. À DEPTH ±63 :
  - HARM, TIMB, MORPH, RES : ± toute la course ;
  - PITCH : ±12 demi-tons ;
  - CUTOFF : ±5 octaves ;
  - LEVEL : le niveau varie entre plein et 1 − |DEPTH|/63.

  Le LFO s'ajoute à la valeur réglée (ou p-lockée), bornée à sa course.
- **MODE** : OFF, UP, DOWN, UP-DN, RAND, ORDER. UP-DN ne répète pas les notes des extrémités ; ORDER suit l'ordre
  d'arrivée des notes.
- **DIV** : les 9 divisions du séquenceur (1/32 … 1/1).
- **Défauts** :
  - ADSR : ATTACK 0, DECAY 64, SUSTAIN 127, RELEASE 20 ;
  - filtre : LP, CUTOFF 127, RES 0, ENV AMT 0 ;
  - LFO : SIN, FREE, RATE 1/4, DEPTH 0, DEST CUTOFF ;
  - arp : OFF, OCT 1, DIV 1/16, GATE 50 %.

## 4. Arpégiateur

- Il reçoit toutes les notes de la piste : clavier, pas STEP, GRIDS, MARBLES. Un pas d'une seule note est arpégé
  aussi (octaves si OCT > 1, sinon répétée à la division de l'arp) ; un pas sans arp se fait par un p-lock MODE OFF.
- Les notes tenues forment l'ensemble arpégé. L'arp part dès qu'il passe de vide à non vide, à l'instant de cette
  note, puis avance à sa division au tempo du projet (sans swing) ; il s'arrête quand l'ensemble se vide. Une
  note de l'arp dure GATE % de la division, avec la vélocité, le pattern et le pas de la note d'origine (donc
  ses p-locks).
- OCT : l'ensemble est joué sur 1 à 4 octaves vers le haut avant de reboucler.
- Les pas liés (slide) et les ratchets arrivent comme des note-on et note-off ordinaires ; un slide n'est pas
  glissé sous l'arp.
- **Latch** : ARP maintenu + touches ajoute les notes à un ensemble verrouillé, qui reste après relâchement. Le
  premier appui sur une touche sans ARP maintenu vide le verrou ; la touche joue alors normalement.
- Ignoré sur une piste KIT : la page ARP l'indique.
- Dans le rendu : entre le séquenceur (et les commandes du clavier) et les voix. Les notes d'une piste en arp vont
  à l'arp, qui rend ses propres notes du bloc, triées par position et fusionnées avec les autres événements.

## 5. P-locks

- Tous les paramètres des pages ADSR, FILTER, LFO et ARP sont verrouillables ; FREE/TRIG ne l'est pas.
- Pas maintenu dans SEQ : SELECT parcourt les pages de p-lock ENGINE, SOUND, ADSR, FILTER, LFO, ARP (sur un KIT :
  ADSR, FILTER, LFO). Une piste KIT a donc enfin des p-locks, sur ces trois pages ; les réglages des pads ne se
  verrouillent pas.
- En overdub, un mouvement de ces encodeurs devient un p-lock sur le pas joué, comme pour ENGINE et SOUND.
- Une voix prend les locks de son pas à sa note-on (règle actuelle) ; l'arp lit MODE, OCT, DIV et GATE dans les
  locks du pas de la dernière note entrée.
- Un lock signé (ENV AMT, DEPTH) est rangé en `uint8_t`, relu en `int8_t`, comme TUNE.

## 6. Architecture

- **`app/mod.c`** (C, sans matériel, testé sur PC) :
  - ADSR par voix ;
  - LFO par piste (FREE) ou par voix (TRIG) ;
  - arp par piste.

  `audio.c` avance les LFO FREE une fois par bloc audio, sur le cœur 1, avant le rendu. L'état d'une voix n'est
  touché que pendant le rendu de cette voix, sur le cœur qui la rend.
- **`dsp/pq.cc`** : à chaque bloc Plaits, prend de `mod` le niveau et les décalages (filtre, hauteur, HARM/TIMB/
  MORPH, niveau), rend le moteur, applique le SVF.
- **`seq.c`** : seulement le drapeau `EV_ACCENT`.
- **`ui.c`** : domaines ENV, LFO, ARP (boutons `B_ENV`, `B_LFO`, `B_ARP`) ; pages de p-lock étendues ; latch de
  l'arp.
- **Projet** : `q_sound` gagne 17 octets ; `LK_*` est étendu aux nouveaux paramètres ; `Q_VERSION` passe à 4. Les
  projets sauvegardés avant ne se rechargent pas (aucune conversion tant que le firmware n'est pas fini).
- **Mémoire** : quelques centaines d'octets en RAM principale ; le pool ne change pas.
- **CPU** : estimé à 1–2 % de cœur par voix pour le SVF et l'ADSR, à mesurer ; le résultat va dans `docs/cpu.md`.

## 7. Tests

- **PC** :
  - formes et temps de l'ADSR (attaque, sustain, release depuis n'importe quel point) ;
  - formes du LFO, FREE recalé au PLAY, TRIG à la note ;
  - arp : chaque mode, OCT, DIV, GATE ; positions à l'échantillon près ; latch ; pas d'une note ; locks MODE OFF ;
  - p-locks des nouveaux paramètres, KIT compris ;
  - accent sur l'ENV AMT ;
  - navigation dans ENV, LFO et ARP, et dans les pages de p-lock.
- **Émulateur** : capture des pages ADSR, FILTER, LFO, ARP.
- **Appareil** : pire cas de la section 1 sans bloc perdu ; écoute des défauts (un projet neuf sonne comme avant).

## 8. Hors périmètre

- Inserts, envois delay/reverb, pan, compresseur (3b) ; punch-ins (3c).
- Second LFO, LFO destination PAN (avec 3b), p-locks des réglages de pads.
- Dessin de l'enveloppe ou du LFO à l'écran (étape 5).
- Bug XY connu de l'étape 2 (`seq.c` ~470, `steps[-4]` après un changement de tempo) : à corriger à part.
