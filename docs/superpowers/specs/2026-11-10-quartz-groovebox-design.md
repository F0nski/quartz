# QUARTZ — firmware groovebox pour le M-VAVE FM-1

Spec de conception globale. Chaque étape (section 12) aura son propre plan d'implémentation.

## 1. But

Un firmware open source qui transforme le FM-1 en groovebox 4 pistes, construite sur les modules
Mutable Instruments (code MIT, `pichenettes/eurorack`) et la plateforme fm1-base (MPL-2.0).

Elle doit être complète mais simple : séquenceur riche (durées et divisions différentes par piste),
générateurs (Grids, Marbles), effets configurables, punch-ins façon Teenage Engineering, enregistrement
live, et un écran avec une identité visuelle forte. Chaque bouton du FM-1 a un rôle clair.

Usage visé : des projets qui mêlent pistes génératives et pistes programmées à la main ; des morceaux
complets de temps en temps (mode chanson).

Critères de réussite :
- zéro bloc audio perdu dans le pire cas mesuré sur l'appareil (section 13) ;
- toute fonction atteignable sans sous-menu (section 8) ;
- un projet sauvegardé se recharge à l'identique.

## 2. Plateforme et capacités mesurées

| Ressource | Valeur | Source |
|---|---|---|
| Processeur | JieLi AC791N, 2 cœurs pi32v2 à 360 MHz, FPU simple précision | fm1-base, spike |
| Audio | 44 117,6 Hz, blocs de 256 échantillons (5,8 ms) | fm1-base |
| RAM | 96 Ko de RAM principale + 336 Ko de pool | linker fm1-base |
| Flash | image d'app ≤ 568 Ko ; zone de données 0x97000–0xDFFFF (292 Ko), objets ≤ 3 840 octets | `include/plat.h` |
| Écran | ST7789 240×240, dessin par bandes de 240×144 au plus | fm1-base |
| Contrôles | 14 boutons, 7 encodeurs sans clic, potentiomètre MASTER, 27 touches (F3–G5) avec vélocité | `include/plat.h` |

Coût CPU d'une voix Plaits, mesuré sur le FM-1 de l'auteur (spike du 2026-11-10), en % d'un cœur :

| Classe | Modèles | Coût |
|---|---|---|
| Légers | SPEECH, CHIPTUNE, WAVESHAPE, WAVETABLE, VA VCF | 5–11 % |
| Courants | 6-OP A/B/C, 2-OP FM, VA, CHORDS, STRING, PARTICLE, SWARM, GRAIN, PHASE DIST, NOISE, BD, SNARE, HI-HAT | 12–16 % |
| Lourds | ADDITIVE, MODAL | 20–21 % |

Le coût est linéaire en nombre de voix et le second cœur divise le temps par deux sans perte.
Les effets, le séquenceur et l'écran ne sont pas encore mesurés : ils le seront à l'étape 1 et à l'étape 3.

## 3. Concepts

- **Projet** : tempo, swing, gamme, 4 pistes, 8 scènes, une chanson, les réglages d'effets globaux.
- **Piste** : un *moteur* (ce qui sonne) + une *source* (ce qui joue) + ses modulations et son insert.
- **Moteur** : un modèle Plaits (synthé), ou KIT (16 pads sur les blanches, chacun une percussion Plaits BD, SN ou
  HH réglée à part, 3 voix au plus).
  Plus tard : le FM 6-op d'origine du FM-1, puis Rings.
- **Source** : STEP (séquenceur pas à pas), GRIDS ou MARBLES, choisie par pattern. Toute piste se joue aussi au
  clavier, quelle que soit sa source.
- **Pattern** : 8 par piste. Une piste joue un pattern à la fois.
- **Scène** : une combinaison des 4 patterns en cours et des mutes. 8 par projet.
- **Chanson** : une liste de scènes avec leur nombre de répétitions.

## 4. Son

### 4.1 Voix et polyphonie
- Pool de **6 voix**, 3 par cœur.
- Chaque piste est mono par défaut ; POLY règle 1 à 4 voix par piste. Une piste KIT prend jusqu'à
  3 voix (une par instrument).
- Quand le pool est plein, la voix la plus ancienne est volée.
- Une voix silencieuse (enveloppe retombée) n'est pas calculée.
- Une voix n'instancie que le modèle choisi ; changer de modèle réinitialise les voix de la piste.

### 4.2 Modulations par piste
- **ADSR**, qui pilote le niveau et, via ENV AMT, le filtre.
- **LFO** : formes sinus, triangle, dent de scie, carré, S&H ; vitesse synchronisée au tempo ou libre ;
  une destination parmi les paramètres des pages de la piste.
- **Arpégiateur** : modes up, down, up-down, random, ordre joué ; 1 à 4 octaves ; division propre.
  Il arpège les notes tenues au clavier, ou les notes d'un pas polyphonique pendant la durée du pas.

### 4.3 Effets
- **Par piste** : filtre SVF (passe-bas, passe-haut, passe-bande ; cutoff, résonance, ENV AMT) puis
  un slot d'insert au choix : overdrive, wavefolder, bitcrush, chorus, phaser, ring mod, comb.
- **Deux envois** : delay synchronisé au tempo (ping-pong) et reverb Mutable. Par piste : envoi delay,
  envoi reverb, pan, niveau.
- **Master** : compresseur-limiteur, avec ducking optionnel déclenché par la BD du KIT.
- Sources du code : Mutable (Plaits, Rings, Clouds/Beads pour la reverb et le chorus, stmlib pour
  les filtres) en priorité ; code propre sinon.

### 4.4 Punch-ins
FX maintenu : les 16 touches blanches deviennent 16 effets momentanés sur le master, actifs tant que la
touche est tenue. Avec REC armé et PLAY, ils sont enregistrés dans les patterns en cours.

1–4 LOOP 1/8, 1/12, 1/16, 1/32 · 5 TAPE STOP · 6 LOW-PASS · 7 HIGH-PASS · 8 SWEEP résonant ·
9 BITCRUSH · 10 DOWNSAMPLE · 11 GATE 1/16 · 12 AUTO-PAN · 13 DELAY THROW · 14 REVERB FREEZE ·
15 STUTTER · 16 STUTTER GATE. Détail : `2026-11-11-quartz-step3c-punchins-design.md`.

Les LOOP utilisent un tampon stéréo de 0,37 s échantillonné à 22 kHz ; une boucle plus longue que le tampon
est divisée par deux jusqu'à y tenir.

### 4.5 Budget mémoire du pool (336 Ko)

| Poste | Taille |
|---|---|
| 6 voix (un modèle chacune : 16 Ko de tampon, dont PARTICLE, STRING et SPEECH ont besoin, et l'objet voix) | 127 Ko (mesuré) |
| Tampon de l'écran (une bande de 240×83) | 39 Ko |
| Projet chargé (patterns, scènes, chanson) | 33 Ko |
| Reverb | 32 Ko |
| Delay (stéréo, 16 bits, échantillonné à 22 kHz : 0,5 s) | 43 Ko |
| Tampon des LOOP (stéréo, 16 bits, 22 kHz : 0,37 s) | 32 Ko |
| Marge | ≈ 29 Ko |

Si un poste manque de place, la durée du delay est réduite avant le reste : la plupart des usages tiennent
dans une croche ou une noire.

## 5. Séquenceur

### 5.1 Par piste
- Longueur libre de 1 à 64 pas.
- Division propre : 1/32, 1/16T, 1/16, 1/8T, 1/8, 1/4T, 1/4, 1/2, 1/1. Pistes de longueurs et de
  divisions différentes = polyrythmes.
- Changement de pattern : à la fin du pattern en cours.
- Sens de lecture (PATH), par pattern. Les 16 pas d'une page forment une grille 4×4 (pas 1–4 en première rangée,
  5–8 en deuxième…) lue dans un ordre : FWD par défaut (rangée par rangée, de gauche à droite), REV, PING-PONG,
  SNAKE, COLONNES, SPIRALE, RANDOM. Le rang `k` joué (0 à LEN − 1) donne le pas `16 × ⌊k / 16⌋ + chemin[k mod 16]` ;
  REV, PING-PONG et RANDOM agissent sur `k`. La touche n reste le pas n, LEN compte les pas joués, le swing suit
  le rang joué.
- PATH = XY, deux horloges (dans l'esprit du René de Make Noise) : sur la grille 4×4 de la première page, la
  colonne X avance à la division de la piste et boucle sur X LEN (1–4) ; la rangée Y avance à sa propre division
  (Y DIV) et boucle sur Y LEN (1–4). Le pas joué est la case (X, Y) ; une note part à chaque pas de X, Y ne fait
  que changer de rangée. Deux divisions différentes donnent des motifs qui ne visitent qu'une partie des cases.

### 5.2 Par pas
- Note (une à quatre si la piste est POLY ; pour un KIT, quels instruments jouent), vélocité 1–127.
- Durée : de 1/8 de pas à 8 pas.
- Micro-timing : de −11/24 à +11/24 de pas, par 1/24.
- Probabilité : 0 à 100 %, par 5 %.
- Condition : A:B (joue la A-ième fois sur B, B de 2 à 8), FIRST, !FIRST, PRE, !PRE.
- Ratchet : 1 à 4 répétitions dans le pas.
- Slide : glissé vers la note suivante (durée = GLIDE de la piste). Accent : vélocité et ENV AMT augmentées.
- P-locks : jusqu'à 128 par pattern, sur tout paramètre des pages de la piste (moteur, enveloppe,
  filtre, insert, envois, LFO).

### 5.3 Projet
- Tempo 40–240 BPM, swing 50–75 %.
- Scènes lancées à la mesure suivante.
- Chanson : jusqu'à 32 entrées (scène, 1 à 16 répétitions).
- Gamme du projet : tonique et mode ; elle quantifie Marbles et l'affichage des notes.

### 5.4 Enregistrement
- **REC + PLAY** : overdub dans le pattern en cours de la piste choisie. Quantisation OFF, 50 % ou
  100 % (réglée dans GLO). Les mouvements d'encodeurs deviennent des p-locks sur les pas traversés ;
  les punch-ins sont enregistrés aussi.
- REC n'écrit que dans un pattern STEP.
- **REC sans PLAY** : saisie pas à pas. Une touche écrit la note au pas courant et avance d'un pas.
- Modes, grille, pas à pas, live et effacement : `2026-11-12-quartz-step-entry-design.md`, qui prime sur cette section.
- Décompte d'une mesure et métronome, activables dans GLO.

## 6. Générateurs

- **GRIDS** : carte de 5×5 rythmes de 32 pas à 3 voix, interpolée selon X et Y ; densité par voix ;
  chaos ; accents. Sur un KIT, les 3 voix jouent les pads 1, 2 et 3. Sur une piste synthé, une des 3 voix
  (au choix) déclenche une note fixe réglée sur la page source.
- **MARBLES** : section T (rythmes aléatoires, 3 sorties t1–t3) et section X (notes aléatoires
  quantifiées dans des notes permises, la gamme par défaut : SPREAD, BIAS, STEPS, RANGE), DÉJÀ VU et longueur de boucle 1–16. Sur un KIT,
  t1–t3 jouent les pads 1, 2 et 3 ; sur une piste synthé, une sortie T au choix joue la note de X.
- Les deux suivent l'horloge et la division du pattern. Détail : `2026-11-10-quartz-step4-generators-design.md`.
- **LIFE** : une grille du jeu de la vie de 16 × 8 lue colonne par colonne, une génération tous les GEN tours.
  Détail : `2026-11-13-quartz-life-design.md`.
- Étape ultérieure (demandée, non planifiée) : un LEN de 1 à 32 pour GRIDS, sur le 4e encodeur libre de MAP ou de DENS ;
  la boucle repart au pas 1 après LEN, les pas au-delà s'affichent sombres.

## 7. Contrôles

| Contrôle | Rôle |
|---|---|
| ALGORITHM | choisir la piste 1–4 |
| SELECT (encodeur) | passer d'une page à l'autre dans le domaine courant |
| PRESET | parcourir ce qui est à l'écran : presets du moteur, type d'insert, pattern… |
| K1–K4 | les 4 paramètres de la page ; couleurs fixes bleu, vert, blanc, orange |
| SEL (bouton) | valider (charger le preset choisi, confirmer une action) ; maintenu : Shift des pas (sélection dans la grille, p-locks en NOTES) |
| MASTER | volume |
| PLAY | lecture / stop |
| REC | appuis courts : GRILLE puis NOTES dans SEQ, NOTES ailleurs ; tenu + PLAY : live ; tenu : couche (CLEAR…) (spec `2026-11-12-quartz-step-entry-design.md`) |
| OCT− / OCT+ | octave du clavier ; dans la grille (SEQ, REC armé), page de pas (1–16, 17–32…) ; pas tenu : durée |

| Bouton de domaine | Pages (SELECT les parcourt) | Maintenu + touches |
|---|---|---|
| EDIT | moteur (4 paramètres du modèle) · modèle / accord / glide / poly ; KIT : pad · kit | KIT : pad tenu + K1–K4 / PRESET règle le pad |
| ENV | ADSR · filtre | — |
| LFO | LFO | — |
| ARP | arpégiateur | touches = notes tenues (latch) |
| FX | envois / pan / niveau · insert | punch-ins sur les 16 blanches |
| SEQ | pas + longueur / division / source (STEP) ; division / source · réglages du générateur (GRIDS, MARBLES) | blanches 1–8 : pattern de la piste |
| GLO | tempo / swing / compresseur · delay / reverb · gamme et réglages REC | — |
| HOME | vue des 4 pistes | blanches 1–4 : mutes ; 9–16 : scènes |
| SAVE | appui : sauvegarder le projet | blanches 1–8 : ajouter une scène à la chanson |

Clavier :
- Hors SEQ, les touches jouent la piste courante (et l'enregistrent quand REC est armé).
- Dans SEQ en GRILLE, les 16 blanches sont les pas : un appui colle ou coupe le pas ; pas maintenu + K = p-lock.
  Désarmé, elles jouent la piste ; en NOTES, elles écrivent au curseur.
- Pas maintenu + touche noire : les fonctions du pas, une famille par noire (spec `2026-11-12-quartz-step-entry-design.md`, section 5).

## 8. Navigation

- Un bouton = un domaine, quatre pages au plus, aucun sous-menu.
- Une page qu'un moteur n'utilise pas est masquée.
- ALGORITHM choisit toujours la piste, PRESET parcourt toujours ce qui est affiché.
- Maintenir un bouton de domaine pose sa couche sur les touches ; le relâcher la retire. Tenu plus de 0,4 s, il affiche la carte de sa couche.

## 9. Écran

### 9.1 Mise en page
- En-tête (30 px) : numéro de piste, nom du moteur ou de la page, page courante.
- Zone de scène (240×166) : dessinée en deux bandes, visée 30 images par seconde.
- Pied (44 px) : les 4 paramètres de la page, valeur et jauge dans la couleur de leur encodeur.

### 9.2 Direction visuelle
Univers arcade rétrofuturiste, inspiré de Rez, sans copier de jeu existant :
- objets en low poly 3D, faces en aplats de trois tons (clair, base, ombre), lumière en haut à gauche ;
- fil de fer fin par-dessus ;
- aucun dégradé, pas de formes rondes ;
- un décor propre à chaque scène ;
- rose réservé aux notes jouées et aux déclenchements ;
- toute note affichée suit la gamme du projet.

### 9.3 Scènes
Une scène animée par famille de moteurs et par source. Chaque encodeur pilote un élément visible de la
scène ; chaque note déclenche un mouvement.

| Scène | Idée | État |
|---|---|---|
| 6-OP A/B/C, 2-OP FM | planète taillée à anneau, vaisseaux en orbite qui tirent à chaque note, sol quadrillé | validée |
| KIT, BD, SNARE, HI-HAT | octaèdre (BD) qui gonfle, cube (SN) qui éclate, tétraèdre (HH) qui pivote, dans un tunnel en fil de fer | validée |
| MARBLES | pachinko, billes icosaèdres, notes de la gamme sous les cases avec petits plots d'historique | validée |
| MODAL | figure de Chladni en relief low poly, posée ; une frappe la fait frémir | validée |
| SPEECH | tête low poly qui parle (celle de `scenes.html`), qui flotte, pupilles néon dans le bandeau des yeux, grille de scan ; HARM teinte la tête selon le modèle de voix, TIMB sa taille, MORPH l'ouverture de la bouche ; à chaque note, deux syllabes, bouche et fil de fer roses, barres de phonèmes (`docs/screens/speech.html`) | validée |
| VA VCF, VA, WAVESHAPE, PHASE DIST | survol d'un relief à facettes dont chaque crête suit la forme d'onde du moteur, décalée d'une crête à l'autre ; un vaisseau tire sur la crête de devant à chaque note ; lune taillée, chaînes lointaines (`docs/screens/analog.html`) | validée |
| WAVETABLE, WAVE TERRAIN | un rider en hoverboard dans un bowl de métal quadrillé de néon : half-pipe dont les parois portent la forme d'onde (WAVETABLE), bowl ayant la forme du terrain, roulé sur le cercle lu (WAVE TERRAIN) ; traînée rose à chaque note, petit kickflip une note sur deux (`docs/screens/wavetable.html`) | validée |
| ADDITIVE, GRAIN FORM | jardin de fleurs de cristal sous le dôme néon d'une station : 16 fleurs, une par harmonique, dont la hauteur et l'ouverture suivent le spectre ; lucioles ; à chaque note, les fleurs ouvertes s'ouvrent davantage, cœur rose et pollen (`docs/screens/spectral.html`) | validée |
| CHORDS, STRING MACH | chorale d'androïdes sur une scène-grille, orgue à tubes néon et rideaux d'hologramme : un androïde par voix, chacun sur une marche aussi haute que sa note (l'accord en escalier), partition en main ; un chef en queue-de-pie, de dos sur son podium, bat la mesure ; à chaque note, bouches et antennes roses, quelques notes s'envolent (`docs/screens/chords.html`) | validée |
| STRING | un acrobate androïde sur une corde laser, sous un chapiteau filaire ouvert sur les étoiles ; à chaque note, il fait un petit saut qui pince la corde sous lui, elle vibre selon HARM (raideur), TIMB (brillance) et MORPH (amortissement) (`docs/screens/string.html`) | validée |
| NOISE, PARTICLE, SWARM | dancefloor néon : les grains allument les dalles (neige continue pour NOISE, éclats épars pour PARTICLE, essaim qui se promène pour SWARM), en violet, cyan ou vert selon leur hauteur ; DJ androïde devant un mur de LED, trois danseurs, boule à facettes ; à chaque note, éclair rose sur les dalles allumées et rayons roses de la boule (`docs/screens/noise.html`) | validée |
| CHIPTUNE | un robot cube saute de bloc en bloc : un bloc par note de l'accord, à sa hauteur, pris dans l'ordre de l'arpège (TIMB), plus étroits quand l'impulsion s'affine (MORPH) ; lune et nuages en voxels ; à chaque note, bloc visé rose, pièce rose qui tourne, score qui avance (`docs/screens/chiptune.html`) | validée |
| GRIDS | les piles sur un sol filaire (spec 5b, section 3 bis ; `docs/screens/backdrops.html`) | validée |

Les maquettes de référence sont dans `docs/screens/` (à ouvrir dans un navigateur) : `scenes.html`, puis une page par famille.

Page TRACKS de HOME (direction 1 de `docs/screens/home.html`) : un socle hexagonal filaire par piste, en léger arc,
devant un soleil taillé en bandes et des crêtes néon sur un sol en fil de fer. Sur chaque socle tourne l'emblème de la
scène de sa piste (planète FM, porte du tunnel du kit, fleur de cristal, androïde de la chorale…). Le bord du socle
porte les pas du pattern : joués en bleu, le pas courant en rose. À chaque note de la piste, l'emblème sautille et un
anneau rose court sur le socle. Une piste coupée n'est plus qu'un fil éteint, immobile. La piste sélectionnée a un
socle cyan. En bas, une colonne par piste : numéro dans la couleur de sa famille, pattern (le prochain clignote,
`P3›4`) et moteur.

Page SEQ (maquette H2 de `docs/screens/sequencer.html`) : les 16 pas de la page en plateau 4×4 isométrique de blocs
low poly (hauteur = vélocité, note sur le dessus, un repère orange pour un pas qui a d'autres attributs), sur un sol
en fil de fer sous un horizon en lignes de fuite. Un viseur verrouille le pas qui joue ; une note fait sauter son
bloc en rose avec une onde au sol et des éclats. Le chemin de lecture se dessine au sol pendant le réglage de PATH.
Pas tenu : le plateau se range à gauche, le bloc tenu se soulève en orange, ses attributs s'affichent à droite.
Une page qui sert à éditer reste lisible d'abord : pas de perspective, une grille droite.

### 9.4 Rendu
Un petit moteur 3D en aplats : rotation, tri des faces, triangles pleins, traits. Moins de 300 faces par
scène. Pas de transparence.

## 10. Architecture

- Application fm1-base en C ; DSP Mutable en C++ derrière de petites fonctions C ; pas de constructeurs
  statiques (placement new dans le pool).
- **Cœur 1** : boutons, écran, horloge du séquenceur, 3 voix, effets, mixage.
- **Cœur 2** : 3 voix.
- L'horloge vit dans le rendu audio ; un bloc est découpé aux instants exacts des notes.
- L'interface envoie ses changements à l'audio par une file sans verrou ; l'audio ne bloque jamais.

| Module | Rôle |
|---|---|
| `seq` | patterns, pas, p-locks, conditions, scènes, chanson, enregistrement |
| `gen` | Grids, Marbles |
| `voices` | pool, attribution, vol de voix, répartition sur les deux cœurs |
| `engines` | interface commune ; Plaits ; plus tard FM 6-op et Rings |
| `mod` | ADSR, LFO, arpégiateur |
| `fx` | filtres, inserts, delay, reverb, compresseur, punch-ins |
| `ui` | pages, couches, saisie, scènes |
| `gfx` | moteur 3D en aplats |
| `store` | projets |

## 11. Sauvegarde
- 8 emplacements de projet dans la zone de données, objets en pool (fm1-base `APP_STORE_POOL`).
- Un pattern = un objet (< 1 Ko : 64 pas × 6 octets + 128 p-locks × 4 octets) ; un projet ≈ 30 Ko.
- La sauvegarde coupe le son un instant (contrainte de la flash) : elle se fait sur SAVE, jamais pendant la lecture.

## 12. Étapes

| Étape | Contenu | Résultat |
|---|---|---|
| 1. Socle | 4 pistes Plaits, pool de 6 voix sur 2 cœurs, STEP simple (note, longueur, division), PLAY/REC, pages EDIT et SEQ, sauvegarde, écran sobre, page CPU | jouer et programmer 4 pistes |
| 2. Séquenceur complet | section 5 entière, patterns, scènes, chanson, enregistrement live | composer un morceau |
| 3. Son | section 4.2 à 4.4 | sculpter et jouer en live |
| 4. Générateurs | GRIDS, MARBLES, KIT | pistes génératives |
| 5. Écran | `gfx`, scènes, animations de HOME | identité visuelle |
| 6. Moteurs | FM 6-op d'origine, puis Rings | palette élargie |

Chaque étape produit un firmware installable et a son propre plan.

## 13. Tests
- **Logique (PC)** : tests C dans le simulateur hôte de fm1-base. On rend N blocs et on vérifie que chaque
  note tombe à l'échantillon prévu (divisions, swing, micro-timing, conditions, ratchets, changements de pattern).
- **Navigation (PC)** : scripts du simulateur (touche, vérification d'état) pour chaque page et couche.
- **Émulateur** (fm1-emu) : démarrage et captures d'écran à chaque étape.
- **Appareil** : une page développeur affiche la charge des deux cœurs et les blocs perdus. Le pire cas
  (6 voix lourdes, tous les effets, scène animée) doit tourner sans bloc perdu pour valider une étape.

## 14. Hors périmètre
- Bluetooth MIDI et audio, MIDI en sortie, horloge MIDI.
- Samples et enregistrement audio.
- Granulaire (Beads ou Clouds) : sous-projet ultérieur, après mesures.
- Second slot d'insert par piste, second LFO.
- Copier-coller de patterns, condition FILL (aucun contrôle libre pour l'activer).
- Plus de 4 pistes.
- Menu de réglages système.
