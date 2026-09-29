#!/bin/sh

# The smugglers' trade check (floppy and CD; notes/smugglers.md, queue item
# 4). At a village the smuggler's "let me see" (event 8, CD seg000:2388,
# floppy 26a0) offers the next item in stock with its price (23a5-23d4);
# the bargaining menu with speaker 13 buys it (ACCEPT 241a: the price on
# the smugglers' bill, the item into the village's stock at +0x14), makes
# him look for something else (REFUSE 2432) or haggle (ARGUE 2453: "Eh!",
# "Forget it!" or the price less an eighth). His talk starts again when his
# lists run out (loc_194b9). A day later he wants his bill paid (condition
# 441 CD / 438 floppy); Duncan pays it from the stock (action 5, event 9:
# 24ee/2517), and the smuggler trades again.
# This runs dune_story_setup=smugglers and checks those steps in the log.
# Silent and headless, about a minute per release.
#
# Usage: scripts/check_smugglers.sh

set -u

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
local_root="${DUNE_LOCAL_BUILD_ROOT:-/private/tmp/dune-scummvm-native-build}"
status=0

SDL_AUDIODRIVER=dummy DUNE_DATA="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}" "$script_dir/test_dune_scummvm_sdl.sh" build >/dev/null 2>&1 || {
	echo "FAIL smugglers: build failed; run scripts/test_dune_scummvm_sdl.sh dump to see why"
	exit 1
}

for release in floppy cd; do
	data="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}"
	[ "$release" = cd ] && data="${DUNE_DATA_CD:-$repo_root/data}"
	run_root="$local_root/sdl-run/smugglers-$release"
	rm -rf "$run_root"
	mkdir -p "$run_root/saves" "$run_root/frames"
	config="$run_root/smugglers.ini"
	printf '[scummvm]\nsavepath=%s\ndune_input=%s\ndune_checkpoint_dir=%s\ndune_floppy_start=99\ndune_no_music=1\ndune_story_setup=smugglers\n' \
		"$run_root/saves" "$repo_root/tests/regression/smugglers.script" "$run_root/frames" > "$config"
	(cd "$local_root/build-sdl-dune" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy perl -e 'alarm 120; exec @ARGV' -- \
		./scummvm -c "$config" --gfx-mode=surface --no-fullscreen --path="$data" dune >/dev/null 2>&1)
	log="$run_root/saves/dune-ios.log"
	missing=""
	for step in "Story setup: at place 20 (type 0x21), smugglers' record 0x10e9" "Smugglers: offer item" "Talk: choice 3" \
			"Smugglers: bought item" "Story setup: village item" "prefer that you pay my last bill" \
			"Smugglers: bill paid" "after Duncan, bill 0 kg, ds:22 = 0"; do
		grep -qF -- "$step" "$log" 2>/dev/null || missing="$missing [$step]"
	done
	# ARGUE did one of its three things.
	grep -qE "Smugglers: ARGUE, (forget it|cut to|losing money)" "$log" 2>/dev/null || missing="$missing [ARGUE's answer]"
	# The price went on the bill: the bought line's price equals the bill.
	grep "Smugglers: bought item" "$log" 2>/dev/null | head -1 | grep -qE "for ([0-9]+) kg, bill \1 kg" || missing="$missing [bill = price]"
	# With the bill paid he offers again (the setup's last talk).
	sed -n '/Smugglers: bill paid/,$p' "$log" 2>/dev/null | grep -q "Smugglers: offer item" || missing="$missing [an offer after the payment]"
	if [ -z "$missing" ]; then
		echo "PASS smugglers $release: offer, ARGUE, ACCEPT (item into the village, price on the bill), 'pay my last bill' a day later, Duncan pays it, a new offer"
	else
		echo "FAIL smugglers $release: missing$missing (see $log)"
		status=1
	fi
done
exit $status
