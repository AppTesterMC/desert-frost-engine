# Dune engine for ScummVM

A from-scratch ScummVM engine for Cryo's *Dune* (1992, DOS; CD and floppy
releases), written so the game can run natively on iOS through ScummVM's iOS
backend. It is a bring-up in progress: the data formats are decoded and the
first screens work; the game logic is not there yet.

This file is the map for contributors. Format details and the reasoning behind
decisions live in [FINDINGS.md](FINDINGS.md); the projects and people this work
builds on, and the licensing questions that must be settled before publishing,
are in [CREDITS.md](CREDITS.md). Please keep all three current when you change
or learn something.

## Status

| Area | State |
| --- | --- |
| `DUNE.DAT` archive, loose floppy files, HSQ compression | done (`resource.cpp`) |
| Sprite sheets: 4-bit, 8-bit, RLE, scaling, flipping | done (`sprite.cpp`) |
| HNM (first generation) video with VOC soundtrack | done for the CD codec (`hnm.cpp`) |
| HERAD AdLib music through ScummVM's OPL emulator | done, version 1 / OPL2 (`music.cpp`) |
| `.SAL` rooms: sprites, gradient/noise polygons, lines | done; line dithering missing (`room.cpp`) |
| Sky: tiles and the 33 time-of-day palettes | drawn at fixed midday; no game clock yet (`sky.cpp`) |
| Control panel: layout, font, compass, touch zones | done; COMMAND1 text and first throne-room commands are live (`panel.cpp`) |
| Mouse pointer (original arrow), direct touch on phones | done (`cursor.cpp`); the original's other pointer shapes are not used yet |
| Palace map with real exits | done for 10 rooms; the palace front (room 1) has no backdrop yet (`palace.cpp`) |
| CD intro | the six videos play; music and fades between them missing |
| Floppy intro | logos through worm-call, Paul and Chani cards; later scenes remain (`intro.cpp`) |
| Game clock, characters, dialogue, commands, map, game logic, saves | not started |

## Source map

| File | Responsibility |
| --- | --- |
| `detection.cpp`, `metaengine.cpp` | ScummVM glue: recognise the CD (`DUNE.DAT`) and floppy (`DUNES.HSQ`) releases |
| `dune.cpp/.h` | `DuneEngine::run()`: data check, graphics, music, intro, event loop |
| `resource.cpp/.h` | Load a file by name from `DUNE.DAT` or the directory; transparently un-HSQ it |
| `sprite.cpp/.h` | Palette blocks and sprite decoding/blitting of a sprite sheet |
| `room.cpp/.h` | Draw one room of a `.SAL` file with a given sprite sheet |
| `palace.cpp/.h` | The palace's rooms and exits, from the executable's location table |
| `panel.cpp/.h` | The control panel below the view: drawing, game font, COMMAND1 strings, hit testing |
| `scene.cpp/.h` | `GameScreen`: placeholder menu, palace room view, input |
| `sky.cpp/.h` | Sky tiles and time-of-day palettes from `SKY.HSQ` |
| `intro.cpp/.h` | CD intro (videos) and floppy intro (sprite sequence through the first character cards) |
| `hnm.cpp/.h` | Blocking HNM video player |
| `music.cpp/.h` | HERAD song player on OPL |
| `sound.cpp/.h` | One-shot VOC sample playback (unused at the moment) |
| `cursor.cpp/.h` | The original arrow pointer via ScummVM's cursor manager; direct-touch default |
| `debug.cpp/.h` | Log file, OSD messages, screenshot dumps, developer config keys |

Data flow for a room: `Resource` loads `PALACE.SAL` and the sheet named by
`palace.h` → `Sprite::setPalette()` → `Room::draw()` into a 320x200 8-bit
surface → `Panel::draw()` on rows 152-199 → one `copyRectToScreen`.

## Conventions

- ScummVM code style: tabs, `_member` names, `kConstant` enums, no exceptions,
  no STL; use `Common::` containers.
- Every decoder bounds-checks its input and fails by returning `false`; game
  data on a phone may be incomplete or from another release.
- Every header explains *what* the class is for and *where the knowledge came
  from* (executable offsets, routine names in the OpenRakis listing, reference
  project). Comments in code explain *why*; they do not narrate.
- Any loop that waits for input while the pointer is visible calls
  `_system->updateScreen()` every iteration; the backend draws the pointer there.
- Anything that is a stand-in for undecoded original behaviour says so in a
  comment ("placeholder", "not decoded yet"), so it is easy to find and replace.
- Magic numbers that come from the original carry their origin. Numbers that
  were measured or guessed say that instead.

## Building and checking your change

The repository lives on a slow external volume, so the scripts in
`Cryogenic/scripts/` copy the ScummVM tree to `/private/tmp` and rsync this
directory into it before building.

```sh
# Desktop build (SDL). Opens the game in a window.
./scripts/test_dune_scummvm_sdl.sh

# Automated check: untimed run that writes screenshots (BMP) of the intro
# steps and of every palace room to
# notes/temp/dune_scummvm_engine_20260916/results/sdl-dump/, then exits.
DUNE_DATA=/private/tmp/dune-data        ./scripts/test_dune_scummvm_sdl.sh dump   # CD
DUNE_DATA=/private/tmp/dune-data/floppy ./scripts/test_dune_scummvm_sdl.sh dump   # floppy

# Null-backend build and detection test.
./scripts/test_dune_scummvm_native.sh

# iOS IPA (ad-hoc signed, for TrollStore).
./scripts/build_dune_scummvm_ios.sh
```

**Before building an IPA**, run `./scripts/check_dune_build.sh`: it builds the
desktop engine, screenshots every screen for both releases and captures the
audio, so a broken build never reaches the user's phone. The procedure, and
what you must judge by eye, is in [`../../../../scripts/PREFLIGHT.md`](../../../../scripts/PREFLIGHT.md).

`DUNE_DATA` points at a local copy of the game data (CD: the directory with
`DUNE.DAT`; floppy: the loose files). Look at the dumped images before asking
anyone to test on a device. Audio can be checked without listening by running
the SDL binary with `SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE=out.raw` and
inspecting the samples.

Developer config keys are documented in `debug.h`. Changes to ScummVM outside
this directory are kept as patches in `Cryogenic/scripts/patches/` (see its
README); the iOS build script applies them.

The Python tools in `Cryogenic/scripts/` (`dune_sprite_sheet.py`,
`dune_hnm_frames.py`, `dune_room.py`) are small reference decoders. They are
the quickest way to look inside a resource, and new format work usually starts
there before it is ported to C++.

## References

Full credits and licences: [CREDITS.md](CREDITS.md). Clone these next to your
checkout before reverse engineering anything by hand; most questions are
already answered in one of them.

- **OpenRakis** (<https://github.com/OpenRakis/OpenRakis>): annotated
  disassemblies `asm/cd/DNCDPRG_RECENT.ASM` and `asm/floppy/DUNEPRG.ASM`.
  Routine names such as `sub_13EFE` in our comments refer to the CD listing.
- **dune-rust** (<https://github.com/madmoose/dune-rust>): room renderer, sprite
  blitter, HNM, globe, map and lip-sync code derived from the disassembly.
- **swift-dune** (<https://github.com/codingstyle/swift-dune>): a macOS
  reimplementation with every intro scene, the prologue, palace, sietch, sky,
  characters and UI as readable Swift. The best source for *how a scene is put
  together* (sprite numbers, positions, timings).
- **madmoose/dune** (<https://github.com/madmoose/dune>): unfinished ScummVM
  engine; lists the intro's scene table with the original's routine addresses.
- **AdPlug** HERAD player; **hnm1dump** by VAG; **dune_revival**; **odrade**
  (save games, troops, locations).
- `Cryogenic/src`: the Spice86/C# hybrid, a behavioural reference.

## Planned releases: Amiga and Sega Mega CD

The goal is one engine for every release of the game. Today only the DOS CD
and DOS floppy releases are detected. What a first look at the other two shows
(details in FINDINGS.md):

| Release | Data we have | First look |
| --- | --- | --- |
| Amiga (3 disks) | `Dune_Amiga_EN.zip`, `Dune 1 (Cryo + Virgin) A/B/C.adf` | Same `.hsq` resource names as DOS (`icone.hsq`, `leto.hsq`, `balcon.hsq`, ...), so HSQ and the sprite sheets probably carry over. Needs: reading the disks' file system, 32-colour palettes, Amiga music (not HERAD/OPL) |
| Sega Mega CD | `Dune (USA)/*.bin+cue`, `Dune (U).zip`, `Dune (EU).zip` | ISO 9660 with a single 471 MB `DUNE.DAT` whose start is zeroed: not the PC archive layout, the index must live in the boot program. Needs: finding that index, the video and audio formats (CD-quality speech and redrawn graphics) |

How the code is meant to absorb them:

- **`Resource` is the seam for storage.** It already hides "archive or loose
  files"; a release adds a way to open a named resource (ADF file system, Mega
  CD index) without the callers changing.
- **Decoders stay release-neutral.** `Sprite`, `Room`, `HnmPlayer` and `Music`
  take bytes, not files. A format variant becomes a flag or a sibling class
  chosen by the release, never an `#ifdef`.
- **Release differences are data.** Things like the font's file name, the
  intro's composition or the sheet list belong in a per-release description
  selected from the detection entry (`ADGameDescription::platform`), which is
  where the current `isCD` flag should move when the third release arrives.
- **Order of work:** finish the DOS game first; the ports share its logic and
  most of its data, and the DOS disassembly is the only annotated one.

## Where to go next

1. The remaining intro scenes (list in `intro.h`), one swift-dune scene at a
   time: WormCall, Background + Character (Paul), Sunrise, ...
2. Characters in rooms from the NPC tables, then the real command list
   (`COMMAND*.HSQ`) and dialogue (`PHRASE*.HSQ`, `DIALOGUE.HSQ`, `CONDIT.HSQ`).
3. The intro script from the executable (both releases) instead of measured
   positions.
4. Other places: sietches, villages and fortresses use the same location table
   (`palace.h` documents its layout).
