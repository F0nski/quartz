# QUARTZ

QUARTZ turns the M-VAVE FM-1 into a four-track groovebox. Each track plays one of 24 models (21 synths, 3 drums) or a 16-pad drum kit,
and each pattern takes its notes from a step sequencer or from one of nine generative sources: a drum map, random
rhythms, the Game of Life, polymetric lanes, chord progressions, a walk on the Tonnetz, moons on orbits, an
improviser that learns what you play, or bytebeat formulas. Every page shows an animated scene of what you hear.
PRINT freezes what a generative source plays into steps you can edit; live transposition, solo, latched punch-ins and
Euclidean rhythms are a gesture away. The keyboard plays in the scale and splits in two: chords of the scale on the
left, changed by the black keys and named on the screen, the melody on the right. It keeps four projects, copies patterns and sounds between them, undoes the last
clear, draw or paste, and backs them up to a computer over USB.

![The HOME page: the four tracks on their pedestals](manual/img/home.png "QUARTZ on the FM-1's screen")

This is version {{VERSION}}. You install it at your own risk, and the installer can put M-VAVE's own firmware back.

- [Try it in the browser](emu/). It runs the same code as the FM-1, with sound.
- [Install](install/) it from Chrome or Edge, with the FM-1 connected by USB.
- [Back up](backup/) your projects to a file, and put them back, from Chrome or Edge. The emulator opens the same files.
- The [Manual](manual/) explains everything, from the first beat to a page for each family of models and each source.
- [Download {{PKG}}](firmware/{{PKG}}) to use the command-line installer: `python3 tools/fm1_install.py {{PKG}}`.
- The [Source](https://github.com/F0nski/quartz) is under GPL-3.0-or-later. It's built on fm1-base and on Mutable
  Instruments' Plaits, Grids, Marbles and Clouds.

M-VAVE and FM-1 are trademarks of their owners. QUARTZ is not affiliated with M-VAVE.
