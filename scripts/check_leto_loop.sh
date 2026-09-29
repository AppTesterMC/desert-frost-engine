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
# Usage: scripts/check_leto_loop.sh [--watch] [--music]
#   --watch  play each case in a window (floppy and CD, option off then on),
#            slower, with what to look for and the log lines that decide it
#   --music  with --watch: play the music too

set -u

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
local_root="${DUNE_LOCAL_BUILD_ROOT:-/private/tmp/dune-scummvm-native-build}"
status=0
watch=0
music=0
for arg in "$@"; do
	case "$arg" in
		--watch) watch=1 ;;
		--music) music=1 ;;
		*) echo "usage: $0 [--watch] [--music]" >&2; exit 2 ;;
	esac
done

SDL_AUDIODRIVER=dummy DUNE_DATA="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}" "$script_dir/test_dune_scummvm_sdl.sh" build >/dev/null 2>&1 || {
	echo "FAIL leto loop: build failed; run scripts/test_dune_scummvm_sdl.sh dump to see why"
	exit 1
}

# --watch: a window instead of the headless run, the script's waits doubled
# and the last screen held 5 s, so each case can be followed on screen.
play_script() { # script -> the script to play
	if [ "$watch" = 1 ]; then
		watched="$run_root/watch.script"
		awk '/^wait /{ $2 = $2 * 2 } /^quit/{ print "wait 5000               # hold the last screen (--watch)" } { print }' "$1" > "$watched"
		echo "$watched"
	else
		echo "$1"
	fi
}
run_game() { # config data
	if [ "$watch" = 1 ]; then
		if [ "$music" = 1 ]; then
			(cd "$local_root/build-sdl-dune" && ./scummvm -c "$1" --no-fullscreen --path="$2" dune >/dev/null 2>&1)
		else
			(cd "$local_root/build-sdl-dune" && SDL_AUDIODRIVER=dummy ./scummvm -c "$1" --no-fullscreen --path="$2" dune >/dev/null 2>&1)
		fi
	else
		(cd "$local_root/build-sdl-dune" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy perl -e 'alarm 120; exec @ARGV' -- \
			./scummvm -c "$1" --gfx-mode=surface --no-fullscreen --path="$2" dune >/dev/null 2>&1)
	fi
}

run() { # release data option -> prints "present talking"
	release=$1 data=$2 fix=$3
	run_root="$local_root/sdl-run/leto-$release-$fix"
	rm -rf "$run_root"
	mkdir -p "$run_root/saves" "$run_root/frames"
	config="$run_root/leto.ini"
	script=$(play_script "$repo_root/tests/regression/leto-loop.script")
	printf '[scummvm]\nsavepath=%s\ndune_input=%s\ndune_checkpoint_dir=%s\ndune_floppy_start=99\ndune_story_setup=letodead\ndune_fix_leto_loop=%s\n' \
		"$run_root/saves" "$script" "$run_root/frames" "$fix" > "$config"
	[ "$watch" = 1 ] && [ "$music" = 1 ] || printf 'dune_no_music=1\n' >> "$config"
	run_game "$config" "$data"
	log="$run_root/saves/dune-ios.log"
	if [ "$watch" = 1 ]; then
		grep -E "Option: dune_fix_leto_loop|after Leto's death, people:|Conversation: character 0,|Dialogue: entry" "$log" 2>/dev/null |
			head -6 | sed 's/^/    log: /' >&2
	fi
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
	[ "$watch" = 1 ] && echo "Now: $release, option OFF. Watch the throne room: Leto is still listed on the rows and talks when clicked (the original's bug)."
	set -- $(run "$release" "$data" false)
	if [ "$1" = yes ] && [ "$2" = yes ] && [ "$3" = no ]; then
		echo "PASS leto loop $release, option off: Leto still in the throne room and talking (as the original)"
	else
		echo "FAIL leto loop $release, option off: present=$1 talking=$2 option=$3 (expected yes yes no)"
		status=1
	fi
	[ "$watch" = 1 ] && echo "Now: $release, option ON. Watch the throne room: Leto is gone from the room, the rows and the talks."
	set -- $(run "$release" "$data" true)
	if [ "$1" = no ] && [ "$2" = no ] && [ "$3" = yes ]; then
		echo "PASS leto loop $release, option on: Leto gone from the throne room"
	else
		echo "FAIL leto loop $release, option on: present=$1 talking=$2 option=$3 (expected no no yes)"
		status=1
	fi
done
exit $status
