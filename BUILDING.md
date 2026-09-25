# Building the Dune engine for ScummVM

This repository holds the engine only (`third_party/scummvm/engines/dune/`),
not ScummVM itself. You build it by dropping the engine into a ScummVM source
tree and compiling ScummVM with just that engine enabled. Game data is not
included; you need your own copy of *Dune* (1992).

There are two ways to do it:

- **By hand**, on any platform ScummVM builds on (Linux, macOS, Windows/MSYS2):
  [section 2](#2-build-by-hand-any-platform).
- **With the scripts** in `scripts/` (macOS), which also run the screenshot
  dumps, the regression harness and the iOS packaging:
  [section 3](#3-build-with-the-scripts-macos).

## 1. What you need

### Tools

| Tool | Why | Install (examples) |
| --- | --- | --- |
| C++11 compiler, GNU make | building ScummVM | Xcode command-line tools; `apt install build-essential` |
| SDL2 development files | the desktop backend | `brew install sdl2`; `apt install libsdl2-dev` |
| git, rsync | fetching ScummVM, staging the engine | usually preinstalled |
| Python 3 with Pillow | regression harness and the resource tools | `python3 -m pip install Pillow` |
| capstone (optional) | `scripts/dune_disasm.py`, reading the executable's code | `python3 -m pip install capstone` |
| Xcode with an iOS SDK | only for the iOS package | Mac App Store |

### ScummVM source

The engine is developed and tested against ScummVM **master at commit
[`430653b98d`](https://github.com/scummvm/scummvm/commit/430653b98d75e86d15a293c0aa3b3dbed56b499d)**
(version string `2026.3.1git`, 2026-09-15). Newer master usually works too. If
it does not compile, go back to this commit and please open an issue.

### Game data

Point ScummVM at a directory holding one of the supported DOS releases. **The
game's executable must be in the same directory**: the engine reads the
location, room and character tables from the executable's data segment.

| Release | Files needed | Tested with (SHA-256) |
| --- | --- | --- |
| DOS CD | `DUNE.DAT`, `DNCDPRG.EXE` | `DNCDPRG.EXE` `5f30aeb84d67cf2e053a83c09c2890f010f2e25ee877ebec58ea15c5b30cfff9`; `DUNE.DAT` `60060efa7fb0bd49447dcc7327549a554808aa0e9f11ccbc4866a30895960623` |
| DOS floppy (v2.1) | all the loose `*.HSQ` / `*.HNM` / `*.BIN` files and `DUNEPRG.EXE` | `DUNEPRG.EXE` `021e6767485735e643fdb842aa0850f339b2e5114a5142e4d7c8a10d0dc6bea7` |

Check yours with `shasum -a 256 <file>` (macOS) or `sha256sum <file>` (Linux).
Other versions may work; please report their hashes. Amiga and Sega Mega CD
data are not detected yet.

## 2. Build by hand (any platform)

```sh
# 1. Get ScummVM at the tested commit.
git clone https://github.com/scummvm/scummvm.git
cd scummvm
git checkout 430653b98d75e86d15a293c0aa3b3dbed56b499d

# 2. Put the engine into it. The engine directory must be called "dune".
cp -R /path/to/desert-frost-engine/third_party/scummvm/engines/dune engines/dune

# 3. Configure with only this engine (much faster to build), then compile.
./configure --disable-all-engines --enable-engine=dune
make -j8

# 4. Check that the engine is registered.
./scummvm --list-engines | grep dune
# dune                 Dune

# 5. Run the game from your data directory.
./scummvm --path=/path/to/dune-data dune
```

Notes:

- ScummVM finds engines by their directory; no file outside `engines/dune/`
  has to change for the desktop build.
- Add `--enable-debug` to `configure` for a debug build.
- If you keep the engine in this repository and edit it there, re-copy (or
  `rsync -a --delete .../engines/dune/ engines/dune/`) before each `make`.
  Alternatively, make `engines/dune` a symlink to the repository's directory.
- Instead of `--path`, you can add the game in ScummVM's launcher
  (`./scummvm`, then *Add Game…* and select the data directory).
- The game writes a log to `dune-ios.log` in ScummVM's save directory. Every
  room it draws is logged, and a problem shows up there first.

## 3. Build with the scripts (macOS)

The scripts stage the ScummVM tree and the build outside the checkout, under
`/private/tmp/dune-scummvm-*` (override with `DUNE_LOCAL_BUILD_ROOT`). They copy
the engine in with rsync before every build, so you edit only this repository.
The scripts get the ScummVM source from `third_party/scummvm-source.tar` if it
exists. Otherwise they fetch the pinned commit from GitHub once (override with
`SCUMMVM_REF` or `SCUMMVM_REPO`).

```sh
# Desktop (SDL) build; opens the game in a window.
./scripts/test_dune_scummvm_sdl.sh

# Headless screenshot run: writes BMPs of the intro steps and rooms to
# notes/temp/dune_scummvm_engine_20260916/results/sdl-dump/ and exits.
DUNE_DATA=/path/to/cd-data     ./scripts/test_dune_scummvm_sdl.sh dump
DUNE_DATA=/path/to/floppy-data ./scripts/test_dune_scummvm_sdl.sh dump

# Null-backend build plus detection check (no window, no audio).
./scripts/test_dune_scummvm_native.sh
```

The first build of each script compiles the ScummVM core and takes several
minutes. Later builds recompile only the engine.

### Regression harness

`make verify` plays scripted input through both releases (the `cd` and
`floppy` scenarios, plus the `story-*` scenarios, which start from a
prepared story state) and compares named checkpoints against reference images in `golden/` (perceptual hash, tolerance
in `tests/regression/manifest.json`). It writes an HTML report to
`notes/temp/dune_scummvm_engine_20260916/results/regression-report/index.html`.

The reference images are frames of the original game and are **not**
distributed. Create your own once from your data:

```sh
export DUNE_DATA_CD=/path/to/cd-data          # defaults: /private/tmp/dune-data
export DUNE_DATA_FLOPPY=/path/to/floppy-data  #           /private/tmp/dune-data/floppy
make init-golden          # runs every scenario and seeds golden/ (refuses if it exists)
# look at the seeded PNGs in golden/ before you trust them
make verify               # now it should pass
```

When you add a checkpoint to the scripts, `make add-missing-golden` seeds only
the new ones. When a change is meant to alter a picture, review the diff in the
report. Then accept that one checkpoint with
`python3 scripts/dune_accept_golden.py cd/<checkpoint>`.

`./scripts/check_speedrun.sh [campaign|full] [seed...]` has a bot play the PC
speedrun route on both releases (uses `DUNE_DATA_FLOPPY` and `DUNE_DATA_CD`).
It prints PASS when the Emperor's throne room is reached. `campaign` starts
after Leto's death; `full`, from a new game, does not pass yet.

`./scripts/check_dune_build.sh` is the wider pre-release check: it screenshots
every reachable screen of both releases and measures the audio. See
[`scripts/PREFLIGHT.md`](scripts/PREFLIGHT.md).

### iOS package (IPA)

```sh
make ipa    # runs `make verify` first and refuses to package if it fails
# or directly:
./scripts/build_dune_scummvm_ios.sh
```

The script builds ScummVM's `create_project` tool and generates an Xcode
project with only the Dune engine. It downloads ScummVM's prebuilt iOS
libraries (`scummvm-ios7-libs-v4.zip`) if they are missing and applies the
patches in [`scripts/patches/`](scripts/patches/README.md). Then it builds and
ad-hoc signs
`notes/temp/dune_scummvm_engine_20260916/packages/ScummVM-ios-dune-prototype.ipa`.

- The SDK defaults to `iphoneos26.5`. Set `IOS_SDK=iphoneos<version>` to match
  your Xcode (`xcodebuild -showsdks`).
- `DUNE_DEBUG=1` builds with the engine's debug overlay.
- The IPA is ad-hoc signed. Install it with TrollStore, or re-sign it with
  your own development certificate. It contains no game data: copy the data
  into the app's documents folder (e.g. with the Files app) and add the game in
  ScummVM.

## 4. Developer options

These keys go in `scummvm.ini`, under `[scummvm]` or the game's section. They
are documented in `engines/dune/debug.h`:

| Key | Effect |
| --- | --- |
| `dune_dump=<dir>` | untimed run that writes BMP screenshots of each step, then quits |
| `dune_dump_every=<ms>` | real-time run that dumps a frame every *ms* milliseconds |
| `dune_floppy_start=<n>` | skip the first *n* floppy intro scenes |
| `dune_intro_start=<n>` | CD only: start the intro at the *n*-th video |
| `dune_no_music=1` | disable the AdLib music |
| `dune_test_flight=<n>` | fly to place *n* in real time and stop (with `dune_dump_every`, samples the flight view) |
| `dune_skip_cd_story=1` | CD only: stop the intro after TITLE instead of playing the story scenes |
| `dune_speedrun=<campaign\|full>` | the speedrun bot (used by `check_speedrun.sh`) |
| `dune_rng_seed=<n>` | fix the random seed so runs are reproducible |
| `dune_hnm_dump_frame=<n>` | on a dump run, the video frame at which each intro video is screenshotted (default 40) |
| `dune_story_setup=<name>` | start from a prepared story state (`comm`, `gathering`, `ecology`, `stillsuit`; used by the story scenarios) |
| `dune_input=<file>`, `dune_checkpoint_dir=<dir>` | scripted input and checkpoints (used by the harness) |

To check audio without listening, run the SDL build with
`SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE=out.raw`.

## 5. Troubleshooting

| Symptom | Cause / fix |
| --- | --- |
| `--list-engines` has no `dune` line | the engine directory is not `engines/dune`, or `configure` ran before it was copied; re-run `configure` |
| Game is not detected | the directory lacks `DUNE.DAT` (CD) or `DUNES.HSQ` (floppy) |
| Intro plays, but after it places, exits or characters are missing or wrong | the executable (`DNCDPRG.EXE` / `DUNEPRG.EXE`) is not next to the data |
| Compile errors in `engines/dune` against newer ScummVM | build at the pinned commit above and open an issue with the error |
| No sound on iPhone | the ring/silent switch; the iOS patch in `scripts/patches/` fixes this, so make sure it was applied |

For the engine's architecture, conventions and status, see
[`third_party/scummvm/engines/dune/README.md`](third_party/scummvm/engines/dune/README.md).
