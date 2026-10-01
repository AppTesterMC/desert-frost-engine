#!/bin/sh

# The Harkonnen raids, the small-troop merge and the skill decay (queue item
# 10), floppy and CD (the same code):
#   - raid (CD 1f64 / 2017, floppy 227c / 232f): on an even day's slot 4
#     with the roll's bit, the northernmost sietch with Fremen in reach of
#     a fortress is attacked by two of its Harkonnen troops; the sietch is
#     in battle, its troops fight, and a loss makes it a fortress;
#   - raid-paul: with Paul in the sietch, he is moved to room 1 and the
#     night battle starts (ds:2b);
#   - troop-rules: a troop under 200 men merges into the smallest other
#     troop at its place (6d19), which says "A small troop has merged with
#     us."; every 64 periods a miner loses a point of army and ecology
#     skill (6d7b);
#   - characters: on a landing (CD 2170) Gurney, left in the desert,
#     walks to the nearest place and Duncan, waiting in room 1 of a sietch,
#     goes into room 2; a new day (1d66) puts a record past its place's
#     room count into room 1;
#   - captain: OVERPOWER THE PRISONER (CD 9584) takes 0x29 from the
#     captain's motivation once a speaker; at 0x30 he says "I can't
#     overpower him alone", at 0x20 he is overpowered (ds:10a7 bit 4) and
#     tells of the fortress he knows (cond 395, 436-438).
# Silent and headless, about a minute.
#
# Usage: scripts/check_raids.sh

set -u

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
local_root="${DUNE_LOCAL_BUILD_ROOT:-/tmp/dune-scummvm-native-build}"
base="${DUNE_RUN_ROOT:-$local_root/sdl-run}"
status=0

SDL_AUDIODRIVER=dummy DUNE_DATA="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}" "$script_dir/test_dune_scummvm_sdl.sh" build >/dev/null 2>&1 || {
	echo "FAIL raids: build failed; run scripts/test_dune_scummvm_sdl.sh dump to see why"
	exit 1
}

for release in floppy cd; do
	data="${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}"
	[ "$release" = cd ] && data="${DUNE_DATA_CD:-$repo_root/data}"
	for setup in raid raid-paul troop-rules captain characters; do
		run_root="$base/raids-$setup-$release"
		rm -rf "$run_root"
		mkdir -p "$run_root/saves" "$run_root/frames"
		config="$run_root/raids.ini"
		printf '[scummvm]\nsavepath=%s\ndune_input=%s\ndune_checkpoint_dir=%s\ndune_floppy_start=99\ndune_no_music=1\ndune_story_setup=%s\n' \
			"$run_root/saves" "$repo_root/tests/regression/hemispheres.script" "$run_root/frames" "$setup" > "$config"
		(cd "$local_root/build-sdl-dune" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy perl -e 'alarm 120; exec @ARGV' -- \
			./scummvm -c "$config" --gfx-mode=surface --no-fullscreen --path="$data" dune >/dev/null 2>&1)
		python3 - "$run_root/saves/dune-ios.log" "$release" "$setup" <<'EOF' || status=1
import re, sys
path, release, setup = sys.argv[1:4]
try:
    log = open(path, errors="replace").read()
except OSError:
    print(f"FAIL raids {setup} {release}: no log"); sys.exit(1)
problems = []
if setup in ("raid", "raid-paul"):
    if len(re.findall(r"Raid: Harkonnen troop \d+ from place \d+ attacks sietch", log)) != 2:
        problems.append("not two raiders")
    m = re.search(r"after slot 4, sietch \d+ status (0x[0-9a-f]+) type (0x[0-9a-f]+), ds:c4 (\d+), ds:2b (\d+), room (\d+), battle (\d)", log)
    if not m:
        problems.append("no raid at slot 4")
    else:
        status_, type_, c4, b2b, room, battle = (int(x, 0) for x in m.groups())
        if not status_ & 2 or c4 != 1:
            problems.append(f"status {status_:#x}, ds:c4 {c4}")
        if setup == "raid-paul" and not (b2b == 1 and room == 1 and battle == 1):
            problems.append(f"Paul: ds:2b {b2b}, room {room}, night battle {battle}")
        fd = re.search(r"ds:fd (0x[0-9a-f]+|0)", log)
        if setup == "raid-paul" and not (fd and int(fd.group(1), 0) & 1):
            problems.append("the battle gauge ds:fd is not seeded (6144)")
        if setup == "raid" and b2b:
            problems.append("ds:2b set without Paul")
    if setup == "raid":
        m = re.search(r"after the battle, sietch \d+ status (0x[0-9a-f]+) type (0x[0-9a-f]+)", log)
        if not m or int(m.group(1), 0) & 2 or int(m.group(2), 0) < 0x28:
            problems.append("the lost sietch is not a fortress")
    if "Vision: queued 0xf0c" not in log and "Vision: queued 0xf0d" not in log:
        problems.append("no \"The Harkonnens are attacking\" message")
elif setup == "characters":
    m = re.search(r"after landing at 12, Gurney (\w\w) (\w\w) (\w\w) (\w\w) flags (0x[0-9a-f]+|0), Duncan room (\d+)", log)
    if not m:
        problems.append("the setup did not run")
    else:
        if m.group(3) != "80" or int(m.group(4), 16) != 13 or not int(m.group(5), 0) & 4 or m.group(1) != "01":
            problems.append(f"Gurney did not walk to place 12's room 1 (record {m.group(1)} {m.group(2)} {m.group(3)} {m.group(4)})")
        if m.group(6) != "2":
            problems.append(f"Duncan in room {m.group(6)}, want 2")
    if not re.search(r"a new day, Jessica room 1\b", log):
        problems.append("Jessica's room past the table was not reset")
elif setup == "captain":
    m = re.findall(r"OVERPOWER clicked 1, ds:ed (0x[0-9a-f]+), overpowered (\d), line \"([^\"]*)\"", log)
    if len(m) != 2:
        problems.append("OVERPOWER THE PRISONER not offered twice")
    else:
        if m[0][0] != "0x7" or m[0][1] != "0" or "overpower him alone" not in m[0][2]:
            problems.append(f"first threat: {m[0]}")
        if m[1][0] != "0xf7" or m[1][1] != "1" or "handcuff him" not in m[1][2]:
            problems.append(f"second threat: {m[1]}")
    if "There are 2 troops there." not in log or "heavily defended" not in log:
        problems.append("no fort lines after the threat")
else:
    if not re.search(r"troop \d+ \(150 men\) merges into troop \d+", log):
        problems.append("no merge")
    if not re.search(r"after a period, troop \d+ occupation 0xa0 men 0, troop \d+ men 1150 equipment 0xc0", log):
        problems.append("men or equipment not merged")
    if "A small troop has merged with us." not in log:
        problems.append("no merge line")
    if not re.search(r"skills spice \d+ army 19 ecology 19", log):
        problems.append("no skill decay")
print(("FAIL" if problems else "PASS") + f" raids {setup} {release}: " + ("; ".join(problems) if problems else {
    "raid": "two raiders, the sietch in battle, its loss makes it a fortress, the message",
    "raid-paul": "Paul to room 1, the night battle, the gauge ds:fd seeded",
    "troop-rules": "the 150 men merge, the line, army and ecology skills - 1 after 64 periods",
    "characters": "Gurney walks from the desert to place 12, Duncan goes into room 2, Jessica's bad room reset",
    "captain": "one threat is not enough at 0x30, at 0x20 he is overpowered and tells of the fort"}[setup]))
sys.exit(1 if problems else 0)
EOF
	done
done
exit $status
