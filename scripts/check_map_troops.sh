#!/bin/sh
# DOS floppy native troop icons, hit testing, real move orders and arrival.
# Expected stationary bounds were read from CS:74af's icon records in the
# original executable, on the unmodified chapter-20 state. No game image or
# save is generated from source; the local fidelity fixture is required.
# This fixture is not distributed with the public engine repository.
# Set DUNE_MAP_TROOPS_SAVE to the matching chapter-20 floppy reference save;
# the exact sprite checks require the documented troop positions.
set -eu
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
local_root="${DUNE_LOCAL_BUILD_ROOT:-${TMPDIR:-/tmp}/dune-scummvm-native-build}"
run_root="${DUNE_RUN_ROOT:-$local_root/sdl-run}"
case_root="$run_root/map-troops-check"
fixture="${DUNE_MAP_TROOPS_SAVE:-$repo_root/tests/fidelity/saves/saboteurs/DUNE21S1.SAV}"
data="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}"
if [ ! -f "$fixture" ]; then
	# The fixture is a test save that is not published; without it there is
	# nothing to check (set DUNE_MAP_TROOPS_SAVE to a mid-game floppy save).
	echo "SKIP map troops: no fixture save ($fixture); set DUNE_MAP_TROOPS_SAVE"
	exit 0
fi
"$script_dir/test_dune_scummvm_sdl.sh" build >/dev/null 2>&1 || {
	echo "FAIL map troops: desktop build failed"
	exit 1
}
mkdir -p "$case_root/fast/saves" "$case_root/animation/saves"
python3 - "$script_dir" "$fixture" "$case_root" <<'PY'
import sys
from pathlib import Path
sys.path.insert(0, sys.argv[1])
from dune_save_patch import unpack, pack
source = Path(sys.argv[2])
if not source.is_file():
    raise SystemExit("FAIL map troops: local chapter-20 fidelity save missing (DUNE_MAP_TROOPS_SAVE)")
root = Path(sys.argv[3])
packed = source.read_bytes()
body = unpack(packed)
base = len(body) - 4718
assert base >= 0, "invalid floppy save"
# Undo only the saboteur fixture's six documented changes; the resulting
# state matches the original chapter-20 save used for reference captures.
for at, value in {0x25a: 0x50, 0x264: 0, 0x8ad: 0, 0x8bb: 1, 0x8c3: 0, 0x118e: 1}.items():
    body[base + at] = value
cases = [{}, {0x8ba: body[base + 0x8ba] | 0x10}, {0x8ad: 0x80}, {0x1174: 1, 0x1175: 0}]
for index, edits in enumerate(cases, 1):
    state = bytearray(body)
    for at, value in edits.items():
        state[base + at] = value
    (root / 'fast/saves' / f'DUNE21S{index}.SAV').write_bytes(pack(state, packed))
(root / 'animation/saves/DUNE21S1.SAV').write_bytes(pack(body, packed))
(root / 'fast.ini').write_text('dune_no_music=1\n')
(root / 'animation.ini').write_text('dune_no_music=1\ndune_real_time=true\n')
PY
DUNE_SKIP_BUILD=1 DUNE_RUN_ROOT="$case_root/fast" DUNE_DATA="$data" \
	"$script_dir/test_dune_scummvm_sdl.sh" harness "$repo_root/tests/regression/map-troops.script" \
	"$case_root/frames" "$case_root/fast.ini" > "$case_root/fast-stdout.log" 2>&1 || {
	echo "FAIL map troops: input harness failed (see $case_root/fast-stdout.log)"
	exit 1
}
python3 - "$case_root" <<'PY'
import re, sys
from pathlib import Path
root = Path(sys.argv[1])
log = (root / 'fast/saves/dune-ios.log').read_text(errors='replace')
failed = []
def check(ok, name):
    print(('PASS' if ok else 'FAIL') + ' map troops: ' + name)
    if not ok: failed.append(name)
def before(end, start=''):
    text = log.split('checkpoint ' + end, 1)[0]
    return text.split('checkpoint ' + start, 1)[-1] if start else text
pattern = re.compile(r'Map icon: troop (\d+) script ([0-9a-f]+) sprite (\d+) rect (-?\d+),(-?\d+),(-?\d+),(-?\d+)')
def icons(text):
    return {int(m[0]): (int(m[1], 16), *map(int, m[2:])) for m in pattern.findall(text)}
expected = {8: (0x16cb, 0, 296, 14, 311, 29), 3: (0x16cf, 26, 228, 82, 245, 97),
            1: (0x16ff, 19, 232, 137, 250, 153), 2: (0x16ff, 19, 198, 21, 216, 37)}
check(icons(before('troops-stationary')) == expected,
      'visible IDs, executable-selected sprites (including frame zero), and native anchor bounds')
contact = before('troops-contact', 'troops-stationary')
check('Map: troop 1 selected by icon' in contact and 'Troops: orders for troop 1' in contact,
      'clicking the displayed troop opens its actual contact')
check('[MOVE TROOP] [CUT CONTACT]' in contact, 'contact exposes existing move and close actions')
dispatch = before('troops-dispatch', 'troops-contact')
check('Troop command: MOVE TROOP' in dispatch and 'Troops: troop 1 marches' in dispatch,
      'the UI destination click issues a move order')
start = icons(before('troops-march-start', 'troops-contact')).get(1)
step = icons(before('troops-march-step', 'troops-march-start')).get(1)
check(start and start[:2] == (0x184c, 3) and 81 < start[3] < 137,
      'the north-facing march uses a GPS position between its source and destination')
check('Map: troop 1 selected by icon' in before('troops-moving-contact', 'troops-march-start'),
      'a marching troop can be contacted through its displayed icon')
arrived = before('troops-arrived', 'troops-march-start')
check('Troops: troop 1 arrives at place 11' in arrived and icons(arrived).get(1) == (0x16ff, 19, 249, 81, 267, 97),
      'arrival replaces the march with a stationed icon at the destination')
hidden = before('troops-hidden', 'troops-arrived')
check('Saves: slot 1 loaded' in hidden and 1 not in icons(hidden), 'hidden troops have no visible or selectable icon')
unhired = before('troops-unhired', 'troops-hidden-click')
check('Saves: slot 2 loaded' in unhired and icons(unhired).get(1, ())[:2] == (0x1848, 96),
      'a visited unhired troop shows its original question mark')
restricted = before('troops-local-range', 'troops-unhired-click')
check('Saves: slot 3 loaded' in restricted and 1 in icons(restricted), 'local-range fixture still draws the remote troop')
check(log.count('selected by icon') == 2 and log.count('Troops: orders for troop 1') == 2,
      'hidden, unhired, and out-of-range clicks never open troop orders')
check('checkpoint troops-local-range-click' in log and 'Script line' in log and 'quit' in log, 'all cases complete cleanly')
if failed: raise SystemExit(1)
PY
# A separate timed run exercises the actual update/render task, while the
# deterministic input run above remains independent of animation phase.
{
	printf 'wait 3000\nkey ESC\nwait 4000\nkey ESC\nwait 6000\n'
	awk 'NR > 3 { print } /checkpoint troops-stationary/ { exit }' "$repo_root/tests/regression/map-troops.script"
	printf 'wait 1100\ncheckpoint troops-animated\nquit\n'
} > "$case_root/animation.script"
DUNE_SKIP_BUILD=1 DUNE_RUN_ROOT="$case_root/animation" DUNE_DATA="$data" \
	"$script_dir/test_dune_scummvm_sdl.sh" harness "$case_root/animation.script" \
	"$case_root/animation-frames" "$case_root/animation.ini" > "$case_root/animation-stdout.log" 2>&1 || {
	echo "FAIL map troops: timed animation harness failed"
	exit 1
}
python3 - "$case_root/animation/saves/dune-ios.log" <<'PY'
import re, sys
from pathlib import Path
s = Path(sys.argv[1]).read_text(errors='replace')
a = s.split('checkpoint troops-stationary', 1)[-1].split('checkpoint troops-animated', 1)[0]
frames = set(map(int, re.findall(r'Map icon: troop 1 script 16ff sprite (\d+)', a)))
if len(frames) < 2 or not frames <= {19, 20, 21, 22}:
    raise SystemExit(f'FAIL map troops: timed mining animation frames {sorted(frames)}')
print('PASS map troops: the live update task advances authored sprite frames')
PY

# Four synthetic save variants exercise executable-selected equipment,
# army/ecology/Harkonnen, captured, battle and unvisited states.
python3 - "$script_dir" "$fixture" "$case_root" <<'PY'
import sys
from pathlib import Path
sys.path.insert(0, sys.argv[1])
from dune_save_patch import unpack, pack
root = Path(sys.argv[3]); saves = root / 'variants/saves'; saves.mkdir(parents=True, exist_ok=True)
packed = Path(sys.argv[2]).read_bytes(); body = unpack(packed); base = len(body) - 4718
# (id: occupation, equipment, position, Harkonnen)
cases = [
    {1:(0,0x80,1,False),2:(0,0x40,1,False),3:(0,0xc0,1,False),8:(0,0,1,False)},
    {1:(0x10,0x80,1,False),2:(0x30,0x40,1,False),3:(0x10,0xc0,1,False),8:(0x11,0,1,False)},
    {1:(6,0,4,False),2:(8,0,1,False),3:(0x8d,0,1,True),8:(6,0,1,False)},
    {1:(6,0,1,False),2:(0x80,0,1,False),3:(0x28,0,1,False),8:(0x8d,0,5,True)},
]
for number, edits in enumerate(cases, 1):
    state = bytearray(body)
    for id_, (occ, equipment, slot, harkonnen) in edits.items():
        at = base + 0x8aa + 27 * (id_ - 1)
        state[at + 2], state[at + 3], state[at + 0x19] = slot, occ, equipment
        state[at + 0x10] = (state[at + 0x10] & ~0x90) | (0x80 if harkonnen else 0)
    if number == 3:
        state[base + 0x234 + 8] = 0x28  # Harkonnen stationed at a visible fortress.
    if number == 4:
        state[base + 0x250 + 10] |= 2
        state[base + 0x26c + 10] &= ~0x10
    (saves / f'DUNE21S{number}.SAV').write_bytes(pack(state, packed))
lines = ['wait 15000', 'click left 160 163', 'wait 1000']
for number in range(4):
    lines += ['click left 35 176','wait 500','click left 160 187','wait 500',
              f'click left 160 {163 + 8 * number}','wait 1000','click left 160 163','wait 500',
              f'checkpoint troops-variant-{number + 1}']
lines += ['quit']
(root / 'variants.script').write_text('\n'.join(lines) + '\n')
PY
DUNE_SKIP_BUILD=1 DUNE_RUN_ROOT="$case_root/variants" DUNE_DATA="$data" \
	"$script_dir/test_dune_scummvm_sdl.sh" harness "$case_root/variants.script" \
	"$case_root/variant-frames" "$case_root/fast.ini" > "$case_root/variants-stdout.log" 2>&1 || {
	echo "FAIL map troops: equipment and occupation fixtures failed"
	exit 1
}
python3 - "$case_root/variants/saves/dune-ios.log" <<'PY'
import re, sys
from pathlib import Path
s = Path(sys.argv[1]).read_text(errors='replace')
expected = [
    {1:(0x17e5,100),2:(0x17f2,105),3:(0x182f,108),8:(0x16ff,19)},
    {1:(0x183c,99),2:(0x1840,104),3:(0x1844,107),8:(0x16cf,26)},
    {1:(0x1765,39),2:(0x1783,71),3:(0x179d,80),8:(0x175b,27)},
    {1:(0x16d3,1),3:(0x16d7,2),8:(0x17b1,84)},
]
names = ['harvester/ornithopter combinations', 'stopped/captured equipment variants',
         'soldier slot, ecology and Harkonnen fortress sprites', 'battle, captured ecology and unvisited troop suppression']
for i, (wanted, name) in enumerate(zip(expected, names), 1):
    text, s = s.split(f'checkpoint troops-variant-{i}', 1)
    got = {int(a): (int(b,16),int(c)) for a,b,c in re.findall(r'Map icon: troop (\d+) script ([0-9a-f]+) sprite (\d+)', text)}
    if got != wanted:
        raise SystemExit(f'FAIL map troops: {name}: {got} != {wanted}')
    print('PASS map troops: ' + name)
PY
