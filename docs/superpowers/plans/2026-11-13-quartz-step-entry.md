# QUARTZ — saisie dans le séquenceur : plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** un firmware QUARTZ où REC passe par GRID, NOTES et LIVE, où la grille coupe, colle et règle les pas d'un geste,
où le mode NOTES écrit au curseur, et où tout bouton tenu montre sa couche.

**Architecture :** tout l'état de saisie vit dans `app/ui.c` (machine d'états de REC, presse-papier, sélection, curseur,
effacement, couches) ; `app/plateau.c` gagne la bande de durée prolongée et les pas sélectionnés, nourris par la vue
`q_plateau` que prépare `ui.c`. L'audio ne change pas : seul le LIVE arme `audio_rec`, ce qui donne le décompte au bon
moment sans toucher `seq.c`.

**Tech Stack :** C (gnu11, sans libc dans l'app, build unité `app/app.c`), canvas `base/device/gfx.c`, `app/p3d.c` ;
tests `tests/run_tests.sh` (tests C `tests/unit/`, scripts du simulateur `app/tests/*.txt`).

**Spec :** `docs/superpowers/specs/2026-11-12-quartz-step-entry-design.md` ; manuel `docs/manual/sequencer-entry.html`.

## Global Constraints

- Identité git `f0nski`, pas de ligne `Co-Authored-By` ; les commits sont faits par l'utilisateur (un hook bloque
  `git commit` pour l'agent) : chaque étape « Commit » propose le message et s'arrête là.
- Code, commentaires, commits en anglais ; style et densité de commentaires de `ui.c` ; pas de nouveau fichier source.
- `HOLD_MS 400u` : au-delà, un bouton tenu est un appui long (pas d'action au relâchement, carte de sa couche).
- `q_step.len` est en huitièmes de pas (8 = un pas, 64 = huit pas). Un pas créé à la main garde le défaut actuel
  (`len 4`, `vel 100`, `prob 20`) : c'est la « durée d'un pas » de la spec.
- La note qu'une touche écrit est celle qu'elle joue : octave, KEYS (la gamme sur les blanches, les noires muettes) et
  KIT (blanches = pads) compris. « Les 27 touches » de la spec suppose KEYS = OFF.
- Badges : `GRID` vert `#22d58e`, `NOTES nn` bleu `#3a6dff`, `LIVE` rose `#ff2f8e`, `ARM` orange `#ff7a2e` clignotant
  à 500 ms ; textes de la couche REC : `CLEAR`, `CLEAR LOCKS`.
- Boutons (plat.h) : `B_FX 0, B_SEL 1, B_HOME 6, B_SAVE 7, B_ARP 8, B_SEQ 9, B_PLAY 10, B_REC 11`.

## Review Focus

1. Changer de domaine ou de piste avec un pas tenu, une sélection ou des touches enfoncées en NOTES : rien ne reste
   écrit à moitié, le pas tenu, la sélection et l'accord en cours sont abandonnés (script `notes_mode.txt`, tâche 6).
2. LEN réduit sous le curseur de NOTES ou sous des pas sélectionnés : curseur ramené à 1, sélection limitée aux pas
   dans LEN (script `notes_mode.txt` tâche 6, `grid_sel.txt` tâche 5).
3. Effacement en live à tempo rapide et au bouclage du pattern (pos qui repasse de LEN − 1 à 0) : aucun pas traversé
   oublié, aucun pas effacé deux fois hors du passage (script `live_erase.txt`, tâche 7).
4. Coller un accord de 4 notes sur une piste POLY 1, ou un pas dont les p-locks ne tiennent plus (128) : les POLY
   premières notes seulement, p-locks en trop perdus avec `msg_locks_full` (script `grid_clip.txt`, tâche 4).
5. Appui long sur REC ou SAVE en pleine lecture : ni bascule, ni sauvegarde, ni arrêt (script `rec_modes.txt`, tâche 1).

---

## Structure des fichiers

```
app/ui.c                   états de REC, routage du clavier, familles, presse-papier, sélection, NOTES, effacement,
                           couches et leur carte, badge, LED, vue du plateau, ui_expect
app/plateau.c, plateau.h   bande prolongée, pas sélectionnés, règles du viseur
tests/unit/plateau_test.c
app/tests/rec_modes.txt, seq_play.txt, families.txt, grid_clip.txt, grid_sel.txt, notes_mode.txt, live_erase.txt,
           layers.txt (nouveaux) ; scripts existants qui éditent des pas, adaptés
docs/screens/layers.html   (nouvelle) maquette de la carte d'une couche
```

---

### Task 1: les états de REC, PLAY et les appuis longs

**Files:** Modify `app/ui.c` (`button`, `ui_input`, `save_release`, `rec_lock`, `punch_rec`, `lights`, `ui_expect`) ;
Test `app/tests/rec_modes.txt` (nouveau), `app/tests/overdub.txt`, `app/tests/save.txt`

**Interfaces :**
- Produces : `enum { R_OFF, R_GRID, R_NOTES, R_LIVE, R_ARM };` dans `ui.rec` ; `uint32_t down_ms[NB]` et
  `uint32_t used` (bit b : le bouton b a servi pendant qu'il était tenu) dans `ui` ; `static int long_held(int b)` (tenu
  depuis plus de `HOLD_MS`) ; `static void rec_set(int r)` (seul endroit qui écrit `ui.rec` et appelle
  `audio_rec(r == R_LIVE)`) ; expect `rec` = l'état.

- [ ] **Step 1: Script** `rec_modes.txt` :
  - dans SEQ (pattern STEP) : `tap REC` → `rec 1`, `tap REC` → `rec 2`, `tap REC` → `rec 0` ; dans EDIT : `tap REC` →
    `rec 2`, `tap REC` → `rec 0` ; GRID dans SEQ puis `tap EDIT`, `tap REC` → `rec 0` ;
  - `press REC`, `wait 500`, `release REC` → `rec 0` ;
  - pattern GRIDS (SOURCE) : `tap REC` → `rec 0` ;
  - GRID, `tap PLAY` → `playing 1`, `rec 1`, `pos >-1` après `wait 100` (pas de décompte, CLICK = BOTH réglé avant) ;
    `tap PLAY` → `playing 0`, `rec 1` ;
  - LIVE : `press REC`, `tap PLAY`, `release REC` en lecture → `rec 3`, `playing 1` ; `tap REC` → `rec 0`, `playing 1` ;
    à nouveau LIVE, `tap PLAY` → `playing 0`, `rec 0` ;
  - ARM : CLICK = OFF, à l'arrêt `press REC`, `tap PLAY`, `release REC` → `rec 4`, `playing 0` ; `key w0 down` →
    `playing 1`, `rec 3` ; `key w0 up`, `wait 300`, `tap PLAY` → `stepon_1 1` ;
  - décompte : CLICK = COUNT, REC tenu + PLAY à l'arrêt → `rec 3`, `playing 1`, `pos -1` après `wait 100` ;
  - NOTES dans EDIT, `tap PLAY` (CLICK = COUNT) → `rec 3`, `pos -1` après `wait 100` ;
  - appui long en lecture : `press REC`, `wait 500`, `release REC` → `rec 3`, `playing 1` ;
  - SAVE à l'arrêt : `press SAVE`, `wait 500`, `release SAVE` → `store_writes` inchangé ; `tap SAVE` → +1.
- [ ] **Step 2: Lancer** `tests/run_tests.sh` : échec sur `rec_modes.txt`.
- [ ] **Step 3: Implémenter** dans `ui.c` :
  - REC agit au relâchement : appui court (`!long_held && !(used >> B_REC & 1)`) → cycle de la spec section 2 (SEQ
    + STEP : OFF → GRID → NOTES → OFF ; ailleurs OFF → NOTES → OFF, tout état armé → OFF ; LIVE / ARM → OFF ; pattern
    GENERATEUR dans SEQ : rien) ; tout bouton ou touche pressé pendant que REC est tenu met son bit dans `used` ;
  - PLAY pendant que REC est tenu : en lecture → `R_LIVE` sans arrêter ; à l'arrêt → `R_LIVE` puis `audio_play(1)` si
    `p->click & 2`, sinon `R_ARM` ;
  - PLAY seul : démarrer en `R_NOTES` hors de SEQ → `R_LIVE` avant `audio_play(1)` (le décompte vient d'`audio_rec`) ;
    arrêter : `R_LIVE` / `R_ARM` → `R_OFF`, GRID et NOTES restent ;
  - `R_ARM` : la première touche qui joue une note fait `rec_set(R_LIVE)`, `audio_play(1)`, puis sa note ;
  - SAVE : sauvegarde au relâchement seulement sur un appui court sans touche (remplace `saveused` par `used`) ;
  - `rec_lock`, `punch_rec`, l'expect `overdub` : `ui.rec == R_LIVE` au lieu de `ui.rec` ; REC allumé quand
    `ui.rec != R_OFF`.
- [ ] **Step 4: Adapter** `overdub.txt` (l'overdub par REC + PLAY devient REC tenu + PLAY) et `save.txt` ; lancer : `all tests ok`.
- [ ] **Step 5: Commit** `UI: REC states (grid, notes, live, armed), live only arms the recorder, long presses do nothing`

---

### Task 2: le clavier dans SEQ et les rôles d'OCT

**Files:** Modify `app/ui.c` (`key_down`, `key_up`, `button` OCT, `lights`, vue du plateau, `ui_expect`) ; Test
`app/tests/seq_play.txt` (nouveau), scripts qui éditent des pas

**Interfaces :**
- Consumes : `ui.rec`, `R_*` (tâche 1).
- Produces : `static int grid_on(void)` (domaine SEQ, pattern STEP, `ui.rec == R_GRID`) ; `static int key_play_note(int k)`
  (la note que joue la touche k, −1 si muette : octave, KEYS, KIT ; extrait de `key_down`) ; expects `octave`, `spage`.

- [ ] **Step 1: Script** `seq_play.txt` : SEQ désarmé, `key w0 down` → `hits` +1, `stepon_1 0`, `held 0` ; `tap OCT+` →
  `octave 1`, `spage 1` ; `tap REC` (GRID), `tap OCT+` → `spage 2`, `octave 1` ; désarmé en lecture d'un pattern de 32
  pas, `wait` jusqu'au pas 20 → `spage 2`.
- [ ] **Step 2: Lancer** : échec.
- [ ] **Step 3: Implémenter** : dans SEQ, les blanches sont des pas seulement si `grid_on()` ; sinon `key_down` joue
  (hors couches) ; OCT tourne la page si `grid_on()` sans pas tenu, sinon l'octave ; désarmé et en lecture, `ui.spage`
  suit `audio_pos / 16` ; le voyant d'OCT suit l'octave dès qu'OCT règle l'octave.
- [ ] **Step 4: Adapter** les scripts qui éditent des pas (`grep -l "tap SEQ" app/tests/*.txt`, ceux qui tiennent ou
  basculent des pas) : `tap REC` après `tap SEQ` ; lancer : `all tests ok`.
- [ ] **Step 5: Commit** `UI: in SEQ the keys play unless REC is on the grid; OCT pages steps on the grid only`

---

### Task 3: les noires en familles

**Files:** Modify `app/ui.c` (`black_key`, `ATTR_ROW`, enum `A_*` inchangé) ; Test `app/tests/families.txt` (nouveau),
`app/tests/step_edit.txt`

**Interfaces :**
- Produces : `static const uint8_t FAMILY[11][2]` (0xFF : rien) =
  `{{A_RATCHET,X},{A_VEL,X},{A_PROB,A_COND},{A_NOTE,X},{A_LEN,A_MICRO},{A_SLIDE,X},{A_ACCENT,X},{X,X},{X,X},{X,X},{X,X}}` ;
  `static void family_key(int b, q_step *s)` : SLIDE et ACCENT basculent sur `s` ; sinon `ui.attr` passe à la
  fonction suivante de la famille quand elle y est déjà, à sa première sinon. `ATTR_ROW` =
  `{A_RATCHET, A_VEL, A_PROB, A_COND, A_NOTE, A_LEN, A_MICRO, A_SLIDE, A_ACCENT}`.

- [ ] **Step 1: Script** `families.txt` (GRID, pas 1 tenu) : `attr 0` (VEL) au départ ; `tapkey b2` → `attr 2` ; `tapkey b2`
  → `attr 3` ; `tapkey b2` → `attr 2` ; `tapkey b4` ×2 → `attr 8` ; `turn PRESET 3` → `stepmicro_1 3` ; `tapkey b5` →
  `stepslide_1 1` ; `tapkey b8` → rien ne change (`stepon_1` gardé) ; `tapkey b1` → `attr 0`.
- [ ] **Step 2: Lancer** : échec.
- [ ] **Step 3: Implémenter** `family_key` (appelé à la place de `black_key` ; la noire 9 n'efface plus le pas).
- [ ] **Step 4: Adapter** `step_edit.txt` aux nouvelles places ; lancer : `all tests ok`.
- [ ] **Step 5: Commit** `UI: the black keys pick a family of step functions, a second press the next one`

---

### Task 4: la grille — couper, coller, aperçu, durée par OCT

**Files:** Modify `app/ui.c` (`key_down`, `key_up`, `button` OCT, `ui_expect`) ; Test `app/tests/grid_clip.txt` (nouveau)

**Interfaces :**
- Consumes : `grid_on`, `key_play_note` (tâche 2).
- Produces : `typedef struct { q_step st; uint8_t nlocks; q_lock lk[16]; } q_clip;` (`lk[].step` inutilisé) dans
  `ui.clip`, initialisé à la note 60 ; `static void clip_cut(int step)`, `static void clip_paste(int step)`,
  `static void clip_notes(const uint8_t *notes, int n)` ; `static int len_step(int len, int d)` : d > 0 → pas entier
  suivant (`(len / 8 + 1) * 8`, 64 au plus), d < 0 → précédent (`((len - 1) / 8) * 8`), jamais sous 8 (un `len` ≤ 8
  ne descend pas) ; expects `clip_n` (notes du presse-papier), `clip_note`, `clip_locks`.

- [ ] **Step 1: Script** `grid_clip.txt` :
  - désarmé, jouer C4 E4 G4 ensemble (POLY 3) → `clip_n 3` ; GRID, `tapkey w2` → `stepon_3 1`, `stepnotes_3 3` ;
  - pas 3 avec un p-lock HARM : `tapkey w2` → `stepon_3 0`, `lockharm_3 -1`, `clip_locks 1` ; `tapkey w5` →
    `stepon_6 1`, `lockharm_6` égal à l'ancien ; `tapkey w2` puis `tapkey w9` → deux copies ;
  - à l'arrêt, `key w5 down` → `hits` +1 (aperçu) ; `tap OCT+` ×2 → `steplen_6 16` puis `24` ; `tap OCT-` ×3 →
    `steplen_6 8` ; `key w5 up` → `stepon_6 1` (édité : pas coupé), `spage 1` ;
  - POLY 1 : coller l'accord → `stepnotes_N 1`, `stepnote_N 60` ;
  - KIT : presse-papier d'une note synthé collé → `stepnote_N` = `KIT_NOTE` ;
  - pattern à 127 p-locks, coller un pas qui en porte 2 → `locks 128`, `msg_locks_full 1`.
- [ ] **Step 2: Lancer** : échec.
- [ ] **Step 3: Implémenter** :
  - `key_up` du pas tenu sans édition : pas allumé → `clip_cut` (copie, puis `step_clear`) ; éteint → `clip_paste` ;
  - `clip_paste` : notes tronquées à POLY ; sur un KIT, un presse-papier sans pad (`note` hors `KIT_NOTE..+15`) donne
    `KIT_NOTE` ; p-locks par `lock_set`, échec → `ui.locks_until` ;
  - jouer désarmé (dans SEQ ou ailleurs) : les notes pressées pendant qu'une autre est tenue forment l'accord passé à
    `clip_notes` ; un pas coupé le remplace ;
  - aperçu : pas allumé tenu à l'arrêt → `audio_live_note(track, n, 2)` pour chaque note, note-off au relâchement ;
  - OCT pendant qu'un pas est tenu : `len_step`, `ui.edited = 1`, pas de changement de page.
- [ ] **Step 4: Lancer** : `all tests ok`.
- [ ] **Step 5: Commit** `UI: grid cut and paste with a shared clipboard, preview of a held step, OCT sets its length`

---

### Task 5: la sélection (SEL tenu dans la grille)

**Files:** Modify `app/ui.c` (`key_down`, `ui_input` PRESET et K, `button` OCT, `ui_expect`) ; Test `app/tests/grid_sel.txt` (nouveau)

**Interfaces :**
- Consumes : `family_key` (tâche 3), `len_step` (tâche 4).
- Produces : `uint16_t sel` (bit : cellule de la page) et `int8_t sel_ref` (cellule, −1) dans `ui` ;
  `static void attr_copy(q_step *dst, const q_step *src, int attr)` ; `lock_turn` prend le pas :
  `static void lock_turn(int step, int k, int d)` (aussi utilisé par la tâche 6) ; expects `sel`, `selref`.

- [ ] **Step 1: Script** `grid_sel.txt` (GRID, pas 1, 3, 5 allumés, 7 éteint) : `press SEL`, `tapkey w0`, `tapkey w2`,
  `tapkey w6` → `sel 69`, `selref 1`, `stepon_7 0` ; `tapkey b2`, `turn PRESET -10` → `stepprob_1 10`, `stepprob_3 10`,
  `stepprob_7` inchangé ; `tapkey b5` → `stepslide_1 1`, `stepslide_3 1` ; `turn K1 5` → `lockharm_1` = `lockharm_3` ;
  `tap OCT+` → `steplen_1 8`, `steplen_3 8` ; `tapkey w2` → `sel 65` ; `release SEL` → `sel 0` ; LEN 2 avec `sel` sur
  les pas 1 et 3 → `steplen_3` inchangé par OCT.
- [ ] **Step 2: Lancer** : échec.
- [ ] **Step 3: Implémenter** : SEL tenu, `grid_on()`, sans pas tenu → les blanches basculent `sel` (la première fixe
  `sel_ref`) ; noires, PRESET (sur la référence puis `attr_copy`), K1–K4 (`lock_turn` sur la référence puis la même
  valeur aux autres), OCT (`len_step` chacun) sur les pas allumés de `sel` dans LEN ; le réglage fin SEL + K ne joue
  pas quand `sel` ; relâcher SEL, changer de page, de piste ou de domaine vide `sel`.
- [ ] **Step 4: Lancer** : `all tests ok`.
- [ ] **Step 5: Commit** `UI: SEL selects steps on the grid; functions, p-locks and lengths apply to the selection`

---

### Task 6: le mode NOTES

**Files:** Modify `app/ui.c` (`key_down`, `key_up`, `ui_input` PRESET, SEL, K, `lights`, `ui_expect`) ; Test
`app/tests/notes_mode.txt` (nouveau)

**Interfaces :**
- Consumes : `key_play_note`, `len_step`, `family_key`, `attr_copy`, `lock_turn(step, k, d)`.
- Produces : `static int notes_on(void)` (`R_NOTES`, et dans SEQ un pattern STEP, ou hors de SEQ à l'arrêt) ;
  `int8_t nchord` (notes écrites depuis que toutes les touches sont relâchées) dans `ui` ;
  `static uint32_t step_keys(const q_step *s)` (les touches dont `key_play_note` est une note du pas) ; expects
  `cursor` (existant), `leds_lit` (les touches allumées pleinement, voir tâche 9 pour `leds_mid`).

- [ ] **Step 1: Script** `notes_mode.txt` (KEYS = OFF, POLY 3) :
  - SEQ, REC ×2 → `rec 2`, `cursor 1` ; `tapkey 7` (C4) → `stepnote_1 60`, `cursor 2` ; accord : `key 7 down`,
    `key 11 down`, `key 14 down`, puis les trois `up` → `stepnotes_2 3`, `cursor 3` ; `tapkey 8` (C#4) →
    `stepnote_3 61` ;
  - pas allumé avec PROB 10 et un p-lock : `turn PRESET -2`, `tapkey 9` → note remplacée, `stepprob` et `lockharm` gardés ;
  - `key 7 down`, `turn PRESET 2`, `key 7 up` → `steplen_N 16`, curseur avancé de 2 ;
  - `turn PRESET 1` → pas sauté resté éteint ; LEN 4 : `cursor` boucle 4 → 1 ; LEN 32, curseur au pas 17 → `spage 2` ;
  - `press SEL`, `tapkey w4` → `cursor 5` ; `tapkey b2`, `turn PRESET -10` → `stepprob_5 10` ; `release SEL` ;
  - `leds_lit` = les touches 7, 11, 14 quand le curseur est sur le pas 2 ;
  - en NOTES, `turn PRESET` ne change pas `pattern` ; désarmé, il le change à nouveau ;
  - `key 7 down`, `tap EDIT`, `key 7 up` → `cursor` inchangé, aucune note écrite après le changement de domaine ;
    `turn ALGO 1` sur une piste de LEN 4 avec `cursor 9` → `cursor 1`.
- [ ] **Step 2: Lancer** : échec.
- [ ] **Step 3: Implémenter** :
  - entrée en NOTES : `cursor = 1` ; `key_down` si `notes_on()` : la note de `key_play_note`, puis première note de
    l'accord (`nchord == 0`) → pas éteint : défauts de création, sinon `note[]` vidé ; notes suivantes jusqu'à POLY (KIT
    : 4 pads) ; la note sonne (`audio_live_note(.., 2)`) ;
  - toutes les touches relâchées après au moins une note → `cursor` avance de `len / 8` (1 au moins), boucle sur LEN ;
    dans SEQ, `spage = (cursor − 1) / 16` ;
  - PRESET : touches enfoncées → `len_step` sur le pas du curseur ; sinon curseur ±1 ; ses autres rôles sont sautés ;
  - SEL tenu : blanche → curseur au pas n de la page s'il est dans LEN ; noire → `family_key` ; PRESET →
    `attr_turn` du pas du curseur ; K → `lock_turn(cursor − 1, k, d)` ;
  - changement de domaine ou de piste : `nchord = 0` (les touches encore enfoncées ne font plus que sonner) ;
  - `lights` en NOTES : `step_keys` du pas du curseur.
- [ ] **Step 4: Lancer** : `all tests ok`.
- [ ] **Step 5: Commit** `UI: NOTES mode, a cursor moved by PRESET and the keys writing its step's notes`

---

### Task 7: effacer en live

**Files:** Modify `app/ui.c` (`key_down`, `key_up`, `ui_input`, `ui_expect`) ; Test `app/tests/live_erase.txt` (nouveau)

**Interfaces :**
- Consumes : `R_LIVE`, `used`, `key_play_note`.
- Produces : `int8_t erase_pos` (dernier pas traité, −1) et `uint8_t erase_notes[8]` dans `ui` ; expect `nsteps_on`
  (pas allumés du pattern affiché).

- [ ] **Step 1: Script** `live_erase.txt` : pattern de 16 pas, note 60 sur tous, TEMPO 240, LIVE en lecture ;
  `press REC`, `key 7 down` → `hits` n'augmente que par le séquenceur (comparer à une lecture sans touche), `wait 1000`,
  `key 7 up`, `release REC` → `nsteps_on 0`, `rec 3` ; même chose avec un accord 60 + 64 sur les pas : `stepnote_N 64`
  reste ; `press REC`, `press OCT-`, `wait 1000` → `nsteps_on 0`, `octave` inchangé ; DIV 1/32 au tempo 240 et pattern de
  LEN 16 qui boucle pendant l'effacement : `nsteps_on 0`.
- [ ] **Step 2: Lancer** : échec.
- [ ] **Step 3: Implémenter** : REC tenu en `R_LIVE` → une touche pressée met sa note dans `erase_notes` (ni son ni
  enregistrement, bit `used` de REC) ; à chaque `ui_input` en lecture, les pas de `erase_pos + 1` à `audio_pos` (en
  boucle sur LEN, PATH FWD ; autre PATH : le pas courant seul) perdent les notes de `erase_notes` (plus de note → `SF_ON`
  retiré) ou sont effacés (`step_clear`) si OCT− est tenu ; `erase_pos = −1` quand REC est relâché.
- [ ] **Step 4: Lancer** : `all tests ok`.
- [ ] **Step 5: Commit** `UI: in live, REC held with keys erases their notes as the playhead passes, with OCT- every step`

---

### Task 8: les couches, leur carte et la couche REC

**Files:** Create `docs/screens/layers.html` ; Modify `app/ui.c` (`key_down`, `lights`, rendu de la zone de scène,
`ui_expect`) ; Test `app/tests/layers.txt` (nouveau), `app/tests/clear.txt`

**Interfaces :**
- Consumes : `down_ms`, `used`, `long_held` (tâche 1).
- Produces : `static int layer_shown(void)` (le bouton de couche tenu depuis plus de `HOLD_MS` sans autre appui, −1) ;
  `static void draw_layer(int b, int32_t oy)` ; expect `layer` (le bouton + 1, 0 aucun).

- [ ] **Step 1: Maquette** `docs/screens/layers.html` dans le style des autres écrans : la carte de FX et celle de REC
  dans la zone 240 × 166 (16 blanches de 15 px en haut, noms des touches actives dessous en deux colonnes, puis les
  combinaisons) ; la faire valider par l'utilisateur avant le code.
- [ ] **Step 2: Script** `layers.txt` : `press FX`, `wait 500` → `layer 1` ; `release FX` → `layer 0` ; `press HOME`,
  `tapkey w0` aussitôt, `wait 500` → `layer 0` (déjà servi) ; `press REC`, `wait 500` → `layer 12`, `release REC` →
  `rec` inchangé ; pattern rempli : `press REC`, `tapkey b0`, `release REC` → `nsteps_on 0`, `rec` inchangé ; p-locks :
  `press REC`, `tapkey b1`, `release REC` → `locks 0`, pas gardés ; en LIVE, REC tenu + `b0` n'efface pas le pattern ;
  `press SEQ`, `tapkey b0` → pattern intact.
- [ ] **Step 3: Lancer** : échec.
- [ ] **Step 4: Implémenter** : couches HOME, FX, SAVE, SEQ, ARP, SEL, REC ; la carte remplace la scène tant que
  `layer_shown()`, elle suit les appuis (punch-in en rose) ; les LED montrent les touches actives de la couche ; REC tenu
  hors LIVE : noire 1 → `step_clear` de tous les pas du pattern affiché, noire 2 → `nlocks = 0` ; SEQ tenu + noire 1
  supprimé.
- [ ] **Step 5: Adapter** `clear.txt` (REC tenu + noire 1) ; lancer : `all tests ok`.
- [ ] **Step 6: Commit** `UI: a held layer button shows its map; REC layer clears the pattern or its p-locks`

---

### Task 9: écran — badge, bande de durée, sélection, viseur, LED

**Files:** Modify `app/plateau.h`, `app/plateau.c`, `app/ui.c` (en-tête, vue du plateau, `lights`) ; Test
`tests/unit/plateau_test.c`, `app/tests/plateau.txt`

**Interfaces :**
- Consumes : `sel`, `sel_ref`, `cursor`, `notes_on`, `grid_on`.
- Produces : `q_pcell.sel` (0/1) ; dans `q_plateau`, `held` vaut aussi la référence de la sélection ou, en NOTES avec
  SEL tenu, le curseur ; `cursor` = la cellule du curseur en NOTES (lecture comprise), −1 en GRID ; expects `leds_mid`,
  `leds_lit`, `badge` (0 aucun, 1 GRID, 2 NOTES, 3 LIVE, 4 ARM).

- [ ] **Step 1: Tests C** dans `plateau_test.c` :
  - `band_two_rows` : pas 3 de `len 32` (4 pas) → pixels `PL_GREEN[2]` sur les cases 4, puis 5 et 6 de la rangée suivante,
    chevron `PL_GREEN[1]` au bout de la rangée 1, trait `PL_GREEN[0]` sur la case 6 ;
  - `band_overflow` : pas 15 de `len 40` → texte `+3 >` présent (pixels de `PL_GREEN[1]` dans la zone du bord avant) ;
  - `band_pink_fill` : `pos` 5 pendant la note du pas 3 → cases 3 à 5 en `PL_PINK[1]`, case 6 en vert ;
  - `sel_orange` : `c[2].sel = 1` → dessus `PL_ORANGE[0]`, bloc non soulevé (même hauteur qu'un bloc normal).
- [ ] **Step 2: Lancer** : échec.
- [ ] **Step 3: Implémenter** :
  - bande : pour un pas de `len > 8`, `L = (len + 7) / 8` pas, sur les cellules suivantes dans l'ordre des numéros, de
    rangée en rangée, chevron au bord droit d'une rangée quittée et au bord gauche de la suivante, trait clair au bout ;
    rose sur les cellules du début à `pos` quand `pos` est dans la note ; `+n >` si la note dépasse la cellule 15 ;
  - pas sélectionné : tons orange, posé ; viseur blanc sur `cursor` seulement en NOTES ;
  - en-tête : badge de l'état (couleurs et textes des Global Constraints) ;
  - `lights` : désarmé et GRID dans SEQ, pas allumés par `plat_lights_mid`, pas en cours et sélection pleinement ;
    NOTES : `step_keys` (tâche 6) ; couche : ses touches (tâche 8).
- [ ] **Step 4: Ajouter** à `plateau.txt` des captures : bande sur deux rangées, sélection, NOTES ; lancer : `all tests ok`.
- [ ] **Step 5: Commit** `Plateau: length strip across rows with its pink fill and overflow, selected steps, REC badge`

---

### Task 10: vérification d'ensemble

**Files:** Modify `docs/manual/sequencer-entry.html` et la spec si un nom a changé en route.

- [ ] **Step 1: Lancer** `tests/run_tests.sh` : `all tests ok` ; relire le manuel contre le comportement du simulateur
  (un script par recette du manuel, ou une vérification à la main) et corriger l'écart côté code ou côté manuel.
- [ ] **Step 2: Appareil** : flasher, jouer chaque recette du manuel ; vérifier que la carte d'une couche et la bande de
  durée ne font perdre aucun bloc (page CPU, pire cas de 5b : 16 pas longs en lecture sur STEPS).
- [ ] **Step 3: Commit** `Docs: manual and spec follow the step entry as built`
