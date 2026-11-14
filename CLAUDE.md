# QUARTZ

A four-track groovebox firmware for the M-VAVE FM-1 (Jieli AC79: two cores at 360 MHz, 240 x 240 screen, 27 keys,
seven encoders, 14 buttons). It builds on fm1-base (submodule `base/`, MPL-2.0) and vendors Mutable Instruments
Plaits, Grids, Marbles and the Clouds reverb in `app/third_party/`. Licence GPL-3.0-or-later.

## Layout

- `app/app.c`: the unity build's entry; includes the app's own `.c` files. `app/app_ui.c`: the screen's code, linked
  last on the device (`APP_LATE`) so the audio code keeps its place in the flash (`docs/cpu.md`).
- `app/project.h`: `q_project`, everything saved, and `Q_VERSION`. `app/store.c`: the project in flash.
- `app/ui.c`: domains, pages (`PAGE[]`), keys, lights, every name drawn on the screen (`MODEL_NAME`, `SRC_NAME`, …).
- `app/seq.c`: the sequencer (steps, p-locks, recording, song). `app/gen.c`: the pattern sources other than STEP.
- `app/audio.c`: the render, per block, split across the two cores. `app/voices.c`: voice allocation.
  `app/mod.c`: ADSR, LFO, arpeggiator. `app/dsp/`: the C++ DSP (Plaits wrapper `pq`, voice `qvoice`, effects `qfx`).
- `app/p3d.c`: the flat 3D renderer. `app/scene*.c`: one animated scene per engine family, source and global page.
  `app/plateau.c`: the SEQ page's 4x4 plateau.
- `app/words.tsv`: what the screen reader says for each word on the screen.
- `app/fm1.toml`: build and site settings. `app/docs/MANUAL.md`: the user manual; `app/site/landing.md`: the
  site's front page.
- `app/tests/*.txt`: host simulator scripts (`tap`, `turn`, `key`, `wait`, `expect`, `shot`; syntax in
  `base/host/plat_host.c`'s header). `tests/unit/`: C and C++ unit tests.
- `tools/device/`: device CPU measurement over USB (`measure.sh`, `worst_case.py`, `snap.py`).
- `docs/superpowers/specs/`, `docs/superpowers/plans/`: one spec and one plan per feature, in French.
  `docs/screens/`: HTML mockups of the screens.
- `patches/`: changes applied to `base/` by `build.sh`; `base/` itself is never edited.

## Commands

```sh
./build.sh                 # app/build/quartz.fwsc (needs .tools/toolchain and .tools/sdk, see README.md)
./build.sh --host          # app/build/host/quartz_host, the simulator
tests/run_tests.sh         # unit tests, then every app/tests/*.txt; ends with "all tests ok"
app/build/host/quartz_host app/tests/X.txt app/build/host     # one simulator script; shots land in the 2nd arg
rm -rf app/build/shots && mkdir -p app/build/shots && app/build/host/quartz_host app/tests/manual_shots.txt app/build/shots && cp app/build/shots/*.png app/docs/img/   # the manual's captures
python3 base/tools/manual_html.py app/docs/MANUAL.md app/build/manual.html --name QUARTZ   # preview the manual
# the whole site (front page, installer, emulator, manual) in app/build/site, for a release X.Y; needs Emscripten
source .tools/emsdk/emsdk_env.sh && ./build.sh --release X.Y && ./build.sh --web --release X.Y \
  && (cd base && python3 tools/make_site.py ../app ../app/build/quartz-X.Y.fwsc X.Y ../app/build/site)
```

`base/tools/publish_site.sh` pushes the site to the `gh-pages` branch: only when the author asks.

Install on the author's FM-1, from `~/perso/fm1`:
`spike-cpu/.tools/venv/bin/python quartz/base/tools/fm1_install.py --yes --classic quartz/app/build/quartz.fwsc`.
Always `--classic`: the fast install stalls at 3 % on this unit's loader; if it stalls, run it again to resume.

## Conventions

- Every text drawn on the screen is English (mockup canvases too) and has its line in `app/words.tsv`.
- Display names are QUARTZ's own (ATLAS, FLEET, SWEEP, CHLADNI…); code identifiers keep the original names
  (`P_GRIDS`, `SRC_MARBLES`, Plaits' model order). The mapping lives in `app/ui.c`'s name tables.
- On a page that shows a scene, every encoder visibly changes the scene, not only through the notes played.
- PRESET has a practical use on every pattern source's pages (step editing where possible).
- A change to `q_project` bumps `Q_VERSION`; old projects are discarded, never migrated, until the firmware is
  finished.
- Scenes: flat low-poly 3D, moderate neon, no gradients; editing pages (SEQ) stay a straight, readable grid.
- One feature = one spec, one plan, one branch; commits are small and say what changes for the player.
- Git: personal identity f0nski, remote through the `github-f0nski` SSH alias. Never push or merge without asking.

## Device

- The FPU is single precision without a fast libm: in per-sample and per-voice-block code, use
  `base/include/fastmath.h` (`fm_expf`, `fm_sinf`, `fm_maxf`…), never `libm` or `fmaxf`. Check hot functions for
  `call`s in `app/build/quartz.dis`.
- CPU is measured on the device only: PC timings are off by ~10x. Measurements are batched at the end of a cycle,
  with the GLO CPU page or `tools/device/measure.sh`, and logged in `docs/cpu.md`. A change is accepted with zero
  LOST BLOCKS in its worst case.

## User manual

Every change the player can see or hear (a page, a knob, a name, a range, a gesture, a default) updates
`app/docs/MANUAL.md` in the same commit, and re-runs the captures command above when a screen changes, committing
the changed images in `app/docs/img/`. The manual names a borrowed idea in an **Inspired by** line, from the
original's own documentation. The Markdown stays within what `base/tools/manual_html.py` handles (its header).

## Docs

- `app/docs/MANUAL.md`: read before changing anything the player sees; it is the behaviour as documented.
- `docs/cpu.md`: read before touching audio code or the link order.
- `docs/superpowers/specs/2026-11-10-quartz-groovebox-design.md`: the overall design; each feature's spec beside it.
