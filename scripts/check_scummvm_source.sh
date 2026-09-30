#!/bin/sh
# Host-only staging regression; no engine build or network required.
set -eu
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if python3 "$script_dir/../tests/regression/test_scummvm_source.py"; then
	echo "PASS: source staging preserves the Dune engine"
else
	echo "FAIL: source staging regression" >&2
	exit 1
fi
