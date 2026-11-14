# QUARTZ user manual and project CLAUDE.md Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An English user manual of QUARTZ in `app/docs/MANUAL.md` (with real screen captures and credited
inspirations), a landing page for fm1-base's site, and a project `CLAUDE.md` that keeps the manual in step with the code.

**Architecture:** Plain Markdown read by `base/tools/manual_html.py` (through `base/tools/make_site.py` at publish
time). Screens come from one host-simulator script, `app/tests/manual_shots.txt`, run with `app/docs/img` as its output
directory. Each section is written from the code (behaviour) and the original's documentation (credits).

**Tech Stack:** Markdown (the subset `manual_html.py` handles), the host simulator's script language
(`base/host/plat_host.c` header), Python 3 for the render check.

**Spec:** `docs/superpowers/specs/2026-11-14-quartz-user-manual-design.md`

## Global Constraints

- All text in the manual, landing page and `CLAUDE.md` is English; specs stay French.
- Screen names are written exactly as drawn (`app/ui.c` `PAGE[]`, `MODEL_NAME`, `SRC_NAME`, `DOMAIN_NAME`, …); the old
  borrowed names (GRIDS, MARBLES, LIFE, LANES, CHORDS, TONNETZ, ORBITS, VA VCF, 6-OP…) appear only in **Inspired by**
  lines and section 12.
- Behaviour is checked in the code (`app/ui.c`, `app/gen.c`, `app/seq.c`, `app/audio.c`, `app/store.c`) and the specs
  in `docs/superpowers/specs/`, never inferred from the original instrument.
- Each borrowed idea: one **Inspired by** line, `[Name](url)`, plus one line on what QUARTZ does differently when it
  differs. The original's manual is read (WebFetch) before writing the subsection.
- Markdown subset only: `#`/`##`/`###` headings, `## N. Title` numbered sections, paragraphs, `**bold**`, `*em*`,
  `` `code` ``, `[text](url)`, flat `-` and `1.` lists (no nesting), pipe tables, `---`, `> ` callouts,
  `<details>`, images `![alt](img/x.png "caption")` (PNG = screen shot, SVG = wide figure).
- Images in `app/docs/img/`, produced by `app/tests/manual_shots.txt` only (SVG figures excepted), named
  `<section>-<what>.png` (e.g. `sound-engine.png`, `source-atlas.png`).
- Repo URL: `https://github.com/F0nski/quartz`. No push, no `gh-pages` publish.
- No new dependency; no change to `base/`.

## Review Focus

1. A sentence that contradicts the firmware (a knob range, what a long press does, PRESET's role): each task's verify
   step cross-checks every table row against `PAGE[]` ranges and the handler in `app/ui.c`.
2. An old borrowed name left in running text (e.g. "the GRIDS page"): Task 8's name check greps for them.
3. A screen name the manual never mentions (a page, model, source, insert type, T mode, PATH, COND): Task 8's
   coverage check lists every string of the `ui.c` name tables missing from `MANUAL.md`.
4. A broken image or a stale capture after a screen change: Task 8 renders and checks every `img/` reference exists;
   `CLAUDE.md` holds the regenerate command.
5. Markdown outside the subset silently mangled (nested list, `_em_`, HTML other than `<details>`): every task's render
   step opens the HTML and the reviewer reads the section rendered, not the source.

## Shared commands

- Captures: `./build.sh --host && app/build/host/quartz_host app/tests/manual_shots.txt app/docs/img`
- Render: `python3 base/tools/manual_html.py app/docs/MANUAL.md app/build/manual.html --name QUARTZ --repo https://github.com/F0nski/quartz --tagline "<tagline from fm1.toml>"`,
  then open `app/build/manual.html` in a browser (Chrome tools) and read the section.
- Tests: `tests/run_tests.sh` → `all tests ok` (it also runs `manual_shots.txt` into `app/build/host`).

---

### Task 1: Project CLAUDE.md

**Files:**
- Create: `CLAUDE.md`
- Modify: memory dir `~/.claude-perso/projects/-home-lucas-perso-fm1/memory/` (drop duplicates, `MEMORY.md`)

**Interfaces:**
- Produces: the rule "a user-visible change updates `app/docs/MANUAL.md` and re-runs the captures in the same commit",
  and the docs index line `app/docs/MANUAL.md`, which Tasks 2-8 fill.

- [ ] **Step 1: Gather.** Read `README.md`, `app/fm1.toml`, `tests/run_tests.sh`, `build.sh`, `docs/cpu.md`, the head
  comment of each `app/*.c`, and memory files `quartz-repo`, `quartz-device-cpu-rules`, `quartz-scene-knob-rule`,
  `quartz-no-project-compat`, `quartz-screen-text-english`, `quartz-preset-in-seq`, `quartz-cpu-tests-at-end`,
  `fm1-install-classic`, `fm1-visual-taste`.
- [ ] **Step 2: Write `CLAUDE.md`** (< 150 lines), sections in this order: Context; Layout (one line per key file of
  `app/`, `tests/`, `docs/`, `base/`); Commands (build, host, tests, captures, install:
  `spike-cpu/.tools/venv/bin/python quartz/base/tools/fm1_install.py --yes --classic quartz/app/build/quartz.fwsc` run
  from `~/perso/fm1`, render); Conventions (screen text English + `words.tsv` entry; own display names vs original code
  identifiers; every encoder of a scene page visibly changes the scene; PRESET useful per source; `Q_VERSION` bumps
  without migration until the firmware is finished; specs and plans in `docs/superpowers/` in French; git identity
  f0nski, remote `github-f0nski` alias); Device (`base/include/fastmath.h` in audio paths, no libm in per-sample loops;
  CPU measured on the device only, batched at the end of a cycle, logged in `docs/cpu.md`; zero LOST BLOCKS); User
  manual (the rule above); Docs index. Present tense, no history, no dates.
- [ ] **Step 3: Verify.** `wc -l CLAUDE.md` < 150; every path named exists (`ls`); every command runs
  (`./build.sh --host`, `tests/run_tests.sh` → `all tests ok`).
- [ ] **Step 4: Trim memory.** Delete the memory files whose whole content is now in `CLAUDE.md`, keep a fact that is
  not (e.g. the flash backup path), and update `MEMORY.md` lines.
- [ ] **Step 5: Commit** `git add CLAUDE.md && git commit -m "CLAUDE.md: the project's context, commands and rules"`

### Task 2: Site plumbing and manual skeleton

**Files:**
- Modify: `app/fm1.toml` (add `tagline`, `repo`, `licence = "GPL-3.0-or-later"`, `credits`)
- Create: `app/site/landing.md`, `app/docs/MANUAL.md`, `app/tests/manual_shots.txt`, `app/docs/img/`

**Interfaces:**
- Consumes: `base/template/site/landing.md` and `base/template/docs/MANUAL.md` as the shape to follow.
- Produces: `MANUAL.md` with the title, a two-sentence intro, the `## Contents` list and the 13 numbered `## N. Title`
  headings of the spec section 4 (Overview, Install, The FM-1 at a glance, Quick start, Tracks patterns and song,
  Sound, Shaping, Sequencer, Sources, Global, Saving, Credits and inspirations, Troubleshooting), each empty; a
  `manual_shots.txt` that boots (`wait 600`) and takes `shot home.png`.

- [ ] **Step 1:** fill `fm1.toml`: tagline one line (e.g. "a four-track groovebox for the M-VAVE FM-1"), `credits`
  one sentence naming Mutable Instruments (Plaits, Grids, Marbles, Clouds) and fm1-base.
- [ ] **Step 2:** write `landing.md` from the template: what QUARTZ is (3-4 sentences), `{{VERSION}}`, links to
  emulator, installer, manual, source; the `home.png` capture.
- [ ] **Step 3:** write the skeleton and the captures script; run the captures command → `app/docs/img/home.png` exists.
- [ ] **Step 4:** render the manual and the landing (`--home`) → both open, contents links jump to each section.
- [ ] **Step 5:** `tests/run_tests.sh` → `all tests ok`. Commit "Manual: skeleton, landing page and captures script".

### Task 3: Sections 1-5 (Overview, Install, FM-1 at a glance, Quick start, Tracks/patterns/song)

**Files:** Modify `app/docs/MANUAL.md`, `app/tests/manual_shots.txt`; Create `app/docs/img/fm1-controls.svg`

**Interfaces:**
- Consumes: Task 2's skeleton. Sources of truth: `app/ui.c` (domains, `DOMAIN_PAGES`, button handlers, long press
  `HOLD_MS`), `app/scene_home.c`, `app/store.c`, `base/template/docs/MANUAL.md` (install wording), spec
  `2026-11-10-quartz-groovebox-design.md` sections 3, 7, 8, `2026-11-12-quartz-step5e-home-design.md`.
- Produces: the vocabulary later sections use: *track*, *pattern* (P1-P8), *page*, *domain* (EDIT, SEQ, GLO, HOME, ENV,
  LFO, ARP, FX), *source*, *scene*, *step*, *p-lock*; the anchor `fm1-controls.svg`.

- [ ] **Step 1:** read the sources above; list every button and what short/long press/hold+key does.
- [ ] **Step 2:** `fm1-controls.svg`: the FM-1's top panel, every control labelled with its name as the manual uses
  it (flat shapes, two colours, `currentColor` text so it reads on the paper style).
- [ ] **Step 3:** write sections 1-5: a "Controls" table (control → what it does), the screen anatomy with a
  capture, the quick start as numbered steps each ending on what you hear/see, HOME TRACKS and SONG tables.
  **Inspired by** line for the overall organisation: OP-Z (read https://teenage.engineering/guides/op-z first).
- [ ] **Step 4:** add their captures to `manual_shots.txt` (`overview-screen.png`, `home-tracks.png`, `home-song.png`,
  …), regenerate, render, read the rendered sections; cross-check each table row against `ui.c`.
- [ ] **Step 5:** `tests/run_tests.sh` → `all tests ok`. Commit "Manual: overview, install, controls, quick start, tracks".

### Task 4: Section 6 Sound

**Files:** Modify `app/docs/MANUAL.md`, `app/tests/manual_shots.txt`

**Interfaces:** Consumes `MODEL_NAME`, pages ENGINE, SOUND, PAD, KIT in `app/ui.c`; `app/voices.c`, `app/scene_*.c`;
`app/dsp/pq.h` (the Plaits registration order: maps each display name to its Plaits model).

- [ ] **Step 1:** read the Plaits manual (https://pichenettes.github.io/mutable-instruments-documentation/modules/plaits/manual/)
  and note per model what HARMONICS / TIMBRE / MORPH do.
- [ ] **Step 2:** write the section: ENGINE and SOUND tables (ranges from `PAGE[]`), a 25-row model table
  (Name | Sound | HARM / TIMB / MORPH | Plaits model), KIT and PAD (16 pads, BD/SN/HH engines), the scene per model in
  one line each. **Inspired by** Plaits; say what QUARTZ adds (DECAY, POLY up to 4, GLIDE, the KIT).
- [ ] **Step 3:** captures `sound-engine.png`, `sound-sound.png`, `sound-kit.png` and 3-4 model scenes; regenerate,
  render, read; check every `MODEL_NAME` string appears in the section.
- [ ] **Step 4:** tests → `all tests ok`. Commit "Manual: sound".

### Task 5: Section 7 Shaping

**Files:** Modify `app/docs/MANUAL.md`, `app/tests/manual_shots.txt`

**Interfaces:** Consumes pages ADSR, FILTER, LFO, ARP, INSERT (`INS_NAME`, `INS_LABEL`), MIX, FX; `LSHAPE_NAME`,
`LRATE_NAME`, `LDEST_NAME`, `AMODE_NAME`, `FTYPE_NAME`; the punch-ins in `app/ui.c`/`app/audio.c`; specs step3a, step3b,
step3c.

- [ ] **Step 1:** read the OP-Z punch-in page (https://teenage.engineering/guides/op-z) and the Clouds manual
  (https://pichenettes.github.io/mutable-instruments-documentation/modules/clouds/manual/) for the reverb credit.
- [ ] **Step 2:** write ENV, LFO (TRIG/FREE), ARP, FX (each insert type with its knobs, MIX, delay/reverb), punch-ins
  (a table of the 16 with how to hold/record them). **Inspired by** OP-Z punch-ins, Clouds reverb.
- [ ] **Step 3:** captures per page; regenerate, render, read, cross-check ranges.
- [ ] **Step 4:** tests → `all tests ok`. Commit "Manual: shaping".

### Task 6: Section 8 Sequencer

**Files:** Modify `app/docs/MANUAL.md`, `app/tests/manual_shots.txt`; Delete `docs/manual/sequencer-entry.html`

**Interfaces:** Consumes pages STEPS, XY, STEP (×3), `PATH_NAME`, `DIV_NAME`, `ATTR_NAME`, `FAMILY`, `COND_NAME`,
`R_*` states; `app/seq.c`; spec `2026-11-12-quartz-step-entry-design.md`; `docs/manual/sequencer-entry.html` (every
fact it holds must land in the section before it is deleted).

- [ ] **Step 1:** read the Digitakt manual's trig conditions, parameter locks and micro timing chapters
  (https://www.elektron.se/support-downloads/digitakt — the PDF manual).
- [ ] **Step 2:** list the facts of `sequencer-entry.html`; write the section: STEPS/PATH (each path in one line,
  XY), recording modes (GRID, NOTES, LIVE, ARM: a table of how to enter each and what the keys do), step functions
  (black-key families, each function with its range, COND's values `NONE FIRST !FIRST PRE !PRE A:B`), p-locks, clearing.
  **Inspired by** Elektron (p-locks, trig conditions, micro timing).
- [ ] **Step 3:** captures (`seq-steps.png`, `seq-held-step.png`, `seq-notes.png`, …); regenerate, render, read;
  tick every fact of the list.
- [ ] **Step 4:** `git rm docs/manual/sequencer-entry.html`; tests → `all tests ok`. Commit "Manual: sequencer
  (absorbs the step-entry page)".

### Task 7: Section 9 Sources

**Files:** Modify `app/docs/MANUAL.md`, `app/tests/manual_shots.txt`

**Interfaces:** Consumes `SRC_NAME`, `GEN_PAGES`, the pages of each source in `PAGE[]`, `T_MODE_NAME`, PRESET per source
(`app/ui.c`), `app/gen.c`, the specs of each source (step4 generators, life, lanes, chords, tonnetz, orbits, mimic).
Template per source, `### <NAME>`: two sentences; its scene capture `source-<name>.png`; a table per page
(Knob | What it does | Range); **PRESET**: one line; **Inspired by**.

- [ ] **Step 1: research**, one source at a time before writing it:
  ATLAS ← Grids manual (https://pichenettes.github.io/mutable-instruments-documentation/modules/grids/manual/);
  FLEET ← Marbles manual (…/modules/marbles/manual/); COLONY ← Conway's Game of Life (Wikipedia);
  LOOM ← Kria in the Ansible docs (https://monome.org/docs/ansible/kria/); LATTICE ← Tonnetz and neo-Riemannian P/L/R;
  MIMIC ← Pachet, "The Continuator" (2003); CADENCE, MOONS ← search for a clear origin (Euclidean/polyrhythm
  sequencers for MOONS, chord sequencers for CADENCE); with none, state they are QUARTZ's own and say so in section 12.
- [ ] **Step 2:** write the intro (what a source is, SOURCE on K4, DIV, synth vs KIT tracks) and the nine
  subsections in `SRC_NAME` order.
- [ ] **Step 3:** captures of each scene (on a synth track, plus a KIT one where the pages differ: COLONY, MOONS);
  MIMIC's LISTEN state; regenerate, render, read, cross-check every range and PRESET line with `ui.c`.
- [ ] **Step 4:** tests → `all tests ok`. Commit "Manual: sources".

### Task 8: Sections 10-13 and the whole-manual checks

**Files:** Modify `app/docs/MANUAL.md`, `app/tests/manual_shots.txt`

**Interfaces:** Consumes pages TEMPO, SCALE (`MODE_NAME`, `QUANT_NAME`, `CLICK_NAME`), KEYS (`KEYS_NAME`), GFX, CPU;
`app/store.c`, `app/fm1.toml` (backup), spec step5g; every **Inspired by** line of Tasks 3-7.

- [ ] **Step 1:** write Global, Saving, Troubleshooting (LOST BLOCKS and CPU page, a project that does not load
  after an update since `Q_VERSION` changes discard old projects, going back to the stock firmware).
- [ ] **Step 2:** write Credits and inspirations: a table (QUARTZ part | Origin | Author | Licence | Link) gathering
  every **Inspired by**, the vendored code (Plaits, Grids, Marbles, Clouds: licences from `README.md`), fm1-base
  (MPL-2.0), the OP-Z guide as the manual's model.
- [ ] **Step 3: coverage check** (one-off script in the scratchpad, not committed): every string of `PAGE[].name`,
  `PAGE[].label`, `MODEL_NAME`, `SRC_NAME`, `INS_NAME`, `T_MODE_NAME`, `PATH_NAME`, `MODE_NAME`, `COND_NAME`,
  `AMODE_NAME`, `LDEST_NAME` found in `MANUAL.md`; old names (`GRIDS|MARBLES|LIFE|LANES|CHORDS|TONNETZ|ORBITS|VA VCF|6-OP|STRING MACH`)
  only inside **Inspired by** lines and section 12. Fix the gaps.
- [ ] **Step 4:** regenerate captures; render; every `img/` reference exists; read the whole manual once at phone
  width (375 px) for tables that overflow.
- [ ] **Step 5:** tests → `all tests ok`. Commit "Manual: global, saving, credits, troubleshooting".
