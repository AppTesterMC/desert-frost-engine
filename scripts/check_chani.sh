#!/bin/sh

# Chani's story check (floppy and CD; queue item 7). The phase callbacks for
# Stilgar (0x2c), Chani (0x48) and the first worm ride (0x50) raise charisma
# through seg000:6f78, which moves every troop's motivation by the change of
# charisma / 4. The kidnapping: phase 0x64 (1f13) holds Chani in room 3 of a
# Harkonnen fortress and stores its names in ds:f2; every troop's motivation
# counts 40 less, at least 10 (6efd, phases 0x64-0x67). Thufir suggests
# espionage (condition 126), Stilgar fears for the troops' motivation; a spy
# at her fortress says "One of my men told me that he was sure they had a
# prisoner." (condition 588, w[0x4e] == w[0xf2]), after "Chani is here."
# (ds:f7, 3385). Taking the fortress (7443) only makes the landing safe
# (503c): the phase stays 0x64. In room 3 Chani's "Oh Paul! I was so
# scared!..." (condition 360, action 12) brings phase 0x68: the motivation
# counts in full; COME WITH ME: "Yes Paul, I want to follow you, now and
# always."; Stilgar: "Good to see you with Chani again...".
# chani-gurney: Gurney's STAY HERE at a sietch with an army troop
# (condition 288, action 12, once) also brings 0x68 while she is held, and
# her line then brings 0x6c; a sietch lost with Stilgar in it (74b6) holds
# him in room 3 of the new fortress.
# Silent and headless, about a minute per run.
#
# Usage: scripts/check_chani.sh

set -u

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
local_root="${DUNE_LOCAL_BUILD_ROOT:-/private/tmp/dune-scummvm-native-build}"
status=0

SDL_AUDIODRIVER=dummy DUNE_DATA="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}" "$script_dir/test_dune_scummvm_sdl.sh" build >/dev/null 2>&1 || {
	echo "FAIL chani: build failed; run scripts/test_dune_scummvm_sdl.sh dump to see why"
	exit 1
}

run() { # release setup -> the log's path
	data="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}"
	[ "$1" = cd ] && data="${DUNE_DATA_CD:-$repo_root/data}"
	run_root="$local_root/sdl-run/$2-$1"
	rm -rf "$run_root"
	mkdir -p "$run_root/saves" "$run_root/frames"
	config="$run_root/chani.ini"
	printf '[scummvm]\nsavepath=%s\ndune_input=%s\ndune_checkpoint_dir=%s\ndune_floppy_start=99\ndune_no_music=1\ndune_rng_seed=7\ndune_story_setup=%s\n' \
		"$run_root/saves" "$repo_root/tests/regression/chani.script" "$run_root/frames" "$2" > "$config"
	(cd "$local_root/build-sdl-dune" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy perl -e 'alarm 120; exec @ARGV' -- \
		./scummvm -c "$config" --gfx-mode=surface --no-fullscreen --path="$data" dune >/dev/null 2>&1)
	echo "$run_root/saves/dune-ios.log"
}

for release in floppy cd; do
	log=$(run "$release" chani)
	missing=""
	for step in "Story: phase 0x2c: charisma 0 -> 20, every troop's motivation +5 (6f78)" \
			"Story: phase 0x48: charisma 20 -> 30, every troop's motivation +2 (6f78)" \
			"Story: phase 0x50: charisma 30 -> 70, every troop's motivation +10 (6f78)" \
			"World: Chani is held at place" "COMM: message from person 10 (variant 43)" \
			"Chani kidnapped by Feyd-Rautha" "espionage" "We all like Chani a lot" \
			"Chani is here." "he was sure they had a prisoner" \
			"Oh Paul! I was s" "Story: chapter -> phase 0x68" "Story: phase 0x68: the kidnapping is over" \
			"I want to follow you" "Good to see you with Chani again"; do
		grep -qF -- "$step" "$log" 2>/dev/null || missing="$missing [$step]"
	done
	# The spill reaches the troops: 80 + 5 + 2 + 10 (capped at 100) and 30 + 17.
	grep -q "after the worm (phase 0x50): phase 0x50, charisma 70; troop [0-9]* motivation 97 modifier 97; troop [0-9]* motivation 47 modifier 47" "$log" 2>/dev/null ||
		missing="$missing [motivation spill 80 -> 97, 30 -> 47]"
	# While she is held: 80 - 40 = 40, and 30 - 40 floors at 10 (6efd).
	grep -q "while Chani is held: phase 0x64, charisma 60; troop [0-9]* motivation 80 modifier 40; troop [0-9]* motivation 30 modifier 10" "$log" 2>/dev/null ||
		missing="$missing [motivation - 40, floor 10]"
	# The floppy's "Muad'Dib" (ds:1201 = 0xfd from phase 0x2c, 14a9), not "GAME  PAUSED".
	grep "Story setup: the spy says" "$log" 2>/dev/null | grep -q "GAME  PAUSED" && missing="$missing [Muad'Dib, not GAME PAUSED]"
	grep "Story setup: kidnapped:" "$log" 2>/dev/null | grep -q "Chani's record 03 2" || missing="$missing [room 3 of a fortress]"
	# Taking the fortress does not free her: still phase 0x64, still - 40
	# (the battle's charisma + 4 moves the motivation by one).
	modifiers() { # label -> "motivation modifier" of the first troop
		grep "Story setup: $1: phase" "$log" 2>/dev/null | grep "motivation" | sed 's/.*; troop [0-9]* motivation \([0-9]*\) modifier \([0-9]*\); troop.*/\1 \2/' | head -1
	}
	set -- $(modifiers "the fortress taken")
	[ "${1:-0}" -ge 50 ] && [ "${2:-0}" -eq $((${1:-0} - 40)) ] && grep -q "the fortress taken: phase 0x64" "$log" ||
		missing="$missing [the fort taken, still held]"
	grep "Story setup: room 3: " "$log" 2>/dev/null | grep -q "people:.* 7" || missing="$missing [Chani in room 3]"
	set -- $(modifiers "Chani met")
	[ "${1:-0}" -ge 50 ] && [ "${2:-0}" -eq "${1:-0}" ] && grep -q "Story setup: Chani met: phase 0x68" "$log" ||
		missing="$missing [the motivation back]"
	grep "Story setup: COME WITH ME:" "$log" 2>/dev/null | grep -q "with Paul 1" || missing="$missing [she follows Paul]"
	if [ -z "$missing" ]; then
		echo "PASS chani $release: the charisma spill (0x2c, 0x48, 0x50), Chani held in room 3 (motivation - 40, floor 10), Thufir, Stilgar, the spy's prisoner, the fort taken (still held), her line (phase 0x68), COME WITH ME, Stilgar"
	else
		echo "FAIL chani $release: missing$missing (see $log)"
		status=1
	fi

	log=$(run "$release" chani-gurney)
	missing=""
	for step in "teach these Fremen" "Story: chapter -> phase 0x68" \
			"Good to see you with Chani again" "Oh Paul! I was s" "Story: chapter -> phase 0x6c" \
			"is held in room 3 of place"; do
		grep -qF -- "$step" "$log" 2>/dev/null || missing="$missing [$step]"
	done
	grep "after Gurney's STAY HERE: phase 0x68, Chani's record 03 2" "$log" >/dev/null 2>&1 || missing="$missing [0x68 with Chani still held]"
	grep -q "after Gurney's STAY HERE: phase 0x68, charisma [0-9]*; troop [0-9]* motivation 80 modifier 80" "$log" 2>/dev/null ||
		missing="$missing [the motivation back without her]"
	grep "sietch [0-9]* lost:" "$log" 2>/dev/null | grep -q "Stilgar's record 03 2" || missing="$missing [Stilgar in room 3 of the lost sietch]"
	if [ -z "$missing" ]; then
		echo "PASS chani-gurney $release: Gurney's STAY HERE (condition 288) brings phase 0x68 while Chani is held, her line then 0x6c; a sietch lost holds Stilgar in room 3"
	else
		echo "FAIL chani-gurney $release: missing$missing (see $log)"
		status=1
	fi
done
exit $status
