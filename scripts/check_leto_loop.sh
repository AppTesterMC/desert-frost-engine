#!/bin/sh

# The Leto loop check (floppy and CD): after the Duke's death (phase 0x4c) the
# original never clears his character record (CD sub_11166 moves Jessica and
# queues vision 0x105 only), so he keeps standing in the throne room, listed
# and talking ("Keep on going Paul!" in the speedrun video, 2105 s). The engine
# plays it that way by default; the option dune_fix_leto_loop (Options >
# Engine, or scummvm.ini) removes him. This runs the throne room right after
# his death (dune_story_setup=letodead) with the option off and on, clicks
# the second row, and checks: off = Leto listed and talking, on = gone.
# Silent and headless, about a minute per run.
#
# Usage: scripts/check_leto_loop.sh

set -u

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
local_root="${DUNE_LOCAL_BUILD_ROOT:-/private/tmp/dune-scummvm-native-build}"
status=0

SDL_AUDIODRIVER=dummy DUNE_DATA="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}" "$script_dir/test_dune_scummvm_sdl.sh" dump >/dev/null 2>&1 || {
	echo "FAIL leto loop: build failed; run scripts/test_dune_scummvm_sdl.sh dump to see why"
	exit 1
}

run() { # release data option -> prints "present talking"
	release=$1 data=$2 fix=$3
	run_root="$local_root/sdl-run/leto-$release-$fix"
	rm -rf "$run_root"
	mkdir -p "$run_root/saves" "$run_root/frames"
	config="$run_root/leto.ini"
	printf '[scummvm]\nsavepath=%s\ndune_input=%s\ndune_checkpoint_dir=%s\ndune_floppy_start=99\ndune_no_music=1\ndune_story_setup=letodead\ndune_fix_leto_loop=%s\n' \
		"$run_root/saves" "$repo_root/tests/regression/leto-loop.script" "$run_root/frames" "$fix" > "$config"
	(cd "$local_root/build-sdl-dune" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy perl -e 'alarm 120; exec @ARGV' -- \
		./scummvm -c "$config" --gfx-mode=surface --no-fullscreen --path="$data" dune >/dev/null 2>&1)
	log="$run_root/saves/dune-ios.log"
	people=$(grep "after Leto's death, people:" "$log" 2>/dev/null | tail -1 | sed 's/.*people://')
	present=no
	case " $people " in *" 0 "*) present=yes ;; esac
	talking=no
	grep -q "Conversation: character 0," "$log" 2>/dev/null && talking=yes
	enabled=no
	grep -q "Option: dune_fix_leto_loop on" "$log" 2>/dev/null && enabled=yes
	echo "$present $talking $enabled"
}

for release in floppy cd; do
	data="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}"
	[ "$release" = cd ] && data="${DUNE_DATA_CD:-$repo_root/data}"
	set -- $(run "$release" "$data" false)
	if [ "$1" = yes ] && [ "$2" = yes ] && [ "$3" = no ]; then
		echo "PASS leto loop $release, option off: Leto still in the throne room and talking (as the original)"
	else
		echo "FAIL leto loop $release, option off: present=$1 talking=$2 option=$3 (expected yes yes no)"
		status=1
	fi
	set -- $(run "$release" "$data" true)
	if [ "$1" = no ] && [ "$2" = no ] && [ "$3" = yes ]; then
		echo "PASS leto loop $release, option on: Leto gone from the throne room"
	else
		echo "FAIL leto loop $release, option on: present=$1 talking=$2 option=$3 (expected no no yes)"
		status=1
	fi
done
exit $status
