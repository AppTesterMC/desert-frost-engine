#!/usr/bin/env python3
"""Exercise real save menus, cross-release isolation and legacy import safely."""
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(ROOT / "scripts"))
from dune_save_patch import unpack, pack
BUILD = Path(os.environ.get("DUNE_LOCAL_BUILD_ROOT", "/tmp/dune-scummvm-native-build"))
BINARY = BUILD / "build-sdl-dune/scummvm"
RUN = Path(os.environ.get("DUNE_RUN_ROOT", str(BUILD / "sdl-run")))
RUN.mkdir(parents=True, exist_ok=True)
OUT = Path(tempfile.mkdtemp(prefix="save-compatibility-", dir=RUN))
DATA = {
    "floppy": Path(os.environ.get("DUNE_DATA_FLOPPY", ROOT / "data/floppy")),
    "cd": Path(os.environ.get("DUNE_DATA_CD", ROOT / "data")),
    "amiga": Path(os.environ.get("DUNE_DATA_AMIGA", ROOT / "data/amiga")),
}
NAMES = {"floppy": "DUNE21S1.SAV", "cd": "DUNE37S1.SAV", "amiga": "DUNEAMS1.SAV"}
ENV = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
ROUTE = "wait 800\nclick left 160 163\nwait 300\nclick left 45 170\nwait 300\n"
COUNT = 0


def play(name, release, saves, action, data=None):
    target = OUT / name
    target.mkdir()
    frames = target / "frames"
    frames.mkdir()
    script = ROUTE + ("click left 160 179\n" if action == "save" else "click left 160 187\n")
    script += "wait 200\ncheckpoint menu\nclick left 160 163\nwait 500\ncheckpoint after\nquit\n"
    (target / "input.script").write_text(script)
    ini = target / "game.ini"
    ini.write_text("[scummvm]\nsavepath=" + str(saves) + "\ndune_input=" + str(target / "input.script") +
        "\ndune_checkpoint_dir=" + str(frames) +
        "\ndune_floppy_start=99\ndune_intro_start=6\ndune_no_music=true\n")
    with (target / "stdout.log").open("w") as stdout:
        subprocess.run([str(BINARY), "-c", str(ini), "--gfx-mode=surface", "--no-fullscreen",
            "--path=" + str(data or DATA[release]), "dune"], env=ENV, stdout=stdout,
            stderr=subprocess.STDOUT, check=True, timeout=45)
    log = (saves / "dune-ios.log").read_text()
    (target / "engine.log").write_text(log)
    assert (frames / "after.png").is_file(), name
    return log


def digest_files(saves):
    return {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in saves.glob("*.SAV")}


def passed(message):
    global COUNT
    COUNT += 1
    print("PASS save compatibility: " + message, flush=True)


def load_case(name, release, payload, accepted, filename=None):
    saves = OUT / (name + "-saves")
    saves.mkdir()
    (saves / (filename or NAMES[release])).write_bytes(payload)
    before = digest_files(saves)
    log = play(name, release, saves, "load")
    loaded = "Saves: slot 0 loaded" in log
    assert loaded == accepted, (name, "loaded", loaded, "expected", accepted)
    if not accepted:
        assert "Saves: slot 0 is incompatible or damaged" in log, name
    assert digest_files(saves) == before, (name, "loading modified a save")
    passed(name + (": loaded without rewriting source" if accepted else ": rejected without rewriting source"))
    return saves


def changed_body(source, body):
    return bytes(pack(body, source[:6]))


def main():
    if not (DATA["amiga"] / "dune").is_file():
        raise ValueError("set DUNE_DATA_AMIGA to installed Amiga data for the cross-version gate")
    shared = OUT / "shared"
    shared.mkdir()
    canonical = {}
    for release in ("cd", "amiga", "floppy"):
        before = digest_files(shared)
        log = play("save-" + release, release, shared, "save")
        assert "Saves: " + NAMES[release] + " written" in log
        canonical[release] = (shared / NAMES[release]).read_bytes()
        assert all(digest_files(shared)[n] == h for n, h in before.items()), release
        passed(release + " writes a separate slot without replacing another version")
    assert canonical["amiga"][2:4] == bytes((0xf7, 0xa1))
    for release in canonical:
        log = play("roundtrip-" + release, release, shared, "load")
        assert "Saves: slot 0 loaded, time 2, place 0 room 10" in log, release
        passed(release + " own slot restores game time, place and room")

    legacy_amiga = bytearray(canonical["amiga"])
    legacy_amiga[3] = 0
    legacy = load_case("amiga-legacy-import", "amiga", legacy_amiga, True, "DUNE37S1.SAV")
    before = (legacy / "DUNE37S1.SAV").read_bytes()
    play("amiga-legacy-save", "amiga", legacy, "save")
    assert (legacy / "DUNEAMS1.SAV").is_file()
    assert (legacy / "DUNE37S1.SAV").read_bytes() == before
    passed("saving after legacy import creates the new namespace and preserves the old file")

    load_case("cd-save-in-amiga", "amiga", canonical["cd"], False)
    load_case("cd-save-in-amiga-legacy-slot", "amiga", canonical["cd"], False, "DUNE37S1.SAV")
    load_case("tagged-amiga-save-in-cd", "cd", canonical["amiga"], False)
    load_case("legacy-amiga-save-in-cd", "cd", legacy_amiga, False)
    load_case("floppy-save-in-cd", "cd", canonical["floppy"], False)
    load_case("cd-save-in-floppy", "floppy", canonical["cd"], False)
    # A matching total length must not disguise another release's dialogue.
    cd_body = unpack(canonical["cd"])
    amiga_body = unpack(canonical["amiga"])
    assert len(cd_body) == 22138 and len(amiga_body) == 22122
    load_case("cd-padded-amiga-body", "cd", changed_body(legacy_amiga, amiga_body + bytes(16)), False)
    load_case("amiga-truncated-cd-body", "amiga", changed_body(canonical["cd"], cd_body[:-16]), False)

    # Original captured saves stay private and are optional reference fixtures.
    original_cd = canonical["cd"]
    for release, relative in (("cd", "cd-dream/DUNE37S1.SAV"),
                              ("floppy", "day1-prospectors/DUNE21S1.SAV")):
        fixture = ROOT / "tests/fidelity/saves" / relative
        if fixture.is_file():
            payload = fixture.read_bytes()
            load_case("original-dos-" + release, release, payload, True)
            if release == "cd":
                original_cd = payload
                load_case("native-cd-in-amiga", "amiga", payload, False)
        else:
            print("SKIP optional original " + release + " save fixture", flush=True)
    # The native Amiga file has BE headers and a different body layout.
    native = DATA["amiga"] / "dune10s0.sav"
    if native.is_file():
        load_case("native-amiga-in-engine", "amiga", native.read_bytes(), False)
        load_case("native-amiga-in-cd", "cd", native.read_bytes(), False)
    else:
        print("SKIP optional native Amiga save fixture", flush=True)

    # Game-directory imports remain supported without moving user fixtures.
    for release, payload in (("cd", original_cd), ("amiga", legacy_amiga)):
        name = release + "-game-directory-import"
        data, saves = OUT / (name + "-data"), OUT / (name + "-saves")
        data.mkdir()
        saves.mkdir()
        for item in DATA[release].iterdir():
            if item.is_file() and item.suffix.lower() != ".sav":
                (data / item.name).symlink_to(item)
        fixture = data / "DUNE37S1.SAV"
        fixture.write_bytes(payload)
        log = play(name, release, saves, "load", data)
        assert "Saves: slot 0 loaded" in log
        assert fixture.read_bytes() == payload and not list(saves.glob("*.SAV"))
        passed(name + ": explicit matching fixture loaded without writing either directory")

    # Historical CD had no 104-byte dialogue gap. Historical floppy put
    # its 36-byte gap BEFORE a raw-offset dialogue header instead of after.
    load_case("legacy-cd-no-gap", "cd", changed_body(canonical["cd"], cd_body[:-4705-104] + cd_body[-4705:]), True)
    floppy = unpack(canonical["floppy"])
    start, length = 0x317f + 0xa2, 4464
    dialogue = bytearray(floppy[start:start+length])
    header = (struct.unpack_from("<H", dialogue)[0] - 0xcfe9) & 0xffff
    for offset in range(0, header, 2):
        value = (struct.unpack_from("<H", dialogue, offset)[0] - 0xcfe9) & 0xffff
        struct.pack_into("<H", dialogue, offset, value)
    old_floppy = floppy[:start] + bytes(36) + dialogue + floppy[-4718:]
    load_case("legacy-floppy-gap-before-dialogue", "floppy", changed_body(canonical["floppy"], old_floppy), True)

    bad = bytearray(canonical["cd"])
    bad[4] ^= 1
    load_case("damaged-length", "cd", bad, False)
    bad = bytearray(canonical["cd"] + bytes((0xf7,)))
    struct.pack_into("<H", bad, 4, len(bad) - 2)
    load_case("incomplete-rle", "cd", bad, False)
    bad_body = bytearray(cd_body)
    bad_body[start] ^= 1
    load_case("damaged-dialogue-header", "cd", changed_body(canonical["cd"], bad_body), False)
    bad = bytearray(canonical["cd"][:6]) + bytes((0xf7, 255, 0)) * 100
    struct.pack_into("<H", bad, 4, len(bad) - 2)
    load_case("oversized-rle-expansion", "cd", bad, False)
    for release, size in (("cd", 4705), ("amiga", 4705), ("floppy", 4718)):
        raw = bytearray(canonical[release])
        body = unpack(raw)
        struct.pack_into("<H", raw, 0, 0x0178)
        struct.pack_into("<H", body, len(body) - size + 2, 0x0178)
        load_case("zlib-like-time-" + release, release, changed_body(raw, body), True)

    # A corrupt current Amiga slot must not silently fall back to legacy.
    priority = OUT / "priority-saves"
    priority.mkdir()
    (priority / "DUNEAMS1.SAV").write_bytes(canonical["cd"])
    (priority / "DUNE37S1.SAV").write_bytes(legacy_amiga)
    before = digest_files(priority)
    log = play("amiga-current-slot-priority", "amiga", priority, "load")
    assert "Saves: slot 0 loaded" not in log
    assert "Saves: slot 0 is incompatible or damaged" in log
    assert digest_files(priority) == before
    passed("invalid current Amiga slot does not fall back to another file")
    print(f"PASS save compatibility: {COUNT} checks; evidence {OUT}", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, OSError, ValueError, subprocess.SubprocessError) as exc:
        print(f"FAIL save compatibility: {exc}; evidence {OUT}", file=sys.stderr)
        sys.exit(1)
