#!/bin/sh
# Functional Amiga game options, input and the engine's own save/load path.
set -eu
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
"$script_dir/test_dune_scummvm_sdl.sh" build >/dev/null 2>&1 || {
    echo "FAIL Amiga options: build failed"
    exit 1
}
exec python3 "$script_dir/check_amiga_options.py" "$repo_root"
