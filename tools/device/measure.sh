#!/usr/bin/env zsh
# measure.sh LABEL SECS REMOTE-ACTIONS...: do the actions (show a page), wait SECS, then open GLO's CPU page and keep
# its picture as app/build/device/m_LABEL.png. The page is checked on the mirrored screen against cpu_head.png (its
# header), SELECT turned until it matches: a turn sent from the computer can be lost. A mirror test build is needed
# (snap.py). FM1_PY: a Python with mido and python-rtmidi (default python3).
set -u
cd "$(dirname "$0")/../.."
P=${FM1_PY:-python3}
R=base/tools/fm1_remote.py
O=app/build/device
mkdir -p $O
label=$1 secs=$2; shift 2
$P $R "$@" wait $((secs * 1000)) tap GLO wait 500 > /dev/null 2>&1
for try in 1 2 3 4 5 6; do
    timeout 20 $P -I tools/device/snap.py $O/m_$label.png 3 > /dev/null 2>&1
    if python3 -c "
from PIL import Image, ImageChops
a = Image.open('$O/m_$label.png').convert('RGB').crop((60, 4, 130, 26)); b = Image.open('tools/device/cpu_head.png').convert('RGB')
raise SystemExit(0 if ImageChops.difference(a, b).getbbox() is None else 1)"; then
        echo "$label: $O/m_$label.png"; exit 0
    fi
    $P $R turn SELECT 1 wait 500 > /dev/null 2>&1
done
echo "$label: CPU page not reached"
exit 1
