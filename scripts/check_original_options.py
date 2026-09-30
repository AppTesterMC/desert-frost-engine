#!/usr/bin/env python3
"""Exercise original-language resources and music/sound flags in the real engine."""
import array
import hashlib
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

ROOT = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(ROOT / "scripts"))
from dune_sprite_sheet import dat, unhsq
BUILD = Path(os.environ.get("DUNE_LOCAL_BUILD_ROOT", "/tmp/dune-scummvm-native-build"))
BINARY = BUILD / "build-sdl-dune/scummvm"
RUN = Path(os.environ.get("DUNE_RUN_ROOT", str(BUILD / "sdl-run")))
RUN.mkdir(parents=True, exist_ok=True)
OUT = Path(tempfile.mkdtemp(prefix="original-options-", dir=RUN))
ENV = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")


def play(name, data, options, script=None, seconds=None):
    target = OUT / name
    target.mkdir()
    (target / "saves").mkdir()
    (target / "frames").mkdir()
    config = {"savepath": target / "saves", **options}
    if script:
        (target / "input.script").write_text(script)
        config["dune_input"] = target / "input.script"
        config["dune_checkpoint_dir"] = target / "frames"
    ini = target / "game.ini"
    ini.write_text("[scummvm]\n" + "".join(f"{k}={v}\n" for k, v in config.items()))
    env = dict(ENV)
    if seconds:
        env.update(SDL_AUDIODRIVER="disk", SDL_DISKAUDIOFILE=str(target / "audio.raw"))
    with (target / "stdout.log").open("wb") as stdout:
        proc = subprocess.Popen([str(BINARY), "-c", str(ini), "--gfx-mode=surface", "--no-fullscreen",
                                 "--path=" + str(data), "dune"], env=env, stdout=stdout, stderr=subprocess.STDOUT)
        try:
            status = proc.wait(timeout=seconds or 45)
        except subprocess.TimeoutExpired:
            proc.terminate()
            proc.wait(timeout=10)
            if not seconds:
                raise AssertionError(f"{name}: engine timed out")
            status = 0
    if status:
        raise AssertionError(f"{name}: engine exited {status}, see {target / 'stdout.log'}")
    log = (target / "saves/dune-ios.log").read_text(encoding="latin-1")
    assert "ERROR:" not in log, (name, log[-1000:])
    return target, log


def sentences(blob):
    count = struct.unpack_from("<H", blob)[0] // 2
    return [blob[struct.unpack_from("<H", blob, 2*i)[0]:].split(b"\xff", 1)[0] for i in range(count)]


def capture_level(target):
    raw = (target / "audio.raw").read_bytes()
    assert len(raw) > 44100, f"short audio capture: {target}"
    samples = array.array("h")
    samples.frombytes(raw[:len(raw)//2*2])
    if sys.byteorder != "little":
        samples.byteswap()
    return max(map(abs, samples)), sum(x != 0 for x in samples)


def main():
    for release, data in (("floppy", Path(os.environ.get("DUNE_DATA_FLOPPY", ROOT / "data/floppy"))),
                          ("cd", Path(os.environ.get("DUNE_DATA_CD", ROOT / "data")))):
        files = dat(data / "DUNE.DAT") if release == "cd" else {p.name.upper(): p.read_bytes() for p in data.iterdir() if p.is_file()}
        pictures = {}
        map_command = next(i for i, value in enumerate(sentences(unhsq(files["COMMAND1.HSQ"])))
                           if value.strip() == b"SEE DUNE MAP")
        for language in range(1, 8):
            phrase_name = f"PHRASE{language}1.HSQ"
            if not all(n in files for n in (f"COMMAND{language}.HSQ", phrase_name, f"PHRASE{language}2.HSQ")):
                continue
            y = 179 if release == "cd" else 171
            target, log = play(f"{release}-language-{language}", data,
                {"dune_floppy_start": 99, "dune_intro_start": 6, "dune_no_music": "true", "dune_language": language},
                f"wait 1200\ncheckpoint room\nclick left 160 {y}\nwait 700\ncheckpoint leto\n"
                "key ESC\nwait 300\nclick left 160 163\nwait 500\ncheckpoint map\n"
                "click left 45 170\nwait 500\ncheckpoint globe\nclick left 160 179\nwait 300\ncheckpoint save\n"
                "click left 160 163\nwait 500\nclick left 160 187\nwait 300\n"
                "click left 160 163\nwait 500\ncheckpoint loaded\nquit\n")
            assert "Startup complete: throne room displayed" in log
            translated_map = sentences(unhsq(files[f"COMMAND{language}.HSQ"]))[map_command].decode("latin-1")
            assert translated_map in log, f"{release} language {language}: command row did not use original translated text"
            assert "Conversation: character 0," in log, f"{release}: Leto did not start"
            talk = log.split("Conversation: character 0,", 1)[1]
            match = re.search(r'Dialogue: entry \d+, condition \d+, sentence (\d+).*?: "(.*)"', talk)
            assert match, f"{release} language {language}: Leto did not speak"
            expected = sentences(unhsq(files[phrase_name]))[int(match.group(1))]
            # Compare a literal word from the ORIGINAL bank to the emitted dialogue,
            # independently of the engine's chosen-language diagnostic.
            words = re.findall(rb"[A-Za-z]{4,}", expected)
            assert words, (release, language, expected)
            assert words[0].decode() in match.group(2), (release, language, expected, match.group(2))
            save_name = "DUNE37S1.SAV" if release == "cd" else "DUNE21S1.SAV"
            assert f"Saves: {save_name} written" in log, f"{release} language {language}: save command failed"
            assert "Saves: slot 0 loaded" in log, f"{release} language {language}: load command failed"
            assert any(p.name.upper() == save_name for p in (target / "saves").iterdir())
            for shot in ("room", "leto", "map", "globe", "save", "loaded"):
                assert (target / f"frames/{shot}.png").is_file()
            pictures[language] = hashlib.sha256((target / "frames/room.png").read_bytes()).hexdigest()
            print(f"PASS original options {release}: language {language}, original dialogue, map commands and save/load", flush=True)
        for language in (2, 3):
            if language in pictures:
                assert pictures[language] != pictures[1], f"{release}: language {language} menu stayed English"
        for disabled in (False, True):
            target, log = play(f"{release}-music-{disabled}", data,
                {"dune_floppy_start": 99, "dune_intro_start": 6,
                 "dune_no_music": str(disabled).lower(), "dune_no_sound": "true"},
                script="wait 6500\nquit\n", seconds=9)
            peak, nonzero = capture_level(target)
            assert (nonzero == 0) if disabled else (peak > 100 and nonzero > 1000), (release, disabled, peak, nonzero)
            print(f"PASS original options {release}: music {'off is silent' if disabled else 'on is audible'}", flush=True)
        target, log = play(f"{release}-music-volume-zero", data,
            {"dune_floppy_start": 99, "dune_intro_start": 6, "dune_no_music": "false", "music_volume": 0},
            script="wait 6500\nquit\n", seconds=9)
        assert "Music: ARRAKIS.HSQ started" in log, f"{release}: volume test did not start its music stream"
        assert capture_level(target)[1] == 0, f"{release}: zero music volume was audible"
        print(f"PASS original options {release}: standard Music volume controls OPL output", flush=True)
    cd = Path(os.environ.get("DUNE_DATA_CD", ROOT / "data"))
    for disabled in (False, True):
        target, log = play(f"cd-sound-{disabled}", cd,
            {"dune_intro_start": 4, "dune_no_music": "true", "dune_no_sound": str(disabled).lower()}, seconds=8)
        peak, nonzero = capture_level(target)
        assert (nonzero == 0) if disabled else (peak > 100 and nonzero > 1000), (disabled, peak, nonzero)
        print(f"PASS original options CD: sampled sound {'off is silent' if disabled else 'on is audible'}", flush=True)
    print(f"PASS original options: evidence {OUT}")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, OSError, subprocess.SubprocessError) as exc:
        print(f"FAIL original options: {exc}; evidence {OUT}", file=sys.stderr)
        sys.exit(1)
