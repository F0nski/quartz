# QUARTZ

QUARTZ turns the M-VAVE FM-1 into a four-track groovebox. Each track plays one of 21 synth models or a 16-pad drum kit,
and each pattern takes its notes from a step sequencer or from one of eight generative sources: a drum map, random
rhythms, the Game of Life, polymetric lanes, chord progressions, a walk on the Tonnetz, moons on orbits, or an
improviser that learns what you play. Every page shows an animated scene of what you hear.

![The HOME page: the four tracks on their pedestals](manual/img/home.png "QUARTZ on the FM-1's screen")

This is version {{VERSION}}. You install it at your own risk, and the installer can put M-VAVE's own firmware back.

- [Try it in the browser](emu/). It runs the same code as the FM-1, with sound.
- [Install](install/) it from Chrome or Edge, with the FM-1 connected by USB.
- The [Manual](manual/) explains everything, from the first beat to each source.
- [Download {{PKG}}](firmware/{{PKG}}) to use the command-line installer: `python3 tools/fm1_install.py {{PKG}}`.
- The [Source](https://github.com/F0nski/quartz) is under GPL-3.0-or-later. It's built on fm1-base and on Mutable
  Instruments' Plaits, Grids, Marbles and Clouds.

M-VAVE and FM-1 are trademarks of their owners. QUARTZ is not affiliated with M-VAVE.
