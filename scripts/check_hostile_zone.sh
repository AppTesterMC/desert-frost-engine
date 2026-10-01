#!/bin/sh

# The Harkonnen-zone warning (queue item 8). In the original (CD
# travel_route_hostile_zone_check 4182, dispatch 35e9/3637/3551; floppy
# 43d6, 3889/38d7/37f7), the first step of an ornithopter flight over
# Harkonnen land (cell stage 0x30) to a place that is not the Atreides' arms
# pending_room_action 4 and holds the flight on the menu CHANGE DESTINATION /
# IGNORE WARNING (the CD adds " WHAT ? "):
#   - nobody aboard: the cockpit with "****  WARNING  ****  ENTERING
#     HARKONNEN ZONE" in its window, " WHAT ? " greyed on the CD;
#   - a companion aboard: the ORNYCAB cabin and the companion's line from
#     block 16 list 4 ("Watch out! We are entering the Harkonnen zone...!"),
#     " WHAT ? " lit on the CD.
# CHANGE DESTINATION opens the cockpit over the flight (Cancel carries on);
# IGNORE WARNING flies on, and the eighth step in a row over Harkonnen land,
# or the Harkonnen place itself, kills Paul.
#
# Four real-time runs of dune_story_setup=hostile-zone[-gurney] (the palace
# to the Harkonnen place whose route enters their land soonest): floppy and
# CD, nobody aboard (IGNORE WARNING) and Gurney aboard (CHANGE DESTINATION,
# then Cancel). Silent and headless, about three minutes (the runs are
# parallel).
#
# Usage: scripts/check_hostile_zone.sh

set -u

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
local_root="${DUNE_LOCAL_BUILD_ROOT:-/tmp/dune-scummvm-native-build}"
cd_data="${DUNE_DATA_CD:-$repo_root/data}"
floppy_data="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}"
base="${DUNE_RUN_ROOT:-$local_root/sdl-run}"
work="$base/hostile-zone"
rm -rf "$work"
mkdir -p "$work"

SDL_AUDIODRIVER=dummy DUNE_DATA="$cd_data" "$script_dir/test_dune_scummvm_sdl.sh" build >/dev/null 2>&1 || {
	echo "FAIL hostile zone: build failed; run scripts/test_dune_scummvm_sdl.sh dump to see why"
	exit 1
}

run_case() { # release(floppy|cd) who(nobody|gurney)
	release=$1
	who=$2
	dir="$work/$release-$who"
	mkdir -p "$dir"
	if [ "$release" = floppy ]; then
		data=$floppy_data
		printf 'wait 4000               # the intro\nkey ESC                 # skip it\nwait 4000\nkey ESC                 # skip the prologue: the story setup starts the flight\n' > "$dir/input.script"
		flight=30000
	else
		data=$cd_data
		printf 'wait 8000               # the intro\nkey ESC                 # skip it: the story setup starts the flight\nwait 6000\n' > "$dir/input.script"
		flight=130000 # 0x300 ticks a step (travel_pump 4f27)
	fi
	if [ "$who" = nobody ]; then
		setup=hostile-zone
		cat >> "$dir/input.script" <<EOF
wait $flight            # the take-off and the flight up to the warning (it holds the flight)
checkpoint hz-warning
click left 160 171      # row 1: IGNORE WARNING
wait 15000              # the flight goes on over Harkonnen land
checkpoint hz-after
quit
EOF
	else
		setup=hostile-zone-gurney
		cat >> "$dir/input.script" <<EOF
wait $flight            # the take-off and the flight up to the warning (it holds the flight)
checkpoint hz-cabin
click left 160 163      # row 0: CHANGE DESTINATION
wait 2000               # the cockpit over the flight
checkpoint hz-cockpit
click left 160 163      # row 0: Cancel, the flight goes on
wait 15000              # over Harkonnen land
checkpoint hz-after
quit
EOF
	fi
	printf 'dune_real_time=true\ndune_no_music=1\ndune_story_setup=%s\n' "$setup" > "$dir/extra.ini"
	DUNE_RUN_ROOT="$dir/run" DUNE_HARNESS_SECONDS=240 SDL_AUDIODRIVER=dummy DUNE_DATA="$data" \
		"$script_dir/test_dune_scummvm_sdl.sh" harness "$dir/input.script" "$dir/out" "$dir/extra.ini" >"$dir/harness.txt" 2>&1
	cp "$dir/run/saves/dune-ios.log" "$dir/dune-ios.log" 2>/dev/null
}

for release in floppy cd; do
	for who in nobody gurney; do
		run_case "$release" "$who" &
	done
done
wait

status=0
for release in floppy cd; do
	for who in nobody gurney; do
		python3 - "$work/$release-$who" "$release" "$who" <<'EOF' || status=1
import sys
from pathlib import Path
d, release, who = Path(sys.argv[1]), sys.argv[2], sys.argv[3]
name = f"hostile zone ({release}, {who})"
try:
    lines = Path(d / "dune-ios.log").read_text(errors="replace").splitlines()
except OSError:
    print(f"FAIL {name}: no log"); sys.exit(1)
def has(text):
    return any(text in l for l in lines)
rows = next((l.split("warning rows ", 1)[1] for l in lines if "Flight: warning rows " in l), "(none)")
if release == "cd":
    want_rows = "CHANGE DESTINATION / IGNORE WARNING / \" WHAT ? \"" + (" (greyed)" if who == "nobody" else "")
else:
    want_rows = "CHANGE DESTINATION / IGNORE WARNING"
problems = []
if not has("Story setup: flight to place"):
    problems.append("no flight into Harkonnen land")
if rows != want_rows:
    problems.append(f"rows {rows!r}, want {want_rows!r}")
if who == "nobody":
    if not has("entering the Harkonnen zone (nobody aboard)"):
        problems.append("no cockpit warning")
    if not has("Flight: IGNORE WARNING"):
        problems.append("IGNORE WARNING not taken")
    if not (has("shot down over Harkonnen land") or has("Paul is shot")):
        problems.append("Paul survived the Harkonnen land")
    # The warning text in the cockpit's window: more than the window's own colour.
    try:
        from PIL import Image
        im = Image.open(d / "out" / "hz-warning.png").convert("RGB")
        colours = {im.getpixel((x, y)) for x in range(100, 240) for y in range(76, 100)}
        if len(colours) < 2:
            problems.append("the WARNING text is missing")
    except Exception as e:
        problems.append(f"no hz-warning picture ({e})")
else:
    if not has("companion 4: Watch out! We are entering the Harkonnen zone"):
        problems.append("no companion line")
    for text in ("CHANGE DESTINATION at the warning", "Cockpit: CHANGE DESTINATION", "Cockpit: Cancel"):
        if not has(text):
            problems.append(f"missing {text!r}")
print(("FAIL" if problems else "PASS") + f" {name}: " + ("; ".join(problems) if problems else f"rows {rows}"))
sys.exit(1 if problems else 0)
EOF
	done
done
exit $status
