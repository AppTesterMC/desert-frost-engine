#!/bin/sh

# The speedrun check (notes/speedrun/route.md): the engine's bot plays the
# PC speedrun through the game's own actions (dune_speedrun) on the floppy and
# the CD data and must reach the Emperor's throne room ("step 56 OK") with no
# BLOCKED line. It covers troop marches, espionage, fort battles, massive
# attacks, worm riding, the Harkonnen captain, the final-attack dialogue and
# the palace's fall.
#
# Usage: scripts/check_speedrun.sh [campaign|full] [seed...]
#   campaign  starts after Leto's death (the story shortcut is logged FORCED)
#   full      plays from a new game (the story bot; not passing yet)

set -u

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
part=${1:-campaign}
shift 2>/dev/null || true
seeds=${*:-1}
floppy="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}"
cd_data="${DUNE_DATA_CD:-$repo_root/data}"
log_file="${DUNE_RUN_ROOT:-${DUNE_LOCAL_BUILD_ROOT:-/private/tmp/dune-scummvm-native-build}/sdl-run}/saves/dune-ios.log"
status=0

for seed in $seeds; do
	for data in "$floppy" "$cd_data"; do
		DUNE_DATA="$data" "$script_dir/test_dune_scummvm_sdl.sh" speedrun "$part" "$seed" >/dev/null 2>&1
		name=$(basename -- "$data")
		copy="$repo_root/notes/temp/dune_scummvm_engine_20260916/logs/speedrun-$part-$name-seed$seed.log"
		cp "$log_file" "$copy" 2>/dev/null || true
		if grep -q "step 56 OK" "$copy" && ! grep -q "BLOCKED" "$copy"; then
			echo "PASS $part $name seed $seed: $(grep -c 'step 33 OK' "$copy") forts, $(grep 'step 56 OK' "$copy" | sed 's/Speedrun: //;s/,.*//')"
		else
			echo "FAIL $part $name seed $seed: $(grep 'BLOCKED' "$copy" | head -1 | sed 's/.*stage [0-9]: //')"
			status=1
		fi
		forced=$(grep -c "FORCED" "$copy")
		[ "$forced" -gt 0 ] && echo "     $forced FORCED shortcut(s): $(grep 'FORCED' "$copy" | sed 's/.*FORCED //' | cut -c1-60 | tr '\n' ';')"
	done
done
exit $status
