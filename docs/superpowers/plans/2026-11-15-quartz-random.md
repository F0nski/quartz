# QUARTZ RANDOM — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** REC tenu + noire 6 / 7 / 8 tire au hasard la page affichée, tout le domaine (et le contenu de la source en
SEQ) ou le MODEL.

**Architecture :** tout dans `app/ui.c` : un xorshift local, `dice_page(pid)` qui tire les encodeurs d'une page par
`knob_set`, `dice_all()` qui parcourt `pages(ui.domain, …)` puis le contenu de la source, et le geste dans la branche
« REC tenu » de `key_down`. Le message d'en-tête et la carte de REC suivent les mécanismes existants.

**Tech Stack :** C (unity build), simulateur hôte (`app/tests/*.txt`).

**Spec :** `docs/superpowers/specs/2026-11-15-quartz-random-design.md`

## Global Constraints

- Branche `random` ; commits petits, qui disent ce qui change pour le joueur ; identité du dépôt (noreply), pas de ligne
  `Co-Authored-By`.
- Aucun changement de `q_project` : `Q_VERSION` ne bouge pas.
- Jamais tirés : les encodeurs libellés `SOURCE`, `MODEL`, `LEVEL`, `TEMPO`, et POLY de la page SOUND (pas celui de
  COLONY) ; tout le domaine HOME ; la page CPU ; les encodeurs sans libellé ou `knob_hidden`.
- Tout texte affiché est en anglais et a sa ligne dans `app/words.tsv` : `RANDOM PAGE`, `RANDOM ALL`, `RANDOM MODEL`,
  `RND PAGE`, `RND ALL`, `RND MODEL`.
- Le manuel (`app/docs/MANUAL.md`) change dans le même commit que le geste visible (tâche 3), captures refaites.
- `tests/run_tests.sh` se termine par `all tests ok` à la fin de chaque tâche.

## Review Focus

1. Encodeur qui choisit (HEX HARM) : la valeur tirée est le milieu d'un choix, comme après un cran. Test tâche 1 :
   sur HEX A, noire 6, noter `harm` (figé, simulateur déterministe), `turn K1 1` puis `turn K1 -1` : `harm` revient à
   la même valeur (d'une valeur hors milieu, l'aller-retour tomberait sur un milieu différent).
2. Noire 8 : le MODEL tiré est toujours différent de l'actuel (sinon le geste paraît mort) ; un synthé peut devenir KIT
   et l'inverse. Test tâche 1 (voir l'étape : `model` figé ≠ 8).
3. Noire 7 avec PATH tiré à XY : la page XY apparaît en cours de route ; `dice_all` recompte `pages()` à chaque tour de
   boucle, donc la tire aussi. Test tâche 2 (bornes de `xlen`, `ylen` après la noire 7).
4. Curseurs rendus : après la noire 7, `chcur`, `tzcur` et `moon` reviennent à leur valeur d'avant, puis sont ramenés
   dans la nouvelle longueur par les gardes existantes de `ui_input`. Test tâche 2.
5. REC relâché après le dé : REC ne s'arme pas (règle de l'appui court) et le projet est `dirty`. Test tâche 1.

---

### Task 1 : le tirage d'une page (noire 6) et du MODEL (noire 8)

**Files :**
- Modify : `app/ui.c` (struct `ui` vers la ligne 210 ; avant `/* ---- input --- */` vers la ligne 708 ; `key_down`
  branche REC tenu vers la ligne 1443 ; `view_now` ligne 2348 ; `draw_head` ligne ~2572 ; `ui_expect`)
- Modify : `app/words.tsv`
- Create : `app/tests/random.txt`

**Interfaces :**
- Produces :
  - `static uint32_t dice_rand(uint32_t n)` : 0 .. n − 1, xorshift32 sur `ui.rng`, état mêlé à `plat_ms()` à chaque
    appui du dé (`ui.rng ^= plat_ms() | 1` puis jamais 0).
  - `static void dice_page(int pid)` : tire K1–K4 de la page.
  - `static int dice_on(int b)` : b = 5, 6 ou 7 (indice dans `BLACK`) ; 1 si la touche agit maintenant (sert aussi à
    allumer la carte, tâche 3).
  - champs `ui.rng` (`uint32_t`), `ui.rnd_until` (`uint32_t`), `ui.rnd_kind` (`uint8_t`, 1 page, 2 all, 3 model).
  - clé `expect msg_random` : `ui.rnd_kind` si `plat_ms() < ui.rnd_until`, sinon 0.

- [ ] **Step 1 : écrire `app/tests/random.txt` (première partie)**

Gestes : `press REC`, `key b5 down`, `key b5 up`, `release REC` (noire 6 = `b5`, noire 7 = `b6`, noire 8 = `b7`).
Commencer par `wait 600`. Cas, chacun avec ses `expect` :

```
# noire 6 sur ENGINE : HARM tiré, borné ; MODEL, LEVEL, POLY intacts ; message ; REC pas armé ; projet modifié
tap EDIT
spin K1 127                 # HARM à 127 : un tirage le fait (presque sûrement) descendre ; le simulateur est déterministe
<REC + b5>
expect harm <127
expect timb <128
expect model 8
expect level 100
expect poly 1
expect msg_random 1
expect rec 0
expect dirty 1
# noire 6 sur SOUND : TUNE borné, MODEL et POLY intacts
turn SELECT 1
<REC + b5>
expect tune >-25
expect tune <25
expect model 8
expect poly 1
# noire 8 sur EDIT : un autre MODEL
<REC + b7>
expect msg_random 3
```

Pour « un autre MODEL » : `expect model` n'a qu'égalité et bornes ; écrire `expect model <8` **ou** `>8` selon ce que
donne le simulateur déterministe (lancer une fois, noter, figer), avec un commentaire qui le dit. Puis :

```
# HEX A (PRESET jusqu'à HEX A sur EDIT) : noire 6, puis un cran aller-retour sur K1 rend la même HARM
turn PRESET <jusqu'à model 2>
<REC + b5>
expect harm <valeur figée>
turn K1 1
turn K1 -1
expect harm <la même>
# noire 8 hors EDIT : rien
tap ENV
<REC + b7>
expect msg_random 0
# HOME : rien
tap HOME
<REC + b5>
expect msg_random 0
expect level 100
# un pas tenu sur la grille : rien (les encodeurs y sont des p-locks)
tap SEQ
tap REC                     # GRID
key w0 down
<REC + b5>                  # avec le pas tenu
expect msg_random 0
key w0 up
tap REC
tap REC                     # OFF
# LIVE : REC tenu + noire 6 efface la note, le dé ne tire pas
```

Pour LIVE, reprendre le début de `app/tests/live_erase.txt` (un pas enregistré en LIVE sur la note de `b5`), puis
`<REC + b5>` pendant la lecture : `expect msg_random 0` et la note effacée comme dans ce test.

- [ ] **Step 2 : lancer, vérifier l'échec**

Run : `./build.sh --host && app/build/host/quartz_host app/tests/random.txt app/build/host`
Expected : échec sur `expect harm <127` (clé `msg_random` inconnue ou dé absent).

- [ ] **Step 3 : implémenter dans `app/ui.c`**

- `dice_page(pid)` : pour k de 0 à 3, sauter si `!PAGE[pid].label[k]`, `!*PAGE[pid].label[k]`, `knob_hidden(pid, k)`,
  libellé `SOURCE` / `MODEL` / `LEVEL` / `TEMPO` (`strcmp`), ou `pid == P_SOUND && k == 3`. Sinon
  `v = min + dice_rand(max − min + 1)`. Si `knob_zone(model, pid, k, &start, &width)` donne n > 0 et `v >= start` :
  `v = round(start + (knob_choice(v, start, width, n) + 0.5) * width)` (sous `start`, la valeur reste). Puis
  `knob_set(pid, k, v)`. `P_SONG` n'est jamais atteint (HOME exclu).
- Noire 8 : rang courant `r = model_rank(model)`, `v = (r + 1 + dice_rand(M_KIT)) % (M_KIT + 1)` (toujours un autre,
  KIT compris), `knob_set(kit() ? P_KIT : P_SOUND, 0, v)`.
- `dice_on(b)` : 0 si `ui.domain == D_HOME`, `ui.rec == R_LIVE`, `ui.held`, `ui.sel_ref >= 0` ou `ui.cell` ; pour
  b = 7, 1 seulement si `ui.domain == D_EDIT` ; sinon 1.
- `key_down`, dans `if (ui.btn >> B_REC & 1u)`, **avant** `if (ui.rec == R_ARM) return;` (le dé marche en armement) :
  si `k` est `BLACK[5]`, `BLACK[6]` ou `BLACK[7]` : si `dice_on`, mêler `plat_ms()` à `ui.rng`, tirer (la noire 7
  appelle `dice_all`, tâche 2 : pour l'instant elle ne fait rien), poser `ui.rnd_kind`, `ui.rnd_until = plat_ms() +
  MSG_MS`, `ui.dirty = 1` ; `return` dans tous les cas (la branche LIVE au-dessus n'est pas touchée).
- `view_now` : `head.msg` 4, 5, 6 pour `rnd_kind` 1, 2, 3 tant que `rnd_until` court, après les trois messages
  existants ; `draw_head` : `RANDOM PAGE`, `RANDOM ALL`, `RANDOM MODEL`.
- `ui_expect` : `msg_random`.
- `app/words.tsv` : `RANDOM PAGE	random page`, `RANDOM ALL	random all`, `RANDOM MODEL	random model`.

- [ ] **Step 4 : lancer, vérifier que ça passe**

Run : `app/build/host/quartz_host app/tests/random.txt app/build/host` puis `tests/run_tests.sh`
Expected : le script sort 0 ; `all tests ok`.

- [ ] **Step 5 : commit**

```bash
git add app/ui.c app/words.tsv app/tests/random.txt
git commit -m "RANDOM: REC held + black key 6 draws the page's knobs, black key 8 another model"
```

---

### Task 2 : tout le domaine et le contenu des sources (noire 7)

**Files :**
- Modify : `app/ui.c` (à côté de `dice_page`)
- Modify : `app/tests/random.txt`

**Interfaces :**
- Consumes : `dice_rand`, `dice_page`, `dice_on` (tâche 1) ; `pages`, `step_clear`, `step_default`, `step_on`,
  `gen_scale`, `lanes_set`, `LANE_MAX`, `KIT_NOTE`, `kit()`.
- Produces : `static void dice_all(void)`, appelée par la noire 7 de `key_down`.

- [ ] **Step 1 : ajouter les cas à `app/tests/random.txt`**

```
# EDIT : ENGINE et SOUND tirés ; MODEL et POLY intacts
# ENV : ADSR et FILTER bornés (atk, rel <128 ; ftype <3 ; envamt >-65 <64)
# FX : MIX tiré, LEVEL intact (expect level 100) ; itype inchangé
# GLO : TEMPO intact (expect tempo 120) ; swing >49 <76
# SEQ STEP (piste 1 par défaut) : src 0 ; locks 0 ; nsteps_on >0 ; len >0 <65 ;
#   les pas allumés ont stepnote_N dans 48..72 et stepvel_N dans 60..127 (vérifier quelques N allumés : lire
#   nsteps_on puis les premiers pas, figer selon le simulateur déterministe) ; xlen et ylen bornés si path 7
# un p-lock posé avant (comme clear.txt) a disparu : expect locks 0
# SEQ CHORDS : chcur placé à 2 avant ; après : cdeg_1..cdeg_8 <7, csize_N <CS_N ; chcur 2 (ou clen - 1 si plus court)
# SEQ TONNETZ : top_1..top_8 <4 ; tzcur rendu
# SEQ MOONS : oper_1..oper_4 >0 <33, opha_N <32 ; osel rendu (borné par omoons)
# SEQ LANES : lv_<lane>_<cell> dans 0..LANE_MAX (lv_0_N <2, lv_1_N <16, lv_2_N <5, lv_3_N <5, lv_4_N <4)
```

Chaque source se choisit par la page STEPS (K4 SOURCE), comme dans les tests existants (`chords.txt`, `tonnetz.txt`,
`orbits.txt`, `lanes.txt`) ; puis `<REC + b6>` et `expect msg_random 2`.

- [ ] **Step 2 : lancer, vérifier l'échec**

Run : `./build.sh --host && app/build/host/quartz_host app/tests/random.txt app/build/host`
Expected : échec sur le premier cas EDIT (la noire 7 ne tire rien).

- [ ] **Step 3 : implémenter `dice_all()`**

- Pages : `for (i = 0; i < pages(ui.domain, id); i++) dice_page(id[i]);`, `pages` relu à chaque tour (PATH peut faire
  apparaître XY). Sur une page CHORD, STEP (`P_TZSTEP`) ou MOON/MOONK, la boucle la saute : le contenu ci-dessous la
  couvre.
- Puis, si `ui.domain == D_SEQ`, selon `pat()->src` :
  - `SRC_STEP` : `step_clear` sur les `Q_STEPS` pas ; densité `d = 25 + dice_rand(51)` % ; pour n de 0 à `len − 1`,
    si `dice_rand(100) < d` : `step_default`, `note[0]` = KIT : `KIT_NOTE + dice_rand(16)`, sinon une note tirée parmi
    celles de `gen_scale(ui.p)` dans 48..72 (compter les notes de la gamme dans l'intervalle, en tirer une), `vel =
    60 + dice_rand(68)`, `step_on`.
  - `SRC_CHORDS` : sauver `ui.chcur`, `dice_page(P_CHORD)` pour chcur de 0 à 7, rendre.
  - `SRC_TONNETZ` : idem `ui.tzcur`, `P_TZSTEP`, 0 à 7.
  - `SRC_ORBITS` : idem `ui.moon`, `kit() ? P_MOONK : P_MOON`, de 0 à `orbits.moons − 1`.
  - `SRC_LANES` : pour chaque lane (sauf `L_OCT` sur un KIT) et cellule 0..15, `lanes_set(l, lane, c,
    dice_rand(LANE_MAX[lane] + 1))`.
- La noire 7 de `key_down` appelle `dice_all()`.

- [ ] **Step 4 : lancer, vérifier que ça passe**

Run : `app/build/host/quartz_host app/tests/random.txt app/build/host` puis `tests/run_tests.sh`
Expected : le script sort 0 ; `all tests ok`.

- [ ] **Step 5 : commit**

```bash
git add app/ui.c app/tests/random.txt
git commit -m "RANDOM: REC held + black key 7 draws every page of the domain, and in SEQ the source's content"
```

---

### Task 3 : la carte de REC et le manuel

**Files :**
- Modify : `app/ui.c` (`layer_keys` case `B_REC` vers la ligne 1930 ; `draw_layer` case `B_REC` vers la ligne 2735)
- Modify : `app/words.tsv`, `app/docs/MANUAL.md`, `app/tests/random.txt`, `app/tests/manual_shots.txt` si la carte de
  REC y est capturée, `app/docs/img/*.png` changées

**Interfaces :**
- Consumes : `dice_on(b)` (tâche 1).

- [ ] **Step 1 : ajouter à `app/tests/random.txt`**

```
# la carte de REC : noires 1, 2, 6, 7 allumées sur ENV ; 6, 7, 8 en plus sur EDIT ; HOME : 1 et 2 seulement
tap ENV
press REC
wait 500
expect layer <B_REC + 1>     # la valeur de ui.layer pour REC, comme dans layers.txt
expect leds_lit <la somme attendue>   # comme layers.txt lit les touches allumées de la carte
release REC
```

Reprendre la forme exacte des vérifications de carte de `app/tests/layers.txt`.

- [ ] **Step 2 : lancer, vérifier l'échec**

Run : `./build.sh --host && app/build/host/quartz_host app/tests/random.txt app/build/host`
Expected : échec sur la carte (noires 6–8 éteintes).

- [ ] **Step 3 : implémenter**

- `layer_keys(B_REC)` hors LIVE : `BLACK[0]`, `BLACK[1]`, et `BLACK[5 + i]` pour i de 0 à 2 si `dice_on(5 + i)`.
- `draw_layer` case `B_REC` hors `lmode` : après `#1 CLEAR` et `#2 CLEAR LOCKS`, `#6 RND PAGE`, `#7 RND ALL`,
  `#8 RND MODEL` pour celles que `dice_on` allume. Vérifier sur une capture que les deux colonnes et la ligne grise
  `ON THE PATTERN SHOWN` tiennent sans chevauchement.
- `app/words.tsv` : `RND PAGE	random page`, `RND ALL	random all`, `RND MODEL	random model`.
- `app/docs/MANUAL.md` :
  - tableau des boutons, ligne REC, colonne Held : ajouter « black keys 6–8 draw at random (section 8) » ;
  - section 8, après « REC held », une sous-section `### Random` : les trois touches, ce qui n'est jamais tiré (le
    tableau de la spec §3, en anglais), le contenu tiré par la noire 7 en SEQ (spec §5), où le dé ne fait rien (LIVE,
    pas tenu, sélection, cellule tenue, HOME ; noire 8 hors EDIT), pas d'annulation ; une capture de la carte de REC
    tenu sur EDIT ;
  - entrée dans la table des matières si les sous-sections y figurent.
- Captures : ajouter un `shot` de la carte de REC tenu sur EDIT à `app/tests/manual_shots.txt`, puis la commande de
  captures du `CLAUDE.md` ; committer les images changées.

- [ ] **Step 4 : lancer, vérifier**

Run : `tests/run_tests.sh` ; `python3 base/tools/manual_html.py app/docs/MANUAL.md app/build/manual.html --name QUARTZ`
Expected : `all tests ok` ; le manuel se construit sans erreur, la capture montre les cinq noms.

- [ ] **Step 5 : commit**

```bash
git add app/ui.c app/words.tsv app/docs/MANUAL.md app/docs/img app/tests/random.txt app/tests/manual_shots.txt
git commit -m "RANDOM: REC's map names black keys 6-8; the manual's Random section"
```
