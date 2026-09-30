#!/bin/sh

# The ecology route to the end (floppy and CD). The vegetation disc of an
# irrigated sietch (CD seg000:6515/653a, floppy 72b5/72da) turns a Harkonnen
# place under it Atreides through the fortress-won routine itself (CD 7443,
# floppy 81a7): held, charisma + 4, its Harkonnens freed or gone, and with at
# most one Harkonnen place left final_attack_stage ds:c2 = 1 (CD 7493-74ac,
# floppy 81f7-8211): Thufir's "We're almost ready for the final attack."
# (condition 127) and no more demands from the Emperor. The CD spares the
# two palaces (6582: cmp di, 138h); the floppy has no such test, so there the
# vegetation takes the Harkonnen palace too, Paul lands safely and entering
# the Baron's hall (location_and_room 0x3002, 4072 / floppy 42cc) ends the
# game. On the CD the palace keeps its garrison and Paul is shot.
# This runs dune_story_setup=ecology-win and checks those steps.
# Silent and headless, about a minute per release.
#
# Usage: scripts/check_ecology_win.sh

set -u

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
local_root="${DUNE_LOCAL_BUILD_ROOT:-${TMPDIR:-/tmp}/dune-scummvm-native-build}"
status=0

SDL_AUDIODRIVER=dummy DUNE_DATA="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}" "$script_dir/test_dune_scummvm_sdl.sh" build >/dev/null 2>&1 || {
	echo "FAIL ecology-win: build failed; run scripts/test_dune_scummvm_sdl.sh dump to see why"
	exit 1
}

for release in floppy cd; do
	data="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}"
	[ "$release" = cd ] && data="${DUNE_DATA_CD:-$repo_root/data}"
	run_root="$local_root/sdl-run/ecology-win-$release"
	rm -rf "$run_root"
	mkdir -p "$run_root/saves" "$run_root/frames"
	config="$run_root/ecology-win.ini"
	printf '[scummvm]\nsavepath=%s\ndune_input=%s\ndune_checkpoint_dir=%s\ndune_floppy_start=99\ndune_no_music=1\ndune_rng_seed=1\ndune_story_setup=ecology-win\n' \
		"$run_root/saves" "$repo_root/tests/regression/ecology-win.script" "$run_root/frames" > "$config"
	(cd "$local_root/build-sdl-dune" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy perl -e 'alarm 120; exec @ARGV' -- \
		./scummvm -c "$config" --gfx-mode=surface --no-fullscreen --path="$data" dune >/dev/null 2>&1)
	log="$run_root/saves/dune-ios.log"
	missing=""
	set -- "Ecology: the vegetation takes place 2 from the Harkonnens" "Battle: 1 Harkonnen place(s) left, final attack stage 0 -> 1" "after the vegetation at place 2: stage 1" "the final attack" "a day later, demands 1 -> 1, stage 1"
	[ "$release" = floppy ] && set -- "$@" "Ecology: the vegetation takes place 1 from the Harkonnens" "status 0x8 density 0, taken" "Story setup: Paul lands at the Harkonnen palace" "Story: Paul enters the Baron's hall, the end"
	[ "$release" = cd ] && set -- "$@" "place 1 type 0x30 status 0 density 0, still Harkonnen" "Paul is shot" "Story setup: Paul is shot at the Harkonnen palace"
	for step in "$@"; do
		grep -qF -- "$step" "$log" 2>/dev/null || missing="$missing [$step]"
	done
	if [ -z "$missing" ]; then
		echo "PASS ecology-win $release: the last fort falls to the vegetation, stage 1, Thufir's line, no demand; the palace: floppy taken and the Baron's hall ends the game, CD spared and Paul is shot"
	else
		echo "FAIL ecology-win $release: missing$missing (see $log)"
		status=1
	fi
done
exit $status
