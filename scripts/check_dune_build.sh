#!/bin/sh

# Pre-IPA check: builds the desktop (SDL) engine, screenshots every screen it
# can reach, and captures the audio it actually produces. Run this and look at
# the results BEFORE ./scripts/build_dune_scummvm_ios.sh; a device round-trip
# costs the user a manual sideload.
#
# Usage: ./scripts/check_dune_build.sh
# Output: notes/temp/dune_scummvm_engine_20260916/results/preflight/
#         *.png  - every screen, ready to view
#         *.wav  - captured audio, playable
#         summary.txt - the same table this prints

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
local_root="${DUNE_LOCAL_BUILD_ROOT:-${TMPDIR:-/tmp}/dune-scummvm-native-build}"
build_root="$local_root/build-sdl-dune"
run_root="$local_root/sdl-run"
evidence_root="$repo_root/notes/temp/dune_scummvm_engine_20260916"
out="$evidence_root/results/preflight"
dump_root="$evidence_root/results/sdl-dump"

cd_data="${DUNE_DATA_CD:-${TMPDIR:-/tmp}/dune-data}"
floppy_data="${DUNE_DATA_FLOPPY:-${TMPDIR:-/tmp}/dune-data/floppy}"

rm -rf "$out"
mkdir -p "$out" "$run_root/saves"
summary="$out/summary.txt"
: > "$summary"

note() {
	echo "$1" | tee -a "$summary"
}

# Local copies of the game data: the repository is on a slow external volume,
# and every run reads DUNE.DAT.
# The executable holds the initial game state (world.cpp), so it travels
# with the data on both releases.
if [ ! -f "$cd_data/DUNE.DAT" ] || [ ! -f "$cd_data/DNCDPRG.EXE" ]; then
	mkdir -p "$cd_data"
	cp "$repo_root/DUNE.DAT" "$repo_root/DNCDPRG.EXE" "$cd_data/"
fi
if [ ! -f "$floppy_data/DUNES.HSQ" ]; then
	mkdir -p "$floppy_data"
	cp "$repo_root"/../exoDOS/eXoDOS/dune/* "$floppy_data/" 2>/dev/null || true
fi

note "=== 1. Build ==="
# The first dump run below also builds; do it here so a compile error is
# reported as a build failure rather than as missing screenshots.
DUNE_DATA="$cd_data" "$script_dir/test_dune_scummvm_sdl.sh" dump >"$out/build.log" 2>&1 || {
	note "build: FAILED - see $out/build.log"
	grep -E " error:" "$out/build.log" | head -20 | tee -a "$summary"
	exit 1
}
note "build: OK"

run_dump() {
	label=$1
	data=$2
	rm -f "$dump_root"/*.bmp
	DUNE_DATA="$data" "$script_dir/test_dune_scummvm_sdl.sh" dump >"$out/dump-$label.log" 2>&1 || true
	count=0
	for bmp in "$dump_root"/*.bmp; do
		[ -f "$bmp" ] || continue
		name=$(basename -- "$bmp" .bmp)
		sips -s format png "$bmp" --out "$out/$label-$name.png" >/dev/null 2>&1
		count=$((count + 1))
	done
	note "$label screens: $count"
	grep -E "FAILED|ERROR|missing" "$run_root/saves/dune-ios.log" >>"$summary" 2>/dev/null || true
	cp "$run_root/saves/dune-ios.log" "$out/$label-dune.log" 2>/dev/null || true
}

note ""
note "=== 2. Screens (look at every PNG in $out) ==="
run_dump cd "$cd_data"
run_dump floppy "$floppy_data"

# Capture what the mixer really outputs. SDL's disk driver writes raw
# 16-bit stereo 44100 Hz; silence here means silence on the device too.
capture() {
	label=$1
	data=$2
	seconds=$3
	shift 3
	ini="$run_root/check-$label.ini"
	{
		printf '[scummvm]\nsavepath=%s\n' "$run_root/saves"
		for key in "$@"; do printf '%s\n' "$key"; done
	} > "$ini"
	raw="$out/$label.raw"
	rm -f "$raw"
	SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}" SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE="$raw" \
		perl -e "alarm $seconds; exec @ARGV" -- "$build_root/scummvm" -c "$ini" --gfx-mode=surface --no-fullscreen \
		--path="$data" dune >"$out/audio-$label.log" 2>&1 || true
	if [ ! -f "$raw" ]; then
		note "$label audio: FAILED - mixer produced no raw capture; see $out/audio-$label.log"
		return 1
	fi
	python3 "$script_dir/dune_audio_level.py" "$raw" "$out/$label.wav" "$label" | tee -a "$summary"
	rm -f "$raw"
}

note ""
note "=== 3. Audio (listen to the WAVs if a verdict looks wrong) ==="
# CD menu music (ARRAKIS), intro videos skipped.
capture cd-music "$cd_data" 25 'dune_intro_start=6'
# CD speech: IRULAN is a later video, not part of the five-entry opening
# loader table. The explicit probe keeps the opening order exact while making
# this audio check exercise the speech stream.
capture cd-speech "$cd_data" 35 'dune_intro_start=4' 'dune_no_music=1'
# Floppy music (WORMINTR over the intro).
capture floppy-music "$floppy_data" 25

note ""
note "=== Next ==="
note "1. View every PNG in $out - is each screen right?"
note "2. Both audio verdicts OK?"
note "3. Only then: ./scripts/build_dune_scummvm_ios.sh"
