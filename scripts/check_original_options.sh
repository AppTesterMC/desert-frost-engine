#!/bin/sh
# Original CLI equivalents: installed text languages and actual mixer output.
# Silent/headless; each run uses a fresh private save/config directory.
set -eu
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
"$script_dir/test_dune_scummvm_sdl.sh" build >/dev/null 2>&1 || {
    echo "FAIL original options: build failed"
    exit 1
}
exec python3 "$script_dir/check_original_options.py" "$repo_root"
