# Findings and decisions

What we know about Dune's data and executable, how we know it, and why the
engine is built the way it is. Each entry says how sure we are:

- **verified**: decoded byte-exact against the game data, or confirmed against
  a recording of the original or on the device;
- **reference**: taken from another project's reverse engineering and consistent
  with our output;
- **measured / guessed**: eyeballed from a recording or inferred; replace when
  the real source is decoded.

Executable offsets refer to the CD executable `DNCDPRG.EXE` (78,700 bytes, not
packed, header 0x200 bytes). `DS:xxxx` is an offset in its data segment, whose
linear base in the OpenRakis listing is 0x1F4B0, so file offset =
0x1F4B0 + xxxx - 0x10000 + 0x200.

## Files and compression

**`DUNE.DAT`** (CD) — verified. 16-bit entry count, then 25-byte entries:
16-byte zero-padded name, 32-bit size, 32-bit offset, one unused byte. 2,547
files: 2,288 VOC, 186 HSQ, 37 HNM, 10 AGD, 10 M32, 6 LOP, 5 BIN, 4 SAL. The
floppy release has the same files loose, a few under other names (font:
`DUNECHAR.HSQ` instead of `DNCHAR.BIN`).

**HSQ compression** — verified. 6-byte header: unpacked size (16 bit), a zero
byte, packed size (16 bit, includes the header), and a salt byte that makes the
header bytes sum to 0xAB; that checksum is how a compressed file is recognised.
The body is an LZ77 bit stream: control bits are read from 16-bit little-endian
words, LSB first. `1` = literal byte. `01` = long match: a 16-bit word holding
a 13-bit negative offset and a 3-bit length; length 0 means "read another byte
as the length", and that byte being 0 ends the stream. `00` = short match: two
more bits of length and one byte of negative offset. Lengths are stored minus 2.

**Resource names in the executable** — verified. A table of 239 names starts at
file offset 0x119AD (`LIM1.HSQ` first). The game refers to resources by index
into it; sprite sheet index 0x13 is `GENERIC.HSQ`, then `PROUGE`, `COMM`,
`EQUI`, `BALCON`, `CORR`, `POR`, `SIET1`, ...

## Sprite sheets (`*.HSQ`)

Verified, and cross-checked with dune-rust's blitter.

- Word 0 = size of the palette chunk. The chunk holds blocks of (first colour,
  count, count x RGB) ended by FF FF. Components are 6-bit VGA values; the
  original promotes them with `v << 2` (so the maximum component is 252, not
  255).
- An offset table follows. It has no length field: entry 0 is both the offset
  of sprite 0 and the size of the table. Offsets are relative to the table.
- Sprite header: width in bits 0-8 and RLE flag in bit 15 of the first word;
  then height and a palette offset byte.
- Palette offset 254 or 255 marks an **8-bit** sprite (255: colour 0 is
  transparent). Otherwise pixels are **4-bit**, two per byte, low nibble first,
  rows padded to a multiple of four pixels; nibble 0 is transparent and the
  offset is added to the others.
- RLE is per row and never crosses a row: a command byte with bit 7 set repeats
  the next byte `257 - command` times, otherwise `command + 1` literal bytes
  follow. Pixels past the row width are dropped.
- Room sprites can be drawn mirrored, upside down, or shrunk by one of eight
  fixed factors (0x100, 0x120, 0x140, 0x160, 0x180, 0x1C0, 0x200, 0x280 as 8.8
  fixed point source steps). This is what makes distant guards small.

Bugs this replaced (kept here because they are easy to reintroduce): ScummVM's
`setPalette` takes RGB **triplets**, not 4-byte entries; the offset table has no
length prefix (off-by-one on every sprite); HSQ sheet entries are separate
sprites, not animation frames.

Two empty rows inside the Atreides hawk (POR sprite 18) are in the data and
visible in the original; they are not a decoder defect.

## Rooms (`*.SAL`)

Reference (dune-rust `room_sheet.rs`, from the disassembly of `sub_13B59`),
consistent with the recording of the original.

A table of 16-bit room offsets, then per room a marker count and commands until
FF FF. Command word:

- bit 15 clear: **sprite or marker**. Bits 0-8 = sprite + 1 (1 = a character
  marker), bit 9 = x + 256, bits 10-12 = scale, bit 13 = flip y, bit 14 = flip
  x; then x, y and a palette offset (0 = use the sprite's own).
- bits 15 set, 14 clear: **polygon**. Low byte = colour, bit 8 = reverse the
  horizontal gradient, bits 9-13 seed a Galois LFSR (mask = those bits | 2). Two
  signed bytes give horizontal and vertical gradients (x 16, 8.8 fixed point),
  then a start point and the right side's vertices up to the one flagged 0x4000,
  then (unless that vertex is also flagged 0x8000) the left side's vertices up to
  the one flagged 0x8000. Each pixel is `(noise & 3) + (colour >> 8) - 1`: that
  is the grain on floors and walls.
- bits 15 and 14 set: **line**, colour in the low byte, two points. The
  original can dither lines; not implemented.

Markers are top-left positions for standing characters: Duke Leto (PERS sprite
0) at the throne room's last marker (186, 53) lands exactly where the recording
shows him — verified.

## Sky

Verified on the CD data; palette numbers are reference (swift-dune).
`SKY.HSQ` has eight 40-pixel-wide gradient tiles: sprites 0-3 (20, 20, 20, 18
rows; the "narrow" sky) and 4-7 (30, 30, 30, 28 rows; the "large" sky), stacked
and repeated across the view. They use colours 128-222. Offset table entries
8-40 are not sprites but 33 **palette records** for that range: a zero word, a
size word (287), first colour (128), count (95), then RGB triplets. `SUNRS.HSQ`
carries six such records (128, 80 colours) after its eight sprites. swift-dune
uses palette 1 for day, 3 for night, 6 for sunset and 16 for sunrise, and blends
neighbours; the original's mapping from game time is in `sub_138B4`, not
decoded.

Rooms with an outside view get the sky first and the room on top: PALACE.SAL
room 10 (balcony; narrow sky, full width) and room 11 (palace front; large sky,
200 pixels wide). **Decision:** the sky is fixed at midday until a game clock
exists.

**Open:** PALACE.SAL room 11 contains only character markers. Its backdrop (the
palace front seen at the end of the prologue) must be drawn by other code; until
that is found the exit to it stays closed (`palace.cpp`).

## Locations and exits

Verified against our earlier by-eye pairing of rooms and sheets (all twelve
agreed) — see `palace.h` for the record layout. Summary: a location is
`(place type << 8) | room`; `DS:0x13C4` holds per place type a pointer to 5-byte
records `room byte, up, right, down, left`. `sub_13EFE` does the lookup,
`sub_13F27` moves (BP = direction 1-4), `sub_15E4F` picks the `.SAL` file from
the place type (< 0x20 sietch, 0x20 palace, 0x21-0x27 village, 0x28-0x2F
Harkonnen, >= 0x30 other). The same table describes sietches and villages, so
adding them is mostly data.

Open questions: exits with bit 7 (doors; conditions unknown), exits 252-254
(leave the place: ornithopter, map, worm?), and room bytes with bit 7 (palace
room 3).

**Decision:** the palace table is embedded in `palace.cpp` instead of being read
from the executable, so the engine needs only data files and works with the
floppy release, whose executable differs.

## Control panel

Verified (layout matches the recording). Hot-zone table at file offset 0x111A0;
geometry and sprite numbers are in `panel.h`. `ICONES.HSQ` carries no palette:
colours 1-15 and 224-239 come from `PERS.HSQ`, 240-255 from the current room
sheet, which is why the panel is sand-coloured in the palace halls and blue-grey
in the equipment room.

Font (`DNCHAR.BIN` / `DUNECHAR.HSQ`) — verified: 256 glyph widths, then 9 rows
of one byte per glyph, MSB left.

`COMMAND1.HSQ` — decoded: an offset table and 333 FF-terminated strings (place
names, troop occupations, status texts with embedded numeric placeholders).

**Decisions:** tapping the view walks (side thirds turn, centre goes forward)
and the compass zones are grown by 3 pixels, because the original's 11-pixel
arrows are too small for a finger. The room name occupies the first command row
because ScummVM's OSD lags by one message on iOS. The panel now reads real
`COMMAND1.HSQ` records; the throne room exposes `SEE DUNE MAP` and `DUKE LETO
ATREIDES`, while their full map/dialogue actions remain pending. Text uses the
brightest interface colour until the original's text colours are decoded.

## Mouse pointer and touch

Verified (bitmap read from the executable). The arrow is stored at `DS:0x2584`
in the DOS mouse driver's cursor layout: hotspot x, y (0, 0), sixteen 16-bit
AND-mask rows, sixteen image rows, MSB leftmost; `_sub_1DBEC_draw_mouse` draws
it and `_word_21A32_mouse_cursor_image_addr` points at the current shape, which
the game swaps in places (other shapes not located yet).

**Decisions:** the pointer goes through ScummVM's `CursorMan` with its own
three-colour palette, so the backend draws and moves it and room palettes cannot
recolour it. On phones ScummVM defaults 2D games to *touchpad* mode (drag moves
the pointer, a tap clicks wherever the pointer is); with no visible pointer that
made taps land in the wrong place on the device. The engine therefore sets
`touch_mode_2d_games=direct` in the transient domain before `initGraphics()`
unless the user has chosen a mode in ScummVM's options. The intro runs without a
pointer, as the original does.

**Bug to remember:** ScummVM backends repaint the pointer only inside
`updateScreen()`. The first pointer build called it only when a room was
redrawn, so on the device the arrow froze between room changes and felt broken
in both touch modes. The main loop now calls `updateScreen()` every frame; any
future loop that waits for input with the pointer visible must do the same.
Taps are written to `dune-ios.log` with their position and what they hit, to
separate touch-mapping problems from hit-zone problems.

## HNM video (first generation)

Verified byte-exact on all 37 CD videos and on the floppy `LOGO.HNM`.

- 16-bit-length chunks. The first holds the initial palette (same block format
  as sprite sheets, count 0 = 256, a lone "colour 0, count 1" block is skipped)
  and a frame offset table we do not need.
- Other chunks: optional tagged blocks `sd` (sound), `pl` (palette), `pt`, `kl`,
  `mm` (ignored), then one frame. Frame header: width bits 0-8, bit 9 =
  HSQ-packed, bit 10 = full frame (no x/y prefix), bit 15 = per-row 8-bit RLE
  (same scheme as sprites); height; mode (0xFF = colour 0 transparent). Width or
  height 0 = hold the picture.
- Pictures exactly 160 pixels wide are shown pixel-doubled (the CD's speech and
  cutscene videos, the logo backdrop); this is decided per frame.
- `sd` payloads concatenate to a Creative VOC file: 8-bit unsigned, 11,111 Hz,
  about 919 bytes per frame, so about 12 frames per second.

**Decisions:** audio paces the video (frame n is due when the audio queued
before it has played); silent videos run at 83 ms per frame, which is a guess.
The "AD" codec that VAG's `hnm1dump` also handles is not implemented because no
file we have uses it. `PRT.HNM` (floppy) is not in this format; unknown.

## Music (HERAD)

Reference (AdPlug's HERAD player), output verified by capturing the mixer.

The plain `.HSQ` songs (`ARRAKIS`, `WORMINTR`, `MORNING`, ...) are HERAD version
1 for OPL2: header word 0 = offset of the 40-byte instrument records, word 1 =
0x32, up to 21 track offsets, loop start/end/count at 0x2C-0x31, speed at 0x32.
Tracks are MIDI-like (variable-length deltas; note on/off, program change,
aftertouch, pitch bend), one per OPL voice, played at 200 Hz. Instruments carry
"macros" that map velocity and aftertouch to operator levels and feedback, plus
transpose and pitch slides.

**Decisions:** AdLib was chosen because ScummVM's OPL emulator works everywhere,
including iOS; `.AGD` (AdLib Gold / OPL3) and `.M32` (MT-32) are unused. Which
song plays where is a placeholder (WORMINTR during the intro, ARRAKIS
afterwards).

## Intro

**CD** — verified on device: `VIRGIN`, `CRYO`, `CRYO2`, `PRESENT`, `TITLE`,
`IRULAN` in that order. The order follows the scene table in madmoose's
`intro.cpp`; the original also fades and plays music between them.

**Floppy** — reference (swift-dune), checked against the recording. There are
no intro videos apart from `LOGO.HNM`; the executable composes everything from
sprite sheets under a script. Reference recording:
`RPReplay_Final1789852776.mov` in the repository root (logo to first map screen,
4.5 minutes). swift-dune's `Game/Scenes/Intro.swift` lists all 35 steps with
durations and transitions, and one Swift file per scene gives sprite numbers and
positions relative to the 320x152 view, which the intro shows 24 rows down the
screen. `intro.h` lists what is reproduced and what is still to do. The original
also uses "pixelate", "zoom" and "dissolve" transitions; we substitute fades.
The current port includes the worm-call card after the DUNE title, followed by
the red Paul card and the sunrise/Chani card. These use the verified SHAI, BACK,
SUNRS and character sheets and their first recovered poses; the original
animation scheduler, later scenes, credits and narrated prologue remain open.

**Decision:** port scene by scene from swift-dune's facts now, and replace with
the executable's own script (madmoose's scene table) when it is decoded.

## Game behaviour notes (from the community)

From xcomcmdr's r/dune post and its comments (OpenRakis maintainer), to guide
the game-logic work:

- Three assembly routines serve as pseudo-random generators. Randomised: where
  characters stand in a room, part of each battle's outcome (with Fremen morale,
  expertise, numbers and equipment against the Harkonnens), worm attacks and
  their outcome, saboteur attacks, the pattern of green lights when watching a
  battle (animation data in `BAGDAD.HSQ`), how much spice the Emperor demands
  and how long between demands.
- The Amiga release was recently disassembled; its file structures and content
  are very close or identical to the PC floppy release, and its clean 68000 code
  decompiles well in Ghidra. The save-game format (troops, locations, sietches,
  game state) was worked out that way. This makes the Amiga a realistic port
  target and its code a good aid for the DOS logic.

## Other releases (first look only)

**Amiga** — guessed from strings on `Dune 1 (Cryo + Virgin) A.adf`: the disk
names the same resources as DOS in lower case (`dm1.hsq`, `icone.hsq`,
`leto.hsq`, `balcon.hsq`, `dunes2.hsq`, ...) and mentions the build commands
(`genam2 dune.s`). Nothing has been decoded yet. The zip holds three-disk sets,
several of them cracked; prefer the uncracked `.adf`/`.scp` images.

**Sega Mega CD** — verified only this far: track 1 is ISO 9660 (volume
`SEGA_SAMPLE`) with `ABS.TXT`, `BIB.TXT`, `CPY.TXT` and one `DUNE.DAT` of
471,040,000 bytes. Its first sectors are zero, so it is not the PC archive
(entry count + 25-byte entries). The index is presumably in the boot program
(the disc's system area), which has not been examined. Track 2 is a short audio
track.

## Platform and workflow decisions

- **ScummVM engine rather than DOSBox or the C# hybrid**: `README_PLAN.MD`. The
  DOSBox variant is parked (TrollStore registration error 181).
- **Desktop first**: every change is checked on the SDL build with screenshot
  dumps before a device build, because a device round trip needs the user
  (TrollStore install, AirDrop of logs).
- **Device facts**: iPhone 14 Pro, iOS 16.4.1. `devicectl` can never pair
  (CoreDevice needs iOS 17). `pymobiledevice3` reads the syslog over USB but
  cannot install an ad-hoc-signed IPA or open the app container (TrollStore
  registers the app as a System app). The IPA is served over HTTP with an
  `apple-magnifier://install?url=` link; logs come back by AirDrop.
- **Audio on iOS**: ScummVM's iOS backend sets no audio session category, so
  the app runs as SoloAmbient and the phone's ring/silent switch mutes it
  entirely; and a single failed `AudioQueueStart` (seen once in a device log as
  "Error starting the AudioQueue!") marks the mixer not ready for the whole
  session. Either makes music *and* speech vanish with no change in the engine,
  which is what happened on 2026-09-20 while the desktop build, checked by
  capturing its audio output, was fine. **Decision:**
  `scripts/patches/0001-ios7-audio-session-playback.patch` asks for the Playback
  session (heard regardless of the switch, like a media player) and retries the
  queue start; the engine logs the mixer state and ScummVM's volume and mute
  settings at startup so a silent run can be diagnosed from `dune-ios.log`.
- **Log file**: `dune-ios.log` in the save directory is the only window into a
  device run, so every stage writes a line and the file is flushed per line.
