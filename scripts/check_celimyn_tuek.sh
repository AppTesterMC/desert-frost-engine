#!/bin/sh

# The Celimyn-Tuek check (floppy and CD): the initial data gives the sietch
# Celimyn-Tuek (place names 0x0c, 0x05) the discovery phase 0xff (location
# byte 0x0b), which the flight search's phase compare (floppy seg000:6257,
# cmp ds:2A, [si+0Bh]) never passes, so it is never found. The engine keeps
# that by default; the option dune_fix_celimyn_tuek (Options > Engine, or
# scummvm.ini) makes it 0x58 at new game and after a load. This runs the
# setup dune_story_setup=celimyn with the option off and on and checks:
# off = 0xff, never findable, still 0xff after a reload; on = 0x58, findable
# from phase 0x58 (not 0x57), 0x58 again after reloading a save holding 0xff.
# Silent and headless, under a minute per run.
#
# Usage: scripts/check_celimyn_tuek.sh

set -u

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
local_root="${DUNE_LOCAL_BUILD_ROOT:-/private/tmp/dune-scummvm-native-build}"
status=0

SDL_AUDIODRIVER=dummy DUNE_DATA="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}" "$script_dir/test_dune_scummvm_sdl.sh" dump >/dev/null 2>&1 || {
	echo "FAIL celimyn-tuek: build failed; run scripts/test_dune_scummvm_sdl.sh dump to see why"
	exit 1
}

run() { # release data option -> prints the log path
	release=$1 data=$2 fix=$3
	run_root="$local_root/sdl-run/celimyn-$release-$fix"
	rm -rf "$run_root"
	mkdir -p "$run_root/saves" "$run_root/frames"
	config="$run_root/celimyn.ini"
	printf '[scummvm]\nsavepath=%s\ndune_input=%s\ndune_checkpoint_dir=%s\ndune_floppy_start=99\ndune_no_music=1\ndune_story_setup=celimyn\ndune_fix_celimyn_tuek=%s\n' \
		"$run_root/saves" "$repo_root/tests/regression/celimyn-tuek.script" "$run_root/frames" "$fix" > "$config"
	(cd "$local_root/build-sdl-dune" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy perl -e 'alarm 120; exec @ARGV' -- \
		./scummvm -c "$config" --gfx-mode=surface --no-fullscreen --path="$data" dune >/dev/null 2>&1)
	echo "$run_root/saves/dune-ios.log"
}

check() { # log pattern...
	log=$1; shift
	for p in "$@"; do
		grep -qF -- "$p" "$log" 2>/dev/null || { echo "$p"; return 1; }
	done
}

for release in floppy cd; do
	data="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}"
	[ "$release" = cd ] && data="${DUNE_DATA_CD:-$repo_root/data}"
	log=$(run "$release" "$data" false)
	if missing=$(check "$log" "discovery phase 0xff" "findable at phase 0x57 no, at 0x58 no" "and a load: discovery phase 0xff") &&
		! grep -q "Option: dune_fix_celimyn_tuek" "$log"; then
		echo "PASS celimyn-tuek $release, option off: discovery phase 0xff, never findable (as the original)"
	else
		echo "FAIL celimyn-tuek $release, option off: missing '$missing' or the option ran ($log)"
		status=1
	fi
	log=$(run "$release" "$data" true)
	if missing=$(check "$log" "Option: dune_fix_celimyn_tuek on" "discovery phase 0x58" "findable at phase 0x57 no, at 0x58 yes" "and a load: discovery phase 0x58"); then
		echo "PASS celimyn-tuek $release, option on: discovery phase 0x58, findable from 0x58, patched again after a load"
	else
		echo "FAIL celimyn-tuek $release, option on: missing '$missing' ($log)"
		status=1
	fi
done
exit $status
