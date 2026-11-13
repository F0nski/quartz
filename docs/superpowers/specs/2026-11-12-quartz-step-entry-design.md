# QUARTZ — saisie dans le séquenceur

Remplace la saisie de l'étape 2 (spec globale `2026-11-10-quartz-groovebox-design.md`, sections 5.4 et 7). Une base
Elektron (le clavier joue, REC passe en grille, REC + PLAY enregistre en live), enrichie de gestes de l'OP-Z (couper et
coller des pas, coller la dernière note jouée, aperçu d'un pas tenu, durée par un geste, édition de plusieurs pas,
enregistrement armé, effacement en live), d'un mode NOTES au curseur et d'une aide qui montre ce que fait chaque bouton
tenu. Les step components de l'OP-Z auront leur propre spec ; celle-ci leur réserve une place (section 5). Manuel
d'usage : `docs/manual/sequencer-entry.html`.

## 1. But

Programmer plus vite et plus directement, sans perdre ce que la saisie fait déjà. Critères : coller, couper, régler
une durée, grouper des pas, changer les notes d'un pas, effacer en live se font chacun en un geste ; aucun attribut,
p-lock ou mode d'aujourd'hui ne disparaît ; tout bouton qui pose une couche montre ce qu'elle fait.

## 2. Les modes du clavier

REC a quatre états : désarmé, GRILLE, NOTES, LIVE. Le badge de l'en-tête montre toujours l'état (section 8).

| | Désarmé | GRILLE | NOTES | LIVE |
|---|---|---|---|---|
| Dans SEQ (pattern STEP) | les touches jouent la piste | les blanches sont les pas (section 3) | curseur, les 27 touches sont les notes du pas sous le curseur (section 4) | les touches jouent et s'enregistrent (section 6) |
| Hors de SEQ | les touches jouent | (comme NOTES) | à l'arrêt : curseur, comme dans SEQ ; en lecture : LIVE (badge `LIVE`) | comme dans SEQ |

- **REC, appui court** (relâché avant 0,4 s) : dans SEQ, désarmé → GRILLE → NOTES → désarmé ; hors de SEQ, désarmé →
  NOTES → désarmé, et un REC armé, quel que soit son état, se désarme. En LIVE, il désarme ; la lecture continue.
- **REC tenu + PLAY** : LIVE.
  - En lecture, le live commence tout de suite ; PLAY n'arrête pas la lecture dans ce cas.
  - À l'arrêt, la lecture part après une mesure de décompte si CLICK vaut COUNT ou BOTH (GLO), sinon REC attend la
    première note jouée (badge `ARM`) : elle lance la lecture et tombe sur le premier pas (l'arm de l'OP-Z).
- **PLAY** seul démarre ou arrête la lecture. En GRILLE et en NOTES dans SEQ, la lecture part sans décompte et REC garde
  son état ; en NOTES hors de SEQ, démarrer passe en LIVE, avec le décompte s'il est activé. L'arrêt termine le LIVE
  (REC se désarme) ; GRILLE et NOTES restent.
- **OCT− / OCT+** : octave du clavier, sauf en GRILLE, où ils tournent la page de pas (1–16, 17–32…), et dans les cas
  des sections 3 et 6 (pas tenu, sélection, effacement).
- **PRESET** : comme aujourd'hui, sauf en NOTES (le curseur) et sur un pas tenu ou une sélection (la fonction choisie).
- Dans SEQ, désarmé, le plateau suit la page qui joue pendant la lecture et garde la page courante à l'arrêt (pour en
  changer : GRILLE) ; les LED des blanches montrent les pas allumés et le pas en cours, comme en GRILLE.
- Un pattern GRIDS ou MARBLES n'est jamais écrit : ses touches jouent (la page NOTES garde son clavier), REC court ne
  fait rien, REC tenu + PLAY passe en LIVE pour jouer sans rien écrire.
- Les couches des boutons tenus (section 7) passent avant ces modes.

## 3. La GRILLE (SEQ)

Un appui court est un appui relâché sans rien avoir édité (la règle `edited` d'aujourd'hui).

- **Presse-papier** : un pas (notes, vélocité, durée, fonctions, p-locks) ou une note seule, commun aux pistes. Le
  dernier événement gagne :
  - couper un pas le remplace par ce pas ;
  - jouer une note ou un accord (touches tenues ensemble), REC désarmé, le remplace par cet accord, à la vélocité 100,
    durée d'un demi-pas (`len` 4, celle des pas du séquenceur).
  Au démarrage, il contient la note 60. Collé sur un KIT, un presse-papier qui ne contient pas de pad donne le pad 1.
- **Appui court sur un pas éteint** : il prend le presse-papier (coller). Les p-locks collés qui ne tiennent plus dans le
  pattern (128 au plus) sont perdus, avec le message des p-locks pleins.
- **Appui court sur un pas allumé** : couper. Son contenu va dans le presse-papier et le pas est vidé (notes, fonctions
  et p-locks). Couper puis coller ailleurs déplace un pas ; recoller sur sa case puis
  ailleurs le duplique.
- **Pas tenu** :
  - à l'arrêt, s'il est allumé, ses notes sonnent jusqu'au relâchement (aperçu) ;
  - blanches : écrivent ses notes (accord jusqu'à POLY ; KIT : ajoutent ou retirent des pads), comme aujourd'hui ; ce
    sont les notes naturelles de l'octave du clavier, le mode NOTES donne toutes les autres ;
  - noires : les fonctions (section 5) ; PRESET règle celle qui est choisie (VEL au départ) ;
  - OCT+ / OCT− : la durée passe au nombre de pas entier suivant ou précédent, de 1 à 8 pas (`len` de 8 à 64) ; OCT+
    sur une durée d'un pas ou moins passe à 2 pas (la note se lie au pas suivant) ; la fonction LEN + PRESET garde le
    réglage fin par 1/8 de pas ;
  - SELECT, K1–K4 : page de p-locks et p-locks, comme aujourd'hui (un p-lock posé sur un pas éteint l'allume).
- **SEL tenu + appuis sur des pas** : chaque appui ajoute ou retire le pas d'une sélection, dans la page affichée ; rien
  ne s'allume ni ne s'éteint. Le premier pas sélectionné (dans l'ordre des appuis) sert de référence : l'écran montre
  ses fonctions comme pour un pas tenu. Tant que SEL est tenu, sur les pas allumés de la sélection :
  - noires et PRESET : la fonction choisie prend sur tous la valeur de la référence, ajustée ; SLIDE et ACCENT passent
    sur tous à l'inverse de l'état de la référence ;
  - K1–K4 : le p-lock prend sur tous la valeur de la référence, ajustée ;
  - OCT+ / OCT− : la durée de chacun, comme pour un pas tenu.
  Relâcher SEL vide la sélection.
- **SEQ tenu + blanches 1–8** : pattern, inchangé.

## 4. Le mode NOTES

Dans SEQ à l'arrêt comme en lecture ; hors de SEQ à l'arrêt seulement. Toutes les touches sont des notes.

- **Curseur** : PRESET sans touche tenue l'avance ou le recule d'un pas, sur tout le pattern (1 à LEN, en boucle) ; le
  plateau tourne la page pour le suivre. Avancer sans écrire laisse un silence. SEL + blanche n le pose au pas n de la
  page de pas courante, s'il est dans LEN. Il part du pas 1 quand on entre en NOTES.
- **Écrire** : une touche, ou un accord (les touches pressées tant qu'une autre est tenue, jusqu'à POLY ; KIT : les
  pads), s'écrit au curseur et sonne.
  - Sur un pas éteint, le pas est créé : vélocité 100, durée d'un demi-pas, probabilité 100 %, sans fonction ni p-lock.
  - Sur un pas allumé, seules ses notes sont remplacées ; vélocité, durée, fonctions et p-locks restent.
  - Quand toutes les touches sont relâchées, le curseur avance de la durée du pas (en pas entiers, 1 au moins).
- **Liaison** : touche(s) tenue(s) + PRESET allonge ou raccourcit la note d'un pas (de 1 à 8).
- **Fonctions du pas sous le curseur** : SEL tenu + noires les choisissent (section 5), SEL tenu + PRESET règle la valeur,
  SEL tenu + K1–K4 posent des p-locks. Le pas sous le curseur est montré comme un pas tenu tant que SEL est tenu.
- PRESET ne parcourt plus ce qu'il parcourt d'habitude (pattern dans SEQ ; modèle, insert, FREE / TRIG, entrée de
  chanson ailleurs) tant que NOTES est actif.

## 5. Les fonctions d'un pas (les noires)

Sur un pas tenu, une sélection, ou en NOTES avec SEL tenu. Chaque noire porte une famille : un appui choisit sa première
fonction, un nouvel appui passe à la suivante de la famille ; PRESET règle la fonction choisie, dont le nom s'affiche.
SLIDE et ACCENT basculent à l'appui.

| Noire | Famille | Fonctions | Place réservée (spec des step components) |
|---|---|---|---|
| 1 (F#3) | répétition | RATCHET | PULSE |
| 2 (G#3) | dynamique | VEL | VEL RANDOM |
| 3 (A#3) | hasard | PROB, COND | sparks |
| 4 (C#4) | hauteur | NOTE | RAMP, STRUM |
| 5 (D#4) | temps | LEN, MICRO | — |
| 6 (F#4) | — | SLIDE (bascule) | — |
| 7 (G#4) | — | ACCENT (bascule) | — |
| 8 (A#4) | saut | — | JUMP |
| 9 (C#5) | — | — | — |
| 10 (D#5) | balayage | — | SWEEP |
| 11 (F#5) | — | — | — |

Une noire sans fonction ne fait rien. La liste des pas tenus de la spec 5b (`ATTR_ROW`) suit cet ordre de familles.

## 6. Le LIVE

- Les notes jouées s'enregistrent comme l'overdub d'aujourd'hui (quantisation de GLO, p-locks des encodeurs,
  punch-ins), y compris dans SEQ, où les touches restent des notes.
- **Effacer** : tant que REC est tenu pendant le live, les touches pressées ne sonnent pas et ne s'enregistrent pas :
  leurs notes sont retirées des pas que la lecture traverse, et un pas sans note s'éteint. REC tenu + OCT− tenu efface
  chaque pas traversé (OCT− ne change alors pas d'octave). L'interface traite tous les pas parcourus depuis l'image
  précédente, pour n'en manquer aucun à tempo rapide.

## 7. Les couches et leur aide

- Un bouton qui pose une couche, tenu plus de 0,4 s, affiche sa carte à la place de la scène : un clavier du FM-1 où
  chaque touche active porte son nom, puis les combinaisons avec d'autres boutons et encodeurs. Les touches actives de
  la couche s'allument. La carte reste tant que le bouton est tenu et suit les appuis (FX : le punch-in joué en rose).
- Un appui court fait l'action du bouton ; après 0,4 s, le relâchement ne fait plus rien (REC ne bascule pas, SAVE ne
  sauvegarde pas).

| Bouton tenu | Couche |
|---|---|
| HOME | blanches 1–4 : mutes ; 9–16 : scènes (SEL + : mémoriser) |
| FX | blanches : les 16 punch-ins |
| SAVE | blanches 1–8 : ajouter la scène à la chanson |
| SEQ | blanches 1–8 : pattern de la piste |
| ARP | touches : notes tenues (latch) |
| SEL | GRILLE : sélection de pas ; NOTES : curseur (blanches) et fonctions (noires) |
| REC | voir ci-dessous |

Couche REC :

| REC tenu + | Hors LIVE | En LIVE |
|---|---|---|
| PLAY | LIVE (arm ou décompte) | — |
| noire 1 | CLEAR : vide le pattern affiché | — |
| noire 2 | CLEAR LOCKS : retire tous les p-locks du pattern affiché | — |
| touches | — | effacent leurs notes au passage |
| OCT− | — | efface chaque pas traversé |

SEQ tenu + noire 1 (l'ancien CLEAR) disparaît.

## 8. Écran et LED

- **En-tête** : un badge `GRID`, `NOTES nn` (nn le pas au curseur), `LIVE`, ou `ARM` qui clignote en attendant la
  première note.
- **Plateau** :
  - en GRILLE, pas de viseur à l'arrêt ; en NOTES, le viseur blanc est sur le curseur, à l'arrêt comme en lecture (le
    viseur de lecture garde son rôle) ;
  - les pas sélectionnés passent en orange, posés ; la référence de la sélection, le pas tenu et, en NOTES avec SEL
    tenu, le pas sous le curseur se soulèvent comme le pas tenu d'aujourd'hui, avec ses fonctions à droite ;
  - la bande de durée (maquette `note-length`, version A) : elle part du centre du bloc et suit les pas suivants dans
    l'ordre des numéros, rangée après rangée, quel que soit PATH ; un petit chevron vert marque le passage d'une rangée
    à la suivante ; un trait clair termine la note ; pendant que la note sonne, la bande se remplit de rose case après
    case ; une note qui dépasse la page affiche `+n >` au bord avant du plateau.
- **Carte d'une couche** (section 7) : une maquette dans `docs/screens/` avant l'implémentation.
- **LED des touches** : désarmé et en GRILLE dans SEQ, pas allumés à mi-niveau (`plat_lights_mid`), pas en cours et pas
  sélectionnés pleinement allumés ; en NOTES, dans SEQ comme ailleurs, les touches des notes du pas sous le curseur ;
  une couche tenue allume ses touches actives.

## 9. Architecture

| Fichier | Rôle |
|---|---|
| `app/ui.c` | Les états de REC, le presse-papier, la sélection, le mode NOTES, les familles des noires, l'effacement en live, les couches et leur carte, les LED, le badge. |
| `app/plateau.c`, `plateau.h` | La bande prolongée et les pas sélectionnés ; la vue reçoit la sélection et les durées. |

- Pas de nouveau module. L'aperçu d'un pas passe par `audio_live_note` (sans enregistrement). L'arm lance `audio_play`
  à la première note, avant que la note parte.
- Le presse-papier tient un `q_step` et ses p-locks (au plus 16).

## 10. Tests

- **Simulateur** (scripts `app/tests/`) :
  - désarmé dans SEQ : une blanche joue la note, aucun pas ne change, OCT change l'octave ;
  - cycle de REC dans SEQ et hors de SEQ, et sur un pattern GRIDS (rien) ; un appui de plus de 0,4 s ne bascule pas ;
  - PLAY en GRILLE ou NOTES dans SEQ : pas de décompte ; l'arrêt garde GRILLE et NOTES, désarme le LIVE ;
  - GRILLE : coller (dernière note jouée, puis pas coupé avec ses p-locks, note d'une piste synthé sur un KIT : pad 1), couper (le pas est vidé), aperçu à l'arrêt,
    durée par OCT (1 à 8 pas, OCT ne change pas de page), OCT seul tourne la page, familles (deux appuis sur la noire 3 :
    COND), sélection SEL + noire + PRESET, + K, + OCT, sélection vidée au relâchement ;
  - NOTES : note et accord sur un pas éteint, sur un pas allumé (attributs et p-locks gardés), note noire, liaison,
    silence, curseur par SEL, bouclage sur LEN et page qui suit, LED des notes du pas sous le curseur, SEL + noire + PRESET sur le pas du curseur, PRESET rendu
    à son rôle quand REC est désarmé ;
  - LIVE : REC tenu + PLAY à l'arrêt attend la première note ; avec décompte, part après une mesure ; en lecture, ne
    s'arrête pas ; effacement par touche (sans son ni enregistrement) et par OCT− ; REC court désarme ;
  - couche REC : CLEAR et CLEAR LOCKS ; SAVE tenu plus de 0,4 s ne sauvegarde pas ;
  - `step_edit.txt`, `plateau.txt`, `overdub.txt`, `seq.txt`, `clear.txt` adaptés.
- **Tests C** : bande sur deux rangées, chevron, `+n` d'une note qui dépasse la page, remplissage rose, pas sélectionnés
  en orange.

## 11. Hors périmètre

- Les step components (PULSE, VEL RANDOM, sparks, RAMP, STRUM, JUMP, SWEEP…) : leur spec, avec le moteur et le format
  de sauvegarde.
- La vélocité des touches (`APP_KEY_SENSE`) : sous-projet séparé.
- Effacer d'un coup une sélection ou les p-locks d'un seul pas (une place reste libre dans la couche REC).
- Annuler un CLEAR.
- Édition groupée relative (chaque pas décalé de +e).
- Durée par geste au-delà de la page affichée.
- Copier-coller de patterns.
