#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
artifact_root="$repo_root/notes/temp/dune_scummvm_engine_20260916"
ipa_path="$artifact_root/packages/ScummVM-ios-dune-prototype.ipa"
staging_root="$artifact_root/device-staging"
app_path="$staging_root/Payload/ScummVM.app"
device_id=${1:-}
result_json="$artifact_root/results/device-install-devicectl.json"
log_path="$artifact_root/logs/device-install-devicectl.log"

if [ -z "$device_id" ]; then
	echo "Usage: $0 <device-udid-or-name>" >&2
	echo "Currently visible devices:" >&2
	xcrun devicectl list devices >&2 || true
	exit 2
fi

[ -f "$ipa_path" ] || {
	echo "Missing IPA: $ipa_path" >&2
	exit 1
}

rm -rf "$staging_root"
mkdir -p "$staging_root"
unzip -q "$ipa_path" -d "$staging_root"
[ -x "$app_path/scummvm" ] || {
	echo "IPA did not contain a runnable ScummVM.app: $app_path" >&2
	exit 1
}

mkdir -p "$artifact_root/logs" "$artifact_root/results"
xcrun devicectl device install app \
	--device "$device_id" \
	--timeout 120 \
	--json-output "$result_json" \
	--log-output "$log_path" \
	"$app_path"

echo "Installed ScummVM Dune IPA on device: $device_id"
