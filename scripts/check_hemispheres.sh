#!/bin/sh

# The north/south quarrel check (floppy and CD). Every troop remembers its
# half of the planet (troop byte 0x12 bit 7, seg000:01e0). On a new day the
# troop at the head of a sietch's chain checks its place (floppy sub_9A58
# 9A95, CD 6e20): away from Paul, when spice troops below motivation 40 from
# both halves stand there, every miner and soldier there below motivation 40
# stops (occupation bit 4) and quarrels (speech bit 4), and Duncan's message
# "Nothing coming from ... I wonder what's going on there!" is queued
# (vision 0x302). Over the map the chief then says "We refuse to work
# anymore." (condition 513), in person "It's difficult to understand Fremen
# from the south..." or "Life is impossible here! We came from the south..."
# (488/489); moving a troop away settles it (troop_06ebf / sub_9AF7).
# This runs dune_story_setup=hemispheres and checks those steps.
# Silent and headless, about a minute per release.
#
# Usage: scripts/check_hemispheres.sh

set -u

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
local_root="${DUNE_LOCAL_BUILD_ROOT:-/private/tmp/dune-scummvm-native-build}"
status=0

SDL_AUDIODRIVER=dummy DUNE_DATA="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}" "$script_dir/test_dune_scummvm_sdl.sh" dump >/dev/null 2>&1 || {
	echo "FAIL hemispheres: build failed; run scripts/test_dune_scummvm_sdl.sh dump to see why"
	exit 1
}

for release in floppy cd; do
	data="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}"
	[ "$release" = cd ] && data="${DUNE_DATA_CD:-$repo_root/data}"
	run_root="$local_root/sdl-run/hemispheres-$release"
	rm -rf "$run_root"
	mkdir -p "$run_root/saves" "$run_root/frames"
	config="$run_root/hemispheres.ini"
	printf '[scummvm]\nsavepath=%s\ndune_input=%s\ndune_checkpoint_dir=%s\ndune_floppy_start=99\ndune_no_music=1\ndune_story_setup=hemispheres\n' \
		"$run_root/saves" "$repo_root/tests/regression/hemispheres.script" "$run_root/frames" > "$config"
	(cd "$local_root/build-sdl-dune" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy perl -e 'alarm 120; exec @ARGV' -- \
		./scummvm -c "$config" --gfx-mode=surface --no-fullscreen --path="$data" dune >/dev/null 2>&1)
	log="$run_root/saves/dune-ios.log"
	missing=""
	for step in "Story setup: troops" "(Fremen from the north)" "(Fremen from the south)" "Vision: queued 0x302" \
			"\"We refuse to work anymore.\"" "\"It's difficult to understand Fremen from the south" "stops quarrelling" \
			"after the move, troop"; do
		grep -qF -- "$step" "$log" 2>/dev/null || missing="$missing [$step]"
	done
	# After the move the northern troop works again: occupation 0, speech bit 4 clear.
	grep "after the move" "$log" 2>/dev/null | grep -q "occupation 0 " || missing="$missing [occupation 0 after the move]"
	if [ -z "$missing" ]; then
		echo "PASS hemispheres $release: both halves quarrel and stop, 'We refuse to work anymore.', the chief's north/south line, a move settles it"
	else
		echo "FAIL hemispheres $release: missing$missing (see $log)"
		status=1
	fi
done
exit $status
