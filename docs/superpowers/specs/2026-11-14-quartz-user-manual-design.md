# QUARTZ — le manuel utilisateur et le CLAUDE.md du projet

Un guide utilisateur complet du firmware, en anglais, publiable sur GitHub Pages et tenu à jour avec le code ; et un
`CLAUDE.md` de projet qui porte les conventions, dont la règle de mise à jour du guide. Inspiration de forme : le guide
OP-Z de teenage engineering (https://teenage.engineering/guides/op-z).

## 1. But

Qu'un possesseur de FM-1 installe QUARTZ et en tire tout, sans lire le code ni les specs.

Critères :
- chaque page, bouton, source, modèle et fonction de pas visible à l'écran est décrit, sous le nom affiché ;
- chaque idée empruntée (Mutable Instruments, autres séquenceurs, théorie) est nommée et liée, avec ce que QUARTZ en
  change ; les descriptions s'appuient sur la doc d'origine lue, pas sur la mémoire ;
- les images sont de vraies captures de l'écran, produites par le simulateur ;
- le site se génère avec les outils de `fm1-base`, sans dépendance nouvelle ;
- le `CLAUDE.md` impose la mise à jour du guide dans le même commit que tout changement visible.

Hors périmètre : la publication effective sur `gh-pages` (`base/tools/publish_site.sh`, quand l'utilisateur le
décidera), un style de site propre à QUARTZ (le style par défaut de `fm1-base` reste), un test automatique de
couverture du guide, une traduction française, l'ajout de MIMIC, DRIFT et LISTEN à `app/words.tsv` (point séparé).

## 2. Chaîne de publication

On réutilise celle de `fm1-base` :
- `app/docs/MANUAL.md` : le manuel, un seul fichier Markdown ; `base/tools/manual_html.py` le convertit (sections
  numérotées, sommaire lié, tableaux, figures, `<details>`). On n'écrit que ce que ce convertisseur gère.
- `app/docs/img/` : ses images ; les captures d'écran prennent la forme `figure.shot` (300 px, pixelisée).
- `app/site/landing.md` : la page d'accueil courte (ce qu'est QUARTZ, liens installeur, émulateur, manuel) ;
  `{{VERSION}}` et `{{PKG}}` y sont remplis par `base/tools/make_site.py`.
- `make_site.py` assemble aussi l'installeur web, l'émulateur, les mots du lecteur d'écran ; il lit `name`,
  `tagline`, `repo`, `licence`, `credits` dans `app/fm1.toml`, à compléter s'il en manque.

Vérification : `make_site.py` produit le site dans `app/build/site` et le manuel s'ouvre sans lien ni image cassés.

## 3. Captures

`app/tests/manual_shots.txt`, un script du simulateur comme les autres de `app/tests/`, met l'app dans chaque état
illustré et prend une capture (`shot <nom>.png`) ; les captures sont copiées dans `app/docs/img/`. Une capture par
page décrite au moins, une par scène de source et de modèle quand elle aide. Après un changement d'écran on relance le
script et on commite les images changées.

## 4. Plan du manuel

Ton du guide OP-Z : sections courtes, tableaux « contrôle → effet », présent, deuxième personne, noms à l'écran tels
quels (`ATLAS`, `K1`). Ce qui vient d'ailleurs est noté en fin de sous-section par une ligne **Inspired by** avec lien.

1. **Overview** : ce qu'est QUARTZ (4 pistes, 8 patterns, scènes, song) ; vocabulaire (track, pattern, page, source,
   scene).
2. **Install** : installeur web, émulateur, retour au firmware d'origine.
3. **The FM-1 at a glance** : boutons, encodeurs, touches ; l'anatomie de l'écran (bandeau, scène, pied K1–K4).
4. **Quick start** : un premier beat en deux minutes.
5. **Tracks, patterns, song** : HOME (TRACKS, SONG), mute, copie, nouveau projet.
6. **Sound** : EDIT (ENGINE, SOUND, PAD, KIT) ; les 21 modèles, BASS DRUM, SNARE, HI-HAT et KIT, chacun avec une
   ligne « ce que ça sonne » et son modèle Plaits d'origine.
7. **Shaping** : ENV (ADSR, FILTER), LFO, ARP, FX (INSERT, MIX, FX), punch-ins.
8. **Sequencer** : STEPS, PATH, XY ; l'enregistrement (GRID, NOTES, LIVE, ARM) ; les fonctions de pas (p-locks,
   RATCHET, VEL, PROB, COND, NOTE, LEN, MICRO, SLIDE, ACCENT). Absorbe `docs/manual/sequencer-entry.html`, qu'on
   supprime.
9. **Sources** : STEP, ATLAS, FLEET, COLONY, LOOM, CADENCE, LATTICE, MOONS, MIMIC ; même gabarit pour chacune : l'idée
   en deux phrases, la capture de sa scène, le tableau de ses pages, ce que fait PRESET, **Inspired by**.
10. **Global** : TEMPO, SCALE, KEYS, GFX, CPU.
11. **Saving** : sauvegarde, projets, sauvegarde depuis le navigateur si `fm1.toml` l'active.
12. **Credits and inspirations** : la liste complète, liens et licences.
13. **Troubleshooting**.

## 5. Recherche des inspirations

Avant d'écrire une sous-section, lire la doc d'origine correspondante :

| QUARTZ | Doc à lire |
|---|---|
| Modèles SWEEP … CHLADNI, BASS DRUM, SNARE, HI-HAT | manuel Plaits (Mutable Instruments) |
| ATLAS | manuel Grids |
| FLEET (T, X, DEJA VU) | manuel Marbles |
| Reverb | Clouds (README : la reverb de Clouds) |
| COLONY | le Jeu de la vie de Conway |
| LOOM | Kria (monome, Ansible) |
| LATTICE | Tonnetz et opérations néo-riemanniennes P, L, R |
| MIMIC | Continuator (F. Pachet, 2003) |
| CADENCE, MOONS | chercher une origine ; à défaut, présentées comme propres à QUARTZ |
| p-locks, trig conditions, MICRO | manuel Digitakt (Elektron) |
| Punch-ins, forme du guide | guide OP-Z |
| Plateforme | fm1-base, M-VAVE FM-1 |

Le guide décrit le comportement de QUARTZ ; l'original n'est que cité. Le comportement se vérifie dans le code
(`app/ui.c`, `app/gen.c`, `app/seq.c`…) et les specs, pas déduit de l'original. Un écart avec l'original est dit en
une ligne.

## 6. Le CLAUDE.md du projet

`CLAUDE.md` à la racine du dépôt, moins de 150 lignes, en anglais, règles au présent, sans historique :
- **Contexte** : QUARTZ, le FM-1 (deux cœurs, 360 MHz), `fm1-base` en sous-module, Plaits/Grids/Marbles/Clouds
  vendorés, GPL-3.0.
- **Architecture** : une ligne par fichier clé de `app/` (`ui.c` pages et noms affichés, `gen.c` sources, `seq.c`,
  `scene_*.c`, `audio.c`/`voices.c`, `store.c`, `project.h`, `words.tsv`…).
- **Commandes** : build, simulateur, tests, installation sur le device (`fm1_install.py --yes --classic`, son venv),
  génération du site.
- **Conventions** : texte à l'écran en anglais, chaque mot dans `words.tsv` ; noms affichés propres à QUARTZ,
  identifiants du code aux noms d'origine ; chaque encodeur d'une page de scène change visiblement la scène ; PRESET
  utile pour chaque source ; `Q_VERSION` monte sans migration tant que le firmware n'est pas fini ; specs et plans dans
  `docs/superpowers/`.
- **Device** : `base/include/fastmath.h` dans les chemins audio ; le CPU se mesure sur le device, jamais sur PC, en
  fin de cycle, consigné dans `docs/cpu.md`.
- **Manuel** : tout changement visible par l'utilisateur met à jour `app/docs/MANUAL.md` et ses captures dans le même
  commit.
- **Index des docs** : une ligne par doc (`docs/cpu.md`, le manuel, les specs).

Les règles reprises dans le `CLAUDE.md` sont retirées de la mémoire de l'assistant.

## 7. Organisation

Branche `manual` depuis `main` (07ba2f2, MIMIC fusionné), worktree `quartz-naming`. Ni push ni publication sans
accord.
