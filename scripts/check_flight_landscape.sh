#!/bin/sh

# The flight landscape check (floppy): the engine flies from the palace to
# Carthag-Tuek in real time (developer key dune_test_flight, about 15 s,
# silent and headless) and logs each landscape seed; they must equal the
# original's, read from its memory (tests/regression/flight-seeds-floppy.txt).
# The seeds depend on the route step by step (World::travelStep), the fill and
# the reseeding five steps ahead, so a match means the same dunes and rocks,
# row by row, as the original draws.
#
# Usage: scripts/check_flight_landscape.sh

set -u

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
data="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}"
expected="$repo_root/tests/regression/flight-seeds-floppy.txt"
local_root="${DUNE_LOCAL_BUILD_ROOT:-/private/tmp/dune-scummvm-native-build}"

SDL_AUDIODRIVER=dummy DUNE_DATA="$data" "$script_dir/test_dune_scummvm_sdl.sh" build >/dev/null 2>&1 || {
	echo "FAIL flight landscape: build failed; run scripts/test_dune_scummvm_sdl.sh dump to see why"
	exit 1
}
run_root="$local_root/sdl-run/flight-check"
rm -rf "$run_root"
mkdir -p "$run_root/saves" "$run_root/frames"
config="$run_root/flight.ini"
printf '[scummvm]\nsavepath=%s\ndune_dump=%s\ndune_dump_every=640\ndune_floppy_start=99\ndune_no_music=1\ndune_test_flight=12\n' \
	"$run_root/saves" "$run_root/frames" > "$config"
cd "$local_root/build-sdl-dune"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy perl -e 'alarm 120; exec @ARGV' -- \
	./scummvm -c "$config" --gfx-mode=surface --no-fullscreen --path="$data" dune >/dev/null 2>&1

log="$run_root/saves/dune-ios.log"
got="$run_root/seeds.txt"
grep "^Land: seed" "$log" 2>/dev/null | sed 's/^Land: seed \([0-9a-f]*\) .*/\1/' > "$got"
want=$(grep -v '^#' "$expected" | grep -c .)
if grep -v '^#' "$expected" | diff - "$(head -n "$want" "$got" > "$got.head"; echo "$got.head")" >/dev/null; then
	echo "PASS flight landscape: $want seeds equal the original's ($(grep 'Flight: place 0 -> 12' "$log" | sed 's/^Flight: //'))"
	exit 0
fi
echo "FAIL flight landscape: seeds differ from the original's (got $(wc -l < "$got" | tr -d ' ') seeds; see $got)"
grep -v '^#' "$expected" | diff - "$got.head" | head -10
exit 1
