#!/bin/sh
# Seeded bot-order regression: legal occupation accepted; disabled movement,
# nonexistent ordinary-miner prospecting and a reaction refusal stay refused.
# A just-won fort is treated as future raid exposure; idle prospectors leave it.
# These are engine menu-action checks, not a full campaign or UI playthrough.
set -u
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
local_root="${DUNE_LOCAL_BUILD_ROOT:-/tmp/dune-scummvm-native-build}"
run_base="${DUNE_RUN_ROOT:-$local_root/sdl-run}/speedrun-orders"
status=0
if ! SDL_AUDIODRIVER=dummy "$script_dir/test_dune_scummvm_sdl.sh" build >/dev/null 2>&1; then
	echo "FAIL speedrun-orders: build failed"
	exit 1
fi
mkdir -p "$run_base"
for release in ${DUNE_ORDER_RELEASES:-floppy cd}; do
	case "$release" in
		floppy) data="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}" ;;
		cd) data="${DUNE_DATA_CD:-$repo_root/data}" ;;
		amiga) data="${DUNE_DATA_AMIGA:-$repo_root/data/amiga}" ;;
		*) echo "FAIL speedrun-orders $release: unsupported release"; status=1; continue ;;
	esac
	run=$(mktemp -d "$run_base/$release.XXXXXX") || exit 1
	code=0
	DUNE_SKIP_BUILD=1 DUNE_RUN_ROOT="$run" DUNE_DATA="$data" \
		"$script_dir/test_dune_scummvm_sdl.sh" speedrun orders 1 >"$run/runner.log" 2>&1 || code=$?
	log="$run/saves/dune-ios.log"
	if [ "$code" -eq 0 ] && [ -f "$log" ] &&
		[ "$(grep -c 'order-check PASS' "$log")" -eq 6 ] &&
		grep -q 'Troops: troop 2 refuses the order' "$log" &&
		! grep -Eq 'order-check FAIL|set directly|ORDER BYPASS' "$log"; then
		echo "PASS speedrun-orders $release: legal occupation accepted; disabled move, missing prospecting row and refusal preserve state; miners and prospectors avoid raid exposure"
	else
		echo "FAIL speedrun-orders $release: expected six menu-action/field-safety assertions and a refusal, engine exit $code (see $log)"
		status=1
	fi
done
exit "$status"
