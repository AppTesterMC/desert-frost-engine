#!/usr/bin/env python3
"""Replace approved golden references with the last regression run's checkpoints.

Usage: python3 scripts/dune_accept_golden.py cd/cd-after-touch [floppy/<name> ...]

The harness itself never overwrites a golden (see Makefile); this is the
explicit step after the visual diff was reviewed and approved. Metadata is
rewritten the same way `dune_regress.py add-missing` writes it.
"""
import json
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dune_regress import phash, read_json  # noqa: E402

root = Path(__file__).resolve().parent.parent
results = root / "notes/temp/dune_scummvm_engine_20260916/results/regression"

for spec in sys.argv[1:]:
    scenario, name = spec.split("/", 1)
    actual = results / scenario / f"{name}.png"
    if not actual.exists():
        sys.exit(f"no checkpoint {actual}; run make verify first")
    target = root / "golden" / scenario
    target.mkdir(parents=True, exist_ok=True)
    shutil.copy2(actual, target / f"{name}.png")
    metadata = read_json(actual.with_suffix(".json"))
    metadata["phash"] = phash(actual)
    (target / f"{name}.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(f"golden replaced: {spec}")
