#!/bin/sh

# The Harkonnen saboteurs and harvester worms check (floppy and CD).
# Once a day, at the period whose low nibble is the troop id's, a spice
# troop with a harvester rolls for events after Paul's first vision (CD
# seg000:714c, floppy 7eb5):
# - saboteurs first (71bc / 7f25): from phase 0x35, for a troop whose speech
#   word has bit 6 (troops 1, 13, 14, 15, 19, 20 in the initial data), 1 in
#   8: the harvester is damaged (bitfield_10 0x8200), the troop stops, the
#   place gets status bit 2 and message 3 "There are saboteurs here in -!"
#   is queued;
# - then a worm, with the region's chance (ds:1142 + region - 1): an orni
#   sees it coming; else nothing, men lost, the harvester damaged, or
#   swallowed (message 6).
# A damaged harvester is repaired at the troop's slot the next day (705c);
# until then the troop mines nothing, refuses a new occupation ("We have to
# repair our equipment before doing anything else!", condition 633) and
# MODIFY EQUIPMENT is greyed (7888). An army troop at the place hunts the
# saboteurs for 0x40 - army skill periods (725f / 7fc3); then the place and
# its troops are rid of them: "Saboteurs have been discovered. You'll have no
# problems here now!" (577).
# This runs dune_story_setup=saboteurs and checks those steps in the log.
# Silent and headless, about a minute per release.
#
# Usage: scripts/check_saboteurs.sh

set -u

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
local_root="${DUNE_LOCAL_BUILD_ROOT:-${TMPDIR:-/tmp}/dune-scummvm-native-build}"
status=0

SDL_AUDIODRIVER=dummy DUNE_DATA="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}" "$script_dir/test_dune_scummvm_sdl.sh" build >/dev/null 2>&1 || {
	echo "FAIL saboteurs: build failed; run scripts/test_dune_scummvm_sdl.sh dump to see why"
	exit 1
}

for release in floppy cd; do
	data="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}"
	[ "$release" = cd ] && data="${DUNE_DATA_CD:-$repo_root/data}"
	run_root="$local_root/sdl-run/saboteurs-$release"
	rm -rf "$run_root"
	mkdir -p "$run_root/saves" "$run_root/frames"
	config="$run_root/saboteurs.ini"
	printf '[scummvm]\nsavepath=%s\ndune_input=%s\ndune_checkpoint_dir=%s\ndune_floppy_start=99\ndune_no_music=1\ndune_rng_seed=7\ndune_story_setup=saboteurs\n' \
		"$run_root/saves" "$repo_root/tests/regression/saboteurs.script" "$run_root/frames" > "$config"
	(cd "$local_root/build-sdl-dune" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy perl -e 'alarm 120; exec @ARGV' -- \
		./scummvm -c "$config" --gfx-mode=surface --no-fullscreen --path="$data" dune >/dev/null 2>&1)
	log="$run_root/saves/dune-ios.log"
	missing=""
	for step in "Story setup: troop" "World: saboteurs damage troop" "Vision: queued 0xf03" "Vision: message 0xf03" \
			"saboteurs here in" "is greyed" "\"We have to repair our equipment before doing anything else!\"" "refuses the order" \
			"World: troop" "has repaired its harvester" "intentionally. We've repaired it" \
			"hunts the saboteurs" "found the saboteurs" "\"Saboteurs have been discovered. You'll have no problems here now!\"" \
			"after the hunt, 0 sabotage(s)" "its orni saw the worm sign" "World: harvester breaks down (a worm attack)" \
			"World: a worm swallows troop" "Vision: queued 0xf06" "World: a worm attacks troop"; do
		grep -qF -- "$step" "$log" 2>/dev/null || missing="$missing [$step]"
	done
	# The damaged troop mines nothing until the repair: the occupation keeps its stopped bit (0x10).
	grep "Story setup: sabotaged after" "$log" 2>/dev/null | grep -q "occupation 0x10 " || missing="$missing [stopped while damaged]"
	grep "Story setup: repaired after" "$log" 2>/dev/null | grep -q "occupation 0 " || missing="$missing [working after the repair]"
	# The refused order is taken back: still a spice miner.
	grep "Story setup: after the order" "$log" 2>/dev/null | grep -q "occupation 0x10," || missing="$missing [the order taken back]"
	if [ -z "$missing" ]; then
		echo "PASS saboteurs $release: sabotage, the chief's message, the lines, greyed MODIFY EQUIPMENT, refused order, repair next day, the hunt; worms: orni, damage, men, swallowed"
	else
		echo "FAIL saboteurs $release: missing$missing (see $log)"
		status=1
	fi
done
exit $status
