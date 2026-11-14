# FLEET GATE — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** un réglage GATE (K4 de la page X) qui fixe la longueur des notes de FLEET, de 1/8 à 4 pas.

**Architecture:** un champ `gate` (0..5) dans `q_marbles` ; `marbles_step` écrit `st->len = 1 << gate` huitièmes de
pas ; la page X l'affiche et le règle ; la scène FLEET garde le cristal allumé le temps de la note.

**Tech Stack:** C freestanding (app), simulateur hôte (`app/tests/*.txt`), tests unitaires C (`tests/unit/`).

**Spec:** `docs/superpowers/specs/2026-11-14-quartz-fleet-gate-design.md`

## Global Constraints

- Branche `fleet-gate`, dépôt `quartz/`. Commits en anglais, petits, qui disent ce qui change pour le joueur ; sans
  ligne `Co-Authored-By`.
- `Q_VERSION` 13 → 14 ; anciens projets écartés, jamais migrés.
- Valeurs de GATE : index 0..5 → `1/8`, `1/4`, `1/2`, `1`, `2`, `4` pas = 1, 2, 4, 8, 16, 32 huitièmes ; défaut 2.
- Tout texte affiché a sa ligne dans `app/words.tsv`.
- Tout changement visible ou audible met à jour `app/docs/MANUAL.md` dans le même commit, et refait les captures
  (commande de `CLAUDE.md`) quand un écran change.
- Vérification finale : `tests/run_tests.sh` jusqu'à `all tests ok`, puis `./build.sh` sans erreur (le build vérifie
  la marge du pool).

## Review Focus

- **Piste passée de synthé à KIT** : un `gate` réglé sur synthé ne doit pas changer les notes du KIT (spec : « rien ne
  change » sur un KIT). Décision du plan : sur un KIT, `marbles_step` garde `len` 4 quel que soit `gate`. Test à la
  tâche 1.
- **Projet existant après la mise à jour** : chargé avec `Q_VERSION` 13, il est écarté ; un `gate` hors bornes dans
  un projet 14 est ramené à 5. Test à la tâche 1.
- **Valeur par défaut** : un nouveau projet doit jouer des notes d'un demi-pas, exactement comme avant. Test à la
  tâche 1 (et les tests existants de `gen_test.c` qui attendent `len == 4` doivent passer sans changement d'assertion).
- **GATE court** : à `1/8`, le cristal doit encore clignoter visiblement (un minimum de 40 ms d'allumage). Vérifié à
  la tâche 3 par la capture.
- **Ratchet avec GATE long** : la règle existante de `seq.c` (plus de 8 huitièmes avec un ratchet → 8) s'applique ;
  rien à coder, mais le manuel le dit (tâche 2).

---

### Task 1: Données et longueur des notes

**Files:**
- Modify: `app/project.h:11` (`Q_VERSION`), `app/project.h:42-43` (`q_marbles` et son commentaire)
- Modify: `app/seq.c:42` (défaut), `app/store.c:107-108` (bornage), `app/gen.c` `marbles_step` (`st->len`)
- Test: `tests/unit/gen_test.c`, `tests/unit/store_test.c`

**Interfaces:**
- Produces: `q_marbles.gate` (`uint8_t`, 0..5, dernier champ de la structure, après `notes[2]`). La longueur d'une
  note FLEET synthé vaut `1 << gate` huitièmes de pas.

- [ ] **Step 1: Mettre à jour les initialiseurs positionnels de `q_marbles` dans les tests**

`tests/unit/gen_test.c:153` (`msetup`) : ajouter `, 2` après `{ 0, 0 }`. `tests/unit/store_test.c:265` : ajouter
`, 4` après `{ 0x55, 0x0A }` et compléter l'assertion d'aller-retour qui suit avec `q->marbles.gate == 4` si elle
compare champ par champ.

- [ ] **Step 2: Écrire les tests qui échouent**

Dans `gen_test.c`, après `marbles_dejavu_mid_loops` :

```c
/* GATE: a synth note lasts 1 << gate eighths of a step; a KIT's stay at 4 */
static void marbles_gate_sets_len(void)
{
    static const uint8_t LEN[6] = { 1, 2, 4, 8, 16, 32 };
    for (int g = 0; g < 6; g++) {
        q_pattern *pt = msetup(8, T_COMPLEMENTARY_BERNOULLI);
        pt->marbles.gate = (uint8_t)g;
        for (int k = 0; k < 16; k++) {
            q_step st; int acc;
            if (played(pt, k, &st, &acc)) assert(st.len == LEN[g]);
        }
    }
    q_pattern *pt = msetup(M_KIT, T_COMPLEMENTARY_BERNOULLI);
    pt->marbles.gate = 5;
    q_step st; int acc;
    assert(played(pt, 0, &st, &acc) && st.len == 4);
    project_defaults(&P);
    assert(P.pat[1][3].marbles.gate == 2);
}
```

(`msetup` met `tout` à 2 : chaque pas joue.) L'appeler dans `main`.

Dans `store_test.c`, `store_clamps_v3` : `b.pat[0][2].marbles.gate = 9;` avant `write_raw`, et
`assert(a.pat[0][2].marbles.gate == 5);` après `store_load`.

- [ ] **Step 3: Vérifier l'échec**

Run: `tests/run_tests.sh`
Expected: échec de compilation de `gen_test.c` (`q_marbles` n'a pas de membre `gate`).

- [ ] **Step 4: Implémenter**

- `project.h` : `gate` en fin de `q_marbles` ; commentaire : `gate 0..5 (1/8 to 4 steps: 1 << gate eighths)`.
  `Q_VERSION` 14.
- `seq.c:42` : `, 2` en fin d'initialiseur.
- `store.c` : `m->gate = (uint8_t)clampi(m->gate, 0, 5);` avec les autres bornes de `m`.
- `gen.c` `marbles_step` : `st->len` vaut `4` sur un KIT (`p->sound[track].model == M_KIT`), sinon
  `(uint8_t)(1 << m->gate)`.

- [ ] **Step 5: Vérifier**

Run: `tests/run_tests.sh`
Expected: `all tests ok` (les assertions existantes `st.len == 4` de `gen_test.c` passent sans modification).

- [ ] **Step 6: Commit**

```bash
git add app/project.h app/seq.c app/store.c app/gen.c tests/unit/gen_test.c tests/unit/store_test.c
git commit -m "FLEET: a gate per pattern sets its notes' length, 1/8 to 4 steps (default 1/2, as before)"
```

### Task 2: Le réglage GATE sur la page X

**Files:**
- Modify: `app/ui.c` : `PAGE[]` ligne `"X"` (`:56`), lecture (`:379-381`), écriture (`:562-564`), texte des valeurs
  (bloc vers `:2155`), lecteur d'état (vers `:3355`), une table `GATE_NAME` près de `DIV_NAME` (`:107`)
- Modify: `app/words.tsv`, `app/docs/MANUAL.md` (section FLEET, `:749`), `app/tests/manual_shots.txt` (`:224`)
- Create: `app/tests/fleet_gate.txt`

**Interfaces:**
- Consumes: `q_marbles.gate` (tâche 1).
- Produces: lecteur d'état `mgate` (valeur brute 0..5 du motif courant).

- [ ] **Step 1: Écrire le script simulateur qui échoue**

`app/tests/fleet_gate.txt`, sur le modèle de `app/tests/marbles.txt` pour atteindre la page FLEET d'une piste
synthé (SEQ, SOURCE sur FLEET avec `spin K4`, puis `spin SELECT` jusqu'à X). Puis :

```
expect mgate 2
spin K4 1
expect mgate 3
spin K4 5
expect mgate 5
spin K4 -6
expect mgate 0
```

(`mgate` rend la valeur brute 0..5, comme `len` et `poly`.) Ajouter enfin : piste passée en KIT (`spin` du MODEL sur la page SOUND,
comme `kit.txt`), `expect` que la page X n'est plus atteignable (le nombre de pages de FLEET sur un KIT, déjà testé
ailleurs : reprendre le même `expect`).

- [ ] **Step 2: Vérifier l'échec**

Run: `./build.sh --host && app/build/host/quartz_host app/tests/fleet_gate.txt app/build/host`
Expected: échec sur `expect mgate` (lecteur inconnu).

- [ ] **Step 3: Implémenter**

- `PAGE[]` : `{"X", {"SPREAD", "X BIAS", "STEPS", "GATE"}, {0, 0, 0, 0}, {127, 127, 127, 5}}`.
- `case P_X * 4 + 3` en lecture et en écriture de `t->marbles.gate`.
- `static const char *const GATE_NAME[6] = {"1/8", "1/4", "1/2", "1", "2", "4"};` et, dans le texte des valeurs,
  `pid == P_X && k == 3` copie `GATE_NAME[v]`, comme le bloc `SRC_NAME`.
- Lecteur `mgate` à côté de `len`.
- `words.tsv` : `1/8	one eighth`, `1/4	one quarter`, `1/2	one half` (s'ils manquent).

- [ ] **Step 4: Manuel et capture**

- `MANUAL.md` : la ligne X devient `| X | SPREAD | X BIAS | STEPS | GATE, 1/8 to 4 steps |` ; une puce après celle de
  **X** : « **GATE** is how long each note lasts, in steps (1/2 by default). Past one step, the notes overlap: they
  ring together up to POLY. A ratcheted note stays within one step. »
- `manual_shots.txt` : après `shot source-fleet-t.png`, `spin SELECT 1`, `wait 300`, `shot source-fleet-x.png`, puis
  ajuster le `spin SELECT 2` suivant en `spin SELECT 1` pour arriver toujours sur NOTES.
- `MANUAL.md` : l'image `![FLEET's X page: SPREAD, X BIAS, STEPS and GATE 1/2](img/source-fleet-x.png "X")` après
  celle de la page T.
- Refaire les captures (commande de `CLAUDE.md`) ; seule `source-fleet-x.png` (nouvelle) doit apparaître dans
  `git status` sous `app/docs/img/`.

- [ ] **Step 5: Vérifier**

Run: `tests/run_tests.sh`
Expected: `all tests ok`.

- [ ] **Step 6: Commit**

```bash
git add app/ui.c app/words.tsv app/tests/fleet_gate.txt app/tests/manual_shots.txt app/docs/MANUAL.md app/docs/img/source-fleet-x.png
git commit -m "FLEET: X page's K4 sets GATE, the notes' length"
```

### Task 3: La scène suit GATE

**Files:**
- Modify: `app/scene.h:78-79` (`q_mview`), `app/scene.c` `marbles_frame` (cristaux `:824-834`, noms `:931`),
  `app/ui.c:2883` (remplissage de `mv`)
- Modify: `app/docs/MANUAL.md` (description de la scène FLEET)

**Interfaces:**
- Consumes: `q_marbles.gate`.
- Produces: `q_mview.gate` (`uint8_t`, après `len`).

- [ ] **Step 1: Implémenter**

- `q_mview` : `gate` après `len` ; `ui.c` le remplit avec `t->marbles.gate`.
- `marbles_frame` : `lit = (1u << gate) * step_ms / 8u`, au moins 40 ms. Le nom du cristal est vif (`hot`) tant que
  `h < lit` (au lieu de 150) ; le cristal garde la couleur `PINK3` tant que `h < max(lit, 450)` (au lieu de 450).
  L'animation d'éclats et de croissance ne change pas.

- [ ] **Step 2: Vérifier à l'œil**

Run : un script jetable (dans le scratchpad, pas dans le dépôt) qui joue FLEET sur une piste synthé, `shot` à
GATE `1/8` puis à `4`, ~300 ms après un tir. Expected : à `4`, le cristal touché et son nom restent roses ; à `1/8`,
non.

- [ ] **Step 3: Manuel**

Dans la description de la scène FLEET (près de `img/source-fleet.png`), ajouter : « a crystal stays lit for as long
as its note lasts ».

- [ ] **Step 4: Vérifier**

Run: `tests/run_tests.sh && ./build.sh`
Expected: `all tests ok`, puis le build appareil sans erreur ni dépassement du pool.

- [ ] **Step 5: Commit**

```bash
git add app/scene.h app/scene.c app/ui.c app/docs/MANUAL.md
git commit -m "FLEET scene: a hit crystal stays lit for its note's length"
```
