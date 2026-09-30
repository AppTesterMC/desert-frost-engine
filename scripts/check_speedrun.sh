#!/bin/sh

# Play the story/campaign through the engine's own game actions. Full runs
# must reach the Emperor's throne room without BLOCKED, FORCED, or direct
# order fallback markers. Passing means assisted engine-logic coverage only.
# Battle losses may reload previously written saves; this is a completion
# regression, not proof of a single uninterrupted playthrough.
#
# Usage: scripts/check_speedrun.sh [campaign|full] [seed...]
#   campaign starts after Leto's death (only its two declared setup FORCED
#   markers are allowed; later forced progression fails)
#   full starts a new game and rejects every FORCED shortcut
# DUNE_SPEEDRUN_RELEASES defaults to "floppy cd"; "amiga" may be added.
# Sega CD uses a separate frontend without the completion bot and is rejected.
# DUNE_SPEEDRUN_ALLOW_ORDER_BYPASSES=1 permits legacy bot shortcuts for engine
# investigation only; output and evidence.json always disclose the bypasses.
# DUNE_SPEEDRUN_REQUIRE_PLAYER_UI=1 fails: current bot coverage is assisted.

set -u

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
local_root="${DUNE_LOCAL_BUILD_ROOT:-/tmp/dune-scummvm-native-build}"
run_base="${DUNE_RUN_ROOT:-$local_root/sdl-run}/speedrun"
part=${1:-campaign}
[ "$#" -eq 0 ] || shift
case "$part" in campaign|full) ;; *) echo "FAIL speedrun: expected campaign or full"; exit 2 ;; esac
seeds=${*:-1}
releases=${DUNE_SPEEDRUN_RELEASES:-floppy cd}
status=0

if ! SDL_AUDIODRIVER=dummy "$script_dir/test_dune_scummvm_sdl.sh" build >/dev/null 2>&1; then
	echo "FAIL speedrun: build failed"
	exit 1
fi
mkdir -p "$run_base"
for seed in $seeds; do
	case "$seed" in ''|*[!0-9]*) echo "FAIL speedrun: invalid seed $seed"; exit 2 ;; esac
	for release in $releases; do
		case "$release" in
			floppy) data="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}" ;;
			cd) data="${DUNE_DATA_CD:-$repo_root/data}" ;;
			amiga) data="${DUNE_DATA_AMIGA:-$repo_root/data/amiga}" ;;
			*) echo "FAIL $part $release seed $seed: unsupported completion frontend"; status=1; continue ;;
		esac
		if [ ! -d "$data" ]; then
			echo "FAIL $part $release seed $seed: game data missing ($data)"
			status=1
			continue
		fi
		# Never reuse a log or save from an earlier run, even if a launch fails.
		run=$(mktemp -d "$run_base/$part-$release-seed$seed.XXXXXX") || exit 1
		code=0
		DUNE_SKIP_BUILD=1 DUNE_RUN_ROOT="$run" DUNE_DATA="$data" \
			"$script_dir/test_dune_scummvm_sdl.sh" speedrun "$part" "$seed" >"$run/runner.log" 2>&1 || code=$?
		log="$run/saves/dune-ios.log"
		set -- "$script_dir/dune_speedrun_evidence.py" "$log" --part "$part" \
			--process-exit "$code" --release "$release" --seed "$seed" --json "$run/evidence.json" \
			--binary "$local_root/build-sdl-dune/scummvm"
		[ "${DUNE_SPEEDRUN_ALLOW_ORDER_BYPASSES:-0}" != 1 ] || set -- "$@" --allow-order-bypasses
		[ "${DUNE_SPEEDRUN_REQUIRE_PLAYER_UI:-0}" != 1 ] || set -- "$@" --require-player-ui
		python3 "$@" || status=1
		echo "     evidence: $run"
	done
done
exit "$status"
