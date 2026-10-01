#!/bin/sh
# Native Amiga tracker PCM; does not touch an emulator or system speakers.
set -eu
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
"$script_dir/test_dune_scummvm_sdl.sh" build >/dev/null 2>&1 || {
    echo "FAIL Amiga music: build failed"
    exit 1
}
exec python3 "$script_dir/check_amiga_music.py" "$repo_root"
