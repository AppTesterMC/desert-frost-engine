#!/bin/sh

# The Fremen epidemic check (floppy and CD). Phase 0x5c's callback stamps
# ds:1156 = day + 3; from that day, while the phase is 0x5c, each new day's
# picker (CD seg000:1e43, floppy 2180) strikes the sietch or village with the
# most hired troops at work: they fall ill (troop speech 0x400) and stop,
# ds:f8 counts the places, ds:11db names the latest, and the chief's message
# 0x0f08 "There is a strange disease here in ..." is queued. Over the map an
# ill troop says "Everybody is ill here. We need help." and its occupation,
# equipment and move rows are greyed (7857). Stilgar and Chani name the
# latest place ("We have to go to ... to stem the epidemic.", 9519). Told to
# stay in room 2 of an ill sietch, Chani says "OK Paul! I'm staying here to
# cure the Fremen..." (action 11: phase 0x5d) and the cure moves on by 0x10;
# every period away from Paul it moves on by 8 (1d9f -> 1eda). At 0x100 the
# troops there are cured (0x800), her message 0x0709 is queued; with no ill
# place left she is taken to the Harkonnen palace (phase 0x60, 11cb). A cured
# troop's contact then brings phase 0x64 (CD 7b79 -> 1ebe; the floppy's line
# "Oh! Chani isn't with you..." has action 12): Chani is held in room 3 of a
# Harkonnen fortress, Feyd-Rautha's COMM message (0x2b0a) arrives and the
# troops' motivation counts 40 less.
# This runs dune_story_setup=epidemic and checks those steps in the log.
# Silent and headless, about a minute per release.
#
# Usage: scripts/check_epidemic.sh

set -u

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
local_root="${DUNE_LOCAL_BUILD_ROOT:-/private/tmp/dune-scummvm-native-build}"
status=0

SDL_AUDIODRIVER=dummy DUNE_DATA="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}" "$script_dir/test_dune_scummvm_sdl.sh" build >/dev/null 2>&1 || {
	echo "FAIL epidemic: build failed; run scripts/test_dune_scummvm_sdl.sh dump to see why"
	exit 1
}

for release in floppy cd; do
	data="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}"
	[ "$release" = cd ] && data="${DUNE_DATA_CD:-$repo_root/data}"
	run_root="$local_root/sdl-run/epidemic-$release"
	rm -rf "$run_root"
	mkdir -p "$run_root/saves" "$run_root/frames"
	config="$run_root/epidemic.ini"
	printf '[scummvm]\nsavepath=%s\ndune_input=%s\ndune_checkpoint_dir=%s\ndune_floppy_start=99\ndune_no_music=1\ndune_rng_seed=7\ndune_story_setup=epidemic\n' \
		"$run_root/saves" "$repo_root/tests/regression/epidemic.script" "$run_root/frames" > "$config"
	(cd "$local_root/build-sdl-dune" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy perl -e 'alarm 120; exec @ARGV' -- \
		./scummvm -c "$config" --gfx-mode=surface --no-fullscreen --path="$data" dune >/dev/null 2>&1)
	log="$run_root/saves/dune-ios.log"
	missing=""
	for step in "World: epidemic at place" "Vision: queued 0xf08" "Vision: message 0xf08" "There is a strange disease here in" \
			"Everybody is ill here. We need help." "(CHANGE TROOP OCCUPATION) is greyed" "(MODIFY EQUIPMENT) is greyed" \
			"(MOVE TROOP) is greyed" "Talk: speaker 5, the ill place" "to stem th" "Strange disease we have here" \
			"staying here to cure the Fremen" "Story: phase -> 0x5d" "World: Chani stays at place" "Vision: queued 0x709" \
			"Vision: message 0x709" "managed to cure everybody" "World: Chani has cured the troops at place" \
			"World: the epidemic is over; Chani is taken to the Harkonnen palace (phase 0x60)" "Chani isn't with you" \
			"Story: chapter -> phase 0x64" "World: Chani is held at place" "COMM: message from person 10 (variant 43)" \
			"We all like Chani a lot"; do
		grep -qF -- "$step" "$log" 2>/dev/null || missing="$missing [$step]"
	done
	# Two places fall ill on two days (the picker runs daily at phase 0x5c).
	[ "$(grep -c 'World: epidemic at place' "$log" 2>/dev/null)" -ge 2 ] || missing="$missing [the illness spreads a day later]"
	# The ill troops stop (occupation bit 4) and have speech bit 0x400.
	grep "Story setup: first illness" "$log" 2>/dev/null | grep -q "occ 0x10 speech 0x2442" || missing="$missing [ill and stopped]"
	# A cure takes 30 periods after STAY HERE (0x10 + 30 x 8 = 0x100).
	grep -q "Story setup: first cure after 30 period(s)" "$log" 2>/dev/null || missing="$missing [cure in 30 periods]"
	# Chani's record after the contact: room 3 of a fortress; ds:f2 its names; motivation - 40 (at least 10).
	grep "Story setup: after the contact" "$log" 2>/dev/null | grep -q "phase 0x64, Chani's record 03 " || missing="$missing [Chani in room 3]"
	grep -q "at phase 0x64: 10 (motivation 26)" "$log" 2>/dev/null || missing="$missing [motivation - 40]"
	if [ "$release" = cd ]; then
		grep -q "cured by Chani, is met at phase 0x60: phase 0x64" "$log" 2>/dev/null || missing="$missing [CD 1ebe]"
	fi
	if [ -z "$missing" ]; then
		echo "PASS epidemic $release: two sietches fall ill, the message, the lines, greyed rows, Chani's cure (30 periods), phase 0x60, a cured troop brings 0x64 and Chani's prison, Feyd's message"
	else
		echo "FAIL epidemic $release: missing$missing (see $log)"
		status=1
	fi
done
exit $status
