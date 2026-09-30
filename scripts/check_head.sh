#!/bin/sh

# Paul's head on the panel (queue item F8). The original draws ICONES
# 0x10 + ds:E8 at 150,137 (E8 = 0..10, 10 faces the player) and steps it one
# frame every 8 ticks: ui_hud_head_animate_up (CD seg000:17e6, floppy 1b64;
# blocked while travelling, CD ds:11C9 / floppy ds:11D6), animate_down (CD
# 181e, floppy 1b82), the fold (CD 1843, floppy 1b98). It turns away for the
# mirror, a restart, THE BOOK and the travel (take-off and the whole flight),
# and faces the player again when a room, the map or the globe is shown.
# The CD also lowers it for a character's line in text mode (ds:28E7 == 0,
# 9fec -> 11803); the voiced modes and the floppy leave it up.
#
# Real-time runs (dune_real_time) of: a new game, Paul's room, LOOK AT MIRROR,
# Look away, down to the palace front, TAKE AN ORNITHOPTER, Carthag-Tuek, the
# take-off, the flight, SKIP TO DESTINATION, the exterior, into the sietch,
# Gurney's first line. Each checkpoint's head is read from the picture (the
# ICONES frame whose pixels fit it) and compared with the original's:
#   throne 26, mirror 16, room after the mirror 26, palace front 26, cockpit
#   26, take-off 16, flight 16, exterior 26, Gurney's line 16 on the CD
#   (text mode) and 26 on the floppy and with dune_cd_voice_mode=2.
# Captured originals: captures/explore (floppy, ds:E8 in the memory dumps),
# captures/cd-speedrun-day1 (CD lines). A fast (capture) run of the same
# script must end on the same frames for the screens it reaches.
# Silent and headless, about six minutes.
#
# Usage: scripts/check_head.sh

set -u

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
local_root="${DUNE_LOCAL_BUILD_ROOT:-${TMPDIR:-/tmp}/dune-scummvm-native-build}"
floppy_data="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}"
cd_data="${DUNE_DATA_CD:-$repo_root/data}"
run_root="$local_root/sdl-run/head"
rm -rf "$run_root"
mkdir -p "$run_root"
log="${DUNE_RUN_ROOT:-$local_root/sdl-run}/saves/dune-ios.log"
status=0

SDL_AUDIODRIVER=dummy DUNE_DATA="$cd_data" "$script_dir/test_dune_scummvm_sdl.sh" build >/dev/null 2>&1 || {
	echo "FAIL head: build failed; run scripts/test_dune_scummvm_sdl.sh dump to see why"
	exit 1
}

write_script() { # release real(1|0) > script
	release=$1
	real=$2
	if [ "$real" = 1 ]; then
		if [ "$release" = cd ]; then
			printf 'wait 8000\nkey ESC\nwait 6000\nkey ESC\nwait 6000\n'
		else
			printf 'wait 3000\nkey ESC\nwait 4000\nkey ESC\nwait 6000\n'
		fi
	fi
	gurney=171
	[ "$release" = cd ] && gurney=179
	# A capture run's flight is instant: no SKIP row to click.
	skip='click left 160 163      # row 1: SKIP TO DESTINATION'
	[ "$real" = 1 ] || skip='wait 100                # (a capture run has landed already)'
	cat <<EOF
wait 1000               # the new game's throne room, the head up
checkpoint h0-throne
click left 275 164      # compass, up arrow: Paul's room
wait 2500               # the room draws
click left 160 171      # row 2: LOOK AT MIRROR: the head turns away, the mirror
wait 2500               # the mirror shows
checkpoint h1-mirror
click left 160 195      # row 5: Look away from the mirror: Paul's room, the head up
wait 2500               # the room draws
checkpoint h2-room-back
click left 275 189      # compass, down arrow: the throne room
wait 2500               # the room draws
click left 275 189      # compass, down arrow
wait 2500               # the room draws
click left 275 189      # compass, down arrow
wait 2500               # the room draws
click left 275 189      # compass, down arrow
wait 2500               # the room draws
click left 275 189      # compass, down arrow: the palace front
wait 3000               # the front draws
checkpoint h3-front
click left 160 171      # row 2: TAKE AN ORNITHOPTER: the cockpit
wait 3000               # the cockpit opens
checkpoint h4-cockpit
click left 181 102      # cockpit map: Carthag-Tuek: the head turns away, the take-off
wait 1500               # the ornithopter lifts off
checkpoint h5-takeoff
wait 6000               # the flight
checkpoint h6-flight
$skip
wait 6000               # the landing, the exterior, the head up
checkpoint h7-exterior
click left 275 164      # compass, up arrow: into the sietch
wait 3000               # the room draws
click left 160 $gurney  # Gurney HALLECK's first line (CD row 3, floppy row 2)
wait 2500               # the line shows
checkpoint h8-line
click left 160 80       # view: the next line or the talk's end
wait 2500               # the line or the room
quit
EOF
}

run_case() { # name release real voice_mode
	name=$1
	release=$2
	real=$3
	voice=$4
	dir="$run_root/$name"
	mkdir -p "$dir"
	write_script "$release" "$real" > "$dir/input.script"
	: > "$dir/extra.ini"
	[ "$real" = 1 ] && printf 'dune_real_time=true\n' >> "$dir/extra.ini"
	printf 'dune_no_music=1\n' >> "$dir/extra.ini"
	[ -n "$voice" ] && printf 'dune_cd_voice_mode=%s\n' "$voice" >> "$dir/extra.ini"
	data=$floppy_data
	[ "$release" = cd ] && data=$cd_data
	rm -f "$log"
	SDL_AUDIODRIVER=dummy DUNE_DATA="$data" "$script_dir/test_dune_scummvm_sdl.sh" harness \
		"$dir/input.script" "$dir/out" "$dir/extra.ini" >/dev/null 2>&1
	cp "$log" "$dir/dune-ios.log" 2>/dev/null
}

run_case floppy floppy 1 ""
run_case cd cd 1 ""
run_case cd-voiced cd 1 2
run_case floppy-fast floppy 0 ""
run_case cd-fast cd 0 ""

python3 - "$run_root" "$repo_root" "$floppy_data" "$cd_data" <<'EOF' || status=1
import sys
from collections import Counter
from pathlib import Path
from PIL import Image
run, repo, floppy_data, cd_data = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4])
sys.path.insert(0, str(repo / "scripts"))
import dune_sprite_sheet as s

def head_frames(release):
    if release == "cd":
        data = s.unhsq(s.dat(str(cd_data / "DUNE.DAT"))["ICONES.HSQ"])
    else:
        data = s.unhsq(open(floppy_data / "ICONES.HSQ", "rb").read())
    return {f: s.sprites(data)[1][f] for f in range(16, 27)}

def head_of(png, frames):
    """The ICONES frame (16..26) whose opaque pixels fit the picture's head rect best."""
    im = Image.open(png).convert("RGB")
    px = im.load()
    best, best_score = None, -1.0
    for f, (w, h, pix) in frames.items():
        colours = {}
        for y in range(h):
            for x in range(w):
                if pix[y][x]:
                    colours.setdefault(pix[y][x], Counter())[px[150 + x, 137 + y]] += 1
        total = sum(sum(c.values()) for c in colours.values())
        fit = sum(c.most_common(1)[0][1] for c in colours.values()) / total
        distinct = len({c.most_common(1)[0][0] for c in colours.values()})
        score = fit * min(1.0, distinct / len(colours)) + total * 1e-6
        if score > best_score:
            best, best_score = f, score
    return best

expect_real = {"h0-throne": 26, "h1-mirror": 16, "h2-room-back": 26, "h3-front": 26, "h4-cockpit": 26,
               "h5-takeoff": 16, "h6-flight": 16, "h7-exterior": 26}
cases = {
    "floppy": ("floppy", dict(expect_real, **{"h8-line": 26})),
    "cd": ("cd", dict(expect_real, **{"h8-line": 16})),
    "cd-voiced": ("cd", dict(expect_real, **{"h8-line": 26})),
    # A capture run ends each animation at once; its flight is instant, so
    # the take-off and flight checkpoints show the exterior.
    "floppy-fast": ("floppy", dict(expect_real, **{"h5-takeoff": 26, "h6-flight": 26, "h8-line": 26})),
    "cd-fast": ("cd", dict(expect_real, **{"h5-takeoff": 26, "h6-flight": 26, "h8-line": 16})),
}
failed = False
for name, (release, expect) in cases.items():
    frames = head_frames(release)
    got, bad = {}, []
    for cp, want in expect.items():
        png = run / name / "out" / f"{cp}.png"
        f = head_of(png, frames) if png.exists() else None
        got[cp] = f
        if f != want:
            bad.append(f"{cp}: {f} (want {want})")
    logp = run / name / "dune-ios.log"
    heads = [l.split("Head: ", 1)[1].strip() for l in open(logp, errors="replace") if "Head: " in l] if logp.exists() else []
    # The travel keeps it down: no "up" between the departure and the landing.
    log = open(logp, errors="replace").read().splitlines() if logp.exists() else []
    start = next((i for i, l in enumerate(log) if "Travel: to place 12" in l), None)
    dep = next((i for i, l in enumerate(log) if "Head: down" in l and "(travel)" in l), None)
    fly = next((i for i, l in enumerate(log) if l.startswith("Flight: place 0 -> 12")), None)
    land = next((i for i, l in enumerate(log) if i > (start or 0) and "Room 1 of place 12" in l), None)
    if None in (start, dep, fly) or not start < dep < fly:
        bad.append("no head-down at the departure, before the flight")
    elif land is not None and any("Head: up" in l for l in log[dep:land]):
        bad.append("the head came up during the flight")
    shown = " ".join(f"{k.split('-', 1)[1]}={v}" for k, v in got.items())
    if bad:
        failed = True
        print(f"FAIL head {name}: " + "; ".join(bad) + f" [{shown}]")
    else:
        print(f"PASS head {name}: {shown}; log: " + ", ".join(h.replace('from ', '') for h in heads[:8]))
sys.exit(1 if failed else 0)
EOF
exit $status
