#!/bin/sh
# All three versions must be installed for this cross-version save gate.
set -eu
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
"$script_dir/test_dune_scummvm_sdl.sh" build >/dev/null 2>&1 || {
    echo "FAIL save compatibility: build failed"
    exit 1
}
exec python3 "$script_dir/check_save_compatibility.py" "$repo_root"
