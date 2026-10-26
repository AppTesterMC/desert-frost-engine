#!/usr/bin/env python3
"""Classify completion-bot logs without certifying untested player controls.

Legacy logs may omit their coverage declaration. No current bot exercises
all player controls: even a fallback-free run remains engine-assisted evidence.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re



REQUIRED_PLAYER_UI_CATEGORIES = {
    "navigation", "dialogue", "orders", "combat", "map", "save-load",
    "options", "equipment", "espionage", "worm",
}

def _player_ui_categories(declarations):
    cats = set()
    for line in declarations:
        if "player-ui-v1" not in line:
            continue
        idx = line.find("player-ui-v1")
        tail = line[idx + len("player-ui-v1"):]
        if ":" in tail:
            tail = tail.split(":", 1)[1]
        for part in tail.replace(";", ",").split(","):
            name = part.strip().lower()
            if name:
                cats.add(name)
    return cats

def assess(text, part="full", process_exit=0, allow_order_bypasses=False,
           require_player_ui=False, require_uninterrupted=False):
    lines = text.splitlines()
    speedrun = [line for line in lines if line.startswith("Speedrun:")]
    fallback = [line for line in speedrun
                if "set directly" in line or "ORDER BYPASS" in line]
    forced = [line for line in speedrun if "FORCED" in line]
    setup = [line for line in forced if part == "campaign" and (
        ": FORCED campaign start:" in line or
        ": FORCED day 3: troops fetched the sietches' free weapons (time passes)" in line)]
    progression = [line for line in forced if line not in setup]
    blocked = [line for line in speedrun if "BLOCKED" in line]
    endings = [line for line in speedrun if "step 56 OK" in line]
    declarations = [line for line in speedrun if "coverage " in line]
    player_ui_declarations = [line for line in declarations if "player-ui-v1" in line]
    player_ui_categories = _player_ui_categories(player_ui_declarations)
    player_ui_declared = bool(player_ui_declarations)
    player_ui_complete = player_ui_declared and REQUIRED_PLAYER_UI_CATEGORIES.issubset(player_ui_categories)
    reload_lines = []
    slot0_reload_lines = []
    for line in lines:
        lowered = line.lower()
        # A battle save/reload retry is the slot-1 reload used by speedrunFight.
        # Slot-0 reloads are troop-approach retries and are disclosed separately;
        # uninterrupted mode disables the slot-1 retry path.
        if re.search(r"saves:\s*slot\s*1\s*loaded", lowered):
            reload_lines.append(line)
        if re.search(r"saves:\s*slot\s*0\s*loaded", lowered):
            slot0_reload_lines.append(line)
    reload_count = len(reload_lines)
    slot0_reload_count = len(slot0_reload_lines)
    interrupted = reload_count > 0
    failures = []
    if process_exit:
        failures.append(f"engine exited with status {process_exit}")
    if not lines:
        failures.append("fresh engine log missing or empty")
    if not endings:
        failures.append("ending milestone missing")
    if not lines or not re.fullmatch(r"Speedrun: .*: end", lines[-1]):
        failures.append("completion bot did not finish cleanly")
    if blocked:
        failures.append(f"{len(blocked)} BLOCKED marker(s)")
    if progression:
        failures.append(f"{len(progression)} FORCED progression shortcut(s) in {part} run")
    if fallback and not allow_order_bypasses:
        failures.append(f"{len(fallback)} direct order fallback(s) bypassed a missing or refused command")
    if require_player_ui and not player_ui_complete:
        failures.append("player-UI completion is not certified by this bot")
    if require_uninterrupted:
        if reload_count:
            failures.append(f"{reload_count} battle save/reload retry/retries")
        if not player_ui_complete:
            failures.append("player-UI completion is not certified by this bot")
    if interrupted:
        classification = "interrupted"
    elif player_ui_complete and not failures:
        classification = "uninterrupted"
    elif declarations and any("engine-assisted" in d for d in declarations):
        classification = "assisted"
    else:
        classification = "unknown"
    uninterrupted = classification == "uninterrupted"
    return {
        "schema": 1,
        "part": part,
        "process_exit": process_exit,
        "passed": not failures,
        "coverage": "player-ui-v1" if player_ui_declared else ("engine-assisted" if declarations else "unknown"),
        "player_ui_completion_verified": bool(player_ui_complete and not failures),
        "player_ui_declared": player_ui_declared,
        "player_ui_categories": sorted(player_ui_categories),
        "required_player_ui_categories": sorted(REQUIRED_PLAYER_UI_CATEGORIES),
        "player_ui_missing_categories": sorted(REQUIRED_PLAYER_UI_CATEGORIES - player_ui_categories),
        "reload_count": reload_count,
        "reloads": reload_count,
        "reload_retries": reload_count,
        "battle_reloads": reload_count,
        "battle_reload_count": reload_count,
        "save_reloads": reload_count,
        "save_reload_count": reload_count,
        "save_reload_retries": reload_count,
        "retries": reload_count,
        "reload_lines": reload_lines,
        "reload_markers": reload_lines,
        "slot0_reload_count": slot0_reload_count,
        "slot0_reload_lines": slot0_reload_lines,
        "slot0_reloads": slot0_reload_lines,
        "troop_approach_retries": slot0_reload_count,
        "interrupted": interrupted,
        "uninterrupted": uninterrupted,
        "classification": classification,
        "run_classification": classification,
        "interruption_status": classification,
        "status": classification,
        "coverage_declaration": declarations,
        "legacy_coverage_unknown": not declarations,
        "direct_order_fallbacks": len(fallback),
        "direct_order_fallback_lines": fallback,
        "forced_shortcuts": len(forced),
        "forced_setup_shortcuts": len(setup),
        "forced_progression_shortcuts": len(progression),
        "forced_shortcut_lines": forced,
        "blocked": blocked,
        "ending_milestones": endings,
        "forts": sum("step 33 OK" in line for line in speedrun),
        "order_bypasses_explicitly_allowed": allow_order_bypasses,
        "failures": failures,
        "limitation": "The bot uses direct campaign actions and battle save/reload retries. "
                      "Logged order fallbacks are only the known bypasses; zero logged "
                      "fallbacks does not prove every order or interaction used player controls.",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--part", choices=["full", "campaign"], default="full")
    parser.add_argument("--process-exit", type=int, default=0)
    parser.add_argument("--release", default="unknown")
    parser.add_argument("--seed", default="1")
    parser.add_argument("--binary", type=Path, help="record the exact tested engine's SHA-256")
    parser.add_argument("--allow-order-bypasses", action="store_true",
                        help="engine-logic investigation only; keeps assisted label and bypass count")
    parser.add_argument("--require-player-ui", action="store_true",
                        help="fails until a complete player-input coverage contract exists")
    parser.add_argument("--require-uninterrupted", action="store_true",
                        help="fails unless the run had no save/reload retries and complete player-UI coverage")
    parser.add_argument("--uninterrupted", action="store_true",
                        help=argparse.SUPPRESS)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    raw = args.log.read_bytes() if args.log.is_file() else b""
    result = assess(raw.decode("utf-8", errors="replace"), args.part,
                    args.process_exit, args.allow_order_bypasses, args.require_player_ui,
                    args.require_uninterrupted or args.uninterrupted)
    result.update(release=args.release, seed=args.seed,
                  log_sha256=hashlib.sha256(raw).hexdigest() if raw else None)
    if args.binary:
        result["binary_sha256"] = hashlib.sha256(args.binary.read_bytes()).hexdigest()
    if args.json:
        args.json.write_text(json.dumps(result, indent=2) + "\n")
    if result["passed"]:
        if result["player_ui_completion_verified"]:
            detail = f"{result['forts']} forts, player-UI completion verified; " \
                     f"{result['direct_order_fallbacks']} direct order fallback(s)"
        else:
            detail = f"{result['forts']} forts, assisted engine completion; " \
                     f"{result['direct_order_fallbacks']} direct order fallback(s); player-UI completion unverified; {result['classification']}"
        if result["forced_setup_shortcuts"]:
            detail += f"; {result['forced_setup_shortcuts']} declared FORCED setup(s)"
        if args.allow_order_bypasses:
            detail += " (order bypasses explicitly allowed)"
    else:
        detail = "; ".join(result["failures"])
    print(f"{'PASS' if result['passed'] else 'FAIL'} {args.part} {args.release} seed {args.seed}: {detail}")
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
