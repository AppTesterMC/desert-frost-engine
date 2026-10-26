#!/bin/sh

# The natural ecology route (floppy). Unlike check_ecology_win.sh, this does
# not use ecologyWinSetup, greenOverForTest or wholePlanetGreen: a Fremen
# troop takes bulbs, builds its wind trap through the native job, irrigates,
# and the native daily ecology routine (seg000:63f0/6515) spreads vegetation
# over 120 days. The log must show the wind trap being assembled and the
# native count of 0x10 (sprouting) cells rising across dated checkpoints.
# Silent and headless, about a minute per release.
#
# Usage: scripts/check_ecology_route.sh

set -u

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
local_root="${DUNE_LOCAL_BUILD_ROOT:-${TMPDIR:-/tmp}/dune-scummvm-native-build}"
status=0

SDL_AUDIODRIVER=dummy DUNE_DATA="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}" "$script_dir/test_dune_scummvm_sdl.sh" build >/dev/null 2>&1 || {
	echo "FAIL ecology-route: build failed; run scripts/test_dune_scummvm_sdl.sh dump to see why"
	exit 1
}

for release in floppy; do
	data="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}"
	[ "$release" = cd ] && data="${DUNE_DATA_CD:-$repo_root/data}"
	run_root="$local_root/sdl-run/ecology-route-$release"
	rm -rf "$run_root"
	mkdir -p "$run_root/saves" "$run_root/frames"
	config="$run_root/ecology-route.ini"
	printf '[scummvm]\nsavepath=%s\ndune_input=%s\ndune_checkpoint_dir=%s\ndune_floppy_start=99\ndune_no_music=1\ndune_rng_seed=1\ndune_story_setup=ecology\n' \
		"$run_root/saves" "$repo_root/tests/regression/story-ecology.script" "$run_root/frames" > "$config"
	(cd "$local_root/build-sdl-dune" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy perl -e 'alarm 180; exec @ARGV' -- \
		./scummvm -c "$config" --gfx-mode=surface --no-fullscreen --path="$data" dune >/dev/null 2>&1)
	log="$run_root/saves/dune-ios.log"
	missing=""
	grep -qF -- "Ecology route: natural vegetation growth" "$log" 2>/dev/null || missing="$missing [natural route marker]"
	grep -qF -- "Ecology: wind-trap assembled" "$log" 2>/dev/null || missing="$missing [wind trap assembled]"
	[ "$(grep -c 'Ecology checkpoint:' "$log" 2>/dev/null)" -ge 4 ] || missing="$missing [dated 0x10 checkpoints]"
	final=$(grep -o 'Ecology test: [0-9]* sprouting cells' "$log" 2>/dev/null | tail -1 | grep -o '[0-9]*')
	[ -n "$final" ] && [ "$final" -gt 0 ] || missing="$missing [0x10 sprouting cells > 0]"
	# The count must rise between the first and last dated checkpoint.
	first=$(grep 'Ecology checkpoint:' "$log" 2>/dev/null | head -1 | grep -o '[0-9]* sprouting' | grep -o '[0-9]*')
	last=$(grep 'Ecology checkpoint:' "$log" 2>/dev/null | tail -1 | grep -o '[0-9]* sprouting' | grep -o '[0-9]*')
	if [ -n "$first" ] && [ -n "$last" ] && [ "$last" -lt "$first" ]; then
		missing="$missing [0x10 count did not rise ($first -> $last)]"
	fi
	if [ -z "$missing" ]; then
		echo "PASS ecology-route $release: wind trap assembled, $final sprouting cells (0x10), dated checkpoints $first -> $last"
	else
		echo "FAIL ecology-route $release: missing$missing (see $log)"
		status=1
	fi
done
exit $status
