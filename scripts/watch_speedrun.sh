#!/bin/sh

# Watch the speedrun bot play a Dune release in a window on this Mac.
# It uses the same bot as scripts/check_speedrun.sh (speedrun.cpp), slowed
# down so each dialogue page, room and battle can be read. Flights and
# animations run in real time unless --skip-flight-after is set. Milestones
# show as status messages, and the full log is written to the saves folder
# (dune-ios.log). Close the window to stop; endless exits after its tour.
#
# Usage: scripts/watch_speedrun.sh [campaign|full|ecology|endless] [options]
#   campaign  from Leto's death to the ending (the default; the story before is a logged shortcut)
#   full      from a new game to the ending (engine bot, seed 1 by default)
#   ecology   assisted whole-planet greening and the release-specific ecology outcome
#   endless   bypass the ending, recruit Liet Kynes, tour the garden and
#             every sietch room, then finish in the palace garden
#   --release floppy|cd|amiga  select the matching local game data
#   --music   play the game's music (off by default)
#   --pace    pause length in percent (100 = about 1.4 s per dialogue page)
#   --room-wait SECONDS  extra idle time in every visited room
#   --skip-flight-after SECONDS  automatically use SKIP TO DESTINATION after this long
#   --action-pause SECONDS  hold the screen before and after each scripted action/click
#   --action-pause-before SECONDS  hold the screen before each scripted action/click
#   --action-pause-after SECONDS   hold the screen after each scripted action/click
#   defaults: 4 seconds before, 5 seconds after, and a 6-second flight skip
#   --intro   play the intro first (skipped by default)
#   --auto-stop  exit the game when the route reaches its endpoint
#   --cd      shorthand for --release cd
#   --data    the game folder (default: the floppy data in data/floppy)
#   --seed    the random seed (the checks pass with --seed 1)
#   -h        this help
#
# From the first worm ride (day 5-6) the bot travels by worm: CALL A WORM's
# map, the call (floppy: the SHAI worm; CD: VER.HNM) and the ride on the
# worm's back. To watch it: scripts/watch_speedrun.sh campaign [--cd]

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
part=campaign
release=floppy
music=0
pace=100
intro=0
seed=1
data="$repo_root/data/floppy"
room_wait=0
skip_flight_after=6
pause_before=4
pause_after=5
validate_seconds() {
	seconds_option=$1
	seconds_value=$2
	case "$seconds_value" in
		''|*[!0-9]*) echo "$seconds_option needs a whole number of seconds" >&2; exit 2 ;;
	esac
	seconds_digits=$(printf '%s' "$seconds_value" | sed 's/^0*//')
	[ -n "$seconds_digits" ] || seconds_digits=0
	if [ "${#seconds_digits}" -gt 7 ] || { [ "${#seconds_digits}" -eq 7 ] && [ "$seconds_digits" -gt 2147483 ]; }; then
		echo "$seconds_option is too large (maximum 2147483 seconds)" >&2
		exit 2
	fi
}
while [ $# -gt 0 ]; do
	case "$1" in
		campaign|full) part=$1 ;;
		green|ecology) part=green ;;
		endless-tour|endless) part=endless-tour ;;
		--release)
			case "${2:-}" in
				floppy) release=floppy; data="$repo_root/data/floppy" ;;
				cd) release=cd; data="$repo_root/data" ;;
				amiga) release=amiga; data="${DUNE_DATA_AMIGA:-$HOME/dune-amiga-data/game}" ;;
				*) echo "--release needs floppy, cd, or amiga" >&2; exit 2 ;;
			esac
			shift
			;;
		--music) music=1 ;;
		--pace) pace=$2; shift ;;
		--room-wait) validate_seconds "$1" "$2"; room_wait=$2; shift ;;
		--skip-flight-after) validate_seconds "$1" "$2"; skip_flight_after=$2; shift ;;
		--action-pause)
			validate_seconds "$1" "$2"
			pause_before=$2
			pause_after=$2
			shift
			;;
		--action-pause-before) validate_seconds "$1" "$2"; pause_before=$2; shift ;;
		--action-pause-after) validate_seconds "$1" "$2"; pause_after=$2; shift ;;
		--intro) intro=1 ;;
		--seed) seed=$2; shift ;;
		--data) data=$2; shift ;;
		--auto-stop) auto_stop=1 ;;
		--cd) release=cd; data="$repo_root/data" ;;
		-h|--help) sed -n '3,/^$/p' "$0" | sed -n '/^#/s/^# \{0,1\}//p'; echo; echo "Worm rides: scripts/watch_speedrun.sh campaign [--cd]"; exit 0 ;;
		*) echo "unknown option $1" >&2; exit 2 ;;
	esac
	shift
done

case "${DUNE_LOCAL_BUILD_ROOT:-}" in "${TMPDIR%/}"/*|/tmp/*) unset DUNE_LOCAL_BUILD_ROOT ;; esac
case "${DUNE_RUN_ROOT:-}" in "${TMPDIR%/}"/*|/tmp/*) unset DUNE_RUN_ROOT ;; esac
local_root="${DUNE_LOCAL_BUILD_ROOT:-$HOME/Library/Caches/Cryogenic-local/dune-scummvm-native-build}"
run_root="${DUNE_RUN_ROOT:-$local_root/sdl-run}"
build_log="$local_root/build-sdl-dune/dune-build.log"
mkdir -p "$(dirname -- "$build_log")"
# Build (or bring up to date) the desktop ScummVM with the engine. Only the
# build: the headless screen tour of "dump" delayed the window by minutes.
if ! SDL_AUDIODRIVER=dummy DUNE_DATA="$data" DUNE_SKIP_BUILD="${DUNE_SKIP_BUILD:-0}" bash "$script_dir/test_dune_scummvm_sdl.sh" build >"$build_log" 2>&1; then
	echo "build failed; details from $build_log:" >&2
	tail -60 "$build_log" >&2 || true
	exit 1
fi
build_id_file="$local_root/scummvm/engines/dune/build_id.h"
build_id=$(sed -n 's/^#define DUNE_BUILD_ID "\([^"]*\)"/\1/p' "$build_id_file" | head -1)
case "$build_id" in
	''|*unstamped*)
		echo "The staged engine has no stamped build ID; rebuilding it before launch."
		if ! SDL_AUDIODRIVER=dummy DUNE_DATA="$data" DUNE_SKIP_BUILD=0 \
				bash "$script_dir/test_dune_scummvm_sdl.sh" build >"$build_log" 2>&1; then
			echo "build failed while stamping the engine; details from $build_log:" >&2
			tail -60 "$build_log" >&2 || true
			exit 1
		fi
		build_id=$(sed -n 's/^#define DUNE_BUILD_ID "\([^"]*\)"/\1/p' "$build_id_file" | head -1)
		;;
esac
if [ -z "$build_id" ] || [ "$build_id" = build_unstamped ]; then
	echo "Could not get a stamped engine build ID from $build_id_file" >&2
	exit 1
fi

config="$run_root/watch.ini"
mkdir -p "$run_root/saves"
{
	printf '[scummvm]\nsavepath=%s\n' "$run_root/saves"
	printf 'dune_speedrun=%s\ndune_speedrun_watch=%s\ndune_rng_seed=%s\n' "$part" "$pace" "$seed"
	printf 'dune_speedrun_room_wait=%s\ndune_speedrun_skip_flight_after=%s\n' "$room_wait" "$skip_flight_after"
	printf 'dune_speedrun_pause_before=%s\ndune_speedrun_pause_after=%s\n' "$pause_before" "$pause_after"
	[ "$music" = 1 ] || printf 'dune_no_music=1\n'
	[ "$intro" = 1 ] || printf 'dune_floppy_start=99\ndune_intro_start=99\ndune_skip_cd_story=1\n'
} > "$config"


echo "Build: ${build_id:-unknown}"
echo "Action pauses: before ${pause_before}s, after ${pause_after}s; flight skip: ${skip_flight_after}s"
echo "Watching the $part speedrun (seed $seed); close the window to stop."
echo "Log: $run_root/saves/dune-ios.log  (grep Speedrun: for the milestones)"
cd "$local_root/build-sdl-dune"
if [ "$music" = 1 ]; then
	exec ./scummvm -c "$config" --no-fullscreen --path="$data" dune
else
	SDL_AUDIODRIVER=dummy exec ./scummvm -c "$config" --no-fullscreen --path="$data" dune
fi
