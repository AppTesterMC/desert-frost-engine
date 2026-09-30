#!/usr/bin/env python3
"""Check working Amiga options; this never drives the original game's menus."""
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(sys.argv[1]).resolve()
DATA = Path(os.environ.get("DUNE_DATA_AMIGA", ROOT / "data/amiga"))
if not (DATA / "dune").is_file():
    print("SKIP Amiga options: set DUNE_DATA_AMIGA to installed Amiga data")
    sys.exit(0)
sys.path.insert(0, str(ROOT / "scripts"))
from check_original_options import play, OUT


def main():
    for enabled in (False, True):
        value = str(enabled).lower()
        target, log = play("amiga-leto-" + value, DATA,
            {"dune_story_setup": "letodead", "dune_fix_leto_loop": value},
            "wait 800\ncheckpoint throne\nclick left 160 171\nwait 400\n"
            "click left 160 179\nwait 400\ncheckpoint after\nquit\n")
        assert "Dune startup: version=Amiga" in log
        people = log.split("after Leto's death, people:", 1)[1].split("\n", 1)[0].split()
        assert ("0" not in people) == enabled, (value, "Leto room presence", people)
        assert ("Conversation: character 0," not in log) == enabled, (value, "Leto conversation")
        print(f"PASS Amiga options: Leto fix {value}, actual room and conversation", flush=True)

        target, log = play("amiga-celimyn-" + value, DATA,
            {"dune_story_setup": "celimyn", "dune_fix_celimyn_tuek": value},
            "wait 800\ncheckpoint discovery-test\nquit\n")
        assert f"findable at phase 0x57 no, at 0x58 {'yes' if enabled else 'no'}" in log
        assert f"and a load: discovery phase {'0x58' if enabled else '0xff'}" in log
        assert "Saves: DUNEAMS3.SAV written" in log and "Saves: slot 2 loaded" in log
        assert (target / "saves/DUNEAMS3.SAV").stat().st_size > 1000
        print(f"PASS Amiga options: Celimyn fix {value}, phase boundary and save/reload", flush=True)

    target, log = play("amiga-keyboard", DATA, {},
        "wait 800\ncheckpoint throne\nkey UP\nwait 500\ncheckpoint bedroom\n"
        "key DOWN\nwait 500\ncheckpoint throne-return\nquit\n")
    after_up = log.split("key UP", 1)[1].split("key DOWN", 1)[0]
    after_down = log.split("key DOWN", 1)[1]
    assert "Room 9 of place 0" in after_up, "Up did not reach Paul's bedroom"
    assert "Room 10 of place 0" in after_down, "Down did not return to the throne room"
    print("PASS Amiga options: directional keyboard navigation", flush=True)

    target, log = play("amiga-savepath", DATA, {},
        "wait 800\nclick left 160 163\nwait 400\ncheckpoint map\n"
        "click left 45 170\nwait 400\ncheckpoint globe\n"
        "click left 160 179\nwait 300\ncheckpoint save-menu\n"
        "click left 160 163\nwait 400\nclick left 160 187\nwait 300\n"
        "click left 160 163\nwait 400\ncheckpoint loaded\nquit\n")
    assert "Saves: DUNEAMS1.SAV written" in log
    assert "Saves: slot 0 loaded" in log
    assert (target / "saves/DUNEAMS1.SAV").stat().st_size > 1000
    for checkpoint in ("map", "globe", "save-menu", "loaded"):
        assert (target / f"frames/{checkpoint}.png").is_file()
    print("PASS Amiga options: engine menu save/load in alternate Save path", flush=True)
    print(f"PASS Amiga options: evidence {OUT}")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, OSError, ValueError, IndexError, subprocess.SubprocessError) as exc:
        print(f"FAIL Amiga options: {exc}; evidence {OUT}", file=sys.stderr)
        sys.exit(1)
