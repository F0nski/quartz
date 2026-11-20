# CPU on the FM-1

Measured on the author's FM-1 (360 MHz, two cores, 256-frame blocks at 44.1 kHz). The GLO CPU page shows each
core's load and LOST BLOCKS; a step is validated only with zero lost blocks in its worst case.

| Case | Core 1 / core 2 | Lost blocks |
|---|---|---|
| MODAL × 6 | 69 % / 63 % | 0 |
| ADDITIVE and mixed models × 6 | 50 % / 50 % | 0 |
| Step 2 worst case (ratchets, p-locks, scene launches) | not recorded | 0 |
| Step 4 worst case (KIT on GRIDS, MARBLES, two heavy POLY tracks) | not recorded | 0 |
| Step 3a (filter, LFO, arp; played by ear, worst case not run) | not recorded | not recorded |
| Step 3b: MODAL x 6 (two POLY 3 tracks), delay and reverb | 82 % / 74 % (effects 6 %) | 0 |
| Step 3b: the same, two PHASER inserts, COMP | 87 % / 71 % (effects 13 %) | 0 |
| Step 3c: the 16 punch-ins held one by one, recorded and played back (with step 5a's case) | see step 5a | 0 |
| Step 5a: MODAL and 6-OP A (POLY 3 each, 6 voices), delay, reverb, two PHASER, COMP, the planet on ENGINE (29 fps) | 64 % / 43 % (effects 12 %) | 0 |
| Steps 5b and 5c, scripted over USB (below): MODAL and 6-OP A, POLY 3, a 3-note chord on each of 16 steps, delay, reverb, two PHASER, COMP; 5b's firmware | 61-64 % / 46-48 % | 0 |
| The same, 5c's firmware as first built (the audio code moved in the flash) | 74-84 % / 53-65 % | 2-8 a minute, scene or not |
| The same, 5c with the audio code placed (o2_first, the screen's code last), MODAL on ENGINE (20 fps) | 70-75 % / 58-63 % | 0 in 3 minutes |
| Step 5d1 (the statics unmerged, the screen's code last), track 1 MODAL on ENGINE (21 fps) | 75 % / 63 % | 0 |
| The same, track 1 VA VCF: the crests (25 fps) | 64 % / 46 % | 0 |
| The same, track 1 WAVETABLE: the bowl (23 fps) | 61 % / 48 % | 0 |
| The same, track 1 ADDITIVE: the crystal garden (16 fps, the heaviest to draw) | 71 % / 62 % | 0 |
| The same, track 1 CHIPTUNE: the platforms (27 fps) | 47 % / 47 % | 0 |
| Step 5d2, the same worst case, track 1 CHORDS: the choir (29 fps) | 67 % / 49 % | 0 |
| The same, track 1 STRING: the tightrope walker (25 fps) | 69 % / 53 % | 0 |
| The same, track 1 NOISE: the dancefloor (23 fps) | 70 % / 54 % | 0 |
| The same, track 1 SWARM: the dancefloor (25 fps) | 68 % / 49 % | 0 |
| The same, but track 1 MODAL and track 2 PHASE DIST: the crests over a heavier render (14 fps) | 79 % / 67 % | 0 |
| Step 5e, the worst case (MODAL, 6-OP A) on HOME's tracks page (17 fps), three 45 s windows | 74-79 % / 57-63 % | 2, 4, 2 |
| The same session, the ENV page (no scene), 45 s | 81 % / 57 % | 1 |
| Step 5f, the same worst case on the ENV FILTER page: the harmonics (20 fps), two 45 s windows | 74 % / 65 % | 0 |
| Step 5g, the same worst case on GLO's TEMPO page: the flat bar (27 fps), 45 s | 81 % / 58 % | 0 |
| 0.7, probe.py over 30 s, twice: tracks 2 and 3 MODAL POLY 3 on LATTICE and MOONS, a KIT on ATLAS, PHASER on both, sends full, COMP | 86 % / 63 % | 0.2-0.4 a second |
| The unity build at -Oz (fm1.toml unity_opt: 578 148 B to 524 900 B), worst_case.py, probe.py 2 x 60 s; the -Os build just before in the same session: 74 % / 65 % | 76 % / 66 % | 0 |
| CIPHER (image 530 596 B), worst_case.py, probe.py 2 x 60 s | 76 % / 66 % | 0 |
| The same, track 3 on CIPHER (CREST, one note a step): 7 voices wanted for 6, 10.7 stolen/s | 80 % / 61 % | 0, then 0.1/s |
| The same, track 3 on FLEET T OUT 2 instead (a note every step): the overload is the third track's, not CIPHER's | 78 % / 62 % | 2.0/s |
| Backup and restore over USB (image 534 836 B, the render's code 38 B further), worst_case.py, probe.py 2 x 60 s; a backup taken while playing, 20 s | 76 % / 66 % | 0 |
| ATLAS's LEN (image 535 004 B, the render's code 8 B further), worst_case.py, probe.py 2 x 60 s | 76 % / 66 % | 0 |
| COND FILL and UNDO (image 535 068 B), worst_case.py, probe.py 2 x 60 s | 76 % / 66 % | 0 |
| Four projects (image 535 452 B, a 53-sector store pool), worst_case.py, probe.py 2 x 60 s | 76 % / 66 % | 0 |

One Plaits voice costs 5 to 11 % of a core for the light models, 12 to 16 % for the common ones and 20 to 21 % for
ADDITIVE and MODAL; the cost is linear in voices and the second core halves the wall time. The generators run once
per step, not per block: their cost does not show on the CPU page.

The screen is drawn by the main loop, outside the cores' figures, which measure the render only: the CPU page's
SCENE FPS shows what a scene gets (30 at most), measured over the last second spent on one.

The EFFECTS row is the effects' share of core 1; the reverb runs on core 2, within its figure. On the device, libm
calls (expf, powf, even fmaxf and fminf) cost far more than on a PC: a PC run underestimated the effects about tenfold.
The per-voice and per-sample code uses fastmath.h, and a filter left open (LP, CUTOFF 127, RES 0) is not computed.

Where the audio code sits in the XIP flash changes its cost through the caches (8 ways of 4 KiB): 12 KB of inert code
added in front of 5b's DSP raised the render from 63 % to 78 % of core 1. So the audio's code keeps a fixed place
(fm1.toml o2_first, patches/fm1-base-layout.patch): the DSP linked first and its tables from a 4 KiB boundary, then the
platform's and the render's code, the screen's code last (app.h APP_LATE, app_ui.c): adding scenes no longer moves it.
A 512-byte pad in front of the DSP changes nothing (MODAL x 6 with a KIT and the effects, probe.py: 86 % / 63 % with or
without): the spread from one session to the next (68-83 % of core 1 on the same build) is the measurement's. A change
to the DSP's or the platform's code moves the audio code: measure it.

Clang emits a function where it is first named, callees included: a screen function called from app.c or from the
platform lands before the platform's code and moves the audio interrupt with it (the boot splash's first version moved
it by 1.2 KB). The render's side reaches the screen only through a pointer the screen's side sets (app.h boot_step).

The unity build's statics are not merged (fm1.toml unity_cflags = -mllvm -enable-global-merge=false): LLVM merged them
into blocks, and a static added to the screen's code changed how the platform's and the render's code reached theirs,
moving them by tens of bytes.

The firmware's CPU probe gives the readings without the screen: tools/device/probe.py SECS prints both cores'
load, LOST BLOCKS, the voices stolen and the held notes of another track cut per second, the voices sounding, the mutes and each track's model, source and
poly (`--mutes MASK` sets the mutes first). The probe (app_ui.c cpu_probe) sends them as CCs on channel 16 only while
a screen reader listens, which probe.py's HELLO makes it do: a DAW gets nothing otherwise. The mirror test build below
no longer fits the RAM. A setup sent with fm1_remote.py can lose a turn: check it in probe.py's tracks line.

Since 5d1 the setup is tools/device/worst_case.py, the readings tools/device/measure.sh (a mirror test build, the CPU
page captured over USB). These figures come from a test build with the screen mirror (FM1B_MIRROR=1 FM1B_POOL_SPARE=0 ./build.sh), the setup
played and the CPU page read over USB (base/tools/fm1_remote.py, fm1_mirror.py).

The worst case runs at the edge: from one session to the next its render takes 74 to 81 % of core 1, and at the top of
that range it loses a block now and then, whatever the page shows (step 5e: one on the ENV page, with no scene). The
scenes' cost is the main loop's, not the render's; the margin would come from the render's hot loops in RAM.

