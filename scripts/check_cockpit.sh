#!/bin/sh
# Real pointer/button paths: cockpit dashboard, hover labels, scrolling,
# current-place guard, Cancel/ESC, destination pick, takeoff and arrival.
# Amiga hunk 0 5e18/604a/60ba; DOS CD 439f/4586/45de, floppy 4bce/4d9f/4df7.
set -eu
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
local_root="${DUNE_LOCAL_BUILD_ROOT:-${TMPDIR:-/tmp}/dune-scummvm-native-build}"
run_root="${DUNE_RUN_ROOT:-$local_root/sdl-run}"
data="${DUNE_DATA_AMIGA:-$HOME/dune-amiga-data/game}"
if [ ! -d "$data" ]; then
	echo "FAIL cockpit: set DUNE_DATA_AMIGA to the extracted Amiga data"
	exit 1
fi
"$script_dir/test_dune_scummvm_sdl.sh" build >/dev/null 2>&1 || {
	echo "FAIL cockpit: desktop build failed"
	exit 1
}
case_root="$run_root/cockpit-check"
mkdir -p "$case_root"
printf 'dune_test_cockpit=1\ndune_no_music=1\n' > "$case_root/extra.ini"
DUNE_SKIP_BUILD=1 DUNE_DATA="$data" "$script_dir/test_dune_scummvm_sdl.sh" harness \
	"$repo_root/tests/regression/cockpit.script" "$case_root/frames" "$case_root/extra.ini" > "$case_root/stdout.log" 2>&1 || {
	echo "FAIL cockpit: harness failed (see $case_root/stdout.log)"
	exit 1
}
python3 - "$run_root/saves/dune-ios.log" "$case_root/frames" <<'PY'
import sys
from pathlib import Path
from PIL import Image
log = Path(sys.argv[1]).read_text(errors="replace")
frames = Path(sys.argv[2])
failures = []
def check(ok, name):
    print(("PASS" if ok else "FAIL") + " cockpit: " + name)
    if not ok:
        failures.append(name)
check("Cockpit: hover Sietch: Carthag-Tuek" in log, "hover identifies Carthag-Tuek")
check("Cockpit: hover DESERT" in log and "Cockpit: hover (outside map)" in log,
      "desert label and caption restore on leaving map")
current = log.split("checkpoint cockpit-current-place")[0]
check("Cockpit: hover Palace: Carthag-(Atreides)" in current and "Cockpit: destination" not in current,
      "the current palace is labelled and does not take off")
check(log.count("Cockpit: Cancel") == 2, "button and Escape cancel both return to exterior")
check(log.count("Cockpit: destination place 12") == 1 and log.count("Travel: to place 12") == 1,
      "one destination click starts exactly one flight to the displayed place")
check("Flight: place 0 -> 12" in log and "Room 2 of place 12" in log,
      "flight arrives and the next compass click enters the sietch")
def picture(name):
    return Image.open(frames / (name + ".png")).convert("RGB")
def crop(name, box):
    return picture(name).crop(box).tobytes()
view = (0, 0, 320, 137)
check(crop("cockpit-open", view) == crop("cockpit-caption", view),
      "caption is restored after hover")
mapbox = (81, 45, 241, 134)
check(crop("cockpit-scrolled", mapbox) != crop("cockpit-recentred", mapbox)
      and crop("cockpit-target", mapbox) == crop("cockpit-recentred", mapbox),
      "scroll changes the window and centre restores the original map")
# Ignore the blinking player marker while comparing the terrain.
terrain = (82, 46, 130, 133)
check(crop("cockpit-edge-left", terrain) != crop("cockpit-edge-right", terrain)
      and crop("cockpit-edge-right", terrain) == crop("cockpit-target", terrain),
      "left and right map-edge clicks scroll and restore longitude")
check(crop("cockpit-edge-up", terrain) != crop("cockpit-edge-down", terrain)
      and crop("cockpit-edge-down", terrain) == crop("cockpit-target", terrain),
      "top and bottom map-edge clicks scroll and restore latitude")
check(crop("cockpit-cancelled", view) == crop("cockpit-escaped", view),
      "both cancellation paths show the same palace exterior")
im = picture("cockpit-open")
# Actual dashboard structure on each side of the map; the old render showed
# only a few horizontal sky bands here. No palette values or game goldens.
for name, x0, x1 in [("left instruments", 0, 70), ("right instruments", 250, 320)]:
    transitions = sum(im.getpixel((x, 70)) != im.getpixel((x - 1, 70)) for x in range(x0 + 1, x1))
    check(transitions >= 8, f"{name} are drawn ({transitions} contour transitions)")
sys.exit(bool(failures))
PY
cp "$run_root/saves/dune-ios.log" "$case_root/selection.log"
printf 'dune_test_cockpit=1\ndune_no_music=1\ndune_real_time=true\n' > "$case_root/change.ini"
DUNE_SKIP_BUILD=1 DUNE_DATA="$data" "$script_dir/test_dune_scummvm_sdl.sh" harness \
	"$repo_root/tests/regression/cockpit-change.script" "$case_root/change-frames" "$case_root/change.ini" > "$case_root/change-stdout.log" 2>&1 || {
	echo "FAIL cockpit: change-destination harness failed"
	exit 1
}
python3 - "$run_root/saves/dune-ios.log" <<'PY'
import sys
from pathlib import Path
log = Path(sys.argv[1]).read_text(errors="replace")
need = ["Cockpit: CHANGE DESTINATION", "checkpoint flight-hover", "Cockpit: Cancel",
        "checkpoint flight-change-cancelled", "Flight: place 0 -> 12", "Room 2 of place 12"]
pos = 0
for token in need:
    found = log.find(token, pos)
    if found < 0:
        raise SystemExit("FAIL cockpit: change-destination interaction missing " + token)
    pos = found + len(token)
change = log.split("Cockpit: CHANGE DESTINATION", 1)[1].split("checkpoint flight-hover", 1)[0]
if "Cockpit: hover Palace: Carthag-(Atreides)" not in change:
    raise SystemExit("FAIL cockpit: nested flight cockpit does not update hover label")
print("PASS cockpit: CHANGE DESTINATION hover, Cancel, resumed flight, skip and sietch entry")
PY
cp "$run_root/saves/dune-ios.log" "$case_root/change.log"
