#!/bin/sh

# The Water of Life check (floppy and CD). Stilgar offers it in a sietch's
# room 4 (DIALOGUE 5, conditions 299-301: current_scene < 0x20, room 4,
# ds:0a bit 1 clear) as a question; his event 8 (CD seg000:2ccf, floppy
# 2f96) always sets ds:0a bit 3 ("has heard of it"). ACCEPT (ds:9f = 1)
# with charisma >= 100: ds:0a bit 1, ds:d5 = 0xff, three periods pass,
# ds:23 = 0x11 and the room scan, where Stilgar says "... You've been
# unconscious for three hours." (CD; the floppy's text differs). Jessica then feels the power (ds:d5 >
# 0x80, lines 76-78) and her lesson (a186) with bit 1 gives charisma + 40
# and an unlimited contact range (0xffe2). ACCEPT below 100: pending room
# screen request 3, "Paul Atreides died as he tried to drink the Water of
# Life". REFUSE: "wise. Maybe someday" and bit 3 only.
# This runs dune_story_setup=water-of-life and checks those steps.
# Silent and headless, about a minute per release.
#
# Usage: scripts/check_water_of_life.sh

set -u

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
local_root="${DUNE_LOCAL_BUILD_ROOT:-/private/tmp/dune-scummvm-native-build}"
status=0

SDL_AUDIODRIVER=dummy DUNE_DATA="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}" "$script_dir/test_dune_scummvm_sdl.sh" build >/dev/null 2>&1 || {
	echo "FAIL water-of-life: build failed; run scripts/test_dune_scummvm_sdl.sh dump to see why"
	exit 1
}

for release in floppy cd; do
	data="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}"
	[ "$release" = cd ] && data="${DUNE_DATA_CD:-$repo_root/data}"
	run_root="$local_root/sdl-run/water-of-life-$release"
	rm -rf "$run_root"
	mkdir -p "$run_root/saves" "$run_root/frames"
	config="$run_root/water-of-life.ini"
	printf '[scummvm]\nsavepath=%s\ndune_input=%s\ndune_checkpoint_dir=%s\ndune_floppy_start=99\ndune_no_music=1\ndune_story_setup=water-of-life\n' \
		"$run_root/saves" "$repo_root/tests/regression/water-of-life.script" "$run_root/frames" > "$config"
	(cd "$local_root/build-sdl-dune" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy perl -e 'alarm 120; exec @ARGV' -- \
		./scummvm -c "$config" --gfx-mode=surface --no-fullscreen --path="$data" dune >/dev/null 2>&1)
	log="$run_root/saves/dune-ios.log"
	missing=""
	for step in "Story setup: Stilgar at place" "extends consciousness" "Talk: choice 2" "wise. Maybe someday" \
			"after REFUSE: ds:0a 0x59," "Your decision frightens me" "Story: Paul drinks the Water of Life" \
			"Story: Paul comes to after the Water of Life" "unconscious" \
			"after ACCEPT: ds:0a 0x5b, ds:d5 0xff, charisma 120" "Story: contact range -> 65506 cells" \
			"after Jessica: ds:0a 0x5b, ds:d5 0," "a day later: ds:0a 0x5b, ds:d5 0," \
			"Story: Paul drinks the Water of Life and dies" "Paul Atreides died as he tried"; do
		grep -qF -- "$step" "$log" 2>/dev/null || missing="$missing [$step]"
	done
	# Three periods pass while Paul is unconscious.
	before=$(grep -F "after REFUSE:" "$log" | sed -n 's/.*period \([0-9]*\).*/\1/p' | head -1)
	after=$(grep -F "after ACCEPT:" "$log" | sed -n 's/.*period \([0-9]*\).*/\1/p' | head -1)
	[ -n "$before" ] && [ -n "$after" ] && [ $(( (after - before + 16) % 16 )) -eq 3 ] || missing="$missing [three periods: $before -> $after]"
	if [ -z "$missing" ]; then
		echo "PASS water-of-life $release: REFUSE sets bit 3; ACCEPT at 120 drinks, 3 periods, Stilgar's wake-up line, Jessica's unlimited range; ACCEPT at 50 kills Paul"
	else
		echo "FAIL water-of-life $release: missing$missing (see $log)"
		status=1
	fi
done
exit $status
