#!/bin/sh

# Watch the speedrun bot play the floppy release in a window on this Mac.
# It uses the same bot as scripts/check_speedrun.sh (speedrun.cpp), slowed
# down so each dialogue page, room and battle can be read. Flights and
# animations run in real time. Milestones show as status messages, and the
# full log is written to the saves folder (dune-ios.log). Close the window to
# stop; when the run ends, the game stays playable from where the bot left it.
#
# Usage: scripts/watch_speedrun.sh [campaign|full] [--cd] [--music] [--pace PERCENT] [--intro] [--seed N]
#   campaign  from Leto's death to the ending (the default; the story before is a logged shortcut)
#   full      from a new game to the ending (floppy; the check passes with --seed 1)
#   --music   play the game's music (off by default)
#   --pace    pause length in percent (100 = about 1.4 s per dialogue page)
#   --intro   play the intro first (skipped by default)
#   --cd      the CD release (data/: DUNE.DAT and DNCDPRG.EXE); campaign
#             reaches the end on the CD, full stops before the final attack
#             (the atomics troops are captured; being fixed)
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
music=0
pace=100
intro=0
seed=$(date +%s)
data="$repo_root/data/floppy"
while [ $# -gt 0 ]; do
	case "$1" in
		campaign|full) part=$1 ;;
		--music) music=1 ;;
		--pace) pace=$2; shift ;;
		--intro) intro=1 ;;
		--seed) seed=$2; shift ;;
		--data) data=$2; shift ;;
		--cd) data="$repo_root/data" ;;
		-h|--help) sed -n '3,/^$/p' "$0" | sed -n '/^#/s/^# \{0,1\}//p'; echo; echo "Worm rides: scripts/watch_speedrun.sh campaign [--cd]"; exit 0 ;;
		*) echo "unknown option $1" >&2; exit 2 ;;
	esac
	shift
done

local_root="${DUNE_LOCAL_BUILD_ROOT:-/tmp/dune-scummvm-native-build}"
# Build (or bring up to date) the desktop ScummVM with the engine; the dump
# run it ends with is silent and headless.
SDL_AUDIODRIVER=dummy DUNE_DATA="$data" "$script_dir/test_dune_scummvm_sdl.sh" dump >/dev/null 2>&1 || {
	echo "build failed; run scripts/test_dune_scummvm_sdl.sh dump to see why" >&2
	exit 1
}

run_root="$local_root/sdl-run"
config="$run_root/watch.ini"
mkdir -p "$run_root/saves"
{
	printf '[scummvm]\nsavepath=%s\n' "$run_root/saves"
	printf 'dune_speedrun=%s\ndune_speedrun_watch=%s\ndune_rng_seed=%s\n' "$part" "$pace" "$seed"
	[ "$music" = 1 ] || printf 'dune_no_music=1\n'
	[ "$intro" = 1 ] || printf 'dune_floppy_start=99\n'
} > "$config"

echo "Watching the $part speedrun (seed $seed); close the window to stop."
echo "Log: $run_root/saves/dune-ios.log  (grep Speedrun: for the milestones)"
cd "$local_root/build-sdl-dune"
if [ "$music" = 1 ]; then
	exec ./scummvm -c "$config" --no-fullscreen --path="$data" dune
else
	SDL_AUDIODRIVER=dummy exec ./scummvm -c "$config" --no-fullscreen --path="$data" dune
fi
