#!/usr/bin/env python3
"""Verify the nine floppy irrigation captures against the original DOS map and screenshots."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

from PIL import Image

from dune_save_patch import unpack
import globe_ref


def sha(data):
    return hashlib.sha256(data).hexdigest()


def word(data, offset):
    return struct.unpack_from("<H", data, offset)[0]


def check(stage, day, checkpoint, golden_dir):
    capture = stage / "original"
    ds = (capture / "stage.ds.bin").read_bytes()
    memory = (capture / "stage.mem.bin").read_bytes()
    meta = json.loads((capture / "stage.json").read_text())
    ds_start = int(meta["game_ds"], 16) * 16
    saved = (stage / "saves/DUNE21S1.SAV").read_bytes()
    vars_start = unpack(saved)[-4718:]
    place = slice(0x100 + 12 * 28, 0x100 + 13 * 28)
    record = ds[place]

    # The DOS far pointer points at the middle of the live map.
    offset, segment = struct.unpack_from("<HH", ds, globe_ref.MAP_PTR)
    start = segment * 16 + offset - globe_ref.MAP_CENTRE
    native_map = memory[start:start + globe_ref.MAP_SIZE]
    engine_map = (stage / "globe/globe-000.map").read_bytes()[:globe_ref.MAP_SIZE]
    if (len(native_map), len(engine_map)) != (globe_ref.MAP_SIZE,) * 2:
        raise ValueError(f"Incomplete map for irrigation day {day}")

    name = checkpoint["checkpoint"]
    original = (golden_dir / f"{name}.ds.bin").read_bytes()
    palette = (golden_dir / f"{name}.pal").read_bytes()
    png = golden_dir / checkpoint["source"]
    results = bool(original[globe_ref.RESULTS_FLAG])
    phase = word(original, globe_ref.T + 4)
    predicted = globe_ref.draw(original, engine_map, results, phase)
    with Image.open(png) as im:
        pixels = im.convert("RGB").load()
        pixel_misses = sum(not globe_ref.same(pixels[a % 320, a // 320], globe_ref.rgb(palette, index))
                           for a, index in predicted.items())
    map_misses = sum(x != y for x, y in zip(native_map, engine_map))
    clock = word(ds, 2)
    checks = {
        "original_memory_segment": memory[ds_start:ds_start + len(ds)] == ds and len(ds) == 65536,
        "capture_timestamp": meta["emulated_ms"] == checkpoint["emulated_ms"],
        "native_game_clock": clock == word(original, 2) == word(vars_start, 2) == word(saved, 0),
        "irrigated_location_record": record == original[place] == vars_start[place] and bool(record[10] & 1),
        "original_screenshot_hash": sha(png.read_bytes()) == checkpoint["sha256"],
        "all_native_map_bytes": map_misses == 0,
        "archived_results_globe_pixels": results and len(predicted) == 11116 and pixel_misses == 0,
    }
    return {
        "irrigation_days": day, "game_day": 1 + (clock >> 4), "period": clock & 15,
        "place": 12, "radius": record[11], "latitude": struct.unpack_from("b", record, 14)[0],
        "water": record[27], "job_progress": record[15], "phase": phase,
        "sprouting_cells": sum((value & 0x30) == 0x10 for value in native_map),
        "map_cells": len(native_map), "different_cells": map_misses, "map_sha256": sha(native_map),
        "source_save_sha256": sha(saved), "golden_sha256": checkpoint["sha256"],
        "globe_pixels": len(predicted), "different_pixels": pixel_misses, "checks": checks,
    }


def check_progression(native, replay, checkpoint):
    """Compare a manually advanced DOS state with a separate engine replay at the same clock."""
    meta = json.loads((native / f"{checkpoint}.json").read_text())
    ds = (native / f"{checkpoint}.ds.bin").read_bytes()
    memory = (native / f"{checkpoint}.mem.bin").read_bytes()
    segment = int(meta["game_ds"], 16) * 16
    base_offset, base_segment = struct.unpack_from("<HH", ds, globe_ref.MAP_PTR)
    base = base_segment * 16 + base_offset - globe_ref.MAP_CENTRE
    dos_map = memory[base:base + globe_ref.MAP_SIZE]
    actual = unpack((replay / "saves/DUNE21S1.SAV").read_bytes())[-4718:]
    last = max((replay / "globe").glob("globe-*.map"), key=lambda path: int(path.stem.split("-")[1]))
    engine_map = last.read_bytes()[:globe_ref.MAP_SIZE]
    if len(dos_map) != globe_ref.MAP_SIZE or len(engine_map) != globe_ref.MAP_SIZE:
        raise ValueError("Incomplete progression map")
    place = slice(0x100 + 12 * 28, 0x100 + 13 * 28)
    different = sum(x != y for x, y in zip(engine_map, dos_map))
    clock = word(ds, 2)
    return {
        "checkpoint": checkpoint, "floppy_clock": clock, "game_day": 1 + (clock >> 4),
        "radius": ds[place][11], "water": ds[place][27], "cells": len(dos_map),
        "different_cells": different, "native_map_sha256": sha(dos_map), "engine_map_sha256": sha(engine_map),
        "checks": {
            "original_memory_segment": len(ds) == 65536 and ds == memory[segment:segment + 65536],
            "same_game_clock": clock == word(actual, 2),
            "same_last_processed_period": word(ds, 0x1172) == word(actual, 0x1172) == clock,
            "irrigated_location_record": actual[place] == ds[place],
            "all_native_map_bytes": different == 0,
        },
    }


def replay_day51(binary, data, seed, output, fixture):
    reference = json.loads((fixture / "reference.json").read_text())
    if sha(seed.read_bytes()) != reference["starting_save_sha256"]:
        raise ValueError("Expected the day-48 irrigation save listed in reference.json")
    output.mkdir(parents=True, exist_ok=False)
    for dirname in ("saves", "frames", "globe"):
        (output / dirname).mkdir()
    save = output / "saves/DUNE21S1.SAV"
    shutil.copy2(seed, save)
    config = output / "game.ini"
    input_file = fixture / "engine-day51.script"
    config.write_text("[scummvm]\ndune_no_music=1\ndune_rng_seed=1\ndune_floppy_start=99\n"
                      "dune_intro_start=99\ndune_room_scans=1\ndune_scene_scripts=1\n"
                      f"savepath={output / 'saves'}\ndune_checkpoint_dir={output / 'frames'}\n"
                      f"dune_globe_dump={output / 'globe'}\ndune_input={input_file}\n")
    with (output / "stdout.log").open("w") as log:
        subprocess.run([str(binary), "-c", str(config), "--gfx-mode=surfacesdl", "--no-fullscreen",
                        "--path=" + str(data), "dune"], stdout=log, stderr=subprocess.STDOUT, check=True,
                       env=dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
                                SDL_RENDER_DRIVER="software"), timeout=180)
    ds = unpack(save.read_bytes())[-4718:]
    expected = next(row for row in reference["independent_progression"] if row["day"] == 51)
    dumped = max((output / "globe").glob("globe-*.map"), key=lambda p: int(p.stem.split("-")[1]))
    return {
        "binary_sha256": sha(binary.read_bytes()),
        "checks": {
            "clock": word(ds, 2) == expected["clock"],
            "last_processed_period": word(ds, 0x1172) == expected["clock"],
            "irrigated_location_record": ds[592:620].hex() == expected["irrigated_place12"],
            "native_map_sha256": sha(dumped.read_bytes()[:globe_ref.MAP_SIZE]) == expected["map_sha256"],
        },
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--stages", type=Path, help="paired day-000 through day-120 captures")
    parser.add_argument("--goldens", type=Path, help="override the manifest's capture_directory")
    parser.add_argument("--progression", nargs=3, metavar=("DOS_CAPTURE_DIR", "ENGINE_REPLAY_DIR", "CHECKPOINT"),
                        help="check a naturally advanced DOS checkpoint against the engine's end save and map")
    parser.add_argument("--replay-day51", nargs=4, type=Path, metavar=("ENGINE", "FLOPPY_DATA", "START_SAVE", "OUT"),
                        help="run the player-input growth regression against the retained original fingerprints")
    args = parser.parse_args()
    if args.replay_day51:
        paths = [p.resolve() for p in args.replay_day51]
        fixture = Path(__file__).resolve().parents[1] / "tests/regression/floppy-irrigation"
        report = replay_day51(*paths, fixture)
        print(json.dumps(report, indent=2))
        return 0 if all(report["checks"].values()) else 1
    if args.progression:
        native, replay, checkpoint = args.progression
        report = check_progression(Path(native), Path(replay), checkpoint)
        print(json.dumps(report, indent=2))
        return 0 if all(report["checks"].values()) else 1
    if not args.manifest or not args.stages:
        parser.error("provide --manifest and --stages for the nine-stage check")
    manifest = json.loads(args.manifest.read_text())
    goldens = args.goldens or Path(manifest["capture_directory"])
    entries = {e["irrigation_days"]: e for e in manifest["entries"] if "irrigation_days" in e}
    reports = [check(args.stages / f"day-{day:03d}", day, entries[day], goldens) for day in range(0, 121, 15)]
    print(json.dumps(reports, indent=2))
    return 0 if all(all(s["checks"].values()) for s in reports) else 1


if __name__ == "__main__":
    sys.exit(main())
