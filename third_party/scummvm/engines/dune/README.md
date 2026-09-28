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
| Amiga release: files, sheets (pictures, planar sprites), `.SAM` rooms, 32+32 colours, copper sky, data segment | converted to the DOS layouts on load (`amiga.cpp`, `amiga_gfx.cpp`); rooms, dialogue, map, globe, book, mirror, flights and the story screens work; intro, music, the desert landscape not ported |
| Sprite sheets: 4-bit, 8-bit, RLE, scaling, flipping | done (`sprite.cpp`) |
| HNM (first generation) video with VOC soundtrack | done for the CD codec (`hnm.cpp`) |
| HERAD AdLib music through ScummVM's OPL emulator | done, version 1 / OPL2 (`music.cpp`) |
| `.SAL` rooms: sprites, gradient/noise polygons, lines | done; line dithering missing (`room.cpp`) |
| Sky: tiles and the 33 time-of-day palettes | drawn at fixed midday; no game clock yet (`sky.cpp`) |
| Control panel: original layout, small game font, day counter, compass | done; which commands a room offers is decoded only for the throne room (`panel.cpp`) |
| Mouse pointer (original arrow), direct touch on phones | done (`cursor.cpp`); the original's other pointer shapes are not used yet |
| Places: palace, sietches, villages, fortresses with their real room tables, exits and characters | done from the executable's data (`world.cpp`); which marker each character takes is a guess |
| CD intro | VIRGIN, CRYO, CRYO2 (held), PRESENT, Irulan's narration with its subtitles, TITLE and the MTG1 flyover, in the executable's intro_script order; the later story scenes (MTG2, MTG3, VER) and the fades are not ported yet |
| Floppy intro, credits, prologue | complete from the logo to the throne room: 33 scenes, credits, 9 narrated cards (`intro.cpp`, `intro_scenes.cpp`, `attack.cpp`) |
| Landing screen | throne room with Duke Leto and the first two commands, as the original (`scene.cpp`) |
| Dialogue | the original's engine: DIALOGUE/CONDIT/PHRASE data, conditions, actions, portrait with talking animation, lines in the command box, a tap per page (`dialogue.cpp`, `text.cpp`); only Duke Leto is selectable so far |
| The book | cover, topics, encyclopedia paragraphs and recorded lines with drop capitals (`book.cpp`); pagination is ours |
| Map screen and globe | flat map from MAP/TABLAT with the places' icons, scrolling, picking a destination and flying there; the original's DUNE MAP title popup, map menu (contact range, greyed rows) and planet panel; the globe with the game menu (`map.cpp`) |
| Saves | the original's four logs in their own format, save and load from the globe menu (`saves.cpp`) |
| Options | music on/off, restart, exit with confirmation | done |
| Sega CD / Mega CD release | detected (extracted `DUNE.DAT` or the data-track image); the disc's index, text, tile screens and the initial game data (converted to the PC layout); the original room screens from the room tables; the original panel (book, day, companions, commands, exit pad; place tints); conversations with measured portraits, close-up backdrops and the text box; the map (zoomed terrain, markers, travel); no verbs, videos, sound, flight or battles yet (`segacd_*.cpp`, FINDINGS.md "Sega CD / Mega CD release") |
| Characters and troops | who stands on which marker as the executable decides; sietch Fremen and chiefs; hiring by talking to the Fremen; troop orders (occupations) | done; hiring and harvest rules are ours (`world.cpp`) |
| Clock, flight, spice, rallying, results | the executable's rules: clock rate, flight steps, harvest and prospecting formulas, charisma check, results layout (`world.cpp`, `notes/research/gameplay-rules.md`) | done for those; story phases and callbacks, Jessica's contact-range lessons, the Emperor's shipments bargained with Duncan, the COMM room, visions, the first vision in the desert, the scripted scenes, Stilgar's Water of Life, the ecology route (wind traps, bulbs, irrigation, vegetation, fortresses taken, MODIFY EQUIPMENT), the Harkonnen zone, arrival deaths and the final scene run; troop marches, espionage, fort battles (MASSIVE ATTACK, FIGHT FOR A WHOLE DAY), the Harkonnen captain, worm riding and the final attack on the Harkonnen palace run (`troops.cpp`, `battle.cpp`); the smugglers' village and chapter run, their trade, Harkonnen raids and GO & SEARCH FOR EQUIPMENT not yet |

## Source map

| File | Responsibility |
| --- | --- |
| `detection.cpp`, `metaengine.cpp` | ScummVM glue: recognise the CD (`DUNE.DAT`), floppy (`DUNES.HSQ`), Amiga (`dune`, `dunechar.hsq`) and Sega CD (`DUNE.DAT` or the data-track image) releases |
| `amiga.cpp/.h`, `amiga_ds_table.h` | The Amiga release: file names, sheet/room/picture conversion to the DOS layouts, the executable's hunks and its data segment converted to the CD layout |
| `amiga_gfx.cpp` | Amiga screen effects: the time-of-day records, the copper's sky gradient in colour 1, the interface colours |
| `dune.cpp/.h` | `DuneEngine::run()`: data check, graphics, music, intro, event loop |
| `resource.cpp/.h` | Load a file by name from `DUNE.DAT` or the directory; transparently un-HSQ it |
| `sprite.cpp/.h` | Palette blocks and sprite decoding/blitting of a sprite sheet |
| `room.cpp/.h` | Draw one room of a `.SAL` file with a given sprite sheet |
| `palace.cpp/.h` | The palace's room labels for the dump names and the debug overlay |
| `world.cpp/.h` | The data segment read from the player's executable (LZEXE unpacking for the floppy): locations, room tables, characters, the player's position, the release's sheet slots |
| `map.cpp/.h` | The flat map renderer (MAP.HSQ + TABLAT.BIN) and the map/globe screen with its icons and panel extras |
| `saves.cpp/.h` | DUNE21S/DUNE37S save files: RLE, map flags, dialogue table, data segment |
| `panel.cpp/.h` | The control panel below the view: drawing, game font, COMMAND1 strings, hit testing |
| `scene.cpp/.h` | `GameScreen`: room view of any place (floppy sheets or the CD's arrival-video backdrop), conversations, the book, map and globe, the game menu, input; the globe renderer |
| `text.cpp/.h` | `SentenceBank`: COMMAND/PHRASE files and their inline codes |
| `story.cpp` | World: the spice shipments, COMM messages, vision queue, first vision, Stilgar's events |
| `ecology.cpp` | World: the live map and its stage bits, ecology jobs, vegetation discs, fortresses taken, equipment |
| `story_scene.cpp` | GameScreen: scripted scenes, the COMM viewer, visions, the open desert, endings, FIND PROSPECTORS, the dump/regression story walks |
| `dialogue.cpp/.h` | `GameState` (the data segment), CONDIT expressions, the DIALOGUE table and the conversation driver |
| `book.cpp/.h` | Paul's journal: BOOK.HSQ, topics, encyclopedia and recorded lines |
| `sky.cpp/.h` | Sky tiles and time-of-day palettes from `SKY.HSQ` |
| `intro.cpp/.h` | CD intro (videos); floppy intro sequencing, narration text, skip to the prologue |
| `intro_first.cpp` | Floppy intro first half: logos, presents, stars, title, worm, Paul, sunrise, Chani and Liet |
| `intro_scenes.cpp` | Floppy intro second half, credits and prologue: sietch, palace, backdrops, kiss, ornithopter, flight |
| `attack.cpp/.h` | The night-attack particle simulation (intro and later battles) |
| `hnm.cpp/.h` | Blocking HNM video player |
| `music.cpp/.h` | HERAD song player on OPL |
| `sound.cpp/.h` | One-shot VOC sample playback (unused at the moment) |
| `cursor.cpp/.h` | The original arrow pointer via ScummVM's cursor manager; direct-touch default |
| `debug.cpp/.h` | Log file, OSD messages, screenshot dumps, developer config keys |
| `segacd_resources.cpp/.h` | Sega CD storage: `DUNE.DAT` or the track image, the index inside the game program, the PC names that have a counterpart on the disc |
| `segacd_gfx.cpp/.h` | Sega CD tile screens (CRAM, VDP name tables, 4-bit tiles) composed with the VDP's plane and priority order; sprite banks (portraits, figures) |
| `segacd_panel.cpp/.h` | The Sega CD panel from files 5 and 30: book, day box, companions, command rows, exit pads, map variant |
| `segacd_world.cpp` | `World::loadSegaCdData`: the Sega CD program's big-endian, padded data segment rebuilt in the PC CD layout, with its room tables |
| `segacd_game.cpp/.h` | The Sega CD host: new game, rooms from the room tables drawn as Sega CD screens (320x224), walking, conversations with portraits, the map, the dump tour; `runSegaCd()` |

Data flow for a room: `World` gives the place's `.SAL` file, the room record
and the sheet its slot names → `Resource` loads them → sky or video backdrop
for exteriors → `Sprite::setPalette()` → `Room::draw()` with the characters
present at the markers into a 320x200 8-bit surface → `Panel::draw()` on rows
152-199 → one `copyRectToScreen`.

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

The scripts in `scripts/` build in a staging folder outside the checkout
(`DUNE_LOCAL_BUILD_ROOT`) and rsync this directory into the ScummVM tree there
before building; see `BUILDING.md` at the repository root.

```sh
# Desktop build (SDL). Opens the game in a window.
./scripts/test_dune_scummvm_sdl.sh

# Automated check: untimed run that writes screenshots (BMP) of the intro
# steps and of every palace room to
# notes/temp/dune_scummvm_engine_20260916/results/sdl-dump/, then exits.
DUNE_DATA=/path/to/cd-data     ./scripts/test_dune_scummvm_sdl.sh dump   # CD
DUNE_DATA=/path/to/floppy-data ./scripts/test_dune_scummvm_sdl.sh dump   # floppy
# Amiga: DUNE_DATA is the folder scripts/dune_amiga_extract.py writes from the
# three ADFs (no intro: the game opens in the throne room).
# Sega CD: DUNE_DATA is the folder with the bin/cue rip (or an extracted
# DUNE.DAT); the dump writes segacd-*.bmp (every palace room, a sietch, a
# village, a fortress, the Harkonnen palace, the credits' cards).
DUNE_DATA=/path/to/amiga-data  ./scripts/test_dune_scummvm_sdl.sh dump   # Amiga
DUNE_DATA=/path/to/segacd-data ./scripts/test_dune_scummvm_sdl.sh dump   # Sega CD

# The Amiga and Sega CD scenarios against their references (a release whose
# data is missing is skipped).
DUNE_DATA_AMIGA=/path/to/amiga-data DUNE_DATA_SEGACD=/path/to/segacd-data \
	./scripts/check_other_releases.sh

# Null-backend build and detection test.
./scripts/test_dune_scummvm_native.sh

# iOS IPA (ad-hoc signed, for sideloading).
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

Developer config keys are documented in `debug.h`; `dune_dump_every=<ms>`
turns a dump run into a real-time intro that writes a frame every so many
milliseconds (`s<scene>-<ms>.bmp`), for checking animations; `dune_floppy_start=<n>`
skips the first n floppy intro scenes so a late scene can be checked quickly.
`make verify` runs the scripted regression scenarios against `golden/` and
must pass before an IPA is built (`make ipa`); `make add-missing-golden`
seeds references for new checkpoints only. Changes to ScummVM outside
this directory are kept as patches in `scripts/patches/` (see its
README); the iOS build script applies them.

The Python tools in `scripts/` (`dune_sprite_sheet.py`,
`dune_hnm_frames.py`, `dune_room.py`) are small reference decoders. They are
the quickest way to look inside a resource, and new format work usually starts
there before it is ported to C++.

## Game options

Options that change the original's behaviour are off by default, so the
engine plays as the original does. Turn them on per game in ScummVM's
**Options > Engine** tab (desktop and iOS), or in `scummvm.ini` under the
game's section:

| Option (GUI) | `scummvm.ini` key | What it does |
|---|---|---|
| Fix the Leto loop | `dune_fix_leto_loop=true` | The original never clears Duke Leto's record when he dies (phase 0x4c), so he keeps standing in the throne room, listed and talking. With the option he is gone from every room from then on. Checked by `scripts/check_leto_loop.sh`. |
| Fix Celimyn-Tuek | `dune_fix_celimyn_tuek=true` | The original's data gives the sietch Celimyn-Tuek a discovery stage the story never reaches (0xff), so it can never be found. With the option it becomes 0x58 at new game and after loading a save, so it can be found from that stage like the other late sietches. Checked by `scripts/check_celimyn_tuek.sh`. |

ScummVM's command line has no switch for engine-specific keys; set the key in
`scummvm.ini` (or with the GUI) instead.

### The Leto loop

**The cause.** When Leto dies, the original game never updates his own
record: it still places him in the throne room. So the game keeps finding him
there, drawing him, listing him and letting him talk. By default the engine
keeps that behaviour, to stay faithful to the original.

**How to turn the fix on** (off by default):

- In ScummVM: the Dune game's **Options > Engine**, tick **Fix the Leto loop**.
  It is the same on desktop and iPhone.
- Or add `dune_fix_leto_loop=true` under the game's section in `scummvm.ini`.
  There is no command-line switch, because ScummVM's command line cannot pass
  engine-specific settings.

With the fix on, Leto is no longer drawn, listed or talking anywhere after his
death. The rest of the story is unchanged, including the handover to Duncan and
the final scene with the Emperor.

### Celimyn-Tuek

**The cause.** Every hidden place has a byte for the story stage from which
flying over it (or a nearby Fremen's hint) reveals it. For Celimyn-Tuek, the
red centre of the map, the original's starting data has 0xff there, a stage the
story never reaches, so the sietch is never found. By default the engine keeps
that behaviour.

**How to turn the fix on** (off by default):

- In ScummVM: the Dune game's **Options > Engine**, tick **Fix Celimyn-Tuek**.
- Or add `dune_fix_celimyn_tuek=true` under the game's section in `scummvm.ini`.

The fix is the community's save patch (0xff to 0x58), done in memory at new
game and whenever a save is loaded, so old saves benefit too. A save file
changes only when the game saves.

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
- [Cryogenic](https://github.com/OpenRakis/Cryogenic) (`src/`): the Spice86/C# hybrid, a behavioural reference.

## Other releases: Amiga and Sega Mega CD

One engine plays every release: the DOS CD and floppy, the Amiga and the Sega
CD / Mega CD are all detected by the same build (desktop and iOS). The two
ports are younger than the DOS game (details in FINDINGS.md, "Sega CD / Mega
CD release" and "Amiga release"):

| Release | Data | State |
| --- | --- | --- |
| Amiga (3 disks) | `Dune 1 (Cryo + Virgin) A/B/C.adf`, extracted by `scripts/dune_amiga_extract.py` | Plays on the shared engine: its files, sheets, rooms, palettes and data segment are converted to the DOS layouts on load (`amiga.cpp`, `amiga_gfx.cpp`). Rooms, dialogue, map, globe, book, mirror, flights and the story screens work. Not ported: the intro (the game opens in the throne room), music and sound, the desert landscape |
| Sega CD / Mega CD | `Dune (USA)/*.bin+cue` (or an extracted `DUNE.DAT`) | Its own host (`segacd_*.cpp`): the disc's index, text, tile screens and initial game data (rebuilt in the PC layout); the original room screens, panel, conversations with portraits, and the map with travel. Not yet: verbs, videos, sound, flight, battles. The Mega CD (Europe) entry is detected but untested |

The game options (the Leto loop, Celimyn-Tuek) are offered on the DOS
releases only.

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

1. The rest of `notes/research/gameplay-rules.md`: battles and attacks,
   troop movement, smugglers, the spice shipments, Harkonnen raids, ecology,
   the story-phase callbacks; the flight views (CD MNT videos, floppy cockpit).
2. The marker each character stands on, the real command list per room, the
   remaining dialogue actions (questions, music, the map); the original's
   justified text layout (`sub_18B11`).
3. The CD's arrival videos played on travel (their last picture is already the
   backdrop) and SKYDN.HSQ for the CD's room skies.
4. The intro script from the executable (both releases) instead of measured
   positions.
