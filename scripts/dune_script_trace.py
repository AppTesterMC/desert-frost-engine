#!/usr/bin/env python3
"""Run one harness script on the engine and show what each line did.

The engine writes "Script line N: <step>" to its log (dune-ios.log) as it
plays each step; the lines after it (taps with the row or place they hit,
dialogue entries, travel, troops) are what that step caused. This prints the
log grouped by script line, which is the quickest way to debug a script:

    scripts/dune_script_trace.py tests/fidelity/cockpit.script --saves tests/fidelity/saves/day2-comm --prelude 15000

Options mirror the regression and fidelity runners: --saves copies the
original's DUNE21S*.SAV files in first, --prelude adds a wait before line 1
(the fidelity scenarios' prelude_ms; line numbers still refer to the file),
--data picks the release, --config adds scummvm.ini keys (e.g.
dune_story_setup=comm). Silent and headless.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LOCAL = Path(os.environ.get("DUNE_LOCAL_BUILD_ROOT", "/private/tmp/dune-scummvm-native-build"))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("script")
    ap.add_argument("--saves", help="folder with DUNE21S*.SAV to start from")
    ap.add_argument("--prelude", type=int, default=0, help="milliseconds to wait before line 1")
    ap.add_argument("--data", choices=("floppy", "cd"), default="floppy")
    ap.add_argument("--config", action="append", default=[], help="extra scummvm.ini key=value")
    ap.add_argument("--full", action="store_true", help="also print engine lines before the first step")
    args = ap.parse_args()

    script = Path(args.script).resolve()
    saves = LOCAL / "sdl-run/saves"
    saves.mkdir(parents=True, exist_ok=True)
    for old in saves.glob("DUNE21S*.SAV"):
        old.unlink()
    if args.saves:
        for sav in Path(args.saves).glob("DUNE21S*.SAV"):
            shutil.copy(sav, saves / sav.name)
    log = saves / "dune-ios.log"
    if log.exists():
        log.unlink()

    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        engine_script = tmp / "input.script"
        text = script.read_text()
        # The prelude goes on the same first line so the numbers still match the file.
        engine_script.write_text(text if not args.prelude else f"wait {args.prelude}\n" + text)
        extra = tmp / "extra.ini"
        extra.write_text("".join(c + "\n" for c in args.config))
        data = ROOT / ("data/floppy" if args.data == "floppy" else "data")
        env = dict(os.environ, SDL_AUDIODRIVER="dummy", SDL_VIDEODRIVER="dummy", DUNE_DATA=str(data))
        subprocess.run([str(ROOT / "scripts/test_dune_scummvm_sdl.sh"), "harness", str(engine_script), str(tmp / "out"),
                        str(extra)], env=env, check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not log.exists():
        print("no engine log: did the engine build and start?", file=sys.stderr)
        return 1
    shift = 1 if args.prelude else 0
    lines = script.read_text().splitlines()
    started = args.full
    for entry in log.read_text(errors="replace").splitlines():
        m = re.match(r"Script line (\d+): (.*)", entry)
        if m:
            started = True
            n = int(m.group(1)) - shift
            if 1 <= n <= len(lines):
                print(f"\n{n:4d}  {lines[n - 1].strip()}")
            else:
                print(f"\n   -  {m.group(2)}  (prelude)")
            continue
        if started:
            print(f"        {entry}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
