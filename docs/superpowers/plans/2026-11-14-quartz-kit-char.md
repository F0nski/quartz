# KIT CHAR — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** chaque pad d'un KIT mélange la sortie principale (`out`) et la sortie auxiliaire (`aux`) de son moteur de
batterie Plaits, selon un réglage CHAR 0–127 (K4 de la page KIT).

**Architecture :** `pq` garde un `chr` par voix et mélange `out`/`aux` là où il lit déjà `out` ; `audio.c`
`set_patch` le passe depuis le pad (0 hors KIT) ; `q_pad` gagne `chr`, la page KIT un K4 et une barre par case.

**Tech Stack :** C (app, unity build), C++ (DSP `app/dsp/`), simulateur hôte (`app/tests/*.txt`), tests unitaires
`tests/unit/`.

**Spec :** `docs/superpowers/specs/2026-11-14-quartz-kit-char-design.md`

## Global Constraints

- Branche `kit-char` (partie de `kit-tune`) ; commits petits, qui disent ce qui change pour le joueur, en anglais.
- Code par échantillon : pas de libm (`base/include/fastmath.h` si besoin ; ici une multiplication-addition suffit).
- `Q_VERSION` 12 → 13.
- Tout texte à l'écran en anglais et dans `app/words.tsv`.
- Tout changement visible ou audible met à jour `app/docs/MANUAL.md` dans le même commit ; captures refaites et
  commitées quand un écran change.
- `tests/run_tests.sh` finit par « all tests ok » ; `./build.sh` passe (pool sous la marge de 4096 o).
- Mesure CPU sur l'appareil en fin de cycle (`docs/cpu.md`), pas dans ce plan.

## Review Focus

1. Une voix qui a joué un pad à CHAR 127 puis sert une piste synthé doit sonner comme avant (`chr` remis à 0) :
   test `char_resets_on_engine_switch` (tâche 1) et `set_patch` qui passe 0 hors KIT (tâche 2).
2. Tourner CHAR pendant qu'un pad résonne agit comme HARM (par `set_patch`, chaque bloc) : pas de clic d'interface,
   vérifié à l'oreille sur l'appareil.
3. CHAR 1 doit se voir sur la grille : largeur de barre arrondie vers le haut (tâche 3).
4. Un pad tenu en EDIT, K4 sur la page KIT règle ce pad, pas le dernier joué : script `kit.txt` (tâche 3).
5. Le pool : `./build.sh` doit rester sous la marge (tâche 2, étape build).

---

### Tâche 1 : le mélange dans `pq`

**Files :**
- Modify : `app/dsp/pq.h` (déclaration), `app/dsp/pq.cc` (`Slot`, `pq_init`, `pq_set_engine`, rendu ~l. 150-163)
- Test : `tests/unit/pq_test.cc`

**Interfaces :**
- Produces : `void pq_set_char(int v, float chr);` — `chr` 0..1, mêmes règles de fil que `pq_set_patch` (ajouter à la
  liste du commentaire de tête de `pq.h`). `pq_init` et `pq_set_engine` mettent `chr` à 0.

- [ ] **Étape 1 : écrire les tests** dans `pq_test.cc`, ajoutés à `main` :

```cpp
// BD (engine 21): CHAR 0 is the sound of old, CHAR 1 the engine's aux, another sound that still plays
static float hit(float chr, bool set, float *energy)
{
    reset();
    stmlib::Random::Seed(1);              // the drums' noise: the same hit each time
    pq_set_engine(0, 21);
    pq_set_patch(0, 0.5f, 0.5f, 0.5f, 0.5f, 0.0f);
    if (set) pq_set_char(0, chr);
    note_on(0, 48.0f, 1.0f);
    e_sum = 0.0f;
    float peak = render(0, 4410, sum_energy);   // 100 ms
    *energy = e_sum;
    return peak;
}
static void char_mixes_out_and_aux(void)
{
    float e_old, e0, e1;
    float p_old = hit(0.0f, false, &e_old), p0 = hit(0.0f, true, &e0), p1 = hit(1.0f, true, &e1);
    assert(p0 == p_old && e0 == e_old);
    assert(p1 > 0.05f);
    assert(fabsf(e1 - e0) > 0.01f * e0);
}
static void char_resets_on_engine_switch(void)
{
    float e0, e1;
    hit(0.0f, true, &e0);
    reset(); stmlib::Random::Seed(1);
    pq_set_engine(0, 21); pq_set_char(0, 1.0f); pq_set_engine(0, 21);   // the switch forgets CHAR
    pq_set_patch(0, 0.5f, 0.5f, 0.5f, 0.5f, 0.0f);
    note_on(0, 48.0f, 1.0f);
    e_sum = 0.0f; render(0, 4410, sum_energy); e1 = e_sum;
    assert(e1 == e0);
}
```

`sum_energy` / `e_sum` : un callback comme `count_crossings`, qui ajoute `b[i] * b[i]`. Inclure
`stmlib/utils/random.h`.

- [ ] **Étape 2 : lancer `tests/run_tests.sh`** — attendu : échec de compilation, `pq_set_char` non déclaré.

- [ ] **Étape 3 : implémenter.** `Slot` gagne `float chr;`. Dans les deux boucles du rendu, l'échantillon lu
  devient `out + chr * (aux - out)` (en `float`, avant `* (1.0f / 32768.0f)`). `pq_set_char` écrit `slots[v]->chr`.

- [ ] **Étape 4 : `tests/run_tests.sh`** — attendu : `pq_test ok`, puis « all tests ok ».

- [ ] **Étape 5 : commit** — `git commit -m "Voices: a voice can mix its drum engine's analog and synthetic outputs"`

---

### Tâche 2 : le CHAR du pad, le projet et le kit par défaut

**Files :**
- Modify : `app/project.h` (`q_pad`, `Q_VERSION`), `app/seq.c` (`KIT_DEFAULT`), `app/audio.c` (`set_patch`, l. 337)
- Test : `tests/unit/seq_test.c`

**Interfaces :**
- Consumes : `pq_set_char` (tâche 1).
- Produces : `q_pad` = `{ uint8_t engine; int8_t tune; uint8_t harm, timb, decay, level, chr; }`, `chr` 0..127.

- [ ] **Étape 1 : écrire le test** `kit_default_char` dans `seq_test.c`, ajouté à `main` :

```c
static void kit_default_char(void)
{
    q_project *p = &P;            /* the file's project */
    project_defaults(p);
    for (int t = 0; t < Q_TRACKS; t++)
        for (int k = 0; k < Q_PADS; k++)
            assert(p->kit[t][k].chr == (k >= 9 && k <= 11 ? 127 : 0));   /* pads 10-12: BD2, SN2, CH2 */
}
```

- [ ] **Étape 2 : `tests/run_tests.sh`** — attendu : échec de compilation, `chr` absent de `q_pad`.

- [ ] **Étape 3 : implémenter.**
  - `q_pad` gagne `chr` en dernier (commentaire : « chr 0..127: 0 the engine's analog sound, 127 its synthetic one »),
    `Q_VERSION 13`.
  - `KIT_DEFAULT` : septième valeur `127` aux entrées 10, 11 et 12 ; les autres la laissent à 0 (initialiseur partiel).
    Le commentaire de tête : `BD2 SN2 CH2` deviennent `SBD SSN SCH` (synthetic).
  - `set_patch` : branche KIT `pq_set_char(v, pd->chr / 127.0f);`, branche synthé `pq_set_char(v, 0.0f);`.

- [ ] **Étape 4 : `tests/run_tests.sh`** — attendu « all tests ok ». Puis `./build.sh` — attendu `ok image … pool …`
  sans erreur `SRAM headroom`.

- [ ] **Étape 5 : commit** — `git commit -m "Kit: pads 10-12 play the synthetic bass drum, snare and hat"` (le
  manuel suit en tâche 3 avec la page : ce commit change déjà le son par défaut, ajouter dans ce commit la ligne du
  tableau des pads par défaut de `MANUAL.md` : pads 10–12 « Synth bass drum », « Synth snare », « Synth hat »).

---

### Tâche 3 : K4 CHAR sur la page KIT, la grille, le manuel

**Files :**
- Modify : `app/ui.c` (table `PAGE` l. 63, `knob_get` ~l. 388, `knob_set` ~l. 570, vue `scene` l. 160 et l. 2310,
  dessin l. 2694, lecteur de test ~l. 3502), `app/words.tsv`, `app/docs/MANUAL.md`, `app/docs/img/sound-kit.png`
- Test : `app/tests/kit.txt`

**Interfaces :**
- Consumes : `q_pad.chr` (tâche 2).
- Produces : clé de test `padchar_<n>`.

- [ ] **Étape 1 : écrire le test** dans `kit.txt`, après `expect padlevel_4 80` (page KIT, pad 4 réglé) :

```
turn K4 20
expect padchar_4 20
expect padchar_1 0
# white 1 held: K4 sets pad 1
key 2 down
turn K4 5
expect padchar_1 5
key 2 up
```

(Vérifier dans le script que la blanche 1 est bien `key 2` ; sinon prendre la bonne touche.)

- [ ] **Étape 2 : `app/build/host/quartz_host app/tests/kit.txt app/build/host`** (après `./build.sh --host`) —
  attendu : échec sur `padchar_4`.

- [ ] **Étape 3 : implémenter.**
  - `PAGE` : `{"KIT", {"MODEL", "ENGINE", "LEVEL", "CHAR"}, {0, 0, 0, 0}, {M_KIT, 2, 127, 127}}`.
  - `knob_get` / `knob_set` : `case P_KIT * 4 + 3` sur `pd->chr`.
  - Vue : `uint8_t chr[Q_PADS];` à côté de `eng[Q_PADS]`, rempli dans la même boucle.
  - Dessin de chaque case : si `chr`, `cv_rect(x, y + 31, (54 * chr + 126) / 127, 2, i == pad ? C_BLACK : KCOL[3])`.
  - Lecteur : `padchar_` sur `pd->chr`.
  - `words.tsv` : `CHAR<TAB>character`.

- [ ] **Étape 4 : `tests/run_tests.sh`** — attendu « all tests ok ».

- [ ] **Étape 5 : manuel.** Section KIT : K4 `CHAR, 0–127` dans le tableau ; une phrase : « CHAR mixes each engine's
  two sounds: 0 the analog one, 127 the synthetic one (a drier 909-like kick and snare, a harsher ring-modulated
  hat), between them a blend. » ; la scène : « a bar under each pad shows its CHAR ». Captures (commande de
  `CLAUDE.md`), vérifier `sound-kit.png` (barres sous les pads 10–12), commiter les images changées.

- [ ] **Étape 6 : commit** — `git commit -m "KIT: K4 CHAR blends each pad between its analog and synthetic sound"`

---

### Tâche 4 : vérification finale

- [ ] `tests/run_tests.sh` → « all tests ok » ; `./build.sh` → pas d'erreur, noter `pool … of 344064`.
- [ ] Ajouter à `docs/cpu.md`, en anglais comme le reste du fichier, une ligne à mesurer : KIT CHAR moves the voices'
  render (pq.cc) ; la mesure se fait dans la session CPU de fin de cycle. Commit `docs/cpu.md: KIT CHAR moves the voices' render (to measure)`.
- [ ] Ne pas installer sur l'appareil, ne pas fusionner ni pousser : demander.
