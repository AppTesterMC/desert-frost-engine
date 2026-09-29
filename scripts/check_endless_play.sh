#!/bin/sh

# The final battle and the endless play (floppy and CD). With the final
# attack launched (ds:c2 = 6) Stilgar's troops attack the Harkonnen palace.
# final-battle, without Paul: the next period's attack callback (CD
# seg000:739e -> 73a9, floppy 8102 -> 810d) drops the shield, ds:c2 7, and
# from then on no troop, vegetation or time-of-day event runs (1b5e,
# floppy 1ea1); the palace never becomes a sietch and the Baron's hall
# (0x3002) ends the game. endless-play, Paul riding in: MASSIVE ATTACK (CD
# 7317, floppy 807b) wins through the fortress-won routine (7419 -> 7429 ->
# 7443 / 81a7), the palace is held and ds:c2 goes back to 1 (7493); two days
# later it becomes a sietch (6e20 / 6dbb: type 0x30 -> 0, the Baron's,
# Feyd-Rautha's and the Emperor's records with it), room 2 no longer ends the
# game, the three prisoners talk but will not follow ("Who do you think you
# are?"), Liet Kynes follows (his topic 5, ds:c2 != 0), and no demand comes.
# This runs dune_story_setup=final-battle and =endless-play.
# Silent and headless, about a minute per run.
#
# Usage: scripts/check_endless_play.sh

set -u

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
local_root="${DUNE_LOCAL_BUILD_ROOT:-/private/tmp/dune-scummvm-native-build}"
status=0

SDL_AUDIODRIVER=dummy DUNE_DATA="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}" "$script_dir/test_dune_scummvm_sdl.sh" dump >/dev/null 2>&1 || {
	echo "FAIL endless-play: build failed; run scripts/test_dune_scummvm_sdl.sh dump to see why"
	exit 1
}

for release in floppy cd; do
	data="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}"
	[ "$release" = cd ] && data="${DUNE_DATA_CD:-$repo_root/data}"
	run_root="$local_root/sdl-run/final-battle-$release"
	rm -rf "$run_root"
	mkdir -p "$run_root/saves" "$run_root/frames"
	config="$run_root/final-battle.ini"
	printf '[scummvm]\nsavepath=%s\ndune_input=%s\ndune_checkpoint_dir=%s\ndune_floppy_start=99\ndune_no_music=1\ndune_rng_seed=1\ndune_story_setup=final-battle\n' \
		"$run_root/saves" "$repo_root/tests/regression/endless-play.script" "$run_root/frames" > "$config"
	(cd "$local_root/build-sdl-dune" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy perl -e 'alarm 120; exec @ARGV' -- \
		./scummvm -c "$config" --gfx-mode=surface --no-fullscreen --path="$data" dune >/dev/null 2>&1)
	log="$run_root/saves/dune-ios.log"
	missing=""
	set -- "Battle: the Harkonnen palace falls, final attack stage 7" "two days later, stage 7, place 1 type 0x30" "Story: Paul enters the Baron's hall, the end"
	[ "$release" = floppy ] && set -- "$@" 
	[ "$release" = cd ] && set -- "$@" 
	for step in "$@"; do
		grep -qF -- "$step" "$log" 2>/dev/null || missing="$missing [$step]"
	done
	if [ -z "$missing" ]; then
		echo "PASS final-battle $release: the shield falls without Paul, stage 7, the world stops, the Baron's hall ends the game"
	else
		echo "FAIL final-battle $release: missing$missing (see $log)"
		status=1
	fi
done
for release in floppy cd; do
	data="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}"
	[ "$release" = cd ] && data="${DUNE_DATA_CD:-$repo_root/data}"
	run_root="$local_root/sdl-run/endless-play-$release"
	rm -rf "$run_root"
	mkdir -p "$run_root/saves" "$run_root/frames"
	config="$run_root/endless-play.ini"
	printf '[scummvm]\nsavepath=%s\ndune_input=%s\ndune_checkpoint_dir=%s\ndune_floppy_start=99\ndune_no_music=1\ndune_rng_seed=1\ndune_story_setup=endless-play\n' \
		"$run_root/saves" "$repo_root/tests/regression/endless-play.script" "$run_root/frames" > "$config"
	(cd "$local_root/build-sdl-dune" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy perl -e 'alarm 120; exec @ARGV' -- \
		./scummvm -c "$config" --gfx-mode=surface --no-fullscreen --path="$data" dune >/dev/null 2>&1)
	log="$run_root/saves/dune-ios.log"
	missing=""
	set -- "Arrival: place 1 is in battle, Paul joins it" "MASSIVE ATTACK 1: won" "Battle: 0 Harkonnen place(s) left, final attack stage 6 -> 1" "Battle: fortress 1 becomes a sietch" "character 9 record 02 00 80 02" "character 11 record 02 00 80 02" "in room 2 of the palace, phase 0x58, people: 6 9 10 11" "9 says \"I've nothing to say.\"" "COME WITH ME to 10: stays" "COME WITH ME to 11: stays" "COME WITH ME to 6: comes" "a day later, demands 1 -> 1, stage 1"
	[ "$release" = floppy ] && set -- "$@" 
	[ "$release" = cd ] && set -- "$@" "6 answers \"OK!\""
	for step in "$@"; do
		grep -qF -- "$step" "$log" 2>/dev/null || missing="$missing [$step]"
	done
	if [ -z "$missing" ]; then
		echo "PASS endless-play $release: MASSIVE ATTACK with Paul takes the palace as a fort, stage 6 -> 1, a sietch two days later, room 2 is no ending, the prisoners talk and refuse, Kynes follows, no demand"
	else
		echo "FAIL endless-play $release: missing$missing (see $log)"
		status=1
	fi
done
exit $status
