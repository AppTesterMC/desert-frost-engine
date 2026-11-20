#!/usr/bin/env python3
"""Exercise the floppy prologue through real keyboard and mouse events on SDL."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

PAGES = ["stars", "globe-1", "desert", "paul-1", "baron", "globe-2", "paul-2", "night", "palace"]


def play(binary, data, root, name, script, settings=None, timeout=30):
    folder = root / name
    folder.mkdir()
    saves = folder / "saves"
    saves.mkdir()
    inputs = folder / "input.script"
    inputs.write_text(script)
    config = {"savepath": saves, "dune_no_music": 1, "dune_input": inputs, **(settings or {})}
    ini = folder / "game.ini"
    ini.write_text("[scummvm]\n" + "".join(f"{key}={value}\n" for key, value in config.items()))
    with (folder / "stdout.log").open("w") as output:
        subprocess.run([str(binary), "-c", str(ini), "--gfx-mode=surfacesdl", "--no-fullscreen",
                        "--path=" + str(data), "dune"], stdout=output, stderr=subprocess.STDOUT,
                       env=dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
                                SDL_RENDER_DRIVER="software"), check=True, timeout=timeout)
    return (saves / "dune-ios.log").read_text()


def pages(log):
    return [line.removeprefix("Prologue page: prologue-") for line in log.splitlines()
            if line.startswith("Prologue page: ")]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--data", required=True, type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--hold-ms", type=int, default=8500)
    parser.add_argument("--timed-watch", action="store_true", help="also run the full minute-long watched prologue")
    args = parser.parse_args()
    if args.hold_ms < 8000:
        parser.error("hold must exceed the previous seven-second page timer")
    root = Path(tempfile.mkdtemp(prefix="floppy-prologue-", dir=args.output)).resolve()
    print("Evidence at", root, flush=True)
    binary, data = args.binary.resolve(), args.data.resolve()
    script = f"wait 1000\nkey SPACE\nwait {args.hold_ms}\ncheckpoint held-first\n"
    for i in range(len(PAGES)):
        advance = "click left 160 80" if i % 2 == 0 else "key SPACE"
        script += f"{advance}\nwait 450\ncheckpoint advanced-{i + 1}\n"
    log = play(binary, data, root, "manual", script + "wait 500\nquit\n", timeout=args.hold_ms / 1000 + 25)
    assert pages(log.partition("checkpoint held-first")[0]) == PAGES[:1], "first page advanced before input"
    for count in range(1, len(PAGES) + 1):
        before, marker, _ = log.partition(f"checkpoint advanced-{count}")
        assert marker and pages(before) == PAGES[:count + 1], f"wrong page after input {count}"
    assert "Startup complete: throne room displayed" in log
    for name, script, expected in [
        ("escape-cinematic", "wait 1000\nkey ESC\nwait 1000\nquit\n", []),
        ("escape-prologue", "wait 1000\nclick left 160 80\nwait 750\nkey ESC\nwait 1000\nquit\n", PAGES[:1]),
    ]:
        log = play(binary, data, root, name, script)
        assert pages(log) == expected and "Startup complete: throne room displayed" in log, name
    frames = root / "frames"
    frames.mkdir()
    log = play(binary, data, root, "capture", "wait 3500\nquit\n",
               {"dune_checkpoint_dir": frames, "dune_floppy_start": 33})
    assert "Startup complete: throne room displayed" in log
    assert all((frames / f"prologue-{page}.png").is_file() for page in PAGES)
    print("PASS manual hold, nine individual advances, both Escape paths and unattended capture", flush=True)
    if args.timed_watch:
        script = "wait 1000\nkey SPACE\nwait 63500\ncheckpoint watch-finished\nquit\n"
        log = play(binary, data, root, "timed-watch", script,
                   {"dune_speedrun": "campaign", "dune_speedrun_watch": 1, "dune_floppy_start": 33}, timeout=85)
        before, marker, _ = log.partition("checkpoint watch-finished")
        assert marker and "Startup complete: throne room displayed" in before and not pages(before)
        print("PASS watched campaign automatically completes the timed prologue")


if __name__ == "__main__":
    main()
