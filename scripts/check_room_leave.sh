#!/bin/sh

# The room-leave scan (CD ui_click_move_room 3faa-3fc2, run_room_leave_
# dialogue_scan 36d3, npc_auto_dialogue 3520; floppy likewise): a move sets
# ds:0c to the room Paul heads for and pending_room_action 1; the first
# person in the room with a topic-4 line that holds says it, and a line
# whose event drops the interrupt gate (event 2) stops the move.
# New game, throne room, Leto not met: the compass's down arrow (room 4)
# brings "Where are you going so fast? I have to talk to you!" and Paul
# stays; the right arrow (no line) moves as before. Floppy and CD, real
# harness runs with dune_room_scans (capture runs skip the room scans).
# Silent and headless, about a minute.
#
# Usage: scripts/check_room_leave.sh

set -u

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
local_root="${DUNE_LOCAL_BUILD_ROOT:-/tmp/dune-scummvm-native-build}"
base="${DUNE_RUN_ROOT:-$local_root/sdl-run}"
status=0

SDL_AUDIODRIVER=dummy DUNE_DATA="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}" "$script_dir/test_dune_scummvm_sdl.sh" build >/dev/null 2>&1 || {
	echo "FAIL room leave: build failed; run scripts/test_dune_scummvm_sdl.sh dump to see why"
	exit 1
}

for release in floppy cd; do
	data="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}"
	[ "$release" = cd ] && data="${DUNE_DATA_CD:-$repo_root/data}"
	dir="$base/room-leave-$release"
	rm -rf "$dir"
	mkdir -p "$dir"
	cat > "$dir/input.script" <<'EOF'
wait 3000               # the throne room
click left 274 186      # compass down: to room 4, Leto not met yet
wait 2000               # his line
checkpoint rl-leto
click left 20 20        # a view click away from Leto (the talk closes)
wait 1000
click left 289 177      # compass right: to room 5 (nobody's line holds)
wait 2000
checkpoint rl-right
quit
EOF
	printf 'dune_floppy_start=99\ndune_no_music=1\ndune_room_scans=1\n' > "$dir/extra.ini"
	DUNE_RUN_ROOT="$dir/run" DUNE_HARNESS_SECONDS=60 SDL_AUDIODRIVER=dummy DUNE_DATA="$data" \
		"$script_dir/test_dune_scummvm_sdl.sh" harness "$dir/input.script" "$dir/out" "$dir/extra.ini" >"$dir/harness.txt" 2>&1
	python3 - "$dir/run/saves/dune-ios.log" "$release" <<'PYEOF' || status=1
import sys
path, release = sys.argv[1], sys.argv[2]
try:
    log = open(path, errors="replace").read()
except OSError:
    print(f"FAIL room leave {release}: no log"); sys.exit(1)
problems = []
if "Room: character 0 speaks as Paul leaves for room 4" not in log:
    problems.append("Leto's line did not come")
if "Where are you going so fast?" not in log:
    problems.append("not Leto's 'Where are you going so fast?'")
if "Room: the move is interrupted" not in log:
    problems.append("the move was not stopped")
after = log.split("Script line 7:", 1)[-1] if "Script line 7:" in log else ""
if "speaks as Paul leaves" in after:
    problems.append("a line on the right-hand move")
if "Room 5 of place 0" not in after:
    problems.append("the right-hand move did not reach room 5")
print(("FAIL" if problems else "PASS") + f" room leave {release}: " + ("; ".join(problems) if problems else
      "Leto's line stops the move to room 4; the move to room 5 goes on"))
sys.exit(1 if problems else 0)
PYEOF
done
exit $status
