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

One Plaits voice costs 5 to 11 % of a core for the light models, 12 to 16 % for the common ones and 20 to 21 % for
ADDITIVE and MODAL; the cost is linear in voices and the second core halves the wall time. The generators run once
per step, not per block: their cost does not show on the CPU page.

The screen is drawn by the main loop, outside the cores' figures, which measure the render only: the CPU page's
SCENE FPS shows what a scene gets (30 at most), measured over the last second spent on one.

The EFFECTS row is the effects' share of core 1; the reverb runs on core 2, within its figure. On the device, libm
calls (expf, powf, even fmaxf and fminf) cost far more than on a PC: a PC run underestimated the effects about tenfold.
The per-voice and per-sample code uses fastmath.h, and a filter left open (LP, CUTOFF 127, RES 0) is not computed.
