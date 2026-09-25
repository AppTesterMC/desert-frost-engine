#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
repo_source_root="$repo_root/third_party/scummvm"
repo_build_root="$repo_source_root/build-native-dune"
local_root="${DUNE_LOCAL_BUILD_ROOT:-/private/tmp/dune-scummvm-native-build}"
. "$script_dir/scummvm_source.sh"
source_root="$local_root/scummvm"
build_root="$local_root/build-native-dune"
game_data="$repo_root"
source_marker="$repo_source_root/.dune-source-ready"

sync_build_back() {
	status=$?
	if [ "$status" -ne 130 ] && [ -x "$build_root/scummvm" ]; then
		mkdir -p "$repo_build_root"
		cp "$build_root/scummvm" "$repo_build_root/scummvm"
	fi
	exit "$status"
}

trap sync_build_back EXIT

if [ ! -f "$source_marker" ]; then
	mkdir -p "$repo_source_root"
	stage_scummvm_source "$repo_source_root"
	touch "$source_marker"
fi

mkdir -p "$source_root"
if [ ! -f "$source_root/.dune-local-source-ready" ]; then
	stage_scummvm_source "$source_root"
	touch "$source_root/.dune-local-source-ready"
fi
mkdir -p "$source_root/engines/dune"
rsync -a "$repo_source_root/engines/dune/" "$source_root/engines/dune/"
mkdir -p "$build_root"
cd "$build_root"

if [ ! -f Makefile ]; then
	"$source_root/configure" \
		--backend=null \
		--disable-all-engines \
		--enable-engine=dune
fi

make -j"$(sysctl -n hw.ncpu)"
./scummvm --list-engines | grep -E '^dune[[:space:]]' >/dev/null
./scummvm --path="$game_data" --detect | grep -F 'dune:dune' >/dev/null

echo "Native Dune build and detection test passed."
