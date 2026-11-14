# QUARTZ — FLEET : GATE, la longueur des notes

Un réglage de longueur pour les notes de FLEET (`SRC_MARBLES`). Spec globale `2026-11-10-quartz-groovebox-design.md`,
section des sources de motifs.

## 1. But

FLEET écrit chaque note avec une longueur fixe d'un demi-pas (`st->len = 4` huitièmes de pas, `gen.c`
`marbles_step`). Les nappes et les notes liées sont donc impossibles, tout comme les notes très sèches. GATE
règle cette longueur.

Critères :
- GATE se règle sur la page X (K4, aujourd'hui libre) : `1/8`, `1/4`, `1/2`, `1`, `2`, `4` pas ;
- par défaut `1/2` : un nouveau projet sonne exactement comme aujourd'hui ;
- au-delà d'un pas, les notes se chevauchent : avec POLY > 1, elles se tiennent en accord ; en POLY 1, la note
  suivante reprend la voix ;
- tourner GATE change visiblement la scène.

Hors périmètre : une part de hasard sur la longueur (un seul bouton libre) ; GATE sur un KIT (la page X n'existe pas
sur un KIT, et les pads sont percussifs) ; un p-lock de GATE (FLEET n'a pas de pas à verrouiller).

## 2. Données

- `q_marbles` gagne `gate`, 0..5, à la fin de la structure : la longueur vaut `1 << gate` huitièmes de pas (1, 2, 4, 8,
  16, 32). `Q_VERSION` 13 → 14 : les anciens projets sont écartés.
- `project_defaults` (`seq.c`) : `gate` 2. Le chargement (`store.c`) borne `gate` à 0..5, comme les autres champs de
  `q_marbles`.
- Pool : 4 pistes × 8 motifs × 1 o = 32 o, à vérifier au build sous la marge (`fm1.toml` `pool_spare`).

## 3. Le son

- `marbles_step` écrit `st->len = (uint8_t)(1 << m->gate)` à la place de `4`, sur une piste synthé comme sur un KIT
  (sur un KIT, `gate` reste à sa valeur par défaut : rien ne change).
- Une note répétée en ratchet garde la règle existante du séquenceur (`seq.c` : plus de 8 huitièmes avec un ratchet
  ramène la longueur à 8).
- Aucun code audio ne change.

## 4. L'interface

- `PAGE[]` (`ui.c`), page X : `{"SPREAD", "X BIAS", "STEPS", "GATE"}`, K4 de 0 à 5. Lecture et écriture de
  `t->marbles.gate` dans les deux `switch` des pages, comme LEN sur la page FLEET.
- Valeur affichée : `1/8`, `1/4`, `1/2`, `1`, `2`, `4`, par une table de noms dans `ui.c`, à côté des autres tables de
  valeurs.
- Scène FLEET : le cristal touché par un tir reste allumé (sa couleur vive) pendant toute la durée de la note,
  `gate` × la durée d'un pas / 8, au lieu du flash fixe de 150 ms d'aujourd'hui. `q_mview` reçoit `gate` à côté de
  `len`, et `step_ms` y est déjà.
- `words.tsv` : `GATE` existe déjà (page ARP) ; ajouter `1/8`, `1/4` et `1/2` (« one eighth », « one quarter »,
  « one half »).
- Lecteur d'état du simulateur (`ui.c`) : `mgate`.

## 5. Manuel

`MANUAL.md`, section FLEET :
- la ligne X du tableau : K4 **GATE**, 1/8 à 4 pas ;
- une phrase : GATE est la longueur de chaque note, en pas ; au-delà d'un pas, les notes se tiennent les unes sur les
  autres si POLY le permet ;
- la description de la scène : le cristal reste allumé le temps de la note.

Le manuel n'a pas de capture de la page X : `manual_shots.txt` en ajoute une, `source-fleet-x.png`, après
`source-fleet-t.png`, et la section FLEET l'affiche sous celle de la page T. Les captures suivantes sont refaites,
même si elles ne changent pas.

## 6. Tests

- `tests/unit/gen_test.c` : un motif FLEET synthé, `tout` 2 (chaque pas joue) : `gate` 2 donne `len` 4 ; `gate` 0
  donne 1 ; `gate` 5 donne 32 ; `project_defaults` met `gate` à 2.
- `tests/unit/store_test.c` : un `gate` hors bornes est ramené à 5 au chargement.
- Script simulateur `app/tests/fleet_gate.txt` : page X d'une piste synthé, `turn K4`, `expect mgate` ; sur un KIT,
  la page X n'apparaît pas.
- `tests/run_tests.sh` jusqu'à « all tests ok » ; build appareil.
