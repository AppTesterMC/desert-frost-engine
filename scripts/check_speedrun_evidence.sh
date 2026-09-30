#!/bin/sh
# Completion-log gate regression. Follow the shared build convention;
# the Python cases themselves require neither a game process nor game data.
set -eu
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if ! SDL_AUDIODRIVER=dummy "$script_dir/test_dune_scummvm_sdl.sh" build >/dev/null 2>&1; then
	echo "FAIL speedrun-evidence: build failed"
	exit 1
fi
if python3 "$script_dir/../tests/regression/test_speedrun_evidence.py"; then
	echo "PASS speedrun-evidence: assisted, bypassed, forced, failed and incomplete runs are distinguished"
else
	echo "FAIL speedrun-evidence: completion classification regression"
	exit 1
fi
