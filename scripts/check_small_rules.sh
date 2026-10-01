#!/bin/sh

# The small troop rules (queue item 9), floppy and CD (the same code):
#   - the new-day routine (CD 6e20, floppy 7b88): a working troop more than
#     8 days past its byte 0x14 loses 1 motivation a day (6f93: below 5 it is
#     4, stopped and sulking); the ring round its sietch (byte 0x0b) grows
#     by one a day (6cfc);
#   - closing a map contact writes the day into byte 0x14 (CD 7b89, floppy
#     88a7), so the next day brings no decay, and clears the news bits:
#     word 0x10 keeps 0x3f0, word 0x12 loses 0x1a00 (7b7c-7b81);
#   - the troop report's duration phrase (CD 32c7, floppy 356b): below 3
#     periods "for a very short time", below 16 "for a few hours", below 32
#     "for 1 day", then "for 12 days" with the days written in, stopped
#     "but our job is finished";
#   - an accepted SPECIALIZE IN ARMY leaves the harvester (CD 6abf, floppy
#     7859).
# dune_story_setup=small-rules drives all of it; the log is checked.
# Silent and headless, under a minute.
#
# Usage: scripts/check_small_rules.sh

set -u

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
local_root="${DUNE_LOCAL_BUILD_ROOT:-/tmp/dune-scummvm-native-build}"
base="${DUNE_RUN_ROOT:-$local_root/sdl-run}"
status=0

SDL_AUDIODRIVER=dummy DUNE_DATA="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}" "$script_dir/test_dune_scummvm_sdl.sh" build >/dev/null 2>&1 || {
	echo "FAIL small rules: build failed; run scripts/test_dune_scummvm_sdl.sh dump to see why"
	exit 1
}

for release in floppy cd; do
	data="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}"
	[ "$release" = cd ] && data="${DUNE_DATA_CD:-$repo_root/data}"
	run_root="$base/small-rules-$release"
	rm -rf "$run_root"
	mkdir -p "$run_root/saves" "$run_root/frames"
	config="$run_root/small-rules.ini"
	printf '[scummvm]\nsavepath=%s\ndune_input=%s\ndune_checkpoint_dir=%s\ndune_floppy_start=99\ndune_no_music=1\ndune_story_setup=small-rules\n' \
		"$run_root/saves" "$repo_root/tests/regression/hemispheres.script" "$run_root/frames" > "$config"
	(cd "$local_root/build-sdl-dune" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy perl -e 'alarm 120; exec @ARGV' -- \
		./scummvm -c "$config" --gfx-mode=surface --no-fullscreen --path="$data" dune >/dev/null 2>&1)
	python3 - "$run_root/saves/dune-ios.log" "$release" <<'EOF' || status=1
import re, sys
path, release = sys.argv[1], sys.argv[2]
try:
    log = open(path, errors="replace").read()
except OSError:
    print(f"FAIL small rules {release}: no log"); sys.exit(1)
def state(when):
    m = re.search(r"Story setup: " + re.escape(when) + r", troop \d+ motivation (\d+) occupation (0x[0-9a-f]+|0) speech (0x[0-9a-f]+|0) ring (\d+)", log)
    return tuple(int(x, 0) for x in m.groups()) if m else None
problems = []
start, day1, low, contacted = state("start"), state("after a day"), state("at 5, after a day"), state("contacted, after a day")
if not (start and day1 and low and contacted):
    problems.append("the setup did not run")
else:
    if day1[0] != start[0] - 1:
        problems.append(f"decay {start[0]} -> {day1[0]}, want -1")
    if day1[3] != start[3] + 1:
        problems.append(f"ring {start[3]} -> {day1[3]}, want +1")
    if not (low[0] == 4 and low[1] & 0x10 and low[2] & 0x20):
        problems.append(f"at 5: motivation {low[0]} occupation {low[1]:#x} speech {low[2]:#x}, want 4, stopped, sulking")
    if contacted[0] != 50:
        problems.append(f"after the contact the motivation is {contacted[0]}, want 50 (no decay)")
m = re.search(r"contact closed, byte 0x14 (\d+), day (\d+), word 0x10 (0x[0-9a-f]+|0), word 0x12 (0x[0-9a-f]+|0)", log)
if not m or m.group(1) != m.group(2) or int(m.group(3), 0) & ~0x3f0 or int(m.group(4), 0) & 0x1a00:
    problems.append("the contact's close did not write the day or clear the news bits (7b7c)")
want = {"2 periods": "for a very short time", "15 periods": "for a few hours", "31 periods": "for 1 day",
        "53 periods": "for  3 days", "stopped": "but our job is finished"}
for k, v in want.items():
    if f'Story setup: duration {k}: "{v}"' not in log:
        problems.append(f"duration {k} is not \"{v}\"")
if "leaves its harvester" not in log or not re.search(r"after SPECIALIZE IN ARMY, troop \d+ occupation 0x4 equipment 0,", log):
    problems.append("SPECIALIZE IN ARMY kept the harvester")
print(("FAIL" if problems else "PASS") + f" small rules {release}: " + ("; ".join(problems) if problems else
      "daily decay after 8 days, sulk below 5, the ring grows, a contact resets the clock, the duration phrases, the harvester left behind"))
sys.exit(1 if problems else 0)
EOF
done
exit $status
