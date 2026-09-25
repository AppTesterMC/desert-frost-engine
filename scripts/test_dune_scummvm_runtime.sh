#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
build_root="$repo_root/third_party/scummvm/build-native-dune"
artifact_root="$repo_root/notes/temp/dune_scummvm_engine_20260916"
log_path="$artifact_root/logs/runtime-audio-smoke.log"
result_path="$artifact_root/results/runtime-audio-smoke.txt"

mkdir -p "$artifact_root/logs" "$artifact_root/results"
cd "$build_root"

set +e
perl -e 'alarm 30; exec @ARGV' -- ./scummvm --debuglevel=3 --path="$repo_root" dune > "$log_path" 2>&1
status=$?
set -e

if grep -Fq 'Dune: rendered INTDS.HSQ frame 0' "$log_path" && \
	grep -Fq 'Dune: decoded and started SN9.VOC' "$log_path"; then
	{
		echo 'Dune runtime graphics/audio smoke test passed.'
		echo '- DUNE.DAT was detected and INTDS.HSQ frame 0 rendered.'
		echo '- SN9.VOC was decoded by ScummVM Audio::makeVOCStream and submitted to the mixer.'
		echo "- Probe termination status: $status (the thirty-second alarm is expected)."
	} > "$result_path"
	exit 0
fi

echo 'Dune runtime graphics/audio smoke test failed; see runtime-audio-smoke.log.' >&2
exit 1
