#!/usr/bin/env python3
"""Run and compare the Cryo Dune desktop regression scenarios.

The engine owns scripted input and checkpoint capture. This tool owns the
portable comparison layer: pHash tolerance, audio windows, and the HTML
side-by-side report. It deliberately never overwrites an existing golden.
"""

from __future__ import annotations

import argparse
import array
import html
import json
import math
import os
import shutil
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageEnhance

RATE = 44100
MANIFEST = "tests/regression/manifest.json"
REPORT = "notes/temp/dune_scummvm_engine_20260916/results/regression-report"


def load_manifest(root: Path) -> dict:
    return json.loads((root / MANIFEST).read_text())


def dct_coeff(values: list[list[float]], u: int, v: int) -> float:
    total = 0.0
    n = len(values)
    for y in range(n):
        for x in range(n):
            total += values[y][x] * math.cos((2 * x + 1) * u * math.pi / (2 * n)) * math.cos(
                (2 * y + 1) * v * math.pi / (2 * n)
            )
    return total


def phash(path: Path) -> str:
    image = Image.open(path).convert("L").resize((32, 32), Image.Resampling.LANCZOS)
    values = [[float(image.getpixel((x, y))) for x in range(32)] for y in range(32)]
    coefficients = [dct_coeff(values, u, v) for v in range(8) for u in range(8)]
    median = sorted(coefficients[1:])[len(coefficients[1:]) // 2]
    bits = [1 if value > median else 0 for value in coefficients]
    result = 0
    for bit in bits:
        result = (result << 1) | bit
    return f"{result:016x}"


def hamming(left: str, right: str) -> int:
	# Keep the verifier runnable on the system Python shipped with older macOS
	# releases as well as current Python, where int.bit_count() exists.
	return bin(int(left, 16) ^ int(right, 16)).count("1")


def read_json(path: Path) -> dict:
    return json.loads(path.read_text()) if path.exists() else {}


def scenario_output(root: Path, scenario: dict) -> Path:
    return root / "notes/temp/dune_scummvm_engine_20260916/results/regression" / scenario["name"]


def run_scenario(root: Path, scenario: dict) -> tuple[Path, int, str]:
    output = scenario_output(root, scenario)
    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)
    extra = output / "extra.ini"
    extra.write_text("\n".join(scenario.get("config", [])) + ("\n" if scenario.get("config") else ""))
    data = os.environ.get(scenario["data_env"], scenario["data_default"])
    cmd = [
        str(root / "scripts/test_dune_scummvm_sdl.sh"),
        "harness",
        str(root / scenario["script"]),
        str(output),
        str(extra),
    ]
    env = os.environ.copy()
    env["DUNE_DATA"] = data
    env["SDL_VIDEODRIVER"] = "dummy"
    try:
        completed = subprocess.run(cmd, cwd=root, env=env, text=True, capture_output=True,
                                   timeout=scenario.get("timeout_seconds", 300))
        (output / "run.stdout.log").write_text(completed.stdout)
        (output / "run.stderr.log").write_text(completed.stderr)
        return output, completed.returncode, data
    except subprocess.TimeoutExpired as exc:
        stdout = exc.stdout or ""
        stderr = exc.stderr or ""
        if isinstance(stdout, bytes):
            stdout = stdout.decode(errors="replace")
        if isinstance(stderr, bytes):
            stderr = stderr.decode(errors="replace")
        (output / "run.stdout.log").write_text(stdout)
        (output / "run.stderr.log").write_text(stderr + "\nHarness runner timeout.\n")
        return output, 124, data


def audio_measure(raw: Path, start: float, duration: float) -> tuple[float, float]:
    if not raw.exists():
        return 0.0, 0.0
    data = raw.read_bytes()
    total_seconds = len(data) / float(RATE * 4)
    begin = max(0, int(start * RATE * 4))
    end = min(len(data), int((start + duration) * RATE * 4))
    samples = array.array("h")
    samples.frombytes(data[begin:end])
    if sys.byteorder != "little":
        samples.byteswap()
    sampled = samples[::7]
    if not sampled:
        return 0.0, total_seconds
    rms = math.sqrt(sum(sample * sample for sample in sampled) / len(sampled))
    return rms, total_seconds


def copy_diff(expected: Path, actual: Path, report: Path, stem: str) -> tuple[str, str, str]:
    expected_dir = report / "expected"
    actual_dir = report / "actual"
    diff_dir = report / "diff"
    expected_dir.mkdir(parents=True, exist_ok=True)
    actual_dir.mkdir(parents=True, exist_ok=True)
    diff_dir.mkdir(parents=True, exist_ok=True)
    expected_name = f"{stem}-expected.png"
    actual_name = f"{stem}-actual.png"
    diff_name = f"{stem}-diff.png"
    shutil.copy2(expected, expected_dir / expected_name)
    shutil.copy2(actual, actual_dir / actual_name)
    left = Image.open(expected).convert("RGB")
    right = Image.open(actual).convert("RGB")
    if left.size != right.size:
        right = right.resize(left.size, Image.Resampling.NEAREST)
    diff = ImageEnhance.Brightness(ImageChops.difference(left, right)).enhance(4.0)
    diff.save(diff_dir / diff_name)
    return expected_name, actual_name, diff_name


def compare(root: Path, manifest: dict, outputs: dict[str, Path], report: Path) -> tuple[bool, list[dict]]:
    tolerance = int(manifest.get("phash_tolerance", 6))
    goldens = root / "golden"
    report.mkdir(parents=True, exist_ok=True)
    rows = []
    all_passed = True
    for scenario in manifest["scenarios"]:
        output = outputs[scenario["name"]]
        for checkpoint in scenario["checkpoints"]:
            name = checkpoint["name"]
            actual = output / f"{name}.png"
            expected = goldens / scenario["name"] / f"{name}.png"
            metadata = read_json(output / f"{name}.json")
            expected_metadata = read_json(goldens / scenario["name"] / f"{name}.json")
            passed = actual.exists() and expected.exists()
            actual_hash = phash(actual) if actual.exists() else "missing"
            expected_hash = phash(expected) if expected.exists() else expected_metadata.get("phash", "missing")
            distance = hamming(actual_hash, expected_hash) if passed else 64
            passed = passed and distance <= tolerance
            if "cursor_visible" in expected_metadata:
                passed = passed and metadata.get("cursor_visible") == expected_metadata["cursor_visible"]
            images = (None, None, None)
            if actual.exists() and expected.exists():
                images = copy_diff(expected, actual, report, f"{scenario['name']}-{name.replace('/', '_')}")
            all_passed = all_passed and passed
            rows.append({
                "scenario": scenario["name"], "name": name, "category": checkpoint["category"],
                "passed": passed, "distance": distance, "tolerance": tolerance,
                "actual_hash": actual_hash, "expected_hash": expected_hash,
                "images": images, "cursor": metadata.get("cursor_visible", "n/a"),
            })
        for window in scenario.get("audio_windows", []):
            rms, duration = audio_measure(output / "audio.raw", window["start"], window["duration"])
            passed = duration >= window.get("min_duration", 0) and rms >= window.get("min_rms", 100)
            all_passed = all_passed and passed
            rows.append({
                "scenario": scenario["name"], "name": f"audio:{window['name']}", "category": "audio",
                "passed": passed, "distance": "—", "tolerance": "—",
                "actual_hash": f"RMS {rms:.1f}", "expected_hash": f">= {window.get('min_rms', 100)}",
                "images": (None, None, None), "cursor": f"{duration:.2f}s captured",
            })

    rows_html = []
    for row in rows:
        status = "PASS" if row["passed"] else "FAIL"
        images = row["images"]
        if images[0]:
            visuals = " ".join(
                f'<img src="{folder}/{html.escape(filename)}" width="320" alt="{folder}">' 
                for folder, filename in (("expected", images[0]), ("actual", images[1]), ("diff", images[2]))
            )
        else:
            visuals = ""
        rows_html.append(
            f"<tr class='{status.lower()}'><td>{status}</td><td>{html.escape(row['scenario'])}</td>"
            f"<td>{html.escape(row['name'])}</td><td>{html.escape(row['category'])}</td>"
            f"<td>{row['distance']} / {row['tolerance']}</td><td><code>{html.escape(row['expected_hash'])}</code>"
            f"<br><code>{html.escape(row['actual_hash'])}</code><br>cursor={html.escape(str(row['cursor']))}</td>"
            f"<td>{visuals}</td></tr>"
        )
    (report / "index.html").write_text("""<!doctype html>
<meta charset="utf-8"><title>Cryo Dune regression report</title>
<style>body{font:14px sans-serif}table{border-collapse:collapse}td,th{border:1px solid #bbb;padding:5px;vertical-align:top}.pass{background:#e8ffe8}.fail{background:#ffe8e8}img{border:1px solid #888;margin:2px}</style>
<h1>Cryo Dune regression report</h1>
<p>Expected / actual / amplified difference. A frame passes when pHash Hamming distance is within the configured tolerance and cursor metadata matches.</p>
<table><tr><th>Status</th><th>Scenario</th><th>Checkpoint</th><th>Category</th><th>Distance</th><th>Hashes / metadata</th><th>Visuals</th></tr>
""" + "\n".join(rows_html) + "</table>\n")
    return all_passed, rows


def init_golden(root: Path, manifest: dict, outputs: dict[str, Path]) -> int:
    goldens = root / "golden"
    existing = list(goldens.rglob("*.png")) if goldens.exists() else []
    if existing:
        print("Refusing to overwrite existing golden files. Review the HTML report and obtain approval before using --accept.", file=sys.stderr)
        return 2
    for scenario in manifest["scenarios"]:
        target = goldens / scenario["name"]
        target.mkdir(parents=True, exist_ok=True)
        output = outputs[scenario["name"]]
        for checkpoint in scenario["checkpoints"]:
            name = checkpoint["name"]
            actual = output / f"{name}.png"
            if not actual.exists():
                print(f"Missing checkpoint for golden initialization: {actual}", file=sys.stderr)
                return 1
            shutil.copy2(actual, target / actual.name)
            metadata = read_json(output / f"{name}.json")
            metadata["phash"] = phash(actual)
            (target / f"{name}.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(f"Initialized golden references under {goldens} from the just-run scenarios.")
    return 0


def add_missing_goldens(root: Path, manifest: dict, outputs: dict[str, Path]) -> int:
    """Create goldens only for checkpoints that have none yet.

    New scenes get references after they were inspected by eye; existing
    references are never touched here, so a changed scene still fails verify
    until its diff has been reviewed and approved.
    """
    goldens = root / "golden"
    added = 0
    for scenario in manifest["scenarios"]:
        target = goldens / scenario["name"]
        target.mkdir(parents=True, exist_ok=True)
        output = outputs[scenario["name"]]
        for checkpoint in scenario["checkpoints"]:
            name = checkpoint["name"]
            expected = target / f"{name}.png"
            actual = output / f"{name}.png"
            if expected.exists():
                continue
            if not actual.exists():
                print(f"Missing checkpoint, no golden created: {actual}", file=sys.stderr)
                continue
            shutil.copy2(actual, expected)
            metadata = read_json(output / f"{name}.json")
            metadata["phash"] = phash(actual)
            (target / f"{name}.json").write_text(json.dumps(metadata, indent=2) + "\n")
            added += 1
            print(f"golden added: {scenario['name']}/{name}")
    print(f"{added} golden reference(s) added; none overwritten.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("verify", "init-golden", "add-missing"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    manifest = load_manifest(root)
    outputs = {}
    for scenario in manifest["scenarios"]:
        output, status, _ = run_scenario(root, scenario)
        outputs[scenario["name"]] = output
        if status:
            print(f"{scenario['name']}: engine exited {status}", file=sys.stderr)
            return status or 1
        print(f"{scenario['name']}: run complete ({len(list(output.glob('*.png')))} PNG checkpoints)")
    report = root / REPORT
    if args.command == "add-missing":
        return add_missing_goldens(root, manifest, outputs)
    if args.command == "init-golden":
        # There are intentionally no expected files on the first run, so a
        # normal comparison would report every checkpoint as missing. Validate
        # that the engine produced the complete manifest, then seed once.
        return init_golden(root, manifest, outputs)
    passed, rows = compare(root, manifest, outputs, report)
    print(f"HTML report: {report / 'index.html'}")
    # The served page is the fidelity report (scripts/dune_fidelity.py); set
    # DUNE_REPORT_SERVE to a folder to publish this regression report too.
    serve = os.environ.get("DUNE_REPORT_SERVE", "")
    if serve and Path(serve).parent.is_dir():
        shutil.rmtree(serve, ignore_errors=True)
        shutil.copytree(report, serve)
        print(f"Served report: {serve}/index.html")
    failed = [row for row in rows if not row["passed"]]
    if failed:
        print(f"VERIFY FAILED: {len(failed)} mismatches; inspect {report / 'index.html'}", file=sys.stderr)
        return 1
    print("VERIFY PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
