#!/bin/sh
# Amiga arrival home-room conversion. Original hunk0 0x33a0 uses the nine
# room bytes at hunk0 0x1beae; the engine's CD-layout rule reads ds:0x144d.
# Gurney left at the palace must reach room 10 before phase 0x24, room 6
# afterwards, and remain a companion when travelling with Paul.
set -eu
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
local_root="${DUNE_LOCAL_BUILD_ROOT:-/tmp/dune-scummvm-native-build}"
run_root="${DUNE_RUN_ROOT:-$local_root/sdl-run}"
data="${DUNE_DATA_AMIGA:-$HOME/dune-amiga-data/game}"
if [ ! -f "$data/dune" ]; then
	echo "SKIP Amiga arrival: original Amiga executable unavailable"
	exit 0
fi
SDL_AUDIODRIVER=dummy DUNE_DATA="$data" "$script_dir/test_dune_scummvm_sdl.sh" build >/dev/null 2>&1 || {
	echo "FAIL Amiga arrival: build failed"
	exit 1
}
work="$run_root/amiga-arrival"
mkdir -p "$work" "$run_root/saves"
: > "$run_root/saves/dune-ios.log"
printf 'dune_no_music=1\ndune_floppy_start=99\ndune_story_setup=arrival-home-rooms\n' > "$work/extra.ini"
SDL_AUDIODRIVER=dummy DUNE_DATA="$data" "$script_dir/test_dune_scummvm_sdl.sh" harness \
	"$repo_root/tests/regression/arrival-home-rooms.script" "$work/frames" "$work/extra.ini" > "$work/stdout.log" 2>&1
log="$run_root/saves/dune-ios.log"
status=0
for expected in \
	'phase 0x23, Gurney room 10, Jessica room 9' \
	'phase 0x24, Gurney room 6, Jessica room 9' \
	'phase 0x2e, Gurney room 6, Jessica room 9' \
	'companion Gurney remains room 1'
do
	if grep -Fq "Arrival home rooms: $expected" "$log"; then
		echo "PASS Amiga arrival: $expected"
	else
		echo "FAIL Amiga arrival: missing $expected (see $log)"
		status=1
	fi
done
exit "$status"
