#!/usr/bin/env python3
"""Replay the original floppy chapter 6 and verify Duncan's shipment and reply."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

from dune_save_patch import unpack


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def shipment_state(ds):
    if len(ds) < 0x1018:
        raise ValueError("Incomplete floppy state")
    return {
        "room": ds[4], "phase": ds[0x2a],
        "demand_kg": int.from_bytes(ds[0xbc:0xbe], "little") * 10,
        "fulfilment": ds[0xbe], "shipment_flags": ds[0xbf],
        "agreed_kg": int.from_bytes(ds[0xc0:0xc2], "little") * 10,
        "demand_count": ds[0xc3], "messages": ds[0xc8], "unread": ds[0xc9],
        "duncan_room": ds[0x1008], "duncan_follows": bool(ds[0x1017] & 0x40),
    }


def verify(chapters, output):
    chapter = chapters / "ch06"
    original_input = chapters / "ch05/saves/DUNE21S1.SAV"
    start = shipment_state(unpack(original_input.read_bytes())[-4718:])
    shipped = shipment_state((chapter / "c6-11-ship.ds.bin").read_bytes())
    posted = shipment_state((chapter / "c6-13-d2.ds.bin").read_bytes())
    seen = shipment_state((chapter / "c6-15-reply.ds.bin").read_bytes())
    final = shipment_state(unpack((chapter / "saves/DUNE21S1.SAV").read_bytes())[-4718:])
    actual = shipment_state(unpack((output / "saves/DUNE21S1.SAV").read_bytes())[-4718:])
    expected = f'Shipments: shipped {shipped["agreed_kg"]} kg'
    log = (output / "saves/dune-ios.log").read_text()
    provenance = json.loads((output / "provenance.json").read_text())
    checks = {
        "original agreement and reply": 0 < shipped["agreed_kg"] < start["demand_kg"]
        and posted["unread"] == seen["unread"] + 1
        and posted["agreed_kg"] == 0 and seen["duncan_room"] == 8,
        "same native input save": provenance["input_save_sha256"] == sha256(original_input),
        "same player inputs": sha256(output / "input.script") == sha256(chapters / "ch06.full.script"),
        "original demand read": f'shipment of {start["demand_kg"]}kgs' in log,
        "original amount shipped once": expected in log and log.count("Shipments: shipped ") == 1,
        "Duncan posts the reply": "AH! The answer has just come" in log,
        "Emperor's reply opened": "COMM: message from person 11" in
        log.partition("checkpoint c6-14-list")[2].partition("checkpoint c6-15-reply")[0],
        "matching saved shipment state": actual == final,
        "chapter saved and finished": "checkpoint ch06-99-saved" in log and
        log.rstrip().endswith(": quit"),
    }
    report = {"checks": checks, "engine_final": actual, "original_final": final,
              "input_save_sha256": sha256(original_input),
              "script_sha256": sha256(chapters / "ch06.full.script"),
              "engine_log_sha256": sha256(output / "saves/dune-ios.log")}
    print(json.dumps(report, indent=2))
    return 0 if all(checks.values()) else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chapters", type=Path, required=True,
                        help="original floppy day-route-v2 chapter capture directory")
    parser.add_argument("--output", type=Path, required=True,
                        help="new run directory, or existing run with --verify-only")
    parser.add_argument("--verify-only", action="store_true")
    parser.add_argument("--binary", type=Path, help="built ScummVM with the Dune engine")
    parser.add_argument("--data", type=Path, default=Path(__file__).resolve().parents[1] / "data/floppy")
    args = parser.parse_args()
    output, chapters = args.output.resolve(), args.chapters.resolve()
    if not args.verify_only:
        if not args.binary:
            parser.error("--binary is required for a new run")
        binary = args.binary.resolve(strict=True)
        source = (chapters / "ch05/saves/DUNE21S1.SAV").resolve(strict=True)
        script = (chapters / "ch06.full.script").resolve(strict=True)
        output.mkdir(parents=True, exist_ok=False)
        for subdir in ("saves", "frames"):
            (output / subdir).mkdir()
        shutil.copy2(source, output / "saves/DUNE21S1.SAV")
        shutil.copy2(script, output / "input.script")
        config = output / "game.ini"
        config.write_text("[scummvm]\ndune_real_time=true\ndune_room_scans=1\n"
                          "dune_scene_scripts=1\ndune_rng_seed=1\ndune_floppy_start=99\n"
                          f"dune_intro_start=99\ndune_no_music=1\nsavepath={output / 'saves'}\n"
                          f"dune_checkpoint_dir={output / 'frames'}\ndune_input={output / 'input.script'}\n")
        (output / "provenance.json").write_text(json.dumps(
            {"binary": str(binary), "binary_sha256": sha256(binary), "input_save_sha256": sha256(source),
             "input_script_sha256": sha256(script)}, indent=2) + "\n")
        with (output / "stdout.log").open("w") as log:
            subprocess.run([str(binary), "-c", str(config), "--gfx-mode=surface", "--no-fullscreen",
                            "--path=" + str(args.data.resolve()), "dune"],
                           env=dict(os.environ, SDL_AUDIODRIVER="dummy", SDL_VIDEODRIVER="dummy"),
                           stdout=log, stderr=subprocess.STDOUT, timeout=360, check=True)
    return verify(chapters, output)


if __name__ == "__main__":
    sys.exit(main())
