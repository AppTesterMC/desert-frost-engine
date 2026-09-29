#!/bin/sh

# The GO & SEARCH FOR EQUIPMENT check (floppy and CD). The order heads the
# three class menus (CD seg000:69b3 ds:216e/2182/21a6, floppy 774d) and is
# greyed below phase 0x10. The troop looks for the first item its class
# lacks (CD 7734/775c/776d, floppy 8498/84c0/84d1): spice a harvester then
# an orni, army krys, laser guns, weirding modules, atomics, ecology bulbs;
# with all of them it says "I have all the equipment I need!" (ds:23 0x0f).
# The CD first takes the item at its own place (77d7, MODIFY EQUIPMENT's
# answer 0x0c); the floppy has no such step. Otherwise the nearest known
# non-fort place under 50 with one free (7f90 / 8ca1); none: "I don't think
# I can find ... available in all of the places around here." (0x0e). The
# troop takes occupation class | 3, answers 0x0a ("OK!") and marches
# (84a6); at the place it takes the item if still free and turns back
# without stopping (841f / 911b); home again it takes its class's first job
# (844d). Over the map it says "My troop is going to - to search for ...",
# "My troop is going back to - with ..." or "... We didn't find any." (the
# floppy: "We didn't find some krys available.").
# This runs dune_story_setup=search-equipment and checks those steps.
# Silent and headless, about a minute per release.
#
# Usage: scripts/check_search_equipment.sh

set -u

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
local_root="${DUNE_LOCAL_BUILD_ROOT:-/private/tmp/dune-scummvm-native-build}"
status=0

SDL_AUDIODRIVER=dummy DUNE_DATA="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}" "$script_dir/test_dune_scummvm_sdl.sh" build >/dev/null 2>&1 || {
	echo "FAIL search-equipment: build failed; run scripts/test_dune_scummvm_sdl.sh dump to see why"
	exit 1
}

for release in floppy cd; do
	data="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}"
	[ "$release" = cd ] && data="${DUNE_DATA_CD:-$repo_root/data}"
	run_root="$local_root/sdl-run/search-equipment-$release"
	rm -rf "$run_root"
	mkdir -p "$run_root/saves" "$run_root/frames"
	config="$run_root/search-equipment.ini"
	printf '[scummvm]\nsavepath=%s\ndune_input=%s\ndune_checkpoint_dir=%s\ndune_floppy_start=99\ndune_no_music=1\ndune_story_setup=search-equipment\n' \
		"$run_root/saves" "$repo_root/tests/regression/search-equipment.script" "$run_root/frames" > "$config"
	(cd "$local_root/build-sdl-dune" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy perl -e 'alarm 120; exec @ARGV' -- \
		./scummvm -c "$config" --gfx-mode=surface --no-fullscreen --path="$data" dune >/dev/null 2>&1)
	log="$run_root/saves/dune-ios.log"
	missing=""
	# The steps in order: greyed, nothing found, the march out, the pickup,
	# the way back, home with the bulbs, fully equipped, back with nothing.
	for step in "Story setup: search-equipment" "is greyed" "phase 0xf, troop 1 occupation 0x8" \
			"(ds:23 = 0xe) \"I don't think I can find some bulbs available" \
			"(ds:23 = 0xa) \"OK!\"" "Troop command: GO & SEARCH FOR EQUIPMENT" "to search for item 6" \
			"on the way out, troop 1 says \"My troop is going to" "to search for some bulbs." \
			"searching for item 6: found, taken" "on the way back, troop 1 says \"My troop is going back to" \
			"with some bulbs." "is back at place" "home at place" "(ds:23 = 0xf) \"I have all the equipment I need!\"" \
			"searching for item 2: none free" "on the way back, troop 2 says \"My troop is going back to" "We didn't find"; do
		grep -qF -- "$step" "$log" 2>/dev/null || missing="$missing [$step]"
	done
	# Troop 1 is home with its bulbs (equipment bit 1) and irrigates again (occupation 8).
	grep "Story setup: troop 1 home at place" "$log" 2>/dev/null | grep -q "occupation 0x8, equipment 0x2" ||
		missing="$missing [troop 1 home: occupation 0x8, equipment 0x2]"
	# Troop 2 comes back empty-handed to military training (occupation 4, no krys).
	grep "Story setup: troop 2 home at place" "$log" 2>/dev/null | grep -q "occupation 0x4, equipment 0" ||
		missing="$missing [troop 2 home: occupation 0x4, no equipment]"
	# A march costs no motivation unless the troop attacks (84d2 / floppy 91ce:
	# occupation byte 6 only; the original keeps 29 -> 29 on Spice86).
	grep "Story setup: after the order, troop 1 occupation 0x4b" "$log" 2>/dev/null | grep -qE "motivation ([0-9]+) -> \1," ||
		missing="$missing [motivation unchanged by the march]"
	# The last order, krys at troop 2's own place: taken there on the CD (77d7),
	# not found on the floppy (its search skips the troop's own place).
	if [ "$release" = cd ]; then
		grep -qF "takes item 2 at its own place" "$log" 2>/dev/null || missing="$missing [CD: krys taken at home]"
		grep -qF "(ds:23 = 0xc) \"Glad to have some Krys knifes." "$log" 2>/dev/null || missing="$missing [CD: 'Glad to have some Krys knifes.']"
	else
		grep -qF "(ds:23 = 0xe) \"I don't think I can find some krys available" "$log" 2>/dev/null ||
			missing="$missing [floppy: no krys elsewhere (0x0e)]"
		grep -qF "takes item 2 at its own place" "$log" 2>/dev/null && missing="$missing [floppy: took krys at home]"
	fi
	if [ -z "$missing" ]; then
		echo "PASS search-equipment $release: greyed below 0x10, nothing found, the march for bulbs, the pickup and the way back, 'I have all the equipment I need!', back with nothing, $( [ "$release" = cd ] && echo "krys taken at home (CD)" || echo "no pickup at home (floppy)")"
	else
		echo "FAIL search-equipment $release: missing$missing (see $log)"
		status=1
	fi
done
exit $status
