#!/usr/bin/env python3
"""snap.py OUT.png SECONDS: keep an FM-1's screen mirror on for SECONDS, gathering what it sends, then write the screen.

The firmware must be a test build with the mirror (FM1B_MIRROR=1 FM1B_POOL_SPARE=0 ./build.sh). base's fm1_mirror.py
stops as soon as the screen goes quiet, which leaves a static page half sent; this keeps the mirror alive (every
second) and asks for the whole screen again 1.5 s before the end. Drive the panel meanwhile with fm1_remote.py.
Needs mido with python-rtmidi."""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "base", "tools"))
import fm1_mirror as M  # noqa: E402

B, I = M.B, M.I


def main(out, secs):
    dev = B.find(I.MidoBackend(), None, B.ANY)
    link, scr = dev.link, M.Screen()
    try:
        link.drain()
        link.send(B.frame(M.MR_REQ, b"\x02"))
        t0 = alive = time.monotonic()
        resent = False
        while time.monotonic() - t0 < secs:
            pkt = link.read(0.1)
            if pkt is not None:
                scr.frame(pkt)
            if time.monotonic() - alive > 1.0:
                late = time.monotonic() - t0 > secs - 1.5 and not resent
                link.send(B.frame(M.MR_REQ, b"\x02" if late else b"\x01"))
                resent = resent or late
                alive = time.monotonic()
    finally:
        link.send(B.frame(M.MR_REQ, b"\x00"))
        link.close()
    scr.png(out, 1)
    print(f"{out}: {scr.frames} frames, {scr.bad} bad")


if __name__ == "__main__":
    main(sys.argv[1], float(sys.argv[2]))
