#!/bin/sh

# Runs the whole desktop check set (make verify, every check_*.sh, the other
# releases and the speedrun bot) in parallel, silent and headless.
#
# It builds the engine once, then starts every check with DUNE_SKIP_BUILD=1
# (no make, no dump tour) and a private DUNE_RUN_ROOT, so checks that used to
# share sdl-run/saves no longer collide. Each check's output goes to
# $out/<name>.log; the PASS/FAIL/VERIFY lines are printed at the end.
#
# Usage: scripts/check_all.sh [-j JOBS] [--no-speedrun] [--no-full]
#   -j JOBS        checks at a time (default: CPU count - 1)
#   --no-speedrun  skip scripts/check_speedrun.sh (the longest checks)
#   --no-full      run the speedrun bot's campaign only, not the full game
# Not included: check_dune_build.sh (the IPA preflight with its screen tour)
# and the fidelity report (scripts/dune_fidelity.py).

set -u

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
local_root="${DUNE_LOCAL_BUILD_ROOT:-/private/tmp/dune-scummvm-native-build}"
jobs=$(( $(sysctl -n hw.ncpu) - 1 ))
speedrun=1
full=1
while [ $# -gt 0 ]; do
	case "$1" in
		-j) jobs=$2; shift ;;
		--no-speedrun) speedrun=0 ;;
		--no-full) full=0 ;;
		*) echo "usage: $0 [-j JOBS] [--no-speedrun] [--no-full]" >&2; exit 2 ;;
	esac
	shift
done

out="$local_root/check-all"
rm -rf "$out"
mkdir -p "$out"
start=$(date +%s)

echo "building the engine ..."
if ! SDL_AUDIODRIVER=dummy DUNE_DATA="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}" \
		"$script_dir/test_dune_scummvm_sdl.sh" build > "$out/build.log" 2>&1; then
	echo "FAIL build: see $out/build.log"
	exit 1
fi

# One line per job: name, then the command. The longest first, so they start early.
list="$out/jobs.txt"
: > "$list"
if [ "$speedrun" = 1 ]; then
	[ "$full" = 1 ] && echo "speedrun-full	$script_dir/check_speedrun.sh full" >> "$list"
	echo "speedrun-campaign	$script_dir/check_speedrun.sh campaign" >> "$list"
fi
echo "verify	python3 $script_dir/dune_regress.py verify" >> "$list"
echo "other-releases	env DUNE_DATA_AMIGA=${DUNE_DATA_AMIGA:-$HOME/dune-amiga-data/game} DUNE_DATA_SEGACD=${DUNE_DATA_SEGACD:-$HOME/dune-segacd-data} $script_dir/check_other_releases.sh" >> "$list"
for f in "$script_dir"/check_*.sh; do
	name=$(basename "$f" .sh)
	case "$name" in
		check_all|check_dune_build|check_speedrun|check_other_releases) continue ;;
	esac
	echo "${name#check_}	$f" >> "$list"
done

echo "running $(wc -l < "$list" | tr -d ' ') checks, $jobs at a time ..."
export out repo_root
# xargs -P runs the jobs in parallel (macOS sh has no wait -n).
tr '\t\n' '\000\000' < "$list" | xargs -0 -n 2 -P "$jobs" sh -c '
	name=$1; cmd=$2; t0=$(date +%s)
	(cd "$repo_root" && DUNE_SKIP_BUILD=1 DUNE_RUN_ROOT="$out/run-$name" SDL_AUDIODRIVER=dummy \
		sh -c "$cmd") > "$out/$name.log" 2>&1
	echo "$?" > "$out/$name.exit"
	echo "  done $name ($(( $(date +%s) - t0 )) s)"
' sh

echo
echo "=== results ($(( $(date +%s) - start )) s) ==="
status=0
while IFS='	' read -r name cmd; do
	code=$(cat "$out/$name.exit" 2>/dev/null || echo "?")
	lines=$(grep -E "^(PASS|FAIL)|VERIFY (PASSED|FAILED)" "$out/$name.log" 2>/dev/null)
	if [ -z "$lines" ]; then
		echo "?? $name: no PASS/FAIL line (exit $code, see $out/$name.log)"
		status=1
	else
		echo "$lines" | sed "s/^/[$name] /"
		echo "$lines" | grep -q -E "^FAIL|VERIFY FAILED" && status=1
	fi
done < "$list"
[ "$status" = 0 ] && echo "ALL PASSED" || echo "SOME CHECKS FAILED (logs in $out)"
exit $status
