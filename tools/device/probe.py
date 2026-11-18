#!/usr/bin/env python3
"""probe.py [--mutes MASK] SECS: read the FM-1's CPU probe (app_ui.c cpu_probe) for SECS seconds and print the cores'
load, LOST BLOCKS, the voices stolen and the held notes of another track cut per second, the mutes, the voices sounding and each track's model, source and
poly. The probe speaks only while a screen reader listens: this sends the reader's HELLO every 2 s, its BYE at the
end.
--mutes MASK: first set the mutes to MASK (bit t: track t + 1 muted), through HOME held and white keys 1-4.
Needs mido with python-rtmidi (FM1_PY in measure.sh's sense: run it with that Python)."""
import argparse
import subprocess
import sys
import time

import mido

HELLO = [0x7D, 0x53, 0x52, 1, 1]   # the screen reader's HELLO (base/device/announce.c)
BYE = [0x7D, 0x53, 0x52, 2]        # and its BYE: the probe stops at once
WHITE = [0, 2, 4, 6]               # white keys 1-4: fm1_remote.py's key numbers
N = 25                             # CC 102..126


def frames(inp, out, secs):
    """the probe's frames over secs: dicts of CC number -> value, one a second"""
    v, rows, t0, hello = {}, [], time.time(), 0.0
    while time.time() - t0 < secs:
        if time.time() - hello > 2:
            out.send(mido.Message('sysex', data=HELLO))
            hello = time.time()
        for m in inp.iter_pending():
            if m.type == 'control_change' and m.channel == 15 and 102 <= m.control < 102 + N:
                v[m.control] = m.value
                if m.control == 102 + N - 1 and len(v) == N:
                    rows.append(dict(v))
        time.sleep(0.02)
    return rows


def word(r, cc):
    return r[cc] | r[cc + 1] << 7 | r[cc + 2] << 14


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('secs', type=int)
    ap.add_argument('--mutes', type=int)
    a = ap.parse_args()
    port = next(n for n in mido.get_input_names() if 'QUARTZ' in n)
    with mido.open_input(port) as inp, mido.open_output(port) as out:
        if a.mutes is not None:
            for _ in range(4):
                rows = frames(inp, out, 2.5)
                diff = (rows[-1][107] ^ a.mutes) & 15 if rows else 0
                if not diff:
                    break
                cmd = ['press', 'HOME', 'wait', '200']
                for t in range(4):
                    if diff >> t & 1:
                        cmd += ['tapkey', str(WHITE[t]), 'wait', '250']
                subprocess.run([sys.executable, 'base/tools/fm1_remote.py'] + cmd + ['release', 'HOME'],
                               capture_output=True)
            time.sleep(2)
        rows = frames(inp, out, a.secs + 2.5)[1:]
        out.send(mido.Message('sysex', data=BYE))
    if len(rows) < 2:
        sys.exit('no probe frames: is QUARTZ running, with the probe?')
    r = rows[-1]
    secs = len(rows) - 1
    print('tracks', ' | '.join('model %d src %d poly %d' % (r[109 + 3 * t], r[110 + 3 * t], r[111 + 3 * t])
                               for t in range(4)))
    print('mutes %d  core1 %.0f%%  core2 %.0f%%  voices max %d  lost/s %.1f  stolen/s %.1f  cuts/s %.1f' % (
        r[107], sum(x[102] for x in rows) / len(rows), sum(x[103] for x in rows) / len(rows),
        max(x[108] for x in rows), (word(r, 104) - word(rows[0], 104)) / secs,
        (word(r, 121) - word(rows[0], 121)) / secs, (word(r, 124) - word(rows[0], 124)) / secs))


if __name__ == '__main__':
    main()
