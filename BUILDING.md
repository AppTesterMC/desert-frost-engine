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

Point ScummVM at a directory holding one of the supported releases. For the
DOS releases, **the game's executable must be in the same directory**: the
engine reads the location, room and character tables from the executable's
data segment.

| Release | Files needed | Tested with (SHA-256) |
| --- | --- | --- |
| DOS CD | `DUNE.DAT`, `DNCDPRG.EXE` | `DNCDPRG.EXE` `5f30aeb84d67cf2e053a83c09c2890f010f2e25ee877ebec58ea15c5b30cfff9`; `DUNE.DAT` `60060efa7fb0bd49447dcc7327549a554808aa0e9f11ccbc4866a30895960623` |
| DOS floppy (v2.1) | all the loose `*.HSQ` / `*.HNM` / `*.BIN` files and `DUNEPRG.EXE` | `DUNEPRG.EXE` `021e6767485735e643fdb842aa0850f339b2e5114a5142e4d7c8a10d0dc6bea7` |
| Amiga (3 disks) | the folder `scripts/dune_amiga_extract.py DISK1.adf DISK2.adf DISK3.adf OUTPUT_DIR` writes from the three disk images | |
| Sega CD / Mega CD | the folder with the disc's bin/cue rip (or an extracted `DUNE.DAT`) | |

Check yours with `shasum -a 256 <file>` (macOS) or `sha256sum <file>` (Linux).
Other versions may work; please report their hashes. The Amiga and Sega CD
ports are younger than the DOS game; the README lists what they do not do yet.

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

### Game options and saves

Open **Game Options > Game** for DOS language/music/sampled-sound controls or
the two optional story fixes available on DOS and Amiga. Amiga uses fixed
English resources; Paula music and sound playback are not implemented.
**Keymaps** configures controller bindings and **Paths > Save path** selects
per-game storage. A generic audio-device choice cannot enable Amiga audio.

DOS saves retain `DUNE21S1.SAV`–`DUNE21S4.SAV` (floppy) and
`DUNE37S1.SAV`–`DUNE37S4.SAV` (CD). Engine Amiga uses `DUNEAMS1.SAV`–`DUNEAMS4.SAV`.
Matching legacy engine Amiga `DUNE37S` saves are read only when the new slot
is absent; loading never rewrites them. Foreign/damaged layouts are rejected
before state changes. Native Amiga `DUNE10S` saves are not supported. Separate
Save paths can also isolate multiple targets using the same release. See
[save compatibility](third_party/scummvm/engines/dune/SAVES.md).

## 3. Build with the scripts (macOS)

The scripts stage the ScummVM tree and the build under `$TMPDIR/dune-scummvm-*`
(override with `DUNE_LOCAL_BUILD_ROOT`). They copy
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
export DUNE_DATA_CD=/path/to/cd-data          # default: $TMPDIR/dune-data
export DUNE_DATA_FLOPPY=/path/to/floppy-data  #         $TMPDIR/dune-data/floppy
make init-golden          # runs every scenario and seeds golden/ (refuses if it exists)
# look at the seeded PNGs in golden/ before you trust them
make verify               # now it should pass
```

When you add a checkpoint to the scripts, `make add-missing-golden` seeds only
the new ones. When a change is meant to alter a picture, review the diff in the
report. Then accept that one checkpoint with
`python3 scripts/dune_accept_golden.py cd/<checkpoint>`.

`./scripts/check_speedrun.sh [campaign|full] [seed...]` checks the PC speedrun
route through scripted engine actions. It uses `DUNE_DATA_FLOPPY` and
`DUNE_DATA_CD` by default; `campaign` starts after Leto's death and `full`
starts a new game. Set `DUNE_SPEEDRUN_RELEASES` to select releases, including
Amiga when its data is available. A passing result is assisted engine-logic
coverage, not a player-controlled UI run or an uninterrupted campaign; the
evidence report records setup, reloads and other assistance. The bot follows
game timing, so a seed can occasionally stop before the end. A FAIL names the
stage where it stopped; check the evidence before treating it as an engine
regression. `scripts/watch_speedrun.sh [campaign|full] [--cd] [--seed N]`
shows the bot in a window.

The bot uses direct campaign calls and battle save/load retries. Full runs
reject forced progression and logged order overrides; seeded campaigns allow
only their two declared setup shortcuts and do not establish new-game
progression. The gate writes `evidence.json` with its coverage classification
and binary hash; `DUNE_SPEEDRUN_REQUIRE_PLAYER_UI=1` deliberately fails this
assisted coverage. Results do not certify another build or an untested IPA.
To test all three bot releases, use `DUNE_SPEEDRUN_RELEASES="floppy cd amiga"`
and provide `DUNE_DATA_AMIGA`; Sega CD has no completion bot.

`scripts/check_speedrun_orders.sh` checks menu-issued orders and state safety.
The speedrun checks use synthetic evidence tests; no captured run is needed.

`scripts/check_map_troops.sh` checks troop icons, selection, orders, marching
animation and arrival. It needs installed floppy data and a compatible
chapter-20 floppy save supplied through `DUNE_MAP_TROOPS_SAVE`; this external
fixture is not distributed. The script does not need or include golden images.

`scripts/check_amiga_arrival.sh` checks the Amiga arrival-room conversion.
`scripts/check_original_options.sh` checks DOS language/audio options, while
`scripts/check_amiga_options.sh` checks the applicable Amiga story options.
`scripts/check_save_compatibility.sh` exercises the isolated DOS and Amiga save
namespaces and matching legacy-save reads. Set `DUNE_DATA_AMIGA` for these
Amiga checks.

The save-compatibility gate requires installed DOS floppy/CD and Amiga data.
Its original-save reference cases are optional when the private fixtures are
absent; no game saves are distributed.

`./scripts/check_flight_landscape.sh` flies from the palace to Carthag-Tuek on
the floppy data (`DUNE_DATA_FLOPPY`) and checks that every row of the flight
landscape is seeded exactly as in the original, whose seeds were read from its
memory (`tests/regression/flight-seeds-floppy.txt`).

`scripts/check_leto_loop.sh`, `scripts/check_celimyn_tuek.sh` and
`scripts/check_hemispheres.sh` check the game options (each with the option
off and on, on both releases) and the north/south quarrel. They read
`DUNE_DATA_FLOPPY` and `DUNE_DATA_CD`.

`scripts/check_other_releases.sh` runs the Amiga and Sega CD scenarios
(`tests/regression/other-releases.json`) against their references. It reads
`DUNE_DATA_AMIGA` and `DUNE_DATA_SEGACD` and skips a release whose data is
missing; like the DOS references, seed them from your own data with
`python3 scripts/dune_regress.py add-missing --manifest tests/regression/other-releases.json`.

`scripts/check_all.sh` builds the engine once and runs `make verify`, every
`check_*.sh`, the other releases and the speedrun bot in parallel, headless (about
six minutes without the full speedrun; `--no-speedrun` or `--no-full` shorten it).
Each check's log goes to `$DUNE_LOCAL_BUILD_ROOT/check-all/`. Like `make verify`,
it needs your own references in `golden/` (see above).

`./scripts/check_cockpit.sh` checks the Amiga ornithopter dashboard and
destination controls with the desktop pointer and button paths. It requires
your extracted Amiga game data in `DUNE_DATA_AMIGA` and checks hover labels,
map scrolling and recentering, cancel, destination changes during flight, and
arrival. It uses generated screenshots rather than original-game captures.

`scripts/check_saboteurs.sh`, `check_epidemic.sh`, `check_chani.sh` and
`check_head.sh` check the Harkonnen saboteurs and worm attacks on harvesters, the
Fremen epidemic, Chani's kidnapping and Paul's panel head.

`scripts/check_search_equipment.sh`, `check_water_of_life.sh`,
`check_ecology_win.sh`, `check_endless_play.sh` and `check_smugglers.sh` check
GO & SEARCH FOR EQUIPMENT, the Water of Life, the ecology win, playing on after
the final battle and the smugglers' trade on both DOS releases (they read
`DUNE_DATA_FLOPPY` and `DUNE_DATA_CD`).

`scripts/dune_script_trace.py <script>` runs one harness script and prints the
engine log grouped by script line (each line is logged as `Script line N`), the
quickest way to see what a step did. Scripts may carry `#` comments after a
step.

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
- The IPA is ad-hoc signed. Sideload it, or re-sign it with
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
| `dune_record=<dir>` | save every shown frame as a BMP with its duration, to make a video of a run |
| `dune_speedrun_watch=1` | show the speedrun bot's run on screen and leave the game playable afterwards |
| `dune_test_cockpit=<n>` | open the ornithopter cockpit for a scripted real-time test |
| `dune_no_raids=1` | suppress Harkonnen raids in deterministic test setups |
| `dune_room_scans=1` | keep room-entry and room-leave dialogue scans enabled during scripted runs |
| `dune_cd_voice_mode=<0\|1\|2>` | CD: the voice mode the original keeps in ds:28E8 (0 text, 2 digital voices); in text mode Paul's head turns away during a character's line |
| `dune_room_rotations=<n,n,...>` | replay given room-rotation bytes, one per landing, instead of random ones (for comparing with a run of the original) |
| `dune_setup_save=1` | with a story setup, save that state as Log 1 (DOS files can be loaded in the matching original; engine Amiga files cannot) |
| `dune_globe_dump=<dir>` | write every globe frame's palette indices, live map and tilt to `<dir>` (read by `scripts/globe_ref.py`) |
| `dune_real_time=true` | keep a scripted (harness) run at the original's speed instead of as fast as possible |
| `dune_hnm_dump_frame=<n>` | on a dump run, the video frame at which each intro video is screenshotted (default 40) |
| `dune_story_setup=<name>` | start from a prepared story state (`comm`, `gathering`, `ecology`, `stillsuit`; used by the story scenarios) |
| `dune_input=<file>`, `dune_checkpoint_dir=<dir>` | scripted input and checkpoints (used by the harness) |

To check audio without listening, run the SDL build with
`SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE=out.raw`.

## 5. Troubleshooting

| Symptom | Cause / fix |
| --- | --- |
| `--list-engines` has no `dune` line | the engine directory is not `engines/dune`, or `configure` ran before it was copied; re-run `configure` |
| Game is not detected | the directory lacks `DUNE.DAT` (CD), `DUNES.HSQ` (floppy), the extracted Amiga files, or the Sega CD rip |
| Intro plays, but after it places, exits or characters are missing or wrong | the executable (`DNCDPRG.EXE` / `DUNEPRG.EXE`) is not next to the data |
| Compile errors in `engines/dune` against newer ScummVM | build at the pinned commit above and open an issue with the error |
| Headless runs (`dump`, `make verify`, the speedrun check) exit at once with no error, while the game window works | Homebrew's `sdl2` is now `sdl2-compat` (SDL 3 underneath), whose dummy video driver cannot create a renderer. The checks need the real SDL 2. `test_dune_scummvm_sdl.sh` relinks the binary to `/opt/homebrew/Cellar/sdl2/<version>` when that is still installed; otherwise install an SDL 2.x release from [libsdl.org](https://github.com/libsdl-org/SDL/releases) and rebuild |
| No sound on iPhone | the ring/silent switch; the iOS patch in `scripts/patches/` fixes this, so make sure it was applied |

For the engine's architecture, conventions and status, see
[`third_party/scummvm/engines/dune/README.md`](third_party/scummvm/engines/dune/README.md).
