#!/usr/bin/env python3
"""worst_case.py OUT.txt: the worst case of docs/cpu.md as panel moves, for fm1_remote.py and the simulator's scripts.

From a fresh start (every page domain on its first page: install or restart first): tracks 1 MODAL and 2 6-OP A, POLY
3, pattern P8 (empty in a new project) at LEN 16 with a 3-note chord on each step written on SEQ's grid (REC), PHASER on both, DELAY and REVERB
sent fully, COMP fully. Every value is set by turning past its end first, so the project's own values do not matter.
The `expect` lines check it in the simulator; drop them for the device:
  grep -v '^expect' OUT.txt | sed 's/$/ wait 80/' | xargs fm1_remote.py ... tap PLAY"""
import sys

WH = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26]   # the white keys: steps 1..16
L, page = [], {}
# PRESET on SOUND steps through the models in ui.c's MODEL_ORDER (by family), not in Plaits' order
MODEL_ORDER = [8, 0, 1, 9, 10, 2, 3, 4, 13, 5, 12, 11, 14, 6, 7, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24]


def go(btn, p):                     # a domain's button, then its page p (1-based): SELECT wraps round
    L.append(f"tap {btn}")
    cur = page.get(btn, 1)
    if p != cur:
        L.extend([f"turn SELECT {p - cur}", f"expect page {p}"])
    page[btn] = p


def track(t, model):
    L.extend(["turn ALGO -8", f"turn ALGO {t}", f"expect track {t + 1}"])
    go("EDIT", 2)
    L.extend(["turn PRESET -30", f"turn PRESET {MODEL_ORDER.index(model)}", f"expect model {model}", "turn K4 -5", "turn K4 2", "expect poly 3"])
    go("SEQ", 1)
    L.extend(["turn PRESET 7", "expect pattern 8", "turn K4 -5", "expect src 0", "turn K1 -70", "turn K1 15",
              "expect len 16"])
    L.extend(["tap REC", "expect rec 1"])     # GRID: the white keys are the 16 steps
    for i, w in enumerate(WH):
        chord = [16, 19, 23] if w not in (16, 19, 23) else [4, 7, 11]
        L.append(f"key {w} down")
        for k in chord:
            L.extend([f"key {k} down", f"key {k} up"])
        L.extend([f"key {w} up", f"expect stepnotes_{i + 1} 3"])
    L.extend(["tap REC", "tap REC", "expect rec 0"])   # GRID, NOTES, then off: the keys and PRESET free again
    go("FX", 2)                     # INSERT: its type on PRESET
    L.extend(["turn PRESET -10", "turn PRESET 5", "expect itype 5"])
    go("FX", 1)                     # MIX: the delay and reverb sends
    L.extend(["turn K1 127", "turn K2 127", "expect dsend 127", "expect rsend 127"])


track(0, 20)
track(1, 2)
go("GLO", 1)
L.extend(["turn K3 127", "expect comp 127"])
open(sys.argv[1], "w").write("\n".join(L) + "\n")
