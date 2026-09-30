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
  255). Keep this exact promotion: in particular, `BALCON.HSQ` has no palette
  and inherits the active `SKY.HSQ` record.
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

The floppy intro's worm card uses a global palette: draw the SKY background
first, then install SHAI's ranges before drawing frame 44 or an animated pose.
The same palette must be captured before the fade starts; otherwise the worm
or a character card becomes a black/stale-palette silhouette. `COMMAND1.HSQ`
is also release-specific: the text records are stable but their indices are
not (CD: `SEE DUNE MAP` 151 / `DUKE LETO ATREIDES` 119; floppy: 141 / 109),
so the panel resolves the first commands by decoded text.

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
- bits 15 and 14 set: **line**, colour in the low byte, two points (raw
  words). The room draw passes the pattern 0xffff (CD 13bdb), so room lines
  are solid; only the map's move route is dotted (0x5555, CD 81c5). Both go
  through the original's line routine (`drawVgaLine`, segvga 1a07; see "The
  small troop rules").

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
carries six such records (128, 80 colours) after its eight sprites. The
record for a time is decoded (2026-09-29, see "The light of the hour" below):
table[period] + 4 per day of the week, not swift-dune's fixed 1/3/6/16.

Rooms with an outside view get the sky first and the room on top: PALACE.SAL
room 10 (balcony; narrow sky, full width) and room 11 (palace front; large sky,
200 pixels wide). The sky follows the game clock (below).

The CD has a second file, `SKYDN.HSQ`, with the same 33 records but for
colours 73-239 (167 entries; its eight "tiles" are 4x1 stubs). Its arrival
videos (SIET, PALACE, FORT .HNM) carry no palette at all — their first chunk is
a frame index — and their 320x152 frames use exactly that range, so the CD
plays them, and keeps their last picture as the exterior backdrop, under a
SKYDN record (dune-rust's room front-end does the same). The engine uses
record 1 (day) for those backdrops and SKY.HSQ everywhere else.

The exterior rooms differ between the releases (verified by parsing both sets
of `.SAL` files): the floppy's PALACE.SAL rooms 10-11, SIET.SAL room 0,
VILG.SAL and HARK.SAL rooms 0-4 hold the sprites and polygons of the view
(SIET0.HSQ is one 156x123 rock arch with palette offset 194, i.e. colours from
the sky's own range; VILG.HSQ and FORT.HSQ carry the buildings), while the
CD's hold only the character markers: there the last picture of the arrival
video stays on screen as the backdrop (`HnmPlayer::lastFrame`). The CD
executable's `RESOURCE_LIST_HNM` entries 6-10 are SIET, PALACE, PALACE, FORT,
FORT, one per place kind of `sub_15E4F` (sietch, palace, village, fortress,
Harkonnen palace); the palace front (room 11) takes PALACE.HNM too. The intro's
palace scenes put BALCON frame 2 under the floppy rooms' pieces, as matched
against the recording; the game rooms do the same.

## Locations and exits

Verified against our earlier by-eye pairing of rooms and sheets (all twelve
agreed) — see `palace.h` for the record layout. Summary: a location is
`(place type << 8) | room`; `DS:0x13C4` holds per place type a pointer to 5-byte
records `room byte, up, right, down, left`. `sub_13EFE` does the lookup,
`sub_13F27` moves (BP = direction 1-4), `sub_15E4F` picks the `.SAL` file from
the place type (< 0x20 sietch, 0x20 palace, 0x21-0x27 village, 0x28-0x2F
Harkonnen, >= 0x30 other). The same table describes sietches and villages, so
adding them is mostly data.

Open questions: exits with bit 7 (doors; conditions unknown) and exits 252-254
(leave the place: ornithopter, map, worm?).

**The room byte** is a full byte: `code - 1` has the `.SAL` room in its low
nibble and the sprite sheet slot in its high nibble; the slot indexes the
executable's sheet list from resource id 0x13 on. The lists differ (verified
from the CD's resource table at data-segment 0x31FF and from every floppy room
code resolving to an existing file):

| Slot | CD | Floppy |
| --- | --- | --- |
| 0 | GENERIC (no backdrop: the arrival video's) | POR |
| 1-5 | PROUGE, COMM, EQUI, BALCON, CORR | same |
| 6 | POR | SIET0 |
| 7 | SIET1 | SIET1 |
| 8 | XPLAIN9 | VILG |
| 9 | "libre" (unused) | FORT |
| A-F | BUNK, FINAL, SERRE, BOTA, PALPLAN, SUN | same |

So the CD palace codes 97-100 (slot 6) and the floppy's 1-4 (slot 0) both
mean POR rooms 0-3, the CD greenhouse 207 is SERRE room 14, the floppy village
129 is VILG room 0 and its fortress 145 FORT room 0 (`World::sheetFor`).

**The world** (`world.cpp`) is the executable's initial data segment, read
from the player's own DNCDPRG.EXE (raw, at file offset 63152) or DUNEPRG.EXE
(LZEXE 0.91 packed, "LZ91" at offset 28, unpacked after UNLZEXE's algorithm)
and found by its first bytes `00 00 02 00 0a 20 80 01 20 00 00 0a`. Both
releases need their executable in the game folder for a new game. The floppy
layout is 13 bytes longer from about offset 0x1190 on (name table 0x11F8, the
palace room table 0x1232 instead of 0x1225, pointer table 0x13D1 instead of
0x13C4); the tables are located by searching for the palace records.

Data-segment offsets (dune-rust `crates/savegame`, the CD disassembly): 0x00
random bits, 0x02 game time, 0x04 room / 0x05 place type, 0x07 location + 1,
0x0e persons met, 0x10 companion, 0x12 in room, 0x14 talking to, 0x27 sietches,
0x28 troops, 0x29 charisma, 0x2a phase, 0xcf days to the shipment, 0xe8 head,
0xea message; the name table at 0x11EB (words: first name id, 12 + last name);
the sixteen characters at 0xFD8 + 16 i (room, place type, 0x80, location + 1
or 0xFF, ..., index at +14, flags at +15: 0x80 enemy, 0x20 met, 0x02 palace);
the seventy locations at 0x100 + 28 i: first-name id (COMMAND first - 1),
last-name id (COMMAND 11 + last), longitude u16, latitude i16, screen position,
type, troop, status (0x80 unknown to the player, 0x40 known, 0x10 palace),
discover phase, spice field / density, equipment counts, water. The palace is
location 0 (type 0x20, longitude 0x1915, latitude -4), the Harkonnen palace
location 1 (type 0x30). Room tables: `pointer table[type]` → 5-byte records;
sietch types 0x00-0x10 are windows ten bytes apart into one shared list
(0x11-0x1F alias 0x01-0x0F), villages share one record, fortresses three.

**Who stands where** (decoded from the CD executable, 2026-09-23). A
character is in the room when its record's first two words equal ds:4 and
ds:6 — (room, place type) and (0x80, location + 1) — as `loc_136EE` tests;
records with 0xFF as location are elsewhere. `sub_13D83` then fills a buffer
of as many slots as the room has markers (its first byte): each person, in
ascending group order, goes to slot `(group + (ds:0xC5 & 15)) mod markers`, or
to the first free slot; `_sub_13B59_draw_SAL` hands the slots to the markers
from the last one down, and `sub_13D2F` draws PERS frame 2 x group (groups
from 15 on as 15). ds:0xC5 (CD `byte_1F575`) is 0 at the start, so Duke Leto
(group 0) lands on the throne room's last marker, which the recording
confirms. The end of every landing draws a new one: CD `loc_14FB0` in
`sub_14F0C` stores `rand`'s al (`sub_1E3CC`, floppy seg000:e3cc) there, so
people stand elsewhere after each flight. The floppy dumps of the speedrun
show 0 in the palace, 5 at Carthag-Tuek, 15 at Carthag-Harg (the unhired
Fremen, group 14, lands in slot 9, which is marker 0 at (139,25)), 132 at
Carthag-Timin and 217 back at the palace; walking into the desert keeps it.
Before 2026-09-28 the engine read ds:0xC7, which is always 0.
`World::rollRoomRotation` draws it after both landing paths, and
`scripts/check_room_rotation.sh` checks each room's layout against the rule.
A compass arrow during a talk ends it and walks on, even while a line is up
(Spice86, captures/harg-probe: Carthag-Harg's chief says "My troop is settled
in Carthag-Harg, awaiting your orders.", then the down arrow shows the
exterior). The engine ignored it until 2026-09-29; the bargain, COMM
messages, scenes and visions keep their own handling. At a sietch
(`sub_13127`) the troops of the place stand in room 2: a troop not hired yet
through group 14 ("Fremen"), each hired troop's chief as groups 15, 16, ...
("Fremen Chief", "2nd Fremen Chief", ...).

**Troops** (layout from Lionel Debroux' odrade `troop.go`, values checked in
both executables, which are identical here): 68 records of 27 bytes at
data-segment 0x8AA — id, next troop at the same place, position, occupation
(bit 7 = not hired; Fremen 0 spice mining, 2 waiting, 4 military training, 5
espionage, 8 irrigation, 9 wind-trap, 10 bulbs; Harkonnen 0x0C-0x0F and
0x1F; the names are COMMAND 24 + occupation), 4-17 coordinates and flags
(byte 16 bit 7 = Harkonnen), 18 dissatisfaction, 19 speech, 21 motivation,
22-24 spice / army / ecology skills, 25 equipment bits (0x02 bulbs, 0x04
atomics, 0x08 weirding modules, 0x10 laser guns, 0x20 krys knives, 0x40
ornithopter, 0x80 harvester), 26 men / 10. A location's troop byte is its
first troop. Initially troops 1-26 are the Fremen of the sietches, none hired;
27-67 the Harkonnen garrisons.

**Gameplay rules.** The clock, flight, spice, rallying, occupations,
discovery, shipments, Harkonnen growth and the results screen were recovered
from madmoose's dune-chani annotations and the disassembly on 2026-09-23; the
full notes with addresses are in `notes/research/gameplay-rules.md` (repo
root). What the engine implements from them:

- Clock: one period per 59.9 s of real time (a 200.3 Hz timer, 12000 ticks),
  16 periods a day, stopped outside rooms and the map; each period runs the
  troops' jobs (seg000:1b23).
- New game: each place snapped to its map cell with its MAP2 spice field and
  the field's size, each troop settled on its place (seg000:0169 / 01e0) —
  checked against the original floppy's new-game save, all 70 places equal.
- Flight: one map cell per 3.83 s, a period every 16 cells, a tap skips to the
  destination; passing within four cells of a sietch the story lets the
  player find offers GO TOWARDS THIS PLACE / RESUME FLIGHT; a hidden place is
  discovered by arriving there. The view is the flat map with ICONES 0x30 /
  0x2f / 0x2e (position, trail, destination); the MNT videos (CD) and the
  cockpit (floppy) are not shown yet.
- Rallying: the Fremen of an unrallied troop talk, then COME WITH ME runs the
  charisma check; the troop waits for orders, charisma + 1. SPECIALIZE IN
  SPICE / ARMY / ECOLOGY, then the class's jobs.
- Spice mining and prospecting per period with the executable's formulas;
  mining needs a prospected place (status bit 6, which is "prospected", not
  "known"). The stock counts 10 kg batches.
- Harkonnen troops grow at period 15; their production is recomputed each
  new day. SEE RESULTS slides the house panels open over the original's stats
  layout and gauges.

Not transcribed yet: military training,
espionage, attacks and battles, ecology jobs, troop movement, skill decay,
the motivation boost of charisma steps, smugglers, the spice shipments, the
Harkonnen raids and the story-phase callbacks.

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

**CD** — the recovered `play_intro` loader table in `DNCDPRG.EXE` calls
`VIRGIN`, `CRYO`, `CRYO2`, `PRESENT`, then `TITLE`. `IRULAN` is a later
intro-stage resource, not part of that loader table, so it is not inserted into
the opening sequence by the port.

**Floppy** — the original has no intro videos apart from `LOGO.HNM`; the
executable composes everything from sprite sheets. `SEKENCE.HSQ` is a HERAD
song (its `HERAD` header and the floppy resource table say so), not a script.
The scene order, durations, transitions, sprite numbers and positions come
from swift-dune's scene code (reuse permitted by its author, 2026-09-22) and
were checked scene by scene against the recording
`RPReplay_Final1789852776.mov` (floppy release, logo to first map screen).
The port plays the whole sequence — 33 intro scenes from the Cryo logo to the
night flight, the credit roll, the nine narrated prologue cards — and lands
in the throne room. `intro.h` carries the timeline with the recording's
timestamps; `intro_scenes.cpp` holds one method per scene. Facts learned on
the way:

- Intro pictures are 320x152 at y=24; the prologue draws them at y=0 with the
  narration in rows 152-199: up to three lines of 9 px ending at row 189,
  x from 12, justified except the last line, colour 25 (STARS.HSQ entry 25,
  the orange d89028). The recording is stretched about 1.075 vertically, so
  SAL and swift-dune geometry is trusted over pixel measurements from it.
- The narration is COMMAND1 records 267-274 on floppy and 279-286 on CD; the
  engine finds the block by its first sentence ("In these times" / "En ces
  temps") instead of a fixed number.
- `INT02..INT15.HSQ` are CD-only stills; the floppy prologue ends with the
  night desert (DUNES under the night sky) and the palace stairs at dawn
  (SKY.HSQ record 8+3 blended towards 8+16).
- The prologue globe is drawn with the STARS palette in place: the map's
  colours 16-45 are the sun's yellows and browns there, so the planet looks
  flat gold, unlike on the map screen (whose palette is in `ONMAP.HSQ`).
- The globe renderer is dune-rust's: rotation 398*r rounded to a row table
  offset, tilt added to the lookup, centre (159,79); the colours are the VGA
  driver's two paths (see "SEE RESULTS: the globe's colours"). A wrong tilt
  sign produced a striped half-black ball.
- `DUNECHAR.HSQ`/`DNCHAR.BIN` (2304 bytes) = 256 widths + 128 ASCII glyphs of
  9 rows + the same 128 glyphs as a 7-row small set at 1408 (widths capped
  at 6). The panel uses the small set.
- Room markers index PERS.HSQ frames: Leto 0, Jessica 2, Thufir 4, Duncan 6,
  Gurney 8, Stilgar 10, Liet 12, Chani 14, Harah 16 (odd frames are their
  tiny map markers). The throne room's Leto stands at marker 8.
- `DUNES.HSQ` and `DUNES2.HSQ` carry no palette: the desert scenes colour
  them from the sky palette (128-222) and paint the ground with colour 190.
- SIET.SAL rooms: SIET0 (0), SIET1 (1-12), BOTA (13); the water basin is
  room 12. PALACE.SAL: 10 balcony and 11 stairs are marker-only rooms drawn
  over BALCON frame 2.
- Night attack: the original's simulation (seeds 0x01d2/0x0273/0x7302, LCG
  primes 0x0e56d/0xcbd1, 14978 µs ticks, sky flash towards palette records
  53-55) is ported from Attack.swift/dune-rust in `attack.cpp`.
- Timings where swift-dune and the recording differ: the night flight lasts
  about 22 s in the recording (swift-dune: 60 s) and the port follows the
  recording; the credits keep swift-dune's 54 s.
- The worm call: SHAI.HSQ's animation tail is a stream of (sprite, x, y)
  triples separated by restore rectangles; 44 groups, each one frame at
  12 fps (about 3.7 s), then the empty desert holds and the scene fades at
  5.5 s, as the recording shows. Doubling the groups (swift-dune) makes the
  worm surface twice. Frames 0-5 are 4-11 px sand puffs on the horizon,
  drawn white here: which palette record colours them is not decoded.
- The first half is now one transcription of swift-dune's Intro.swift steps
  (LogoSwap, Presents, Stars planetsPan, DuneTitle, WormCall, Background +
  Character, Sunrise, the Chani/Liet steps) on the scene runner, which
  freezes the picture during transitions as swift-dune's Intro node does.
  Durations from the recording where they differ: Cryo logo 2.4 s (swift
  1.28), stars fade 2 s then still until 6.5 s, pan and growth 2.8 s, zoom
  into the planet 0.5 s; title pixelate 1 s, dunes still until 4 s, scroll
  4-7 s, letters 7.5-8.5 s, 15 s in all; worm puffs 3.5 s then the worm at
  12 fps, 8 s in all; Paul 5 s; sunrise palettes 2.5 s each; Chani close-up
  1.75 s, zoom-out and animation 6 s, Liet 3.5 s, Chani 4 s, Paul in the
  desert 4 s with a 1 s fade; the sietch fades in over 1 s.
- After LOGO.HNM the original shows the Cryo logo box (CRYO.HSQ 0-4 at
  113,15) for 1.28 s, flips it out horizontally in 0.3 s, flips in the
  Virgin Games logo (5 at 98,13; 6 at 98,118) and fades it after 4 s
  (recording 2.5-8 s; swift-dune LogoSwap.swift).
- Chani's close-up (68.75-70.5 s) cuts in hard from the sunrise, holds one
  frozen frame, zooms out in 0.25 s and only then animates; Chani, Liet and
  Chani again are hard cuts. A tap during the intro skips to the narrated
  prologue; one during the prologue skips to the throne room.

**Decisions:** every scene renders into a 320x152 view at 40 fps through one
runner (`FloppyIntro::runScene`) with the original's transitions (palette
fade, 4x4 dissolve); on dump and harness runs each scene renders one
representative frame instead so the check stays fast and deterministic.
`dune_floppy_start=<n>` skips scenes during development. The placeholder start
menu was removed (2026-09-22): the game lands in the throne room as the
original does, and the harness checks that landing.

## Dialogue and the book

Recovered from the CD executable (OpenRakis' `DNCDPRG_RECENT.ASM`; the
floppy uses the same data) and checked against the data files with Python
before the port was written. Implemented in `text.cpp`, `dialogue.cpp`,
`book.cpp` and the talk/book modes of `scene.cpp`.

- **Sentences.** `COMMANDx.HSQ` (commands, names, menu words) and
  `PHRASEx1/x2.HSQ` (dialogue lines; x = language) are offset tables of
  0xFF-terminated strings. Ids are 1-based; bit 11 selects the phrase files
  (`sub_1CF70` decrements first, then tests the bit). Which phrase file a
  dialogue entry uses depends on its position: entries from the sixth
  character on (the word at DIALOGUE offset 0x60) use `PHRASEx2`.
- **Codes inside strings** (`sub_18944`): 0x0D line break; 0xFE page break;
  0x80 hi lo another sentence; 0x81-0x8F a sentence whose id sits in the
  name table at data-segment word 0x11EB + 2n (sietch first/last name,
  Paul's Fremen name...); 0x91 n / 0x92 n a byte/word variable as a number
  (the phrases append a literal 0, so spice is shown in tens of kg);
  0xA0-0xCF literal; 0xD0-0xEF layout codes carrying 2/4/1 extra bytes;
  0xF0+ end of a nested sentence.
- **DIALOGUE.HSQ** is 17 characters x 8 lists of four-byte entries ending in
  0xFFFF: byte 0 = said (bit 7), repeatable (bit 6), mask bits (4-5),
  action (0-3); byte 1 + the top two bits of byte 2 = condition; bits 2-5 of
  byte 2 = book topic (1 politics, 2 Paul on Dune, 3 spice, 4 the Fremen);
  the low two bits of byte 2 + byte 3 = sentence number. Characters in
  group order: Leto, Jessica, Thufir, Duncan, Gurney, Stilgar, Liet, Chani,
  Harah, the Baron, Feyd, the Emperor, a Harkonnen prisoner, the smuggler,
  two Fremen, and 16 = messages/the book's encyclopedia (list 7). The said
  flags mutate the table, which is why saves carry it (4600 bytes).
- **CONDIT.HSQ** (`sub_1A396`, dune-rust `crates/condit`): 707 expressions
  over the first 256 bytes of the data segment: operand, then (operator,
  operand) pairs to 0xFF; operators with bit 7 bind tighter and are resolved
  afterwards from the oldest on. Comparisons give 0xFFFF; `<`/`>` are
  unsigned, `<=`/`>=` signed. Variables seen: 0x2A story phase, 0x28 Fremen
  troops working for us, 0x0E/0x10/0x12/0x14 character bitmasks (met, with
  Paul, in the room, talking to), 0xFC a constant true (gates every
  self-introduction, the "Nothing to say" fallbacks and the book's first
  paragraphs), 0xEA the pending message number.
- **A conversation** (`loc_19472`, `sub_19F9E`, `sub_1A03F`): selecting a
  character sets the met/talking-to bits and starts at list 0; the first
  entry whose condition holds and which is not (said, non-repeatable, in the
  mask) is shown page by page; then its action runs and it is marked said;
  the search continues after it, and when a list ends the next one follows
  while the list number is not a multiple of four. Actions: 6 ends the
  conversation after the line, 11 story phase + 1, 12 next chapter
  (phase & ~3) + 4, 14 counter 0xC2 + 1 (each once); 1/2/7 set the answer
  flag 0x47A5, 3 a scripted scene, 4/5 the bargaining menu (Duncan and
  the Emperor's spice / a smuggler: ARGUE, ACCEPT, REFUSE, WHAT ?; the talk
  waits for the choice), 13 the map, 8/9/15 speaker effects, 10 the CD
  voice's lip-sync record (no effect without speech). Event 8 (seg000:a125, disassembled from the raw
  bytes after the table at cs:a107) switches on the speaker ds:47c4:
  Jessica (1) raises Paul's contact range ds:1176 (1 -> 30 with charisma
  + 10, then + 20 per line; ds:0a bit 1 set: charisma + 40 and no limit;
  ds:d5 = 0x80 - range / 6 below 100, else 0); Duncan (3) seg000:2239,
  Stilgar (5) arms the Water of Life scene (ds:227e = 0x2ccf), character 12
  clears the hidden bit of the place ds:11ce points at, the smugglers (13)
  seg000:2388. Event 9: Duncan 24ee, Stilgar 2d2c, smugglers 2419. Event
  15: Jessica ds:f5 + 1, Duncan 24a3. Events 3, 8, 9 and 15 run each time
  their line is spoken (sub_1A03F calls them before it tests the said bit).
  Leto at phase 0 therefore says: "I'm the Duke Leto Atreides, your
  father.", "My son, we must mine the spice as soon as possible...",
  "We've spotted three troops of Fremen around the palace..." and stops.
- **Portraits.** The character sheets' animation header places the bust at
  (0,0) of the view, 148-256 px wide; animations 0-3 are talking/gesturing
  sets (played at 12 fps for frames x 160 ms each, as swift-dune does), the
  last one is the 64-frame lip-sync set the CD drives from the speech data.
- **The book** shows, per topic, the encyclopedia paragraphs of character 16
  list 7 whose conditions hold (most open with a phase) and the lines said
  in conversations that carry a topic (`sub_1A03F` records
  character << 11 | entry index), under "And the Duke said to Paul:"
  (COMMAND 230 + character). BOOK.HSQ: cover 0-2, parchment tile 3 (33x29),
  drop capitals A D E L O P S T U (5-13). Page layout is ours (9-row font,
  14 lines, ink colour 94): how the original paginates is not recovered.

**Decisions:** the dialogue text is wrapped to the command box's five rows
and continued with a tap when longer, since the original's justified
layout routine (`sub_18B11`) is only partly read; harness and dump runs
draw the rest pose so frames stay deterministic; the intro line is said
once per character because it is not repeatable in the data.

## The story systems

`story.cpp` (World) and `story_scene.cpp` (GameScreen) carry the Emperor's
spice shipments bargained with Duncan, the COMM room's messages, the vision
queue, Paul's first vision in the open desert, Stilgar's Water of Life and
the scripted " Continue..." scenes. The rules, from the executable read with
capstone (the OpenRakis listing drops some instructions), are in
`notes/research/gameplay-rules.md`. The scene scripts are read from the
player's executable: the phase-scene handler's immediates (seg000:a1f7) and
the map lesson's bytes (0e 10 ff) locate them on both releases.

**Decisions:** the shipment's star-field animation, the COMM console
animation, the desert's MNT/DUNES views and ornithopter, the vision dream's
shimmer and the scenes' transitions are not drawn; the scenes skip themselves
in dump and harness runs unless started directly (`dune_story_setup`, the
story regression scenarios). The desert offers TAKE AN ORNITHOPTER as a row,
standing for the parked ornithopter's hotspot. The final attack chooses its
troops but does not march them: troop movement is not built.

## The CD intro, flights and approach clips

The CD's intro_script (seg000:0337, 48 entries of wait/init/wait/transition/
play/wait) runs VIRGIN; CRYO; CRYO2 (held on the logo until the music cues
0x6f and 0xa8); PRESENT; Irulan's narration (seg000:cefc: IRULAN.HNM, with the
subtitles of IRULn.HSQ, n = the language, 1 English: a sprite sheet of 29
lines drawn centred at row 190 from the first to the second frame of each
pair in the table at ds:35a8, seg000:cf4b); TITLE; the desert sky
(seg000:07fd); the flyover to the palace (MTG1.HNM, 24 rows down,
seg000:06f3); then the story scenes shared with the floppy (equipment room,
Jessica, Leto, Paul, the sietch, Chani, Kynes, the Baron, the night attack)
with MTG2 (to the sietch), MTG3 and VER.HNM among them. The engine plays up
to MTG1; the rest of the CD script is not ported yet.

In the game the CD flies over the MNT clips (travel_select_flight_video,
seg000:4ec6): MNT1 sand, MNT2 sand to rock, MNT3 rock, MNT4 rock to sand, the
next chosen at each clip's end from the terrain six cells ahead (low nibble
>= 8 is rock); the arrival plays the place's approach clip (SIET, PALACE,
FORT; travel_arrival_landing_sequence, seg000:488a). None of these videos has
a palette: the SKYDN.HSQ record of the hour colours them, over ONMAP's for the
minimap (`drawCdFlightView`, `playArrivalVideo`). The original also waits
for MNT1 to come round before the approach clip; built 2026-09-30 (see "The
CD flight's clips").

## Location staging and room-entry lines

The dialogue conditions read a location block (ds:4c-0x9c, ds:f7, ds:ca-e6)
that `prepare_location_data_for_condit` (seg000:331e) stages: the place's
names word, status, density, water, stock, free-equipment mask, troop counts,
forces and nearest places with compass points. The engine stages it on room
entry, per period and with each troop (`World::stageLocationForConditions`).
A room move marks the place visited (status bit 4, ds:25, ds:26) and, with
pending_room_action 5, the first person with a topic-4 line says it
(`roomEntryScan`, seg000:35b4); travel arrivals scan too. The scan before
leaving a room (ds:23 = 1, seg000:36d3) is not built yet.

## The ecology route and the end

`ecology.cpp` holds the live map (MAP.HSQ with its stage bits, shared by the
map renderer and the saves), the wind trap, bulb and irrigation jobs, the
vegetation discs that turn the land Atreides and take fortresses, the daily
growth, and MODIFY EQUIPMENT. The flights check the Harkonnen zone, arrival
at a hostile place kills Paul, and the Baron's hall (room 2 of the Harkonnen
palace) plays the final scene with FINAL.HSQ and the cast. Rules and sources
in `notes/research/gameplay-rules.md`, "Ecology".

**Decisions:** the ending is the same for both routes, as the binary and the
dune2k players say (the details and the floppy's palace takeover: "The
end-game paths"). Not built at the time: troop marches (GO & SEARCH FOR EQUIPMENT, built
2026-09-29, see its section; the final attack's march), battles and the night attack, so the palace garrison
cannot yet be removed in play and the green route ends at the arrival rule
unless the garrison is gone; the SEE RESULTS globe colours (built 2026-09-29); the credits'
portrait scenes (the cast is listed on black). The wait before the final
scene's pictures is a " Continue..." instead of timed fades.

## Troops at war, worms and the final attack (2026-09-25)

Implemented in `troops.cpp`, `battle.cpp` and `scene.cpp` from the CD 3.7 disassembly; the full spec with formulas is `notes/speedrun/battle-worm-spec.md`.

- **Random numbers.** Battles use the executable's two LCGs (`e3cc`: $s \leftarrow 0\mathrm{xcbd1}\,s+1$; `e3b7`: $0\mathrm{xe56d}$), seeded from the clock and not saved, so reloading a log rerolls a battle, which the speedrun relies on. `dune_rng_seed` pins them for checks.
- **Marches** (`84a6`, `8308`, `8604`, `8357`): 7 sub-steps at the order, then 4 a period (8 with an ornithopter); arrival within 7 cells. Arriving at a hostile place makes every hired troop there attack (`83fd`); a marching troop always travels, even one refusing to work (`6c92`-`6ceb`).
- **Strength** (`342d`, checked against the disassembly): $S=\min(255,\lfloor bM/256\rfloor)$ with $b=\lfloor \min(255,2m+a)\,p/16\rfloor$ and $M=1+2k+4l+8w+16t$. The balance (`33d9`) saturates: at half the Harkonnen strength it is 4 of 256.
- **Battles**: one roll per attacking troop per period (`739e`); MASSIVE ATTACK is one roll repeated up to 16 times with no time passing (`7317`); Paul's presence adds 30 motivation, and losing with him there kills him (COMMAND after "You know what?", 2 records on). A won fort gives its whole equipment row as free stock, its Harkonnens become recruitable freed Fremen (slots below 8), and it becomes a sietch two days later (`6e20`).
- **Harkonnen captain**: a defeated Harkonnen troop at a fort stands in room 3 as character 12 (`316e`); before he speaks he stages the nearest hidden fort within 30 (`932e`), measured as $\max(\lvert\Delta\mathrm{lng}\rvert \gg 8,\lvert\Delta\mathrm{lat}\rvert)$ (`5274`), not in row cells.
- **Worms**: CALL A WORM (greyed before phase 0x4f), GO THERE RIDING A WORM once `ds:0a` bit 6 is set; the first ride is phase 0x50 (+40 charisma); no ornithopter is taken and no Harkonnen zone is checked (`4182` runs for the ornithopter only).
- **Final attack** (`ds:c2`), all from the dialogue data: 1 when the last fort falls; Thufir's COME WITH ME answer (topic 5, action 0x0e) 2; his STAY HERE answer (topic 6) 3; Gurney's entry line with Jessica, Thufir, Gurney, Stilgar and Chani in the room (`w[12] & 0xb6`) starts the council scene cs:12db, whose Thufir line makes it 4; any Thufir line with 10 000 men and atomics training at locations 2-4 makes 5 (`1243`); Stilgar's question, ACCEPT, action 9 marches them and makes 6; the palace falls on the first attack period with no roll (`73a9`, 7).
- **Engine corrections found on the way**: a new job writes the whole occupation byte (`6aea`, it clears captured/moving/not hired) and shows its skill class in the troop's lines (`6b06`); the per-job troop counts are `dx + 1 + job` for both sides (`34d9`-`3504`, `ds:66` counts military training); a verb's answer is one line whose action counts before the gate is read (`95e2`-`95f7`); rallying the `ds:1178`-th troop opens phase 0x4c (`66e1`); a flight spots any findable hidden place (villages too) and only with someone travelling with Paul (`40f9`, `4101`); a village of appearance 0x21 has the smuggler in every room (`3157`) and its record is staged (`2318`); the smugglers' event 8 opens phase 0x3c (`2388`); walking into a room sets `ds:23 = 5` for the entry lines (`3f27`).

## The navigation panel, outdoor colours and the desert walk

The details, with addresses, are in `notes/desert-walk-spec.md`. They were
checked against a DOSBox-X recording of a desert walk in the floppy
release.

- **Navigation panel** (floppy `329F`). Inside a room it shows box 33 with its
  lit exits; the red dot appears only in the Atreides palace. Room 1 shows box
  34. The desert and villages show box 35 with all four arrows. Scripted scenes
  blank the box with colour 240. Exits 0x80..0xFA are doors the story has not
  opened yet: they are never lit.
- **Outdoor panel colours** (floppy `3B13`). A SKY.HSQ record holds 95
  colours: 80 for the sky (128..207) and 15 for the panel (240..254). That is
  why the panel turns blue-grey, orange or purple outdoors with the time of day.
- **Walking out** (floppy `422C`, `B4CC`). An exit 0xFB..0xFF leaves the place
  on foot. Each arrow then moves 1/256 of a latitude row north or south, or one
  longitude unit east or west, so on foot Paul can only walk back into the
  place he left. He arrives in room 1 when the fine latitude is 0 on the place's
  cell and longitude. The sun's glare (SUN.HSQ) shows at steps 20, 36 and 52.
  At step 68 Paul faints: five periods pass and he wakes in the palace
  (room 10) or the sietch (room 2), with `ds:E7` raised. There is no TAKE AN
  ORNITHOPTER on foot.
- **Landscape** (floppy `57FA`, `5A8D`). Up to 29 rows of DUNES3 dunes and
  rocks are placed by an LCG seeded from the position
  ($\mathit{seed}_{n+1} = 58733\,\mathit{seed}_n + 1 \bmod 2^{16}$) and
  projected with $T[z] \approx \lfloor 256/z \rfloor$. The place's building
  (DUNES2) appears at depth $\mathit{fine}+1$.
- **Where the landscape is drawn.** It is used for the walk and for a landing
  in the open desert. Sietch and fortress entrances are drawn over it at the
  place's own position (floppy `3C5C`). The sand fill starts at y 77, colour
  191.
- **Not yet done:**
  - the harvester overlay (`56DE`, partly inferred);
  - the decay of `ds:F4` when time passes;
  - the CD's desert stills (`380c`); the CD draws the floppy landscape.

## The ornithopter: cockpit, steering and sightings

Details are in `notes/orni-cockpit-spec.md` and `notes/orni-flight-spec.md`. They were checked against the DOSBox-X captures `duneprg_003.png` to `duneprg_005.png`.

- **Destination screen** (CD `430b`). TAKE AN ORNITHOPTER opens the cockpit instead of the full-screen map:
  - ORNYPAN 0 and 1, then the flat map in the window (81,45)–(241,134), then ORNYPAN 2's green grid on top;
  - "SELECT DESTINATION ON MAP" typed out one glyph per 0x18 ticks (the capture's "0" is the O of ON);
  - the blinking ICONES 0x4c ornithopter, the orange scroll pad, and a single Cancel row.

  A tap in the window takes off at once; the GO THERE step is gone. Cancel returns to the pad or to the desert. CHANGE DESTINATION reopens the same screen during a flight and re-aims the flight.
- **Route** (IDA `sub_7E87`, seg000 `5FB7`; `World::travelStep`, `compassAngle`, `unitsPerCell`). It was checked step by step against the original's memory.
  - Before every step, a homing flight re-aims from the current position (`7E4C`, `7DB4`).
  - The heading splits into a major component of 0x20 and the in-octant remainder (`7E19`). Both are scaled by the units per cell at the latitude: round(65536 / cells in the TABLAT row), which matches ds:43C7 on all 99 rows.
  - The latitude's fraction (ds:11D9) starts at 0x80. A step moves at most one row.
  - A flight arrives when its map cell is the destination's.
- **Free flight** (floppy `51C7`). A desert point picked in the cockpit sets a fixed heading, and the flight never lands there.
  - The rows are BACK TO STARTING POINT, TOWARDS NEAREST PLACE (from phase 0x32) and CHANGE DESTINATION.
  - The compass shows the steering arrows ICONES 42/43/44. A press turns the heading by 4 units, and so does every 50 ticks of holding. The fraction resets to 0x80 and the landscape pans 4 px.

  A homing flight shows SKIP TO DESTINATION and CHANGE DESTINATION over a dark compass. The worm and the check runs still land at the point.
- **Flight landscape** (IDA `76CA`, `7AC2`, `7833`). The desert walk's object engine is used with DUNES.HSQ and perspective height 0x48. The original's memory was dumped every 80 ms of a palace → Carthag-Tuek flight on Spice86, and this model reproduces:
  - its object ring (ds:1989) row by row;
  - its seed (ds:20E3) at every step;
  - its screen: 1 % of landscape pixels differ, which is the mouse cursor.

  In detail:
  - The take-off takes the first step. The rows are then laid out in 5 groups of 8 (z 1–40). Group g is seeded lng ^ lat (the latitude as a word) at the position g register-steps ahead.
  - Every 80 ms a frame: the objects move one step nearer and a row of 4 comes in at $z=40$.
  - The first frame already brings a step, then every 8th frame. The step's row still uses the old seed.
  - After a step, the seed is lng ^ lat five register-steps ahead, with the heading re-aimed from the new position. The picker comes from that cell's map byte, so the dunes and rocks are the terrain the route is about to cross.
  - `scripts/check_flight_landscape.sh` compares the engine's seeds with the original's (`tests/regression/flight-seeds-floppy.txt`).
- **Sightings** (floppy `4353`, `3924`). The check needs someone travelling with Paul (ds:10). A findable place counts when it is in the 9×9 block and within 135° of the heading; the last one found wins. It is marked discovered and the flight homes on it at once. The companion then speaks PHRASE12 0x1A0 in the ORNYCAB cabin, with the place's kind and side substituted. The single row is GO TOWARDS THIS PLACE. The old GO TOWARDS / RESUME FLIGHT choice is gone.
- **Globe access.** The planet at the bottom left of the flat map opens the globe and its game menu, as in the original. So does the box's top edge under Paul's head (92..229 x 152..159); a click on the head itself, in the view, does nothing, as in the original (the widened zone round the head was dropped on 2026-09-28).
- **Save files** read raw. A save begins with the game time, so a time such as 0x0178 writes `78 01`, a valid zlib header, and `openForLoading()` returned an empty stream. The speedrun check found this after the route timing changed.
- **Not yet done:**
  - the zoomed-globe renderer (`b6c3`) that draws the cockpit window and the minimap at a finer scale;
  - hover labels in the cockpit;
  - the take-off blink of the chosen label;
  - the CD's two-way side split and its " WHAT ? " row.

## The zoomed map window, the spice-density overlay and the save slots

These were checked pixel by pixel against the original on Spice86. Both programs loaded the same save and made the same clicks.

- **The zoomed window** (map_draw_zoomed_globe, windowed mode: CD `b6c3`, floppy `sub_D467`/`sub_D490`). One map row per screen row, one cell per pixel.
  - Each row is centred on the column `longitude × cells >> 16` of the centre.
  - The top row is `latitude − (h − 1) / 2`; the latitude is clamped to ±(0x56 − h/2).
  - Terrain pixels are `(cell & 0x0F) + 0x10`.
  - Place markers are ICONES 0x3A + kind; a sietch beyond the contact range (ds:1176) takes +5. Paul is ICONES 0x4C at (x − 13, y − h).

  It draws three screens:
  - the ornithopter cockpit's window: 99% of pixels match, with a click resolving to the nearest marker within 9 px;
  - the flight minimap: (204,4)–(316,60) in a four-ring border, with the trail (ICONES 0x2F), destination (0x2E) and position (0x30). It recentres when the position leaves x 0xD6–0x131, y 0x0A–0x35. ORNYPAN's palette stays installed through the flight and colours it. It matches within 28 of 7,936 pixels;
  - the SEE SPICE DENSITY overlay.
- **The spice-density overlay** (floppy `sub_80B0`, CD `542f`). It matches in 99.98% of pixels.
  - The panel is ONMAP 0x8D (170 × 108) at (75,15), with the window at +(5,7), 160 × 89, centred on the map's position.
  - The window shows MAP2.HSQ's spice-field ids through build_spice_density_xlat: backdrop 0x70; a known place's field is 0x75, or 0x50 + density/16 once prospected.
  - A pixel shows only where it equals its right and lower neighbours (vga_draw_landscape), so the fields show outlines. The last row is backdrop.
  - Over it: the markers, Paul, and an 80 × 40 dotted box (colour 0xFB, pattern 0x5555, starting with a gap) round the view's centre.
  - The legend reads "SPICE DENSITY", "−", the shades 0x50–0x5F in 3 × 5 squares, and "+".
  - The row keeps its name and toggles the overlay; the panel's close box also closes it.
- **Save slots.** The original's Log 1 is DUNE21S1.SAV, Log 2 is S2, and the two "last entering" logs are S3 and S4. The engine used S0–S3.
- **Still to do:** the curved edges of the window near the poles (map_globe_edge_insets).

## The prospectors: the map lesson and MOVE TROOP

Built from the floppy disassembly (`sub_A5E5`, `sub_ACC0`, `sub_AD0A`, `sub_AE22`, `sub_9D05`, `sub_B05B`) and the CD decompile (`8064`–`84a6`). The lesson and the pick screen were compared with the speedrun video, which is the CD release. Spice86 draws this popup as noise, so there is no pixel capture of the original. `scripts/check_prospector_lesson.sh` replays it from the original's chapter 3 saves.

- **The lesson.** SPECIALIZE IN SPICE for the Carthag-Timin prospectors (troop 3) answers with a list-4 line whose action is 3. Below phase 0x14 that runs scene `0x12f8` (bytes `0e 10 ff`) over the troop popup, one step per " Continue...":
  - action 0x0E raises the density popup and speaks list 7's next line ("Here, take this map of the planet");
  - action 0x10 drops the popup and speaks the next line;
  - 0xFF gives the orders menu back.
- **The popup over a troop.** Over a troop's contact popup, the density popup is at ds:426C = (0x5C, 0x1E), and at (0x5C, 0x0E) when the contact popup is in the lower half. It is drawn after the contact panel, so the text keeps a two-line area (0x19 high).
  - Instead of Paul's ornithopter it shows the troop: ICONES 0x36 at $(x, y-h)$, where the troop stands or, while marching, where it is.
  - It shows the troop's route: dotted lines, colour 0x0C, pattern 0x5555, clipped to the window. They run from the troop's position through its destination, or through the prospectors' queue.
- **MOVE TROOP** (floppy `8d7a`, CD `8064`) keeps the contact popup and raises the density popup.
  - Captions: COMMAND 0x4A, "Show me where you want me to go..."; for the prospectors, the next one, "Show me 3 sietchs where you want me to go next...". The engine's command table is 0-based, so these are the ASM's 0x4A/0x4B minus one.
  - A tap picks the nearest marker within 9 px on the popup's window.
  - Any other troop goes straight to the done path. Its menu is only Cancel.
- **The prospectors' queue.** The queue is ARRAY_PTR_Location_prospector_destinations: CD ds:11D3, floppy ds:11E0. It holds three place offsets, and the fourth word, 0, ends the list.
  - MOVE TROOP copies the queue to a working copy (ds:4274, with its count at ds:4294).
  - The menu is ADD A DESTINATION (greyed unless the count is 1 or 2), GIVE NEW DESTINATIONS (empties the working copy), Done and Cancel. ADD itself does nothing; the next pick appends.
  - A pick must be a sietch (appearance < 0x20) or a place with status bit 3. A fourth pick starts the list over.
  - The third pick commits after a 0x32-tick beat.
- **The done path** (`8214`):
  - The prospectors store the working copy; an empty head cancels.
  - The acknowledgement is list 4, with ds:23 = 0x0B, or 0x10 when the troop is already there. It is spoken with the destination staged as the troop's place.
  - A line with event 2 drops the gate, and no march starts. For the prospectors at work, this is "As soon as we finish prospection here, we'll leave for …". Otherwise the order issues and the map's main menu returns.
- **Marching on.**
  - `troop_issue_move_order` sends the prospectors to their queue's head, first dropping heads already reached.
  - On arrival the head is dropped (`8357`).
  - When a place is prospected, or already was (status bit 6), the prospectors march to the next head (`9d5f`). With none left they stop and report.
- **Harness.** A `periods N` step passes N game periods. The clock is otherwise stopped in harness runs.

## GO & SEARCH FOR EQUIPMENT (2026-09-29)

Traced in the CD 3.7 executable (capstone on `DNCDPRG.EXE`; the dune-chani names) and in the floppy image (`DUNEPRG.unpacked.bin`, offsets as in the image). Built in `troops.cpp` (`searchedEquipment`, `searchEquipmentHere`, `equipmentSearchTarget`, `startEquipmentSearch`, the `troopArrive` branch) and `scene.cpp` (`searchForEquipment`, `troopReaction`, the class menus). Checked by `scripts/check_search_equipment.sh` and the fidelity scenario `search-equipment`.

- **The row.** It is not only the ecology troop's order: it heads all three class menus of CHANGE TROOP OCCUPATION (`menu_map_troop_dialog_change_troop_occupation`, CD `69b3`, floppy `774d`):
  - spice troop ds:216e (floppy 27d4): GO & SEARCH FOR EQUIPMENT, SPECIALIZE IN ARMY, SPECIALIZE IN ECOLOGY, Cancel;
  - army troop ds:2182 (floppy 27e8): GO & SEARCH, ESPIONAGE (greyed while ds:e2 >= 0x1e), SPECIALIZE IN SPICE, SPECIALIZE IN ECOLOGY, Cancel; on espionage ds:219a (ATTACK, Cancel) instead;
  - ecology troop ds:21a6 (floppy 280c): GO & SEARCH, ASSEMBLY WIND-TRAP, SPECIALIZE IN SPICE, SPECIALIZE IN ARMY, Cancel.
  - The first entry is greyed below phase 0x10 (`69f6`-`6a02`, floppy `7790`-`779c`), in every class; every SPECIALIZE IN ECOLOGY (entry 0x77) is greyed until Kynes is met, ds:0a bit 5 (`6a07`-`6a23`).
  - The engine's spice menu had "Spice Mining" / "Spice Prospecting" rows, which the original does not have; it now has the original's rows. The army menu's greying arguments were swapped (ESPIONAGE and ECOLOGY were never greyed); fixed.
- **What is looked for** (the three handlers, CD `776d` spice, `7734` army, `775c` ecology; floppy `84d1`, `8498`, `84c0`): the first item the class lacks, in this order. Item numbers are the equipment bit's position (troop byte 0x19, bit 7 = item 0):
  - spice: a harvester (0), then an orni (1);
  - army: krys knives (2), laser guns (3), weirding modules (4), atomics (5);
  - ecology: bulbs (6).
  - With all of them the troop answers with ds:23 = 0x0F, "I have all the equipment I need!" (list 4, condition 685), and nothing else happens.
  - The item's name goes to subst_id_0c (CD ds:1203 = 0xE8 + item, floppy ds:1210 = 0xDC + item: "a spice-harvester", "an orni", "some krys", "several laser-guns", "weirding modules", "atomics weapons", "some bulbs"), the placeholder 0x8C.
- **CD only: taken at home** (`77d7`, then `7d81`). The CD first looks at the troop's own place: if the free count there (`7f27`: the place's stock at +0x14..+0x1a less what its troops hold) reaches 1, or 2 for an orni at Paul's place (ds:1150), the troop simply takes it: its bit is set, ds:3d/3e/3f are staged as MODIFY EQUIPMENT's tail does (added, removed, 0x40 for an orni taken where Paul is), and it answers with ds:23 = 0x0C ("Glad to have some Krys knifes. My men will appreciate it.", "I'm sure that we'll do a better job with this harvester!"). The floppy has no such step (`84e4` goes straight to the search).
- **Where it goes** (`7f90`, floppy `8ca1`): the nearest place, over the location records from 0x138 (the two palaces are never searched) to the 0xFFFF end, that is:
  - known (status bit 7 clear), not a fortress (type < 0x28), not the troop's own place;
  - closer than 50: $d=\max(\lvert\Delta\mathrm{lng}\rvert \gg 8,\ \lvert\Delta\mathrm{lat}\rvert)$, compared by low bytes; a village (type >= 0x21) counts $d/4$;
  - with the item free there (`7f2a` into ds:4c60), less one for each troop already marching there for the same item (`8018`: moving, occupation bits 0-1 = 3, +04 the place, +0c not the place, +0e's low byte the item), and less one orni at Paul's place before phase 0x50 (`804c`);
  - ties keep the first place in table order.
  - None: ds:23 = 0x0E, "I don't think I can find some bulbs available in all of the places around here." (condition 684), no march.
- **The order** (`77a9`-`77c5`, floppy `8504`-`8520`):
  - `6a33` (floppy `77cd`) sets the occupation to class | 3 (3, 7 or 0x0B) through `troop_apply_occupation_choice` (`6a89`): the reaction line ds:23 = 0x0A ("OK!", condition 666; troop 5 before phase 0x2c refuses: "I don't want to move my troop. I was born here, you know.", 632; also the fortress-turning, sick and repairing refusals). A refusal takes the job back and stops here. Accepted in the army or ecology class, the troop drops its harvester bit (`6abf`).
  - Troop +0e = item | bit << 8, +0c = the place left, then `troop_issue_move_order` (`84a6`) to the target; then NO MORE ORDERS (`8770`): the map's main menu.
  - The prospectors (troop 3) are sent to their queue's head by `84a6` instead (`848f`), or nowhere when it is empty: an original quirk kept.
  - A new MOVE TROOP while searching clears bits 0-1 (`84b8`-`84c4`): the search is dropped.
- **The march** is the ordinary one (`8308`, `8604`): 7 sub-steps at the order, then 4 a period (8 with an orni), arrival when the gap is under 7 cells (the longitude gap in cell units of the row).
- **No motivation cost.** In `84a6` (floppy `91a2`) the defence check, the refusal to march into a place under attack and the motivation cost ($-3$, `6f93`) all sit behind `cmp byte [si+3], 6` (`84d2`, floppy `91ce`): they are for a troop whose occupation byte is exactly 6 (attacking). The engine applied the cost and the refusal to every march; fixed in `World::issueMoveOrder`. Confirmed on Spice86: troop 1's motivation stays 29 through the whole search.
- **At the place** (`8357` → `83a7` → `841f`, floppy `9053` → `90a3` → `911b`), when it is friendly and not under attack (otherwise it arrives as any troop and may fight):
  - the free count there (`7f27`; the arriving troop is not linked) decides: one left, the place's count drops by one, the troop's bit is set and +0f (the bit) is cleared;
  - either way +04 = +0c: the troop turns back at once, still marching, and the rest of that period's sub-steps are lost. It never stops at the searched place.
- **Home again** (`844d`, floppy `9149`): bits 0-1 are cleared, so the class's first job restarts (0 spice mining, 4 military training, 8 irrigation; `6ad4`), and the troop is linked and its items counted at its place as on any arrival (`83bc`).
- **What the troop says.** No message is queued on the return. Over the map (list 2, sentence mask 0x20):
  - going out, "My troop is going to - to search for some bulbs." (condition 504: bits 0-1 = 3 and ds:2c != ds:44);
  - coming back with it, "My troop is going back to - with some bulbs." (505: ds:2c == ds:44, ds:46's high byte 0);
  - coming back without, CD "My troop is going back to -. You asked us to find some krys. We didn't find any." (506), floppy "... We didn't find some krys available.".
  - ds:44/46 are troop +0c/+0e, staged by `31f6`, which also stages placeholder 0x83 = 0xE8 + +0e's low byte (the engine now stages it too).
- **Reaction lines repeat.** `troop_present_reaction_line` (`7bb9`/`7bbe`) presents through `96f1` → `9f8b`, which forces the sentence mask to 0x20; list 4's entries have no bit 5, so a said line is never skipped. The engine used mask 0x80 for list 4 (MOVE TROOP's acknowledgement and SPECIALIZE IN ...), so a second "OK!" never showed and the popup kept the previous line; all three now use 0x20.
- **The original, on Spice86** (a Spice86 capture of the original, floppy, from the chapter 12 saves: day 3, phase 0x15; the fidelity scenario `search-equipment` with `tests/fidelity/saves/day3-ch12`):
  - Carthag-Tuek's troop 1 (spice mining, no equipment), GIVE ORDERS TO TROOP, CHANGE TROOP OCCUPATION: the menu is GO & SEARCH FOR EQUIPMENT, SPECIALIZE IN ARMY, SPECIALIZE IN ECOLOGY (greyed, Kynes not met), Cancel.
  - GO & SEARCH at game time 0x25: occupation 0x43, +04 = place 15 (troop 5's sietch, 12 cells east, one free harvester), +0c = 0x250 (Carthag-Tuek), +0e = 0x8000; the map's main menu returns, with GIVE ORDERS TO TROOP greyed (no troop left where Paul is).
  - Home by game time 0x2b (about 6 periods): occupation 0, equipment 0x80; place 15's harvester count 1 → 0, Carthag-Tuek's 0 → 1; motivation 29 throughout.
  - The engine's run of the same script takes the same place and item and comes back with the harvester (`Troops:` lines). The scenario scores 77.6 %; the losses are the outdoor palette at the palace front and the sietch exterior (58 % and 12 %, unrelated to this order) and the map's icons (88 %: the original draws the marching troop's icon).
- **Engine decision:** ds:1150 (the place Paul is at or last left) is not kept by the engine; `currentLocation()` (ds:7) stands for it in the orni rules (`77e6`, `804c`).
- **Not done:** the same harvester drop (`6abf`) on a plain SPECIALIZE IN ARMY / ECOLOGY is still not transcribed in the engine's `kRowSetOccupation`.

## The smugglers' trade (2026-09-29)

Traced on 2026-09-29 in the CD 3.7 executable (OpenRakis' `asm/cd/DNCDPRG_RECENT.ASM`, `sub_1xxxx` = seg000:xxxx) and in the floppy image (`notes/temp/dune_scummvm_engine_20260916/results/DUNEPRG.unpacked.bin`, capstone 16-bit, offsets as in the image). The dialogue data was read with `scripts/dune_dialogue_dump.py` (CD `data/DUNE.DAT`; floppy `data/floppy/*.HSQ`). The trade was captured on the original floppy under Spice86.

### Addresses

In this block the floppy code is the CD's moved by +0x318. The data segment is the same up to ds:1156. From ds:1158 the floppy's block is two bytes shorter, and from ds:11c0 it is thirteen bytes longer (`World::ds`).

| What | CD | Floppy |
| --- | --- | --- |
| Stage the village's smugglers (`init_room_persons` → at a place of appearance 0x21) | `2318` | `2630` |
| Copy a record's fields for the conditions | `235f` | `2677` |
| Smuggler event 8: phase 0x3c, the roll, the date, the next offer | `2388` (the offer `23a5`-`23d4`) | `26a0` (`26bd`-`26ec`) |
| ARGUE's price cut | `23d5` | `26ed` |
| The purchase | `23e6` | `26fe` |
| Smuggler event 9 (a bare `retn`) | `2419` | `2731` |
| Menu ACCEPT / REFUSE / ARGUE | `241a` / `2432` / `2453` | `2732` / `274a` / `276b` |
| Common tail: ds:9f, ds:1a + 1, the walk goes on | `2496` → `loc_19472` | `27ae` → `9f3f` |
| Duncan's event 8 (offers, then the bill to present) | `2239` (bill pick `2253`) | `2551` (`256b`) |
| Duncan's event 9 (agreed offer, or the bill paid) | `24ee` (pay `2517`, flags `252d`/`2541`) | `2806` (`2829`, `283f`/`2853`) |
| Dialogue actions 4 / 5 (the bargaining menu; party ds:476d = 0 / 1) | `a244` / `a248` | `aa2d` / `aa31` |
| The talk walk: at the end of the lists, speaker 13 starts again | `194b9` | `9f83` |
| New day: stock refill | `1ca5`-`1cd9` (in `sub_11C46`) | `1fe9`-`201c` |

| Variable | CD | Floppy |
| --- | --- | --- |
| The six smuggler records, 17 bytes each, 0xff after the last | ds:10d8 | ds:10d8 |
| The current record | ds:10b4 | ds:10b4 |
| The number of goods on sale | ds:1141 | ds:1141 |
| The item's word (placeholder 0x83, subst_id_03) | ds:11f1 = 0xE8 + item | ds:11fe = 0xDC + item |
| The region's word (Duncan's bill line) | ds:11f7 | ds:1204 |
| Spice spent today | ds:1172 | ds:1170 |
| Stock at the start of today | ds:1170 | ds:116e |
| The place where Paul is | ds:114e | ds:114e |
| The speaker | ds:47c4 | ds:431b |
| The bargaining party (0 Duncan's own offer, 1 a smuggler's bill) | ds:476d | ds:42c9 |
| The walk's sentence mask | ds:47c2 | ds:4319 |

#### The record (ds:10d8 + 17·k)

| Byte | Meaning |
| --- | --- |
| +00 | the village's first name (the place's byte 0; all six villages are "-Pyons", place byte 1 = 0x10) |
| +01 | haggling tolerance (0: "I never haggle") |
| +02 | flags: 0x08 visited once; 0x20 bill argued, 0x40 bill refused (Duncan's menu); both cleared by a new purchase |
| +03 | the day of the last offer (event 8) |
| +04..+08 | stock of goods 0-4: harvester, orni, krys knives, laser guns, weirding modules |
| +09..+0D | the price of goods 0-4: $(b \mathbin{\&} 0x7F) \times 2$ batches of 10 kg; bit 7 = restocked when sold out |
| +0E | the unpaid bill (word, batches of 10 kg) |
| +10 | the day of the last purchase |

The initial records (identical in every floppy dump before any trade; region numbers from the place's first name):

| Record | Region | +01 | Stock H O K L W | Price in kg (R = restocked) |
| --- | --- | --- | --- | --- |
| 10d8 | 1 | 0 | 1 2 0 2 2 | 600 R, 1500 R, 200, 800 R, 2500 R |
| 10e9 | 3 (Tuono) | 1 | 1 2 0 2 1 | 600 R, 1500 R, 200, 800 R, 2000 R |
| 10fa | 5 (Oxtyn) | 3 | 1 1 0 0 1 | 1000 R, 1980 R, 200, 800, 2000 R |
| 110b | 6 | 2 | 0 2 3 2 2 | 800, 1600 R, 300 R, 1000 R, 2500 R |
| 111c | 9 | 3 | 2 1 0 0 1 | 1000 R, 1600 R, 200, 800, 2200 R |
| 112d | 11 | 6 | 1 1 2 1 0 | 1200 R, 1800 R, 200 R, 800 R, 2000 |

ds:1141 is 3 at the start: only harvesters, ornis and krys knives are on sale. The Duke's death (phase 0x4c, `sub_11166`) adds laser guns, and phase 0x5c (`sub_111b3`) adds weirding modules. The engine already does both increments (`world.cpp`).

### The rules

```text
stage (2318), on every room draw at a place of appearance 0x21 (init_room_persons 3166):
    rec = the record whose +00 == place byte 0;  ds:10b4 = rec
    ds:1c = rec+02; ds:20 = bill; ds:1f = bill ? today - rec+10 : 0; ds:1d = rec+01
    ds:1e = rec+02 & 8 ? today - rec+03 : 1;  rec+02 |= 8
    ds:9d = 0 (no offer);  ds:9f = 0
    item word = base + ((ds:0 & 7) mod ds:1141)        # ds:0 = the rolling random word

smuggler event 8 (2388), "Well, let me see if I have something for you...":
    phase 0x3c;  ds:9e = rand & 3;  rec+03 = today;  ds:1a = 0
    i = item - base; up to two wraps: i = i + 1 (mod ds:1141) until rec+04+i != 0
    if found: item word = base + i;  ds:9d = (rec+09+i & 0x7f) * 2      # else ds:9d stays 0

menu (speaker 13; any other speaker just sets ds:9f = 1/2/3):
    ACCEPT: rec+02 &= 0x9f;  p = ds:9d, ds:9d = 0;  ds:20 += p;  bill += p
            if bill == p: ds:22 += 1 (one more smuggler with a bill)
            rec+10 = today;  rec+04+i -= 1;  (ds:114e)+0x14+i += 1     # into the village's own stock
            ds:9f = 1
    REFUSE: if rand & 7 == 0: ds:9e |= 0x10, ds:9f = 3                 # "Eh! ... losing money!"
            else ds:9d = 0, ds:9f = 2                                 # he looks for something else
    ARGUE:  r = rand & 3
            if r == 0: ds:9e |= 0x10                                  # "Eh! ... losing money!", same price
            else: ds:9e = (ds:9e + 1) & 3
                  if (r & 1) + ds:1a - rec+01 >= 0: ds:9d = 0         # "Forget it!" (or "I never haggle")
                  else: ds:9d -= ds:9d >> 3                           # the price cut
            ds:9f = 3
    then ds:1a += 1 and the talk goes on after the line that opened the menu.

new day (1ca5): r = one timer-random word
    for each record while +00 < 0x14, if visited (+02 & 8):
        for i = 4 .. 0: if stock[i] == 0 and price[i] & 0x80: r = rol(r, 2); stock[i] = r & 3

Duncan (character 3, list 2), event 8 (2239): the offers, then the bill:
    ds:20 = 0; ds:9f = stock ? 3 : 0; ds:1a = 0
    rec = the record with a bill and no 0x60 flag whose purchase is the oldest (today - +10 largest),
          else the next record after ds:113f that has a bill (ds:113f = it); none: no bill
    stage rec (235f); region word ds:11f7 = rec+00
Duncan event 9 (24ee), after his answer line:
    party 0: ACCEPT/0 = the Emperor's offer agreed (engine duncanAccept)
    party 1: choice < 2: stock ds:a0 -= bill, spent today += bill, bill = 0, ds:22 -= 1
             choice 2:   rec+02 = rec+02 & 0x9f | 0x40      (refused)
             choice 3:   rec+02 = rec+02 & 0x9f | 0x20      (argued: "you have more important things")
```

The price cut is $p \leftarrow p - \lfloor p/8 \rfloor$. From 600 kg: 530, 470, 420 kg.

With tolerance $t$ (+01) and $a$ menu choices already made in this offer (ds:1a), an ARGUE that is not the 1-in-4 "Eh!" lowers the price only while $(r \mathbin{\&} 1) + a < t$. A tolerance-1 smuggler therefore cuts at most once, on the first ARGUE with an even roll. A tolerance-0 smuggler never cuts: every ARGUE ends the talk with "I never haggle over the price" (condition 454 CD / 451 floppy, action 6).

Payment is never taken at the village. Every purchase is on credit. The bill is paid only through Duncan, from the palace stock.

#### The talk walk

When the lists run out, the walk (`194b9`) starts again, but only for speaker 13. It goes back to his list 0 with the sentence mask 0x20 (`ds:47c2`). His first line (flags 0x20) then stays said, while all his other lines are repeatable. So the talk runs until a line with action 6 ends it. Otherwise it keeps coming back to "let me see" (`ds:9d == 0`), the offer (`ds:1a == 0`), a price line (`ds:9e` 0-3) or "Eh!" (`ds:9e & 0x10`). After a menu choice the walk goes on after the line that opened the menu (`loc_19472`: `ds:47ba`, the saved position).

The item's name comes from placeholder 0x83 (subst_id_03, the same word a searching troop's lines use). On the floppy the price is printed by `0x91 0x9d` "0 kgs" (byte ds:9d, then a literal 0). The CD lines carry no price: none of the CD code reads ds:9d except the conditions.

#### Smuggler lines (character 13, list 2; CD condition, floppy condition)

| # | Condition | Action | Line (CD / floppy) |
| --- | --- | --- | --- |
| 1 | 1 / 1, once (flag 0x20) | – | "Here you are, the best place on Dune to buy all kinds of equipment..." |
| 2 | 440 / 437: at Oxtyn-Pyons (ds:4e = 0x510), phase > 0x47 | – | "another village on the Fish's Tail" |
| 3 | 441 / 438: bill, 1-2 days old | 6 end | "I prefer that you pay my last bill before I deal with you again." |
| 4 | 442 / 439: bill, 3+ days old | – | "I don't want to do business with you..." |
| 5 | 443 / 440: the same, ds:e2 < 0x32 | – | "...no problems getting paid by the Harkonnens." |
| 6 | 442 / 439 | 6 end | "Don't waste my time!" (so line 7 is never reached) |
| 7 | 444 / 441: the same, ds:1e < 5 | – | "...I will give you another chance." |
| 8 | 445 / 442: ds:19 = 0, visited today | – | "It seems I see you very often, these days." |
| 9 | 446 / 443: ds:19 = 0, 6+ days | – | "Good to see you, it's been many days." |
| 10 | 447 / 444: ds:9d = 0 | 8 | "Well, let me see if I have something for you." |
| 11 | 447 / 444 | 6 end | "I have nothing to trade at the moment..." |
| 12 | 448 / 445: offer, ds:1a = 0 | 4 menu | "I have [item] for trade. (floppy: You can have it for N kgs of spice.)" |
| 13-16 | 449-452 / 446-449: ds:9e = 0..3 | 4 menu | the four price lines |
| 17 | 453 / 450: ds:9e & 0x10 | 4 menu | "Eh! [item] of the highest quality! At this price, I'm losing money!" |
| 18 | 454 / 451: ARGUE, tolerance 0 | 6 end | "I never haggle over the price." |
| 19-21 | 455-457 / 452-454: ACCEPT, ds:1a < 2, < 4, else | 6 end | "You've got a deal..." / "OK! So, it's always possible..." / "You're a tough guy..." + "It's yours now. Send someone to pick it up..." |
| 22 | 458 / 455: ARGUE with ds:9d = 0 | 8 | "Forget it! Do I have something else for you?" |

Duncan's bill lines (character 3, list 2; CD 198-206, floppy the same numbers):

- "Ah, one moment, I have received some kind of bill" (ds:22, action 8 → `2239`).
- "A smuggler from the [region] region is claiming a payment of N kgs" (flags & 0x60 = 0). Variants: 0x20 "I've already told you about it...", 0x40 "are you still refusing to pay...".
- "we only have N kgs in stock... We can't pay him right now." (stock < bill).
- "Do you want me to send N kgs to this smuggler?" (bill ≤ stock, action 5: the menu with party 1).
- "OK that'll be done." / "I hope he'll wait." / "I understand that you have more important things to think about now." (ds:9f = 1/2/3, action 9 → `24ee`).
- Also Duncan's "I've been told about another village in the east, south of [place]" (condition 169) needs ds:22 = 0.

#### Moving between villages and days

The smugglers never move: each record belongs to one village for the whole game (+00). What changes over the days is:

- the stock, refilled on each new day for goods marked with price bit 7 that are sold out, once the village has been visited;
- the goods on sale (ds:1141, at phases 0x4c and 0x5c);
- the lines, through the bill's age (ds:1f) and the days since the last offer (ds:1e).

The item offered first cycles from a random start (ds:0 at the room draw), and each new offer takes the next item in stock.

### The original, captured (floppy, Spice86)

the maintainer's Spice86 captures of the original. `dsx.py` prints the trade variables from a checkpoint's `.ds.bin`.

- **`run1-village`** (`village-save.script`): day-route-v1 chapter 15's Log 1 (Ergsun-Timin, day 4, phase 0x2c), the flight to Tuono-Pyons, saved in Log 2 → `saves-village/`.
- **`run-e`** (`trade-e.script`), from `saves-village` Log 2, day 5 slot 0. Stock 22 (220 kg). Record 10e9, tolerance 1, ds:1141 = 3.
  - **Offer.** "let me see" makes the offer: harvester, ds:9d = 60 (600 kg), ds:9e = 3. Then "I have a spice-harvester for trade. You can have it for 600 kgs of spice."
  - **ARGUE → "Forget it!"** ds:9d = 0, so the next offer is picked in the same line: orni, 150 (1500 kg).
  - **ARGUE on the orni → "Forget it!"** The harvester is offered again, ds:9e = 0.
  - **ARGUE on the harvester → the cut.** "Hmm... I don't like to haggle over the price but I'll cut the price down to 530 kgs of spice": ds:9d = 53, ds:9e = 1, ds:1a = 1.
  - **ACCEPT.** "Ok! So... it's always possible to come to an agreement" (ds:1a = 2), then "It's yours now...". The record becomes `03 01 08 04 00 02 00 02 01 9e cb 0a a8 e4 35 00 04`:
    - the harvester stock is 0;
    - the bill is 0x35 (53);
    - the purchase day is 4.
  - **After ACCEPT.** ds:20 = 53 and ds:22 = 1. Tuono-Pyons' place +0x14 (harvesters) goes 0 → 1. The stock ds:a0 is unchanged (22).
  - **STOP TALKING.** The room redraw restages the record: ds:1e = 0, ds:9d = 0 and ds:9f = 0.
  - Saved in Log 1 → `saves-after-trade/`.
- **`run3-accept`** (`trade-b.script`): ARGUE → "Forget it!" → orni 1500 kg → ACCEPT. Bill 150, the orni count 1 → 2. A view click after the talk ended starts a new talk: "let me see" → the harvester again, the same day, with a bill open (ds:1f = 0).
- **`revisit-50` / `revisit-70`** (`revisit.script`): `saves-after-trade` Log 1 with the game time **patched** (`dune_save_patch.py --set 2=50,00` / `70,00`):
  - day 6 (ds:1f = 1): "Well... I prefer that you pay my last bill before I deal with you again." (end);
  - day 8 (ds:1f = 3): "I don't want to do business with you...", "I'm sorry... getting paid by the Harkonnens..." (ds:e2 = 0x2e), "Don't waste my time!" (end).
- **Duncan, `probe-duncan-bill` and `duncan-accept` / `-refuse` / `-argue`** (`duncan-*.script`): day-route-v1 chapter 19's Log 1 (COMM room, day 6, stock 41), **patched**:
  - Duncan moved to the COMM room (ds:1008 = 8);
  - record 10e9 given a bill of 53 dated day 4, ds:22 = 1;
  - for the menu runs, the stock set to 60 (`saves-duncan-bill-paid`).
  - The run in the COMM room:
    - Others... → Duncan: his stock and Emperor lines. REFUSE the Emperor offer, then "Ah... one moment... bill" (event 8: ds:20 = 53, ds:1f = 1, region word 3).
    - "A smuggler from the Tuono region is claiming a payment of 530 kgs of spice."
    - Stock 41: "the problem is that we only have 410 kgs in stock. And he wants 530 kgs. We can't pay him right now.", then "I understand that you have more important things..." (ds:9f = 3 from `2239`, party 0: no change).
    - Stock 60: "Our stocks of spice are currently 600 kgs. Do you want me to send 530 kgs to this smuggler?", the menu with party ds:42c9 = 1. Then:
      - ACCEPT: stock 60 → 7, ds:1170 (floppy spent today) 0 → 53, bill 0, ds:22 0;
      - REFUSE: record flags 0x08 → 0x48;
      - ARGUE: 0x08 → 0x28.
- Not captured: the CD (no CD save near a village) and the day-change refill (a village can't wait for a new day).

### Built (2026-09-29)

- **`world.cpp`**:
  - `stageSmugglers` also clears the offer and the choice (ds:9d, ds:9f) and sets the first item word from ds:0.
  - The new day refills the goods (`smugglerRestock`).
  - The new day computes today's production as the original does, with the spice spent today (ds:1172). It clears that counter and keeps yesterday's stock through `word()`/`setWord()`. The floppy's yesterday's stock had been written to its spent-today word.
- **`story.cpp`**: `smugglerOffer` (`23a5`), `smugglerChoice` (`241a`/`2432`/`2453`/`2496`) and `smugglerBuy` (`23e6`); `duncanAccept(smugglerBill)` pays, refuses or puts off the bill (`2517`-`2541`).
- **`scene.cpp` / `story_scene.cpp`**:
  - speaker 13's event 8 makes the offer;
  - the bargaining menu with speaker 13 goes to `smugglerChoice`;
  - Duncan's event 9 passes the party (`Conversation::bargainParty`);
  - STOP TALKING restages a village's smugglers (`stageVillageSmugglers`, as the original's room redraw does).
  - The item words' base is the COMMAND id of "a spice-harvester" (`World::setItemWords`).
- **`dialogue.cpp`**: `findEntry` starts speaker 13's talk again at list 0 with the mask 0x20 (`194b9`), once per search.
- **`speedrun.cpp`**: the bot stops talking at a smuggler's offer: a REFUSE now only brings the next offer. The route buys nothing. Buying harvesters for spice output was not added; the bill would have to be paid from the stock the Emperor wants.
- **Checks.**
  - `scripts/check_smugglers.sh` runs `dune_story_setup=smugglers` on the floppy and the CD. The steps: the offer, ARGUE (forget it / cut / "Eh!"), ACCEPT (the village's count +1, bill = price), the next day's "pay my last bill", Duncan paying the bill, then a new offer.
  - The fidelity scenario `smugglers-trade` (`tests/fidelity/smugglers-trade.script`, saves `tests/fidelity/saves/smugglers-village`, the original `captures/smugglers/run-e`). It scores low:
    - the engine's first item and rolls come from other random words;
    - it was 38.2 % while the engine showed the menu one click late; with the menu fixed (below) it is 44.4 %. All clicks now stay in step with the original up to ACCEPT, and those checkpoints score 50-54 %. The cap is the village exterior itself: 51 % before any talk. After ACCEPT the rolls part ways.
- **The bargaining menu comes with its line** (fixed 2026-09-29, every action 4/5 question: the smugglers, Duncan's shipment and bill, Stilgar's Water of Life and final attack).
  - In the original, `sub_1A03F` runs a line's action when its last segment is drawn (after `sub_188D2`; table cs:A107). Actions 4/5 (`a244`/`a248`, floppy `aa2d`/`aa31`) then open menu ds:1ffe (`sub_1D323`) at once, so ARGUE / ACCEPT / REFUSE appear with the question and the line stays up.
  - A view click at the menu changes nothing (Spice86: `captures/menu-click`, the smuggler's offer and Duncan's offer, two clicks each: only the cursor and the mouth move).
  - The engine opened the menu one click later, over the room. `advanceConversation` now raises the menu (`_talkBargain`) with the page that commits the action, the balloon's last part. A view click at the menu does nothing, and nothing but the answer moves the talk on.
  - Goldens replaced after comparison with the original's `captures/chapters/day-route-v1/ch12/c12-04-offer` (Duncan's offer with the menu) and `captures/menu-click/duncan`: `story-comm/story-duncan-offer`, `story-comm/story-bargain`.
- **Not done:** the CD's lines were not captured on the original.

## The end-game paths (2026-09-29)

Queue item 3, traced in the CD 3.7 executable (capstone on `DNCDPRG.EXE`, `scripts/dune_disasm.py`; the dune-chani names) and in the floppy image (`DUNEPRG.unpacked.bin`, offsets as in the image), with the dialogue data (`scripts/dune_dialogue_dump.py`). Checked on Spice86 where a save could reach it (a patched save, `scripts/dune_save_patch.py`).

### The Water of Life

- **The offer.** Stilgar's list 1 in a sietch's room 4 (the reservoir): condition 299 (CD numbering; the floppy's is one lower) `current_scene < 0x20 & room == 4 & (ds:0a & 2) == 0` asks "The Water of Life extends consciousness ... Do you want to try?" with action 4, so ARGUE / ACCEPT / REFUSE come up with the line (ds:9f = 3 / 1 / 2). Condition 300 (`ds:9f > 1`, ARGUE or REFUSE) answers "You are wise..."; 301 (`ds:9f == 1`) "Your decision frightens me... Drink it if that's your will." Both answers carry event 8.
- **Event 8** (`callback_event_dialogue_line_08_Stilgar_drink_Water_of_Life`, CD `seg000:2ccf`, floppy `2f96`):
  - always sets ds:0a bit 3 ("has heard of it"; Jessica's lines 92-94 read it);
  - with ds:9f != 1 that is all;
  - with ACCEPT it waits for the voice (CD `1abcc`/`1abd5`) or 600 ticks (`0x258`, floppy only);
  - charisma (ds:29) below 100: pending_room_screen_request = 3 (CD ds:46d9, floppy ds:4235), the ending "Paul Atreides died as he tried to drink the Water of Life";
  - 100 or more: ds:0a bit 1 (has drunk), ds:d5 = 0xff, transition 0x38, 1000 ticks, transition 0x36 (on Spice86 the screen is black), three periods pass (`0fd9`, cx = 3), ds:23 = 0x11 and the room scan (`jmp 35ad`). The scan finds Stilgar's topic-4 line of condition 344 (`ds:23 == 0x11`): "Ah, he is coming to. You gave us such a fright, Muad'Dib! You've been unconscious for three hours." (floppy: "Aaah, here he goes again! ... unconscious three hours."). The talk then goes on where the answer left it: "I don't know what the Water of Life has done to you..." (condition 62, `ds:0a & 2`).
- **What it gives.** ds:d5 (`contact_distance_related_ds_d5`) above 0x80 opens Jessica's lines 76-78 ("Believe me Paul! I can sense that your powers have increased."), whose event 8 is her lesson (CD `a186`, floppy `a967`). With ds:0a bit 1 the lesson gives charisma + 40 (through `6f78`, the troops' motivation moving with it) and the contact range (ds:1176) 0xffce + 0x14 = 0xffe2: the whole planet ("You are now able to contact Fremens on the entire planet."). ds:d5 becomes 0 (range >= 100).
- **ds:d5 on a new day** (CD `1c62`, floppy `1fa5`, before the stage-7 gate): `d5 + 1` is stored when it is 2 or more, so 0 stays 0, 0xff stays 0xff, and a lesson's 0x80 - range / 6 climbs a step a day until Jessica offers the next lesson. The engine never grew it, so Jessica never offered a second lesson.
- **Engine changes.** `World::runPeriod` grows ds:d5 on the new day; `raiseContactRange` uses `changeCharisma` (6f78); Stilgar's event 8 with drink 1 arms `_pendingWakeUp`: after the line's 600 ticks (at once in capture runs) `GameScreen::waterOfLifeWakeUp` blacks the screen (5 s in real play), shows the room, sets ds:23 = 0x11 and runs the room scan (`roomEntryScan(true)`, which capture runs otherwise skip); after the wake-up line the talk goes on from the interrupted walk (`Conversation::position` / `resumeAt`). Closing the talk before the wake-up runs it first.
- **Not the same yet.** The transitions 0x38 / 0x36 are a plain black screen. (ARGUE / ACCEPT / REFUSE now come with the question line, as in the original: see "The smugglers' trade".)
- **Checks.** `scripts/check_water_of_life.sh` (dune_story_setup=water-of-life: REFUSE, ACCEPT at charisma 120 with Jessica's lesson after it, ACCEPT at 50) and the fidelity scenario `water-of-life` (a patched chapter 20 save, original captured in captures/endgame/water-of-life: ds:0a 0x51 -> 0x5b, ds:d5 0x7e -> 0xff, game time 88 -> 91).

### The ecology win

- **No separate ending.** The only ending is phase 0xc8 (`game_phase_set_to_c8_game_ending`, CD `16fc`, floppy `1aa4`), reached by entering the Harkonnen palace's room 2, the Baron's hall (location_and_room 0x3002, CD `4072`, floppy `42cc`). The other endings are deaths (pending_room_screen_request 3 the Water of Life, 4 shot on arrival, 6 a lost battle, 7 the Emperor's patience). Greening is a second way to the same place.
- **What the vegetation takes.** The daily disc of an irrigated sietch (`6515`/`653a`, floppy `72b5`/`72da`) passes over place cells (map bit 0x40): the place's spice density (+0x12) becomes 0, and a place that is not Atreides (`5d36`: type >= 0x28 without status bit 3) loses its hidden bit and goes through the fortress-won routine itself (CD `7443`, floppy `81a7`), the same as a won battle: the land round it Atreides (radius 5), held (status bit 3) until the day after tomorrow (+0x0b), charisma + 4 (`6f78`), every troop's motivation + 1 (`6f56`), the hired troops back from the battle (`75af`), its Harkonnens freed as Fremen up to slot 8 (`75ea`) or gone, perhaps one captive raider (`762a`).
- **The release difference.** The CD skips locations 0 and 1 (`6582: cmp di, 138h; jb`), so its vegetation never takes the Harkonnen palace. The floppy has no such test (`7313`-`7326`): the vegetation takes the Harkonnen palace like a fort. Its garrison is freed or leaves, it is held, Paul lands safely (`503c` finds no Harkonnen), and walking into room 2 ends the game, with no final attack. The palace stays type 0x30 (the conversion `6e20` runs only for a troop stationed there), so room 2 stays the Baron's hall. So on the floppy the wiki's "vegetation reaching the palace makes them abandon it" is true, and the ending follows at once; on the CD it is not.
- **The final attack's start** (`7493`-`74ac`, floppy `81f7`-`8211`, after both battles and vegetation): `accumulate_harkonnen_spice_production` (`1cda`) also counts the places not Atreides (dl); with at most one left, ds:c2 is SET to 1 whatever it was, Jessica's and Thufir's record byte 0x0f bit 1 ("in the palace") goes (ds:ff7, ds:1007), and `765e` adds the planet's atomics beyond ten (the sum of every place's byte 0x19, in a byte) to this place. So the last fort, by battle or by vegetation, starts the chain Thufir -> council -> 10 000 men -> Stilgar (see "Troops at war"): Thufir's "We're almost ready for the final attack." (condition 127, ds:c2 == 1). ds:c2 != 0 stops the Emperor's demands (`20ae`) and the shipment reminders (`1df0`).
- **The final battle's conditions**, against the wiki: 1000 or more (x 10 men) in hired troops of occupation 4 (military training) with the atomics bit (troop byte 0x19 bit 2) at locations 2-4 (`1243`/`1258`/`1269`), counted on any Thufir line at stage 4 (`9f48`); the council needs Jessica, Thufir, Gurney, Stilgar and Chani in the room (`w[0x12] & 0xb6 == 0xb6`, condition 131). No skill level and no other weapon is tested.
- **What else the greening does:** it kills the spice under it (a place's density 0; `1cda`'s Harkonnen production sums density / 8 over the places not Atreides), and sprouting cells turn into tufts (`65b6`). Charisma + 4 and motivation + 1 come with each place taken.
- **Engine changes.** `World::fortressWon` is the one routine for battles and the vegetation (the separate `fortressTaken` is gone: it skipped `75af`, `762a`, used `addCharisma` and kept Harkonnens past slot 8); `spreadVegetation` takes the Harkonnen palace on the floppy (`World::floppy()`, which includes the Amiga: not checked there); `battleWonTail` sets ds:c2 = 1 unconditionally (the engine had `!ds:c2`) and runs `gatherAtomics` (`765e`); `battleWon` is `7429` (message 7 is now sender 0x0f, 0x0f07, as `71b2` sends it; the palace's message 0x0f0a too, and a chief's message is delivered in person only at its place, `2ad8`).
- **Checks.** `scripts/check_ecology_win.sh` (dune_story_setup=ecology-win: the last fort falls to a disc, stage 0 -> 1, Thufir's line, no demand; then the palace: floppy taken and the Baron's hall ends the game, CD spared and Paul shot). No original capture: the discs take days of irrigation to reach a place, and a patched disc is only picked up by the irrigation job's own step.

### Playing on after the final battle

- **The normal end.** Stilgar's ACCEPT (action 9, `2d2c`, floppy `2fec`) makes ds:c2 6 and marches the atomics troops at locations 2-4. An attacking troop's period callback (`739e`, floppy `8102`) at the palace (`cmp di, 11ch`) goes to `73a9` (floppy `810d`), with no roll: ds:c2 + 1 (7), the hired troops there train, the Harkonnens there are removed, every Harkonnen cell (0x30) turns Atreides, and message 0x0f0a "The shield is down, the Harkonnen troops here have surrendered!" (a chief tells it at the palace, else a dream). From stage 7 no troop, vegetation, Chani cure or time-of-day event runs (`1b5e`, floppy `1ea1`); the new-day hook still does. The palace is not held, keeps type 0x30, never becomes a sietch; room 2 ends the game.
- **The wiki's trick.** Paul arrives while the palace is in battle (`503c`: status bit 1 or attacking Fremen there; ds:2b = 1) and chooses MASSIVE ATTACK (`7317`, floppy `807b`). Its rounds call `7419` / `7516` directly, not `739e`, so a won battle goes `7429` -> the fortress path `7443`: the palace is held until day + 2, and with no Harkonnen place left ds:c2 is set back to 1 (checked on Spice86: 6 -> 1, status 0x0a, byte 0x0b = 2, the troops at occupation 4, charisma 4 -> 8). The world goes on. Two days later a troop there converts it (`6e20`: type & 7 = 0, ds:27 + 1, `6dbb` moves the characters whose record says (0x80, palace + 1) to the new type: the Baron, Feyd-Rautha and the Emperor, characters 9-11, whose records start at (room 2, 0x30, 0x80, 2)). Room 2 is then 0x0002, not 0x3002: no ending.
- **The prisoners.** In room 2 they talk with their lists 3/4 (ds:24 is the COMM message's place, so their message lines do not come up): "I've nothing to say." / "I've nothing to tell you."; COME WITH ME gets their list 5 "Who do you think you are?" (action 2: they refuse).
- **Kynes as a companion.** His list 5 has condition 94 (`ds:c2 != 0`) "OK!" before his refusal ("All of my life has been devoted to this old Fremen dream..."), so he follows from the last fort on, not only after the battle.
- **The rest.** ds:c2 = 1 again means no demand from the Emperor (`20ae`) and Thufir back at "We're almost ready for the final attack." (a new COME WITH ME to him would raise it to 2). A fighting troop left in occupation 6 at the palace (the prospectors, whom `75af` skips) would still hit `73a9` later: ds:c2 + 1 and the shield message, but not the freeze unless it reaches 7.
- **Engine changes.** `battleWonTail` (above) makes ds:c2 1 after the palace's MASSIVE ATTACK; `palaceFalls` sets occupation 4 on the hired troops only and removes only unhired Harkonnens (`7399`, `764d`); `runPeriod` skips the vegetation, the period's actions and the place staging at stage 7 as well; ds:2b is kept with `_battle` (written on arrival and when the battle ends, read back after a load), so a save made in a battle offers MASSIVE ATTACK after loading, as in the original. The palace-as-sietch, its prisoners and Kynes needed no new code: they follow from the data once the palace converts.
- **Checks.** `scripts/check_endless_play.sh` (dune_story_setup=final-battle: stage 7, the world stops, the Baron's hall ends it; =endless-play: MASSIVE ATTACK, stage 6 -> 1, a sietch two days later, room 2 no ending, the prisoners, Kynes, no demand) and the fidelity scenario `endless-play` (an engine-written save, captured in captures/endgame/endless-play; the data match, the pictures do not: the original shows the night battle, which the engine does not draw).
- **Not built / seen on the way.** The night battle view (known gap). On the floppy, Kynes's topic-5 "OK!" shows as "Stop it, now!" and his other lines look like other characters' (sentence 110 / 295): the floppy's phrase-file split for characters from 6 on may be off; to check.

## The palace plan

The red dot in the compass box of the Atreides palace opens a floor plan of the palace over the view (`ui_draw_palace_plan`, CD `seg000:18ee`, `sub_118EE` in `DNCDPRG_RECENT.ASM`). It works from day 1 and needs no story event. It was checked against the original floppy on Spice86: `captures/palace-plan` and `captures/palace-plan-people`, and the fidelity scenarios `palace-plan` and `palace-plan-people`.

- **The hotspot** is UI element ds:1CBC of the room navigation panel: rect (269,173)–(280,181), sprite 0x24 (the dot), handler 0x18EE. It only works in the Atreides palace (ds:4 high byte 0x20), and not in its first room, where the dot isn't drawn. The engine checks it before the arrows, whose widened touch zones reach over it.
- **Toggling.** The handler closes the plan when its own menu is up (bp = 0x2012), so a second click on the dot closes it. So does the plan's only row, Done (menu ds:2012). The engine also closes it when the room changes.
- **The window** is ds:143C, (160,0)–(320,116), filled with colour 0xF1. Four rings frame it (`loc_15B6E`): ds:1444 = (164,4)–(316,112) grown by one pixel per ring, in colours 0xF7, 0xF5, 0xF3, 0xF1. Then come `PALPLAN.HSQ`'s frames from the list at ds:120B, made of (frame, x, y) words up to 0xFFFF: the plan (frame 0 at 182,12) and frames 3, 4 and 5 at (266,65), (238,65) and (193,65).
- **The marks** (`sub_11948`) show where the people of this place stand:
  - It counts the 16 character records (0xFD8 + 16 i) whose byte 3, location + 1, equals ds:7. Each is counted in the room of byte 0.
  - There are two rows: the second is for records with flag 0x40 in byte 15.
  - Gurney (id 4 in byte 14) is left out while the phase is 0x15–0x1F (`sub_1127C`), which is his disappearance.
  - Each row shows up to five frame-2 marks, 4 px apart. The first row is at the room's offset + (3,2), the second 7 px lower. Paul's room (ds:4) gets frame 1, the red mark, at + (12,5).
  - The room offsets for rooms 2–12 are at ds:1426 (x and y bytes). The front, room 1, is not on the plan.
- **Data.** The engine reads these tables from the executable's initial data segment through `World::ds`, so the floppy (+13 above 0x11C0) and CD layouts both work.
- **Result:** 99.9% of pixels match the original with the plan open (throne room, Paul's room). The chapter 13 save's plan in room 11 matches too; that checkpoint's remaining difference is Jessica's figure in the room view, which the engine draws cut off.

## The Leto loop (an original bug, fixed as an option)

- **The bug.** After Jessica's "The Duke is dead" (phase 0x4c) the throne room still lists DUKE LETO ATREIDES and he talks ("Keep on going Paul!", speedrun video 2105-2115 s and 6265-6295 s).
- **The cause.** The death callback (CD `sub_11166`; `phaseCallback(0x4c)` in `world.cpp`) only adds one to ds:1141, moves Jessica (room 2, 0x80, place 0 + 1) and queues vision 0x105. It never touches Leto's character record at ds:FD8 (room 10, the palace, 0x80, place 0 + 1). The presence test (`loc_136EE`: the record's two words against ds:4 and ds:6) therefore still finds him in the throne room. So do the command rows, the room's figures and the persons-in-room bits the dialogue conditions read.
- **The engine.** By default it keeps the bug, as the original does: `World::characterInRoom` is the same test.
- **The option.** `dune_fix_leto_loop`, shown in Options > Engine and read from `scummvm.ini`, makes `characterInRoom(0)` false from phase 0x4c on. Leto then leaves every room, row, figure and presence test, and the story flags set at his death are unchanged. Scripted scenes place their own cast and are not affected.
- **Check.** `scripts/check_leto_loop.sh` runs the throne room right after his death (`dune_story_setup=letodead`) on both releases. With the option off, Leto must be listed and talk; with it on, he must be gone.

## Northern and southern Fremen quarrel (2026-09-28)

The Dune wiki says tribes from both hemispheres in one sietch "quarrel and refuse to work". The original does this:
- **The hemisphere.** At game start each troop's byte 0x12 gets its region in the low nibble and bit 7 for the south (floppy `seg000:01e0`). The same byte holds bit 4 (quarrel) and bit 5 (sulk).
- **When.** The new-day routine (floppy `sub_9A58`, tail 9A95–9AB8; CD 6e20) runs on the first period of a day (ds:423A). It is called at the start of the spice, army and irrigation handlers (9C1E, 9E28, A2C7). It only acts for the troop at the head of its place's chain.
- **The test.** It skips Paul's place (ds:114E) and places of type 0x21 or more. Callback 7BEA (CD 6e82) walks the place's troops whose motivation (byte 0x15) is below 0x28 and whose occupation & 0x2F is 0, setting side bit 1 for the north and 2 for the south.
- **The penalty.** If both sides are there, callback 7C10 (CD 6ea8) takes every such troop: `sub_9CBE` sets occupation bit 4 (stopped: no work), and byte 0x12 gets bit 4. Then `sub_4BB0` with ax 0x302 queues Duncan's message "Nothing coming from ... I wonder what's going on there!" (CD 6e77).
- **What it shows.** The dispatcher (`sub_98B0`, CD 6c92) skips a troop whose word 0x12 has 0x430. The chief's lines read that word: 0x90 both "Life is impossible here! We came from the south...", 0x10 alone "It's difficult to understand Fremen from the south...", 0x20 the sulk line, 0xA0 "Some men even talk about going back to the south...". On contact: "We refuse to work anymore." (CD condition 513, floppy 511).
- **The release.** Moving a troop out (`troop_issue_move_order`, floppy `sub_B072` at B096 calling `sub_9AF7`; CD 6ebf/6ecb) re-applies the job of every troop left behind that has bit 4. That goes through `sub_973E` (CD 6ad4), which clears bits 4–5 (`and byte [si+12h], 0CFh`, floppy 9759). The quarrel starts again the next day if both sides are still there and still below motivation 0x28.
- **The engine.** `World::fremenQuarrel` (troops.cpp), called from `runPeriod` at time slot 0 for miners, trainers and irrigators. `applyJob` clears bits 4–5 and `issueMoveOrder` releases the troops left behind. The speedrun bot asks `World::wouldQuarrel` before sending miners or soldiers to a place.
- **The rest of the routine** (the sietch's ring and the daily motivation decay) was built on 2026-09-30; see "The small troop rules". The floppy addresses above are IDA's numbering; in the unpacked image the routine is at 7b88.
- **Check.** `scripts/check_hemispheres.sh` puts a northern and a southern troop at one sietch (`dune_story_setup=hemispheres`) on both releases. It checks that both quarrel the next day, that message 0x302 is queued, the contact and in-person lines, and that both work again after the southern troop leaves.

## Celimyn-Tuek (an original bug, fixed as an option)

- **The bug (wiki).** The sietch Celimyn-Tuek can never be found. The wiki's save patch looks for the place record `0C 05`, then 6 bytes, then `03 00 80 FF F7 04 00`, and changes FF to 58.
- **The field.** The record is the location record at ds:0x100 (28 bytes): names 0x0C (Celimyn) and 0x05 (Tuek), then longitude, latitude and map cell, type 3, troop 0, status 0x80 (hidden), and byte 0x0B = 0xFF. Byte 0x0B is the discovery phase: the flight search (floppy `sub_6223` at 6257), `sub_6305` (6340) and `sub_7EF5` (7F56) all do `mov al, ds:2Ah; cmp al, [si+0Bh]; jb skip`. A hidden place is found only once the story phase ds:2A has reached that byte; CD 4125–4131 is `World::discoverable`. Finding a place clears the byte (`sub_637F`: `mov byte [di+0Bh], 0`; CD 425b).
- **Why FF blocks it.** The phase never reaches 0xFF, so the compare always skips the sietch. With 0x58 it can be found from phase 0x58 on, as the other late sietches are.
- **The option.** `dune_fix_celimyn_tuek` (Options > Engine, `scummvm.ini`), off by default. `World::applyCelimynTuekFix` sets the byte to 0x58 in memory after new-game setup and after a save is loaded, only while the sietch is still hidden with 0xFF. A save writes the patched byte only because the game writes its state.
- **Check.** `scripts/check_celimyn_tuek.sh` runs `dune_story_setup=celimyn` on both releases:
  - with the option off: 0xFF, never findable, and still 0xFF after a reload;
  - with it on: 0x58, findable at phase 0x58 but not 0x57, and 0x58 again after reloading a save written with 0xFF.

## Test scripts: comments, traces and real time

- **Comments.** The harness (`harness.cpp`) and the Spice86 host both strip a `#` that follows whitespace, so every `click` and `wait` carries what it hits or waits for: `click left 160 171      # row 2: DUKE LETO ATREIDES`.
- **Traces.** In harness runs the engine logs "Script line N: <step>" before each step. The lines after it say what the step did:
  - taps name their row ("Tap: room 10 at (160, 171) -> command DUKE LETO ATREIDES", "Troop command: …");
  - dialogue lines show their first words.

  `scripts/dune_script_trace.py SCRIPT [--saves DIR] [--prelude MS] [--data cd] [--config k=v]` runs a script and prints the log grouped by script line.
- **Check scripts' data paths.** A new `scripts/check_*.sh` takes its game data from `"${DUNE_DATA_FLOPPY:-$repo_root/data/floppy}"` and `"${DUNE_DATA_CD:-$repo_root/data}"`, as `check_flight_landscape.sh` does, so a clean checkout can point them elsewhere.
- **`periods N`** passes N game periods. The harness's clock is stopped, so a march or prospection needs it.
- **`dune_real_time`** keeps a harness run at the original's speed (`isDuneFastHarness`): intros, flights and the clock. The fidelity report's CD flight scenarios use it.
- **The CD intro's ESC.** ESC ends the whole intro, the story after TITLE included, as in the original: ESC at 8.0 s, the throne room at 8.2 s (Spice86, `captures/cd-flight`). A click or Return skips only the current video.
- **The CD's Mixer Panel row.** CD rooms end their verbs with Mixer Panel (CD `sub_4DCB`, `loc_4E73`): after the ornithopter, messages or mirror, and before the people. So in the CD throne room Leto is row 3. The panel itself (`seg000:a3f0`: volume sliders, subtitle buttons) is not built.

## The speedrun check

`scripts/check_speedrun.sh [campaign|full] [seeds]` runs the engine's bot (`speedrun.cpp`, developer key `dune_speedrun`) on the floppy and CD data. It plays through the same actions as the menu rows and logs each route step. A shortcut the bot had to take is logged `FORCED`.

- **campaign** starts after Leto's death with one day of the route's preparation forced. From the first worm ride on, it plays the route's war without shortcuts on both releases: espionage, marches, massive attacks with reloads, the captains, and the war council.
- **full** plays from a new game with no shortcut (2026-09-28). The user's route:
  - Until Stilgar joins (phase 0x2c), each troop goes to SPICE MINING the moment it agrees to WORK FOR ME, with a free harvester if one is there. The prospectors are sent to the three richest unprospected sietches. Miners move to richer fields, and on to new ones when theirs is spent.
  - After Stilgar, new troops train for the army; the earlier miners stay on spice.

  With this route every shipment is 100% up to demand 5 (7650 kg, day 28). Both releases reach the worm phase on day 30 and take 5 forts by day 42.
- **Watchable route (2026-09-28).** The bot now plays like a player to watch (`scripts/watch_speedrun.sh full`):
  - A place, a room or a talk is done once until something changes: the phase, the party, charisma, the troops there, the unread messages. When nothing is new anywhere, everything is looked at again.
  - Companions talk once a chapter, plus once in each palace room where their lines matter. The COMM room's messages come before its talks (Thufir: "View the message before anything else").
  - Orders go through the troop popup's rows (`speedrunOrders`): the chief's GIVE ORDERS TO TROOP in his sietch, or the map's contact; then SELECT/CHANGE TROOP OCCUPATION, MODIFY EQUIPMENT and MOVE TROOP with a pick on the density popup. A missing row or a refusal falls back to the direct write, which is logged.
  - The war council waits for the next day while its place is still a fort (a fort becomes a sietch overnight, `sub_9A58`; Jessica refuses it before then).
- **What still stops it:**
  - The Emperor keeps demanding through the war; shipments pause only when the Harkonnen palace alone is left.
  - A short shipment right after another short one sets the fulfilment to 0, and the Emperor strikes (floppy `sub_4748`; the engine matches it).
  - The demand grows by about 1500 kg each time; by demand 7 it is 13730 kg. The 8 miners' output falls as their fields thin, from about 90 kg a period to about 25.
  - Only 2 harvesters (×4 output, `seg000:708a`) were found lying free all game. Buying from the smugglers is built (see "The smugglers' trade"); the bot does not buy.

## Harkonnen saboteurs and harvester worms (2026-09-29)

The wiki says saboteurs stop production and the troop takes no orders "for a few days". The original has one daily event roll for every harvester, with saboteurs and worms, and the damage lasts one day. The CD and floppy code is the same; the floppy is the CD address + 0xd69 here.

### Addresses

| What | CD seg000 | Floppy |
| --- | --- | --- |
| Spice mining, one period | `6fe5` | `7d4e` |
| Damaged harvester: wait for the slot, repair (`7068`) | `705c` | `7dc5` |
| Harvest rate | `708a` | `7df3` |
| The daily event roll (worms) | `714c` | `7eb5` |
| Damage: bitfield_10 bit 9, the troop stops (`7085`) | `719c` | `7f05` |
| Harvester swallowed | `71a4` | `7f0d` |
| Message to the vision queue (ah = 0x0f) | `71b2` -> `29f0` | `7f1b` -> `2ce0` |
| Saboteurs | `71bc` | `7f25` |
| Army training | `71ef` | `7f58` |
| Army troop hunting saboteurs | `725f` | `7fc3` |
| Saboteurs found (callback `7289`) | `727d` | |
| Troop menu greys (MODIFY EQUIPMENT: `7888`) | `7847` | |
| Vision dream: stages the message's place and its names | `2bd2` (`2c1d`, `2c20` -> `2e98`) | |

### When

- **The roll.** It runs from the spice-mining period of a troop that can mine (`6b96`). It needs all of these:
  - Paul's first vision (ds:0a bit 0);
  - a harvester (troop byte 0x19 bit 7);
  - the troop's daily slot: game time & 0x0f == troop id & 0x0f (`715c`).
- **Saboteurs first** (`71bc`). They need all of these:
  - phase 0x35 or later: the phase of Thufir's line "I am worried about these Harkonnens... they will surely try to infiltrate saboteurs" (character 2, list 1, condition 119);
  - the troop's speech word bit 6. It comes from the initial data, for troops 1, 13, 14, 15, 19 and 20, and the game-start setup keeps it (`01e0` masks byte 0x12 with 0x70);
  - the word at ds:0 rotated left three times having its low three bits clear. The main loop stores a fresh `rand` there every pass (`1d84e`), so the chance is $1/8$ a day.
- **Nothing else counts.** No Harkonnen proximity, no espionage. An espionage troop (job 5) never touches speech bit 6 or the place's status bit 2. The only code that clears them is the hunt (`7289`).
- **Then a worm** (`7168`). The high byte of `rand` (`e3cc`), $r$, is compared with the place's region chance $c$ = ds:1141 + the region (the place's first name, byte 0; the xlat). The event comes when $r \le c$, so $P = (c+1)/256$ a day.

| Region | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| $c$ | 0x0d | 0x0f | 0x32 | 0x64 | 0x80 | 0x28 | 0x14 | 0x28 | 0x23 | 0x32 | 0x46 | 0x80 |

  The condition staging (`331e`) reads the same table by the place's second name byte into ds:50 ("Worms aren't rare in this area", condition 638).

### What it does

- **Saboteurs.**
  - bitfield_10 (troop word 0x10) gets 0x200 (damaged) and 0x8000 (on purpose);
  - the occupation gets its stopped bit 0x10;
  - the place's status gets bit 2 (saboteurs here);
  - message 3 is queued with the place.
  - The troop still mines that period: the call returns to `6ffa`.
- **A worm.** bitfield_10 gets 0x4000. An ornithopter (equipment bit 6) sees the worm sign in time, and nothing more happens. Otherwise, by $r \mathbin{\&} 3$:
  - 0: nothing;
  - 1: men lost (0x2000), population byte − 2 (20 men), unless that would leave none;
  - 2: the harvester is damaged (0x200 and the stopped bit), as with the saboteurs;
  - 3: the harvester is swallowed (0x1000). Equipment bit 7 goes, the place's harvester count (byte 0x14) drops by one, and message 6 is queued.
- **How long.**
  - A damaged harvester mines nothing. Word 0x0c is 0 and the troop stays stopped, until the troop's slot comes round the next day, 16 periods later (`705c`).
  - It is then repaired and mines at once (`7068`), without that day's roll.
  - "A few days" only comes from new strikes: while the troop keeps speech bit 6, the saboteurs can strike again any day.
- **Orders while damaged.**
  - A new occupation is refused: list 4 with ds:23 = 0x0a, condition 633 (w[0x32] & 0x200), action 2: "We have to repair our equipment before doing anything else!"
  - MODIFY EQUIPMENT stays greyed (`7888`).
  - MOVE TROOP is taken: its answer has ds:23 = 0x0b.
  - The map's troop info popup would say "Repairing" (COMMAND 0x3F, `178e9`). The engine does not build that popup.
- **The event bits** 0x8000, 0x4000, 0x2000 and 0x1000 stay until the next map contact closes (7b7c keeps only 0x3f0; checked on Spice86 2026-09-30, see "Closing a map contact"), so the troop talks about them until then. The CD's army training clears bit 9 (`71f2`); the floppy's does not (`7f58`).
- **The hunt** (`725f`).
  - An army troop (job 4) at a place with status bit 2 does not train. It counts word 0x0e down instead: high byte 0xff, low byte from $\mathtt{0x40} - 	ext{army skill}$, one a period.
  - When the count goes negative (`727d`):
    - the place's bit 2 goes;
    - every troop there with speech bit 6 loses it (`7289`), so the saboteurs are gone for good;
    - if any did, the hunter gets speech bit 8.

### How Paul learns

- **Messages** (DIALOGUE character 16, list 4, from the troop chief, sender 0x0f):
  - 3 (condition 689): "There are saboteurs here in \x81-\x82!";
  - 6 (692): "A worm has swallowed our harvester here in \x81-\x82!".

  In person they come only at that place. Elsewhere they come as the dream, which stages the message's place and its names (`2bd2`: `331e`, `2e98`). It shows "... in Carthag-Tuek!", not the current place.
- **The troop's contact lines** (character 15, list 2):

  | Condition | Line |
  | --- | --- |
  | 516 (w[0x32] & 0x8200, action 6: the list ends there) | "We're repairing... Damn these saboters... Please, Muad'Dib, do something!" |
  | 517 | "We're repairing our equipment. It's going to take us the whole day!" |
  | 519 | "All our equipment has been mysteriously damaged..." |
  | 575 (after the repair) | "Our equipment has been damaged, intentionally. We've repaired it, but what a waste of time!" |
  | 549-555 | The worm lines: "A worm has come...", "The worm attacked the harvester... We're making repairs now.", "This gigantic worm... swallowed the harvester.", "All of this could have been avoided with an orni...". |
  | 577 (the hunter; job 4, the place's bit 2 clear, speech 0x100) | "Saboteurs have been discovered. You'll have no problems here now!" |

- **Gurney** (character 5, list 2, condition 330): "I just had a look at the damaged equipment. No doubt, it was done intentionally..."

### Built

- **`World::mineSpice`, `harvesterEvents`, `sabotage` (world.cpp); `huntSaboteurs` (battle.cpp).**
  - They log "World: saboteurs damage troop ...", "World: a worm comes near / attacks / swallows ...", "World: harvester breaks down (a worm attack) ...", "World: troop N has repaired its harvester ...", and "World: troop N hunts / found the saboteurs ...".
  - The saboteurs' ds:0 roll uses the engine's rolling word (`rollRandom`). The rotation is not written back: the original's value is replaced at once, and writing it back makes the LFSR cycle, so the saboteurs never came.
- **The troop menu.** MODIFY EQUIPMENT is greyed while bitfield_10 bit 9 is set. The menu's other greys (`7847`–`78b8`) are still not transcribed.
- **Vision messages.**
  - The dream stages the message's place and names it. In person the staged place is put back (`2b25`).
  - A harness run shows idle messages when it plays in real time (dune_real_time). The fast harness keeps them off.
- **Checks.**
  - `scripts/check_saboteurs.sh` (`dune_story_setup=saboteurs`, seed 7) covers, on both releases: the strike, message 0x0f03, the lines, the greyed row, the refused order, the repair a day later, the hunt, and the four worm outcomes and the orni.
  - Fidelity scenarios:
    - `saboteurs-contact`: the chief's lines, the greyed row and the refusal, on a patched chapter 20 save;
    - `saboteurs-message`: the dream, on the same save with the queue patched.
  - The original is captured in `captures/saboteurs/`.
- **The speedrun bot.**
  - It does not ask a repairing troop for a new job or equipment; it asks again after the repair.
  - Short of 10 000 men with atomics, it sends its biggest troop without atomics to fetch those lying free elsewhere.
- **Not built.**
  - The map's troop info popup ("Repairing", "Inactive").
  - The dream's screen: the chief's portrait over the VIS clouds; the engine draws its text box over black, with " Continue...".
  - The prospectors' "prospection is finished" message (0x0f0e, `7144`).
  - The fifth contact row: the original's CUT CONTACT against the engine's NO MORE ORDERS.

## The Fremen epidemic (2026-09-29)

The 1992 demo only announces an "epidemic among the Fremens" (its script ends at phase 0x54; its PHRASES.DAT has no line about it). The release plays it on both the floppy and the CD, between Liet Kynes's bulb (phase 0x5c) and Chani's kidnapping (0x60, 0x64). The illness strikes the sietches with the most working troops, one a day, until Chani starts curing. Chani is the only cure: nothing else clears the illness, and no item or time limit ends it. The Water of Life plays no part.

Phase 0x64 is not the illness itself. Its callback runs after the epidemic, when a cured troop tells Paul that Chani has vanished. The engine had it as "the illness (not yet)".

### Addresses

The floppy addresses in this section were found by their bytes; the CD + 0xd69 rule of the troop code does not hold this early in the code.

| What | CD seg000 | Floppy |
| --- | --- | --- |
| Phase 0x5c callback: ds:1156 = day + 3 (`sub_111B3`) | `11b3` | `157c` |
| New-day hook -> the illness picker | `1c5f` -> `1e43` | `1fa2` -> `2180` |
| Picker: count the working troops (`cmp [si+3], 8; adc dx, 0`) | `1ea1` | `21d8` |
| Picker: a troop falls ill (0x400) and stops (`7085`) | `1ea9` | `21e0` |
| Message 8 (sender 0x0f) | `1e9c` -> `71b2` | `21d3` -> `7f1b` |
| Period step: Chani not in Paul's room | `1d9f` | `20e2` |
| Chani at an ill place in phase 0x5d? (room 2) | `1e01` | `2146` |
| Does a place house an ill troop? | `1e24` (every troop) | `2169` (the first troop only) |
| The cure step, + 8 | `1eda` | `21f5` |
| A troop cured: 0x400 -> 0x800 | `1eb1` | `21e8` |
| Chani's message 0x709 | `1eec` -> `29f0` | `2207` -> `2ce0` |
| No ill place left: phase 0x60 (`sub_111CB`, called directly) | `1f0d` -> `11cb` | `2228` -> `1594` |
| STAY HERE to Chani: the cure + 0x10 | `9548` | `a00c` |
| A cured troop met at phase 0x60-0x63: phase 0x64 | `1ebe` (from `7b79`, `93a2`) | none (dialogue action 12) |
| Contact close: event bits cleared, among them "cured" | `7b7c` | `889a` |
| Phase 0x64 entry: a `jmp` in the callback table | `11e6` -> `1f13` | `15af` -> `222e` |
| Speaker < 9 before phase 0x64: the ill place named | `9519` | `9fdd` |
| Troop menu: an ill troop's rows greyed | `7857` | `857d` |
| Period loop: an ill troop does nothing | `6c92`, `6ce4` | `7a00`, `7a4c` |
| Motivation − 40 in phases 0x64-0x67 | `6f31` | `7c99` |
| Harkonnen raids skip ill sietches | `2034` | `234c` |

Data: ds:f8 counts the ill places, ds:f9 is Chani's cure progress (a byte), ds:11db is the latest ill place (floppy ds:11e8), ds:1156 is the picker's first day, and ds:f2 holds Chani's prison names. In troop speech word 0x12, bit 0x400 is "ill" and bit 0x800 "cured by Chani".

### How it starts

- **Phase 0x5c.** Liet Kynes's bulb line (character 6, condition 351, in room 2, action 12) moves the story from 0x58 to 0x5c. The 0x5c callback stamps ds:1156 = today + 3.
- **The picker** (`1e43`) runs at every new day. It needs all of these:
  - today ≥ ds:1156;
  - the phase is exactly 0x5c;
  - Paul is not at place 62 (Sihaya-Tuek, ds:114e ≠ 0x7c8).
- **The place.** Of the places of type below 0x28 (sietches, the palace, villages) that are not hidden, it takes the one with the most hired troops at work. A troop counts when its occupation byte is below 8: not moving, stopped or captured. The first place wins a tie. The CD leaves out place 16 (Tuono-Timin, ds:2c0); the floppy does not. With no working troop anywhere, nothing happens.
- **What it does.** ds:11db = the place, ds:f8 + 1. Every hired troop there gets speech bit 0x400 and the stopped bit, and message 0x0f08 is queued with the place.
- **It spreads.** The phase stays 0x5c, and the ill troops (stopped, so above 8) no longer count. So the next day the picker strikes the next busiest place, and so on each day, until Chani's STAY HERE line moves the phase to 0x5d.

### What the illness does

- **No work.** The period loop skips a troop with speech bits 0x430 (`6c92`). ds:fa clears only 0x30, and an ill troop is skipped again (`6ce4`). So it mines, trains, spies and irrigates nothing, and the palace's production drops.
- **No orders.** Over the map, CHANGE TROOP OCCUPATION, MODIFY EQUIPMENT and MOVE TROOP are greyed (`7857`). A click on a greyed row closes the contact, as CUT CONTACT does (a Spice86 capture of the original). The refusal "We are too sick to do anything." (list 4, condition 629) answers an order given some other way.
- **Nothing else.** Motivation, population and equipment are left alone: no one dies and no one leaves. The line "I am sad to report F deaths among our people." belongs to battles (word 0x10 bit 0x400).
- **Harkonnen raids** pick no sietch with an ill troop (`2034`). The engine does not build the raids yet.

### Who says what

| Speaker | Condition (CD / floppy) | Line |
| --- | --- | --- |
| Troop chief, message 8 | 694 / 688 | "There is a strange disease here in \x81-\x82. We're all ill... very ill." (CD action 13) |
| Troop chief, contact | 514 / 512 (w[0x34] & 0x400, action 6) | "Everybody is ill here. We need help." |
| Stilgar, Chani (list 0) | 293 / 292: b[0xf9] == 0, b[0xf8] > 0, not at an ill place | "We have to go to \x81-\x82 to stem the epidemic." (CD action 13) |
| Stilgar | 297 / 296 (at an ill place) | "In the matter of diseases, Chani is far more qualified than I am." |
| Chani (list 1) | 365 / 362 (at an ill place, not room 2; action 6) | "Let's go and see these Fremen!" |
| Chani | 366 / 363 (room 2 of an ill place) | "Strange disease we have here, very strange. I wouldn't be surprised if the Harkonnens were behind all this. But I'm sure I can cure this disease. All I need is time." |
| Chani | 367 / 364 (travelling with Paul) | "Strange indeed." |
| Chani | 297 / 296 | "I'm working on it now. But, please Paul, don't stay here or I won't get anywhere." |
| Chani, STAY HERE (list 6) | 366 / 363 (action 11: phase + 1) | "OK Paul! I'm staying here to cure the Fremen. I guess that it will take me at least a couple of days." |
| Chani, COME WITH ME (list 5) | 394 / 391 (ill place, b[0xf9] > 0; action 2) | "No Paul! I have to stay here to cure these Fremen." |
| Chani, message 9 | 695 / 689 | "Paul, I'm so happy! I've managed to cure everybody, here in \x81-\x82." |
| Troop chief | 507 / 510 (0x800; the CD also phase 0x60-0x64) | "Chani has cured all of us, \x93" |

The \x81-\x82 names are staged at every line of a speaker below 9 before phase 0x64 (`9519`): they name ds:11db, the latest ill place. Chani's message 9 in a dream therefore names the next ill place when one is left, not the cured one. Action 13 (`a28e`) zooms the globe on a place elsewhere; the engine logs it and does not build it.

### Chani's cure

- **What she needs.**
  - Phase 0x5d, set only by her STAY HERE answer in room 2 of an ill place (condition 366, action 11, the first time only).
  - Her record at a place (byte 2 = 0x80) that has an ill troop (`1e24`).
  - Paul not in her room. Each period `1d9f` skips the step while persons_in_room has her bit (7). Her line says it too: "don't stay here or I won't get anywhere".
- **How long.**
  - ds:f9 grows by 8 a period (`1eda`), and STAY HERE adds 0x10 (`9548`). From 0 that is $(\mathtt{0x100} - \mathtt{0x10}) / 8 = 30$ periods, nearly two days.
  - Each period she is moved to room 2 of her place (`1e01`).
  - While ds:f9 > 0 she refuses to come along.
- **The recovery** (ds:f9 wraps to 0).
  - Every troop at her place loses 0x400 and gets 0x800.
  - Her message 0x709 is queued, and ds:f8 − 1.
  - ds:11db = the first place with an ill troop left. Paul must bring her there and tell her to stay. The phase is already 0x5d, so her STAY HERE adds 0x10 again.
  - The stopped bit stays. A spice troop's viability check clears it the next period; an army troop trains with it.
- **The end.** With no ill place left, `11cb` is called directly: phase 0x60 and ds:ff = 0, with no phase triggers. Chani goes to room 2 of the Harkonnen palace (place 1). Stilgar: "Let's go and see Chani." / "I don't remember where we left her. Maybe you can try to contact Fremen." (condition 320).

### Phase 0x64: Chani has vanished

- **How it comes.**
  - **CD:** `1ebe` runs when a map contact closes (`7b79`) and before an in-person troop chief's lines (`93a2`). With the troop's 0x800 and phase 0x60-0x63, it calls `121f` with 0x64: the phase triggers, then the callback.
  - **Floppy:** no such code. The contact line "Oh! Chani isn't with you... She vanished! Nobody has seen her." (condition 504) carries action 12, the next chapter. On the CD the same line (507) has no action.
  - **Both:** closing the contact clears the troop's 0x800, along with word 0x12's 0x1200 and word 0x10's bits outside 0x3f0 (`7b7c` / `889a`). Built 2026-09-30 (see "Closing a map contact").
- **The callback** (the table's entry for 0x64 is the byte 0xe9 at `11e6`: with the next word, the first entry, it forms `jmp 1f13`).
  - It takes the Harkonnen fortress with the largest latitude word above −100 (type ≥ 0x28 without status bit 3, `5d36`) where no Fremen troop is attacking (occupation exactly 6, `5098`).
  - The scan goes on while the next record's first name is below 8.
  - Chani goes to its room 3 (her record = 03, type, 80, place + 1), and ds:f2 = (first name << 8) | last name.
  - Feyd-Rautha's COMM message 0x2b0a follows ("Ahh, your little darling is in my hands. I don't think you will see her again soon, little pup!"), with "A message has arrived in the palace" (0x201).
  - **CD:** the scan starts at place 2. With no candidate, Chani goes to place 0, the Atreides palace.
  - **Floppy:** the scan starts at place 0. With no candidate, she stays where she is (still the COMM message).
- **The original, captured.** Spice86, floppy, the patched chapter 20 save. Right after "Oh! Chani isn't with you...":
  - ds:2a = 0x64;
  - Chani's record 03 2d 80 06 (place 5, Arrakeen-Harg, room 3);
  - ds:f2 = 0x0106 (Arrakeen-Harg);
  - the COMM list gains 0x2b0a;
  - vision 0x201 is queued;
  - the contact's close clears 0x800.

  The engine gives the same place and values.
- **For the next queue item (Chani's kidnapping, not built here).**
  - In phases 0x64-0x67 every troop's motivation counts 40 less, at least 10 (`6f31`). Stilgar: "We all like Chani a lot. I'm sure her disappearance will have a bad effect on the motivation of the Fremen troops."
  - Thufir (condition 126) suggests espionage. A spy at her fortress says "One of my men told me that he was sure they had a prisoner." (condition 588, w[0x4e] == w[0xf2]).
  - Found in her room, Chani's "Oh Paul! I was so scared!..." (condition 360) has action 12, which brings phase 0x68.
  - The dialogue data also lets Gurney's STAY HERE answer at a sietch with army troops ("Good! I'm going to try to teach these Fremen...", condition 288, action 12, once) move any phase to the next chapter. The first time at 0x5c or 0x5d, it would skip the rest of the epidemic.

### Built

- **World** (story.cpp):
  - `illnessNewDay` (the picker, in `runPeriod` at slot 0, before the stage-7 gate);
  - `chaniCurePeriod` and `chaniCureStep`: the period step, the cure, phase 0x60 through `phaseCallback(0x60)`;
  - `chaniStaysHere` (STAY HERE);
  - `illTroopAt` (the CD's chain or the floppy's first troop);
  - `curedTroopMet` (CD only) and `clearCuredBit`;
  - `chaniPrisoner`, the 0x64 callback, in both release variants.
- **GameScreen:**
  - the contact's close runs `curedTroopMet` and `clearCuredBit`, then the story;
  - a contact line's action 12 applies at once (`nextTroopLine`);
  - `startConversation` runs `curedTroopMet` for a troop chief;
  - `stageIllnessNames` runs in `startConversation`, `presentVerb` and `presentLine`, and the current place's names come back when the talk ends;
  - the troop menu greys the three rows of an ill troop;
  - a click on a greyed contact row closes the contact.
- **Log lines:**
  - "World: epidemic at place ...";
  - "World: Chani stays at place ... (cure ...)";
  - "World: Chani has cured the troops at place ...";
  - "World: the epidemic is over; Chani is taken to the Harkonnen palace (phase 0x60)";
  - "World: troop N, cured by Chani, is met at phase ...: phase 0x64";
  - "World: Chani is held at place N, room 3";
  - "Talk: speaker N, the ill place N named (9519)";
  - "Troop command: ... is greyed: the contact closes".
- **Checks:**
  - `scripts/check_epidemic.sh` (`dune_story_setup=epidemic`, both releases). It covers two sietches falling ill on two days, the message, the contact lines, the three greyed rows, Stilgar's line, Chani's talk and STAY HERE (phase 0x5d), the 30-period cure, message 0x709, the second sietch, phase 0x60, the cured troop's contact (CD `1ebe`, floppy action 12), Chani's prison, Feyd-Rautha's message and the motivation − 40.
  - Fidelity scenarios `epidemic-contact` and `epidemic-cured` use patched chapter 20 saves. The original is in `captures/epidemic/`.
- **Not built:**
  - action 13's globe zoom;
  - the Harkonnen raids;
  - the other bits the contact's close clears;
  - the troop menu's other greys (the original's cured troop also has MODIFY EQUIPMENT greyed).

## Chani's kidnapping and rescue (2026-09-29)

Queue item 7, traced in the CD 3.7 executable (OpenRakis' asm/cd/DNCDPRG_RECENT.ASM, capstone on `DNCDPRG.EXE`) and in the floppy image (`DUNEPRG.unpacked.bin`, addresses found by their bytes), with the dialogue data. Checked on Spice86 with a patched save (`scripts/dune_save_patch.py`). The demo's "Chani gonna be kidnapped by Feyd-Rautha" is phases 0x60 and 0x64 (see "The Fremen epidemic"); this section follows her from there.

### Addresses

| What | CD seg000 | Floppy |
| --- | --- | --- |
| Phase 0x64: her prison (room 3 of a fortress, ds:f2, COMM 0x2b0a) | `1f13` | `222e` |
| Motivation − 40 in phases 0x64-0x67, at least 10 | `6f31` (in `6efd`) | `7c99` |
| Dialogue action 12: (phase & 0xfc) + 4, once | `a235` -> `121f` | `aa1e` |
| Phase 0x68 and 0x6c callbacks: nullsubs | table `11e7` | |
| The speaker's flags into ds:18, the time since its stamp into ds:16 | `94f3` (from `9f9e`) | `9fb7` |
| COME WITH ME: flag 0x40, stamp word 8, ds:10 bit | `9603`-`9616` | |
| STAY HERE: flag 0x40 cleared, stamp word 0x0a, ds:10 bit cleared | `9556` | |
| The talk's close: flags + 0x20, − 0x04; the HUD follows flag 0x40 | `2997` -> `97cf`, `9825` | |
| Gurney, Stilgar and Chani staying at a staged place (ds:f7) | `3385` | |
| A sietch lost: the first nine characters there held in room 3 | `74d3`-`74e8` | `821a`, `823a` |
| Charisma with the motivation spill | `6f78` | `7ce0` |

### How she is held

- `1f13` puts her record at (room 3, the fortress's type, 0x80, place + 1) and ds:f2 = the fortress's names; Feyd-Rautha's COMM message 0x2b0a follows. The fortress is the one with the largest latitude word over −100 that is not Atreides and has no attacking troop (see "The Fremen epidemic").
- Nothing else moves her. The only code that reads her record directly is the cure step (`1d9f`), ds:f7 (`3385`) and `1f13`. The character-table loops (`1d66` daily, `2170` the room changes, `6dbb` a fortress turned sietch, `74b6` a sietch lost) treat her like the others: a fortress that becomes a sietch two days after its capture moves her to room 2 of the new sietch (`6dbb`).
- The fortress's room table (`cs:1d35`, three rooms for types 0x28-0x2f) keeps room 3.

### What Paul learns, and from whom

| Speaker | Condition (CD / floppy) | Line |
| --- | --- | --- |
| Feyd-Rautha, COMM | message 0x2b0a | "Ahh, your little darling is in my hands. I don't think you will see her again soon, little pup!" |
| Thufir | 126 / 126 (phase 0x64) | "Chani kidnapped by Feyd-Rautha Harkonnen. That's not good. I wonder where Chani is now, probably not in the Harkonnen palace. My guess is that she is in one of these Harkonnen fortresses. Why don't you use espionage troops to try to locate her?" |
| Stilgar | 126 / 126 | "We all like Chani a lot. I'm sure her disappearance will have a bad effect on the motivation of the Fremen troops." |
| Any troop chief | 625 / 623 (phase 0x64, a roll of 1 in 8) | "We're all sad about Chani. Where can she be now?" |
| A troop at her fortress | 512 / 510 (ds:f7 bit 7) | "Chani is here." |
| A spy at her fortress | 588 / 586 (occupation exactly 5, w[0x4e] == w[0xf2]) | "One of my men told me that he was sure they had a prisoner." |

- The spy's line comes after its count ("We've seen N Harkonnen troops.", 587): before that, lines 583 and 585 end the list. ESPIONAGE (`6a45`) marches to the nearest hidden fortress within 30 cells (ds:e2/e4), so a spy reaches her fortress only while it is hidden and near one of Paul's sietches.
- "Chani is here." needs no spy: any troop at her fortress says it, from `3385`'s ds:f7, when the contact is not at Paul's place.
- Paul still cannot land there while Harkonnens hold it (`503c`: he is shot).

### How she is freed

- **Taking the fortress** (`7443`, by battle or by the vegetation) does not touch her record or the phase. It makes the landing safe (`503c`), nothing more. The motivation stays − 40.
- **Meeting her.** In room 3, TALK walks her list 0 first. Its condition 360 (CD; floppy 357) is phase 0x64-0x68: "Oh Paul! I was so scared! I'm so glad you're able to deliver me from these Harkonnen thugs." (floppy "Oh Paul! I was scared to death... I'm so happy you've been able to deliver me from these Harkonnens."). The line is said once; its action 12 moves the phase to (phase & 0xfc) + 4 = 0x68, and `121f` runs the phase triggers and the 0x68 callback, a nullsub.
- **The morale.** `6efd` subtracts 40 only in phases 0x64-0x67, so 0x68 ends it: every troop's motivation counts in full again. No charisma comes with it. Stilgar at phase 0x68 (321 / 319): "Good to see you with Chani again. The Fremen have recovered their motivation!"
- **The motivation rule.** With $m$ the troop's motivation (+ 20 with ds:fa; + 30, at most 100, for an attacking troop at Paul's place; 100 for the ecology jobs 8 and 9; else at most 100), phases 0x64-0x67 give $\max(m-40,10)$ (a signed compare after the subtraction). An attacking troop elsewhere keeps its unclamped $m$. It weighs in battles (`342d`), the chiefs' consent (`95c1`), and the contact lines (ds:36).
- **The original, captured** (Spice86, floppy, `captures/chani/rescue`, the patched chapter 20 save): ds:2a 0x64 -> 0x68 at her line; ds:18 = 0x30 (her flags) through the talk; COME WITH ME sets her flag 0x40 and ds:10 bit 7 (0x20 -> 0xa0); the talk's close puts her on the HUD (ds:1153 = 7).

### Gurney's skip (condition 288)

Gurney's STAY HERE answer at a place with a training troop (CD 288 / floppy 287, `b[0x66]`: ds:66 counts military training) is "Good! I'm going to try to teach these Fremen the handling of arms." with action 12, once. It moves any phase to the next chapter the first time it is said:
- at 0x64-0x67 it ends the kidnapping without Chani: phase 0x68, the motivation back, Stilgar's "Good to see you with Chani again" while she is still in room 3. Her line then still comes when she is met (360 accepts 0x68) and brings 0x6c (a nullsub);
- at 0x5c-0x5f it would skip the rest of the epidemic; at 0x60-0x63 it would bring the kidnapping itself (`1f13`).

Most players say it early (the first time Gurney stays at a sietch with an army troop), so it rarely lands in these chapters. The engine follows the data.

### Her other roles

- **The love scene** (phase 0x48, `11139`, floppy `1502`). Her night line in the desert (condition 372, action 12) brings phase 0x48: charisma + 10 through `6f78` (so every troop's motivation + 2 or + 3), the scene 0x1313, her flags + 0x10 ("follows Paul now and always") and − 0x02, ds:1178 = the Fremen troops + 2 (two more rallies bring Leto's death, phase 0x4c), four places revealed.
- **Following Paul.** Her COME WITH ME answers read her flags through ds:18: before the love scene "Follow you? Okay but I don't want to travel far from this place." (368), after it "Yes Paul, I want to follow you, now and always." (395); STAY HERE likewise (368 / 395). The epidemic's refusals come first (394).
- **Her lines by phase** (CD numbers): 373 (0x48, at the place where they met, names 0x0503), 374 (0x4e "Ask Stilgar!"), 375 (0x4f the worm), 318 (0x50 Thufir and the worm), 376-380 (0x54-0x57 her father), 349 (0x58 meeting Kynes), 293 and 365-367 (the epidemic), 360 (0x64-0x68 the rescue), 241, 134 and 135 (the final attack), 104 (0xc8 the end).
- **Healing the sick:** "The Fremen epidemic".

### Found on the way

- **ds:18 and ds:16 were never staged.** `94f3` stages the speaker's record flags (byte 15) into ds:18 and the time since its stamp into ds:16 before every line search; 53 conditions read b[0x18], among them Gurney's introduction (231, flag 0x20: not yet talked), the companions' lines (0x40: with Paul), Chani's before and after the love scene (0x10), and Gurney's espionage hint (266, w[0x16] > 0x10). The engine left ds:18 at its initial 0, so after the love scene Chani still answered COME WITH ME with "Follow you? Okay but I don't want to travel far..." and could say her first-meeting line (368, action 11: phase + 1) again.
- **Flag 0x40 was never kept.** COME WITH ME and STAY HERE changed only ds:10; the original's talk menu (`9825`), the HUD and ds:18 read the record's flag 0x40. The talk's close (`97cf`) also sets flag 0x20 and clears 0x04.
- **The floppy's "Muad'Dib".** Phase 0x2c wrote the CD's COMMAND id 0x109 into ds:1201; the floppy (`14a9`, ds:120e) writes 0xfd, so a floppy game from a new start said "Oh, GAME  PAUSED!" for "Muad'Dib".
- **A sietch lost** (`74b6`, floppy `821a`): the first nine characters staying there go to room 3 of the new fortress, as Chani does in `1f13`. The engine left them in their room of the old type, so they were absent.

### Not built / seen on the way

- `2170` / `221d`: characters left in the desert walk to the nearest place and set flag 0x04 when it is Paul's ("If this was a race, I won!", conditions 37-40). The engine does not move them.
- `1d66` (daily): a character whose place changed type, or whose room is past the type's room count, goes to room 1.
- The Harkonnen raids (`1f64`/`2017`, `1fcb`): a sietch taken by a raid also holds its characters.

### Built

- `World::changeCharisma` returns the spill; the phase callbacks 0x2c, 0x48 and 0x50 use it and log "Story: phase N: charisma A -> B, every troop's motivation +S (6f78)".
- The daily Harkonnen production (`1cda`, floppy `201d`) counts the places that are not Atreides (`friendlyPlace`, `5d36`): no hidden sietch, no held fortress.
- `Conversation::findEntry` stages ds:18 and ds:16 (`94f3`); `World::setTravelling` (COME WITH ME / STAY HERE, the companion sent home) and `World::talkEnded` (`97cf`).
- Phase 0x2c's "Muad'Dib" id follows the release; `battleLost` holds the characters in room 3 ("Battle: character N is held in room 3 of place P").
- The 0x68 callback logs "Story: phase 0x68: the kidnapping is over; ...".
- **The speedrun bot** (the fixes above changed its runs from Stilgar's spill on). Two bot bugs came up and are fixed: after its last known fort fell with only hidden ones left, the bot kept the last attack group "busy" for good, so no troop was free and no spy went out (the CD full run stalled on day 46 and the Emperor ended it on day 97); and it held the war council at place 2 while that was still a fort (no troop training there to convert it), where Jessica refuses to stay ("Oh no Paul! I don't like this place"), when place 4 was already a sietch. It also logs "war: N known fort(s), M hidden, ..." every 16 rounds.
- **Checks.** `scripts/check_chani.sh` (`dune_story_setup=chani` and `chani-gurney`, both releases): the spill at 0x2c, 0x48 and 0x50; the prison and ds:f2; − 40 with the floor of 10; Thufir, Stilgar, the spy's "Chani is here." and "prisoner"; the fortress taken (still 0x64, still − 40); Chani in room 3; her line and phase 0x68; COME WITH ME "now and always"; Stilgar at 0x68; Gurney's skip; Stilgar held in a lost sietch. The fidelity scenario `chani-rescue` plays the rescue on the patched chapter 20 save, with the original in `captures/chani/rescue`.

## SKIP TO DESTINATION on the CD: no approach clip (2026-09-29)

Queue item F1. The diagnosis, with the Spice86 memory dumps, is in notes/f1-cd-speedrun-desync.md.

| What | CD seg000 |
| --- | --- |
| The approach clip, the CD's landing (SIET, PALACE, FORT) | `travel_arrival_landing_sequence`, `488a` |
| Called by the scene reload only when ds:4732 bit 0 is set | `2dfb` in `2db1` |
| The pump's normal arrival arms it: ds:4732 = ds:11C9 & 1 | `loc_4fb0` in `travel_pump` (`4f0c`) |
| SKIP TO DESTINATION: closes the flight video, fast-forwards, jumps to `loc_4fc3` | `4ffb` |

- A flight that runs to its end lands with the clip. A skipped one jumps past the ds:4732 store, so the room is drawn at once, with no clip and no landing animation. Measured on Spice86: ds:11C9 = 5 before the skip, ds:4732 stays 0, and the exterior is on screen 250 ms after the click.
- **Built.** `GameScreen::flyToward` records `_flightSkipped`: false at the top, `skipping` after `stopCdFlightView()`. The hostile-zone warning clears `skipping`, as the original hands the flight back to the pump, whose arrival arms the clip. `travelTo`'s CD branch plays `playArrivalVideo` only after a flight that was not skipped, and never falls through to the floppy's `animateOrni(-1)`, so capture runs no longer dump a CD "orni-landing" frame. The log says "Travel: skipped to the destination, no approach clip".
- **Checks.** `scripts/check_cd_flight_skip.sh` runs two real-time CD flights (the first hop of `cd-speedrun-day1`). Skipped: no clip, and the next click is the exterior's up arrow into the sietch. Left to finish: "SIET.HNM approach clip (result 0)". Fidelity `cd-speedrun-day1` rose from 25.0 to 65.3 %: sp-05 0.8 to 94.3, sp-19 6.9 to 96.3. The rest of the gap is the CD talk layout (F1b), the exterior panel colours (F1c) and the sky (F3).

## Paul's head on the panel (2026-09-29)

Queue item F8. The head above the command box is ICONES 0x10 + ds:E8 at 150,137, drawn over the hinge (ICONES 15 at 126,148). E8 runs from 0 to 10, and 10 (frame 26) faces the player. The routines are the same on both releases; their callers differ.

| Routine | CD seg000 | Floppy |
| --- | --- | --- |
| Draw the hinge and the head | `1797` | `1b2c` |
| Redraw: the rect ds:1E6E (150,137 to 170,160) restored, then `1797` | `17be` | `1b53` |
| Up: one frame every 8 ticks to 10, nothing while travelling | `17e6` (ds:11C9) | `1b64` (ds:11D6) |
| Down: one frame every 8 ticks to 0 | `181e` | `1b82` |
| Fold: 9, a wait, 8 (only when not 0) | `1843` | `1b98` |
| The line wrapper: skipped when ds:28E7 != 0; sets ds:CE66 while it runs | `1803` | none |

| Event | CD | Floppy |
| --- | --- | --- |
| LOOK AT MIRROR (down) | `0eac` | `1279` |
| RESTART GAME from the game-over menu (ds:46D9 set: DEAD2.HNM, down as its first frame shows); from the mirror the head is already down | `0e47` -> `0e6c` | `1197`: the mirror's zoom out (`11a7` -> `1239`), then `124a` |
| Game over (DEAD.HNM, down as its first frame shows) | `0dc2` -> `0e66` | none |
| Paul collapses in the desert (DEAD3.HNM, five frames with the head redrawn, then down; not built in the engine) | `3757` -> `0e77` -> `0ea3` | |
| THE BOOK (down) | `aee1` (ui element 03) | `ae77` |
| Travel confirmed (down) | `4745`, only when (ds:11C9 & 3) == 1: the ornithopter | `4f55`, every travel |
| The departure transition | `47ad`: ds:E8 = 0 at once when not leaving from the cockpit (the worm); `47db`: down for the cockpit's take-off | `4fc7`, `5019`: down in both cases |
| A character's line (lip-sync id < 0x10, ds:46EB == 0) | `9fec` -> `1803` (down) | none |
| ui_enter_room_view at the start (fold) | `1868` | `1bbc` |
| Up | ui_present_room_screen `18b7`, draw_room_game_screen `2df8`, `2e7a`, the globe/map view `5a3a`, menu_npc_actions_cleanup `9895`, the globe element `b8e7` | `1c05`, `30ae`, `3126`, `67e0`, `b7e6` |
| The dream | `2c9a`: ds:E8 = 0 with the backdrop; `2c7f`: ds:E8 = 10 before the room | nothing: the head stays up |

- **The voice mode.** `cfa0` sets ds:28E8 (the default) to 2 when DNCDPRG finds digital voices and the language is 0 or 3; otherwise it stays 0 (text). The room view copies it into ds:28E7 (`1877`); the globe/map view forces 1 (`5a1a`). So the CD lowers the head for a character's line in text mode, and not with voices or over the map.
- **The originals, captured.** Floppy, captures/explore (ds:E8 in the memory dumps): 10 in the rooms, the talks, the map and the globe; 0 in the book, the mirror and its menus, the take-off and the flight (ds:11D6 = 5); 10 again at the exterior. CD, captures/cd-speedrun-day1: every character's line shows the head turned away, and it faces the player again after the talk. The floppy's dream (captures/saboteurs/message-memdump) keeps ds:E8 = 10.
- **Built.** `Panel` draws ICONES 0x10 + its head index last and keeps what lies under it; `Panel::redrawHead` steps it in place. `GameScreen::headUp`, `headDown`, `headFold` and `setHead` keep ds:E8 (`GameState::kHeadIndex`, so it is saved) and step 40 ms a frame; capture runs set the last frame at once, so a harness run always ends on it. `headForDeparture` follows the release; `_headTravel` keeps the head from coming up between the departure and the arrival; `lineHeadDown` is the CD's `1803`; the CD's game over lowers it. RESTART needs no call of its own: the mirror has lowered the head, and the new game's first room raises it from 0. `_voiceMode` models ds:28E7: the engine plays no CD voices, so its default is 0, and the dev key `dune_cd_voice_mode` (0 to 2) models the voiced modes. The take-off animation draws the head over the view.
- **Check.** `scripts/check_head.sh`: real-time runs on both releases, and on the CD with `dune_cd_voice_mode=2` (the mirror, the room after it, the palace front, the cockpit, the take-off, the flight, the exterior, Gurney's first line), the head read from the pictures; capture runs of the same script must end on the same frames.
- **Goldens.** `cd/cd-leto-1..3` were replaced (the head turned away while Leto speaks, as the CD capture sp-20 to sp-22 shows). The other checkpoints whose head changed (`story-scene/scene-*`, `story-stillsuit/*`) stay within the tolerance and keep their goldens: no capture of those moments exists.

## The vision dream (2026-09-29)

Queue item F5. When Paul idles in a room with a message queued (0x1c2 ticks, CD `2b8a`, floppy `2e75`) and the sender is not there, the message comes as a dream. The two releases share the idea, not the code: the floppy's routine is not the CD's shifted by a constant.

| Step | CD seg000 (`present_vision_dream`, `2bd2`) | Floppy (`2eba`) |
| --- | --- | --- |
| Stage the place for the conditions and the names 0x81/0x82 | `2bf1`, `2c1d` -> `331e`, `2e98` | `2ec8`, `2ee7` -> `35ca`, `3144` |
| A Fremen report goes to the place's troop (byte 9 of the place record; troop 3 for message 0x0e), lip-sync 0x0e | `2c23`-`2c43` | `2eed`-`2f0d` |
| Transition 6 into the backdrop | `2c92` -> `2c9a` | `2f4c` -> `2f54` |
| The backdrop | `2c9a`: ds:E8 = 0, VIS.HSQ sprite 0 | `2f54`: VIS.HSQ sprites 0-4 (five layers); the head untouched |
| The line (DIALOGUE character 16, list 4) | `96d8` | `a195` |
| The voice | `2c4a` -> `9ef1`: the line's voc with lip-sync when a voice file exists (`a6cc`, `a75c`) | none |
| Dequeue | `2a34` | `2d24` |
| Blank verbs | `2c52`-`2c5a` | `2f17`-`2f1f` |
| The shimmer: a frame task every 6 ticks, effect 0x0a | `2cc7` | `2f8e` |
| The wait (a click or a key ends it) | 0xbb8 ticks (`ddb0`) | 0x7d0 ticks (`d779`) |
| Back: ds:EA = 0xff, the room presented (al = 6) | `2c7a`-`2c8c`, ds:E8 = 10 first | `2f3f`-`2f46` |

- **The shimmer** is a palette rotation: colour 128 + i shows VIS's colour 128 + ((i + step) mod 64), one step every 6 ticks. Measured from the DAC dumps on both releases.
- **The floppy's line** is in the talk balloon (ICONES 0x1c tiles, blue under VIS's palette) at 152,17 to 316,101, in colour 240, which VIS's palette makes light.
- **The CD's line.** In text mode (ds:28E7 = 0) it sits at the foot of the view: the 9-row font, white with a dark one-pixel outline, from x 15, the last line's top at y 136.
- **The CD's map window.** The line carries dialogue action 13 (`callback_event_dialogue_line_0d`, `a28e`). Unless the voices speak alone (ds:28E7 == 1) or the place is Paul's own, it draws PALPLAN.HSQ sprite 7 at 168,23, the map window 176,32 to 288,88 centred on the place (`5b55`, `map_draw_zoomed_globe`), the markers (`5dce`) and ICONES 0x36 over the place (`62fe`). The floppy's capture has no window.
- **Evidence.** The floppy dream is built to match its capture (captures/saboteurs/message; message-memdump adds the memory: ds:E8 = 10, speaker 0x0e). The CD dream is built from the code above (and madmoose's annotations and ports), not from a capture, as the user asked. A CD capture had been taken before that change of plan (captures/cd-dream: a patched day-1 CD save, tests/fidelity/saves/cd-dream; series/ holds one frame a second). It shows the same screen: the dream about 3 s after the load, for 16 s, ds:E8 = 0, ds:47C4 = 0x0e, ds:28E7 = 0, then the palace front with ds:E8 = 10.
- **What was wrong.** The engine drew VIS sprite 0 alone (on the floppy a sparse layer, so the clouds came out mostly black), deleted the portrait, showed " Continue..." and waited for a click. It also spoke the report through the troop at Paul's place, not the one at the message's place.
- **Built.** `presentVision` keeps the portrait (`_dreamTroop` from the place record), sets the CD's ds:E8 = 0 and times the dream. `drawTalk` draws the release's backdrop, the floppy's balloon or the CD's subtitle, and the CD's map window (`drawPlaceInset`, only for the dream so far). The verbs are blank. `update` rotates the palette and ends the dream; a click or a key ends it early (`endDream`). Not built: transition 6 (the engine has no transitions yet) and the CD voice (the engine plays no CD voices).
- **Checks.** `scripts/check_dream.sh` (both releases, real time: the troop, the portrait, clouds not black, blank verbs, the head, the end by itself). Fidelity `saboteurs-message` (floppy) and the new `cd-dream`. `scripts/dune_fidelity.py` now also copies a scenario's CD saves (DUNE37Sn.SAV).

## The light of the hour and WAIT FOR EVENING (2026-09-29)

Queue items F2 (the evening fade), F3 (the exterior colours by time of day)
and F1c (the CD exterior's panel colours). One cause for F3 and F1c, and the
fade for F2.

### Addresses

| What | CD (seg000) | Floppy |
| --- | --- | --- |
| sky_palette_id_for_time | 395c / 395f, table ds:2280 | 3bea, table ds:28d6 |
| open_sky_or_skydn_palette | 3971 (resource 0x28 + ds:22e3) | 3bff (0x28) |
| write live / fade target | 398c / 39b9 | 3c16 / 3c36 |
| set_sky_palette | 388d | 3b13 |
| the periods' sky refresh | 38e1 (from 1b43) | 3b7b |
| the blend's step task | 3916, every 0x10 ticks | 3bb1 |
| drain a running blend | 390a | (the same loop) |
| WAIT FOR EVENING / MORNING | 0f48 / 0f67, preset 0fb2 | the same code |
| present with transition 0x2a | 189a (al = 0x2a), c108 | the same |
| draw_outdoor_backdrop (CD only) | 380c, bases ds:1972 | none |
| draw_SAL skips sheet 0 (CD only) | 3b68 | 3dde opens every sheet |

### The record

`sky_palette_id_for_time` puts the period of the day (time & 15) through the
table 8, 8, 9 x 9, 10, 10, 11 x 3 and adds (time >> 2) & 0x1c, four records
per day of the week: sunrise, day, sunset and night of days 0-7, the offset
table's entries 8-39. With $t$ the game time:

$$
\mathit{record} = T[t \bmod 16] + 4 \left( \left\lfloor t / 16 \right\rfloor \bmod 8 \right)
$$

Checked against the DAC of the captures: floppy evening/desert (time 3: 9),
evening/e39 (0x0c: 10), go-search se-02/se-03 (0x24, 0x25: 17); CD cd-dream
cdd-3 (8: 9). The engine used swift-dune's fixed 1, 3, 6, 16 (records 9,
11, 14, 24): right only for the day of day 0. SwiftDune's "min(day, 5)"
does not appear in the code.

### The ranges

The floppy writes SKY.HSQ's first 80 colours at 128 and the next 15 at 240
(the panel). The CD reads SKYDN.HSQ when ds:22e3 = 1 (every room but 0x1005:
loc_139EC sets it, 13A18 clears it) and writes 151 colours at 73 and the next
16 at 240; with ds:22e3 = 0, 80 at 128 and 16 at 240.

### The CD's exteriors

A place's first room on the CD is not the arrival video's last picture:
`draw_outdoor_backdrop` (380c) sets ds:22e3 = 1, calls set_sky_palette and
draws sprite 0 of resource room1_backdrop_base[kind] (ds:1972: 0x3c DS0
sietch, 0x72 DP1 palace, 0x7f village, 0x76 DF1 fortress, 0x84 DH0 Harkonnen
palace; a village's 0x7f becomes 0x7a + first name / 2, VIL1-VIL6). The
stills carry no palette. Then `draw_SAL` does not open sheet 0 (GENERIC, 3b68),
so the sky record's tail stays on the panel: blue by day. The engine drew the
PALACE.HNM picture (its sky a flat violet) and re-applied GENERIC's colours
(the yellow panel of F1c). DP1.HSQ under record 9 matches cd-speedrun-day1
sp-03 pixel for pixel.

### The blend

`set_sky_palette` (388d) sets ds:46df (an outdoor view is up) and, unless a
blend runs toward the same record, writes the record. Each period passed
(1b23 -> 38e1) while ds:46df is set, when the record changes: the record
becomes the target (ds:46d6), ds:46d7 = 0x40 and the step task every 0x10
ticks, each step moving the live colours by (target - live) / steps left,
truncated. A room draw clears ds:46df (2e05, 08f0, ui_teardown_room_view
18de), so the blend stops when the view is left.

WAIT FOR EVENING (0f48) goes to period 12 of today, WAIT FOR MORNING (0f67)
to period 0 of the next day (only from period 11). Before dawn (EVENING from
periods 0-1, MORNING from 11-12) the record of time + 2 is set first (0fb2).
A running blend is drained (390a), the periods pass (each arming the blend),
and the view comes back through transition 0x2a (the spiral, segvga 2eea):
one pixel of each 8x8 block per step goes black through a 65-entry spiral
walked backwards, the new palette is set, and the new picture comes back
through the spiral forwards. The palette is still the old light (the blend
is armed, not stepped); the frame tasks do not run during the transition,
so the blend's 64 steps start after it. With the truncated step the first
visible change comes some 17 steps in. Measured (captures/evening, one
checkpoint every 500 ms): the spiral by 1.5 s, the first change at 3.0 s,
the evening reached at about 6.75 s.

### Built

- `GameScreen::skyPaletteFor` (the record), `loadSkyRecord` (the ranges),
  `setSkyPalette` (388d), `skyPeriodChanged` (38e1, at once in capture runs
  except for the WAIT verbs), `updateSkyBlend` (3916), `spiralPresent`
  (transition 0x2a, paced at 11 ms a step, fitted to the capture), the WAIT
  verbs as 0f48/0f67; `drawBackdropStill` (380c) for the CD's first rooms;
  the CD's draw_SAL leaves sheet 0's palette alone.
- The Amiga keeps its own table (`amigaSkyRecord`, the same rule).
- Checks: `scripts/check_sky_light.sh` (floppy blend and pixels at e05/e39,
  CD palace front pixels and the still); fidelity scenario `evening`
  (captures/evening, real time).
- Not built: the CD's desert stills (380c's DN20-DN38 / VG01-VG10 terrain
  tiles); the engine's desert and cockpit on the CD still draw the floppy's
  landscape under SKY.HSQ.

## The map contact's rows and view (2026-09-29)

Queue item F6. The same code in both releases.

### Addresses

| What | CD (seg000) | Floppy |
| --- | --- | --- |
| CONTACT FREMEN TROOPS | 86cc | (by bytes, not needed) |
| contact at Paul's place (short range) | 86b9 | |
| NEXT TROOP / the next troop with an icon | 86fa | |
| open the contact, patch the fifth row | 780d-783b (782d) | 8553 |
| the greys | 7847-78bb, menu ds:210c | 856d-85e1, menu ds:2772 |
| free equipment at the place | 7f27 | 8c38 |
| GIVE ORDERS TO TROOP from the room | 5a03 (inc ds:46f3) | |
| the DUNE MAP popup | 5a1a -> 5bb0 (first visit), dismissed at 5c03 | |
| EXIT GLOBE | bc81 -> 5a3d (no popup) | |
| " Others..." paging | d36d-d410, d423, d429, d45d | |

### The rows

The contact menu is ASK FOR MORE INFORMATION, CHANGE TROOP OCCUPATION,
MODIFY EQUIPMENT, MOVE TROOP and a fifth row. The fifth is patched when the
contact opens (782d: `mov ax,52h; cmp [46f3],1; adc ax,0`): CUT CONTACT (0x53
CD, 0x49 floppy) on a map visit, NO MORE ORDERS when the room's GIVE ORDERS
TO TROOP opened the map (5a03 bumps ds:46f3; that close returns to the room).
The static record holds NO MORE ORDERS on the CD and CUT CONTACT on the
floppy, which is why a memory dump alone suggests the releases differ.

The greys: the occupation, equipment and move rows start greyed. An ill troop
(word 0x12 bit 0x400) keeps all three. Otherwise the occupation opens unless
the troop prospects (job 1); a waiting troop (job 2) gets SELECT TROOP
OCCUPATION and nothing more; MOVE TROOP opens from phase 5; MODIFY EQUIPMENT
opens from phase 4, not while the troop repairs (word 0x10 bit 0x200), at a
place held (status bit 3) or not a fortress (type below 0x28), and only with
something free there (7f27) or carried (byte 0x19).

### The view

CONTACT FREMEN TROOPS does not move the map. It resumes the last troop
contacted while that troop still has an icon on the map, else takes the
next troop by id that has one (86fa). Only the short-range case (ds:1176
below 2) centres on Paul and contacts the troop at his place (86b9). The
engine centred the map on the troop, so the contact showed another part of
the planet. That was the "late-game terrain" of the saboteurs and epidemic
contacts. The engine has no troop icons yet: a troop whose place (or march
position) lies in the view stands in for "has an icon".

The DUNE MAP popup ("Map to command rallied troops") comes only on the way
in from the room (5a1a, first visit). EXIT GLOBE (5a3d) and the return from a
move order do not raise it.

### " Others..."

A command menu shows five rows from its skip byte. The fifth becomes
" Others..." (COMMAND 0xa0) when more records follow or the menu is
scrolled. After the last record, a scrolled menu still shows " Others...".
The row turns the page by four records (d423) or rewinds (d429). A rebuild
resets the skip. Built for the room menu (the endless-play battle shows it:
four Fremen chiefs after CALL A WORM).

### Built

- `drawTroop`: the greys, the fifth row by the way in (`_troopFromRoom`, set
  before the menu is drawn), a log line of the rows.
- `kRowContact`: no recentring, a troop in view (`troopInView`).
- No DUNE MAP popup after EXIT GLOBE or a move order.
- `drawRoom`: the " Others..." paging (`kRowOthers`, `_roomRowSkip`).
- Check `scripts/check_contact_rows.sh` (the ill troop's and the repairing
  troop's rows, floppy).
- Not built: the map's troop icons (6827/68d2); the CD run of the check (the
  saves are floppy).

## The CD flight's clips (2026-09-30)

Queue item F7. Only the order of the clips and screens has to match the
original (the user, 2026-09-28).

### Addresses (CD)

| What | seg000 |
| --- | --- |
| travel_pump: a flight frame a pass, a step every 0x300 ticks | 4f0c-4f31 |
| travel_probe_terrain_ahead | 4e8e-4ec4 |
| travel_select_flight_video | 4ec6-4f03 |
| travel_arrival_landing_sequence | 488a-48e3 |

### The rules

- **The pace.** The CD takes a travel step every 0x300 ticks (4f2e,
  3.83 s). The floppy's recordings show 0.64 s a cell, which the engine had
  used for both releases. Palace to Carthag-Tuek is 22 steps, about 84 s on
  the CD. The flight clips advance one frame a game-loop pass: 16 ticks,
  measured on the capture (MNT2 191 frames and MNT3 197 frames in about
  15 s each).
- **The probe.** After each step, the map bytes six and seven steps ahead
  are averaged whole, `(a + b) >> 1`, with the stage bits included. The low
  nibble 8 or more is rock. The engine averaged the low nibbles of the
  current cell and the one six steps ahead.
- **The choice at a clip's loop point.** On rock: MNT2 (sand to rock) from
  MNT1 or MNT4, else MNT3. On sand: MNT1 from MNT1, MNT4 (rock to sand) from
  MNT2 or MNT3, else MNT1.
- **The arrival.** A sietch (SIET) or the palace (PALACE): the minimap goes
  (489d). The clips run on over sand until MNT1 plays frame 0x3c (sietch) or
  0x16 (palace), then the approach clip plays. A fortress plays FORT.HNM. A
  village or the Harkonnen palace has no clip. SKIP TO DESTINATION has no
  run-in and no clip (F1).

The original's sequence on palace to Carthag-Tuek (captures/cd-flight-long,
the clip id at ds:dc00 read every second): take-off, MNT1, MNT2, MNT4, MNT2,
MNT3, MNT4, MNT1, SIET, the exterior. The engine's positions now match the
capture's step by step (ds:4/ds:6), and so does the sequence. The switch
times are within a few seconds of the capture.

### Built

- `kCdFlightStepMillis` (3834 ms) for the CD's steps.
- The probe of bytes 6 and 7.
- The clip frames at 80 ms, paced by the clock.
- The sand run-in before SIET or PALACE; no approach clip for a village or
  the Harkonnen palace.
- The blocking take-off and the spiral now let a harness script's
  checkpoints fall at their time (`waitPumping`), so the take-off shows in
  cd-flight-timed at 2-3 s, as in the original.
- `scripts/check_cd_flight_skip.sh` checks the unskipped flight's clip
  sequence against the original's (`DUNE_HARNESS_SECONDS` lengthens the
  harness's time limit).

## The CD talk screen and " WHAT ? " (2026-09-30)

Queue items F1b and F1d.

- **The zoom, both releases** (CD 3af9-3b55, floppy 3d80-3dc4, the same
  code). The speaker's anchor is the SAL character part's position, recorded
  as the room is drawn (CD ds:47f8/47fa). Clamped to 0xf0 and 0x71, it is the
  top-left of an 80x38 window that vga_zoom scales 4x (bp = 6) over the
  view. With no anchor there is no zoom (3b21). The engine had a 2x zoom
  round a guessed centre. The floppy's Leto window (186, 53) now equals the
  capture's (explore-16).
- **The CD's text mode** (voice_subtitle_mode 0, subtitle_setup_layout 8d62)
  has no balloon. The line sits in the strip ds:223c at the foot of the view
  (it ends at y 0x92, loc_09025), with 16-pixel side pads, in colour 15 with
  a colour-8 outline. layout_subtitle_lines (8e16) gives each word its width
  plus a 6-pixel gap from the 288-pixel budget. commit_line spreads a line's
  leftover over its gaps (leftover / gaps + 6, the remainder one pixel each).
  The last line keeps 6-pixel gaps, and nothing is justified when any line
  would need gaps of 0x1e or more (8df0). The dream uses the same strip.
- **" WHAT ? "** (F1d): the CD's menu_NPC_actions (setup_npc_dialogue_menu
  90bd-9118) is " TALK TO ME ", the speaker's verb, " WHAT ? " (the last line
  again, 9ed5) and STOP TALKING. The floppy's COMMAND1 has no " WHAT ? ".
- **The room rotation** of cd-speedrun-day1: ds:C5 is random at each
  landing. The original's were read back from its zoom windows: Carthag-Tuek
  8, Carthag-Harg 6, Carthag-Timin 2 and the palace 7, now set in the
  scenario (`dune_room_rotations`).

## The night battle view (2026-09-30)

Queue item F4 (built last, the user's order).

### Addresses

| What | CD (seg000) | Floppy |
| --- | --- | --- |
| location_arrival_hostility_check (ds:2b, backdrop) | 503c-5075 | |
| the room draw in a battle | 2dd3-2df8 | 308c-30ab |
| stage_28_night_attack_start | 0acd-0b1e | 0c51-0ca2 |
| its teardown | 0b21-0b44 | 0ca5-0cc1 |
| the particle task | 0b45 | 0cc2 |
| the menu in a battle | 2f1d-2f3b, then 2fa3 | 31c4-31e2 |
| the compass in a battle | | 329f -> 32be |

### What the original does

While Paul is in a battle (ds:2b, set by 5058 on arrival at a place in
battle, or at a place not the Atreides' with Fremen attacking it), every
room draw goes the night-attack way (2dd3). It clears ds:46df (no sky
light) and draws ATTACK.HSQ (colours 0, 128-183 and 240-254, so the panel
turns green): sprite 2 tiled from the top, sprite 3 from y 81, the backdrop
at (0, 76) and sprite 1 at (0, 134). The backdrop is patched by the place's
kind at 505f-5075: 0x2f for a sietch, 0x30 for the palace, a village or the
Harkonnen palace, and 0x33 for a fortress. The intro's night attack uses
0x31. The particle task runs every 3 ticks (0b10), and SN3 plays (0b1e).
The compass box is blank (floppy 329f).

The first room's menu is SEE DUNE MAP, MASSIVE ATTACK, FIGHT FOR A WHOLE
DAY and CALL A WORM (greyed before phase 0x4f). As in every room, the CD's
Mixer Panel (2fa3) and the people follow. With more than five records the
fifth row is " Others..." (captures/endgame/endless-play ep-0: the four
Fremen chiefs on the next page).

### Built

- `drawNightBattle` / `updateNightBattle` / `endNightBattle` (scene.cpp),
  with the engine's `NightAttack` simulation (the intro's) and
  `NightAttack::setBackdrop`. The menu goes on to the people, and the
  compass is blank.
- The simulation runs in real play only. Capture runs draw its first state.
- The place's Fremen troops stand in room 1 during a battle, at any kind
  of place (classify, 3187), so the menu lists their chiefs.
- Check `scripts/check_night_battle.sh`: the view, its backdrop, and the
  menu's two pages.
- Not built: the attack's sound (SN3.VOC, looped); MASSIVE ATTACK's denser
  pattern (the simulation's `massive` flag, set by the original's
  set_massive_attack); the troop icons the task also moves (0afb -> 5ba0).

## Harkonnen raids, merges, skill decay, the captain and the gauge (2026-09-30)

Queue item 10 (the wiki-audit gaps), the rules. The code is the same in both
releases unless stated.

### Addresses

| What | CD (seg000) | Floppy (unpacked image) |
| --- | --- | --- |
| actions_time_in_day_4, the raid | 1f64-2014 | 227c-232c |
| harkonnen_pick_attack_target | 2017-208f | 232f-23a7 |
| the byte that skips a raid | ds:11bc | ds:11c9 |
| the troop walk (merge, handler, skill decay) | 6c92-6cfa | 7a00-7a62 |
| the small-troop merge | 6d19-6d7a | 7a81 |
| the skill decay | 6d7b-6dba | 7ae3 |
| the captain's set-up | 31c9-31f5 | (likewise) |
| OVERPOWER THE PRISONER | 9584-95be | (likewise) |
| the battle gauge | 60f8-6143, 6144 | (likewise) |

### The raids

- **When.** A raid can come on slot 4 of the day. Before phase 0x3c it also
  needs 0x70 periods (7 days) past ds:1154, the stamp that phase 0x2c
  (Stilgar) writes. The day must be even (time & 0x10 clear). The skip byte
  must be clear; it is always cleared here, and every period of a Fremen
  attack sets it (739e). ds:0 is rotated left and the raid needs the bit
  rotated out (one time in two).
- **The target.** It is a sietch (type below 0x20), neither hidden nor in
  battle (status 0x82), and without an ill troop (1e24; the floppy's 2169
  tests the first troop only).
  - It needs Fremen troops other than the prospectors (ds:60 - ds:63).
  - It needs a fortress within 30: the nearest hidden one (ds:e2/e4), else
    the nearest known one (ds:dc/de). That fortress is not the Harkonnen
    palace, and holds Harkonnen troops but no attacking Fremen.
  - Of those sietches, the northernmost wins (the first of equals, starting
    from latitude 100).
- **The raid.**
  - ds:c4 + 1. Twice, the first Harkonnen troop of the fortress gets
    occupation 0x8d, a move order, loses its hidden bit and lands at once
    (8357; with bit 7 it keeps that occupation).
  - The sietch is put in battle (status 2) and its troops defend it (83fd:
    occupation 6, the prospectors stop).
  - The characters staying there go to its room 1 (140ae).
  - A troop chief tells "The Harkonnens are attacking -!" (message 0x0c),
    or the prospectors' warning (0x0d) when they are there.
  - When Paul is there: room 1, ds:0b, ds:2b = 1 (the night battle) and the
    gauge (6144).
- **The outcome** is the usual battle (739e): a lost sietch becomes a
  fortress (74b6), its characters held in room 3. Every landing clears ds:2b
  and ds:fd first (503c).

### The troop walk

- **The merge.** A troop under 20 (× 10 men) is merged before its handler
  runs, if its occupation has none of the bits 0xe3, it lacks bitfield bit 7,
  and it is not the prospectors.
  - It merges into the troop at its place with the fewest men that still
    fits in a byte (the last of equals), counting only Fremen troops that
    are not 0xa0 and not the prospectors.
  - That troop gets the men and both troops' equipment, and speech word
    0x200: "A small troop has merged with us." (condition 528).
  - The small troop leaves the game (66b1).
- **The skill decay** runs after every handler, when the clock word is a
  multiple of 64. The mask is 0xc000 rotated left by the job class and
  masked with the speech word: bit 15 is ecology, 14 army, 13 spice. So a
  spice troop can lose a point of army and of ecology, an army troop only
  of ecology, and an ecology troop nothing. The wiki's "the other classes"
  is only true for spice.
- **The floppy's walk** (7a41) has no march test in the 0x430 branch: a
  troop that sulks, quarrels or is ill stops on the march until ds:fa is
  set. The CD's walk (6cd3) keeps it marching.

### The Harkonnen captain

- **Entering room 3** (31c9): his record's flag 0x10 (ds:10a7) mirrors his
  troop's occupation bit 4. ds:ee is cleared. ds:ed is 0xff when he is
  overpowered, else his troop's motivation.
- **His talk menu** (90c0) offers " OVERPOWER THE PRISONER " while he is not
  overpowered.
- **The verb** (9584) acts once per speaker (the ds:ee bit): 0x29 comes off
  his motivation and off ds:ed. Below 0 he is overpowered: occupation bit 4
  and ds:10a7 bit 4, which his lines read as ds:18 bit 4.
- **Then block 0x85** is presented (character 16's list 5): "We were enough
  to handcuff him..." or "I can't overpower him alone...". His lines then
  tell of the fort he knows (cond 395, 435-438).

### The battle gauge

The gauge is 60f8, over the place's chain:

- the Harkonnens' men H;
- for the Fremen fighting there (occupation 6), their men F, the Harkonnens
  they killed (word 0x0c) and their own losses (word 0x0e).

Each side's share still standing is men × 256 / (men + losses). The gauge
is 0x80 + 128 × (Fremen share − Harkonnen share) / the larger share, as a
byte. ds:fd = gauge | 1 is set when Paul lands in a battle (505c) or is in
a raided sietch (2010). The chiefs' contact lines read it (conditions
600-602: "we have nearly routed the Harkonnens", "very close", "severe
losses").

### Built

- `World::harkonnenRaid`, `pickRaidTarget`, `raidSource`,
  `raidSuppressOffset`, `mergeSmallTroop`, `skillDecay`, `captainEnters`,
  `overpowerCaptain`, `battleGauge` and `seedBattleGauge`.
- The row `kRowOverpower`, the night battle picked up in `battleCheck`, and
  the floppy's walk difference.
- A test key `dune_no_raids` for the epidemic check's setup.
- **Found on the way:**
  - The skip byte was written at the CD's offset on the floppy too.
  - `linkTroop` could link a troop twice. A raider that the move order had
    already landed was linked again and looped the chain; that fort then
    counted every Harkonnen troop in the game.
- **The bot adapts as a player would:**
  - It keeps its soldiers out of reach of a fortress that could beat them.
  - Its miners prefer fields out of reach.
  - It keeps in touch with every troop weekly.
  - Soldiers march to a raided sietch.
  - It gathers away from raid reach, and walks into a fort that its raiders
    left empty.
  - It puts the prospectors back to work after a battle.
  - It turns soldiers into miners when the spice runs short, and parks the
    council's allies in room 2 (see below).
  - With all that it takes longer: day 87 (floppy) and day 95 (CD), against
    62 before.
- Check `scripts/check_raids.sh` (raid, raid-paul, troop-rules, captain,
  characters) on both releases.
- **Not built:**
  - the map place popup's "Battle:" panel (160ac: ICONES 0x8e + (gauge +
    15) / 32) and its spice, water and equipment lines;
  - the map's troop icons (6827, 68d2: the icon scripts at ds:1672-1935,
    about 800 lines in dune-re's troop_icons.rs);
  - the density panel's TROOP OCCUPATION overlay (ds:4722, 563e/57b5).

## The characters, the room scans and the desert collapse (2026-09-30)

Queue leftovers.

### The room-leave scan

- **The scan** (CD ui_click_move_room 3faa-3fc2, run_room_leave_dialogue_
  scan 36d3, npc_auto_dialogue 3520). A move sets ds:0c to the room Paul
  heads for, pending_room_action 1, and arms the interrupt gate.
  - The first person in the room whose topic-4 line holds says it. The line
    is picked before that person counts as met (35a6 marks them afterwards).
  - A line with event 2 stops the move: Leto's "Where are you going so
    fast? I have to talk to you!" (room 4, Leto not met), Jessica's "DON'T
    USE THIS DOOR!", a sietch's "No, no wait!".
  - Other lines (Duncan's "Hey, that's not the way to the communication
    room!") let the move go on. The engine then closes their talk.
- **Capture runs** skip the scan, as they skip the entry scan, unless
  `dune_room_scans` is set. Check `scripts/check_room_leave.sh`.

### The characters on a landing and a new day

- **On a landing** (CD 2170, floppy 2488, from 4054), the first nine
  records, those not travelling with Paul:
  - Duncan at the palace goes to its room 4.
  - A character waiting in room 1 of another sietch or the palace, not in
    battle, goes one room further in. At the palace it goes to its own room
    (ds:144d by the record's byte 0x0e; room 6 counts as 10 before phase
    0x24).
  - Stilgar and the later characters wander the palace rooms 2-12 (not 3
    before phase 0x54, nor 6 or 11 before phase 0x24). The engine's random
    generator stands in for rand_iterated.
  - A character left in the desert walks to the nearest place's room 1
    (221d: 5344, 40ae), with flag 4 if that is where Paul lands.
- **In the desert**, STAY HERE now keeps the desert position in the record
  (ds:4 the longitude, ds:6 fine << 8 | row).
- **A new day** (CD 1d66, floppy 20a8, from 1c5c): a record whose place
  changed kind, or whose room is past the kind's room count (cs:1d35), goes
  to room 1.
- **The bot:**
  - It parks the council's allies in room 2, since room 1 empties on every
    landing elsewhere.
  - It reads a character's record again after landing: Stilgar wanders.
  - Check `scripts/check_raids.sh` (characters).

### The desert collapse (CD)

After 0x37 unrested steps the CD plays desert_collapse_cutscene (3757 ->
0e77): DEAD3.HNM's first frame, then its five other frames, each revealed
by transition 0x3c. That transition is the slow dissolve (segvga 2a10): a
15-bit LFSR walks the game area, 80 pixels a tick. Then the head goes down.
The WORMSUIT score is not built. The floppy has no cutscene; its 3a06 path
(ds:15cf / 15d1, 11c7) is not traced.

### The night battle: sound and MASSIVE ATTACK

- The battle's start plays sound effect 3 (CD 0b1c: SN3.HSQ; the floppy's
  SD3.HSQ).
- **MASSIVE ATTACK** (7317-7396):
  - It sets the simulation's massive flag, whose fire is denser.
  - After the rounds, 20 rounds of rand_masked(0x201) force the sky flash
    to 0x0b or 0x11, with a wait of 0x28 ticks, 2 more when the high byte
    is set. The ticks are taken as 5 ms each, an assumption.

### Action 13 in person (CD)

The CD's action 13 (a28e) draws the map window on the line's place (ds:47e6)
whenever it is not Paul's place and the voices do not speak alone.

- The line's place is a vision message's place when a chief delivers it in
  person, and the ill place for "We have to go to - to stem the epidemic".
- The dream already drew the window. The floppy's event 13 (a9ce) only sets
  ds:42ce to 600, which is not built.

### Closing a map contact

Closing a map contact (CD 7b7c-7b89, floppy 889a-88a7) runs every time: the
ds:4c test always reads 0, since 7b65 has just cleared it. It does three
things:

- word 0x10 keeps only 0x3f0;
- word 0x12 loses 0x1a00;
- byte 0x14 = the day.

Checked on Spice86 (floppy, the saboteurs' patched save, captures/contact-
close): troop 1's word 0x10 went 0x8300 -> 0x0300 and its byte 0x14 went
1 -> 5. The saboteurs section's "the event bits stay" is wrong for the
contact's close.

## The small troop rules (2026-09-30)

Queue item 9. The same code in both releases.

### Addresses

| What | CD (seg000) | Floppy (unpacked image) |
| --- | --- | --- |
| troop_location_do_stuff_upon_new_day | 6e20-6e81 | 7b88-7be9 |
| the sietch's ring | 6cfc | 7a64 |
| lower motivation (sulk below 5) | 6f93 | 7cfb |
| charisma loss (spill through 6f93) | 6fb0 | 7d18 |
| the contact's day (byte 0x14) | 7b86-7b89 | 88a4-88a7 |
| the duration phrase | 32c7-330f | 356b-35bb |
| the number written into it | d03c, e2e3 | ca9f, ddef |
| SPECIALIZE IN ARMY / ECOLOGY | 6a83/6a87 -> 6a89-6ac3 | 781d/7821 -> 7823-785d |
| the line routine | segvga 1a07 / 1adc | (DUNEVGA) |

### The rules

- **The new-day routine** is the first thing the spice (6fe5), army (71ef)
  and irrigation (76cb, once vegetation has started) handlers do. It acts
  on a day's first period (ds:46de).
  - A fortress still held two days after its fall turns sietch. Until then
    the rest of the routine is skipped.
  - **The ring (6cfc).** At a sietch (type below 0x20) that is not being
    irrigated (status bit 0), byte 0x0b grows by one a day, up to 12. It
    grows once for each working troop there, since every troop's handler
    runs the routine. The disc of that radius becomes Atreides land (644e:
    stage 0x20; vegetation cells keep theirs). The first rally set it to 2.
  - **The decay.** A troop more than 8 days past its byte 0x14 loses 1
    motivation (6f93). Byte 0x14 is the day it rallied (6701) or the day of
    its last map contact (7b89). Below 5 the motivation is 4, the troop
    stops (occupation bit 4) and sulks (speech word 0x20).
  - Then the north/south quarrel (see its section).
- **A charisma loss (6fb0)** goes no lower than 1. Its spill lowers every
  active troop through 6f93, so a troop can sulk from it. The engine had
  used a plain clip.
- **The duration phrase (32c7)** counts the periods since the job began
  (word 0x0a) into ds:42, and the days into ds:41.
  - A stopped troop says "but our job is finished".
  - Otherwise: below 3 periods "for a very short time", below 16 "for a
    few hours", below 32 "for 1 day".
  - From 32 periods on it is "for 12 days", with the days written over the
    number: three right-aligned characters ending after the digits, so 3
    days reads "for  3 days". The COMMAND record stays patched.
  - The engine's thresholds had been 4, 16 and 96, with no number.
- **SPECIALIZE IN ARMY / ECOLOGY.**
  - Choosing the troop's own job closes the menu with no answer (6a91).
  - Otherwise the job is applied and the troop answers (list 4, ds:23 =
    0x0a). A refusal restores the old job and speech word.
  - Accepted, a job of class 1 or 2 (occupation >> 2 not 0) clears
    equipment bit 7 (6abf). The harvester stays in the place's stock as
    free equipment.
- **Lines.** The line routine takes a 16-bit pattern that rotates left a bit
  each step, drawing where the rotated-out bit is set, with each pixel
  clipped to a rectangle.
  - A horizontal or vertical line runs from its left or top end and draws
    both ends.
  - Any other line takes max(|dx|, |dy|) steps with the error seeded at half
    the major delta, and never draws its start pixel.
  - Only two callers exist. The room draw passes 0xffff, clipped to the
    game area. The map's move route (81c5) passes 0x5555 in colour 0x0c,
    clipped to the map window. So "dithered lines" is the route only.
  - The engine had drawn rooms with ScummVM's line and the route with its
    own Bresenham. Both use `drawVgaLine` now.

### Built

- `World::troopNewDay`, `fortressConversion` (split out of
  `militaryTraining`), `lowerMotivation`, `troopContacted` and
  `dropHarvester`.
- `changeCharisma`'s loss path.
- `SentenceBank::patchCommandNumber`, and the duration phrase in
  `stageTroopForConditions`.
- `drawVgaLine` (room.cpp), used by the room renderer and
  `MapScreen::drawRoute`.
- Check `scripts/check_small_rules.sh` (`dune_story_setup=small-rules`),
  2/2.
- Not built: the route's map-seam case (81a3-81be).

## The Harkonnen-zone warning and the companion's fly-over lines (2026-09-30)

Queue item 8.

### Addresses

| What | CD (seg000) | Floppy |
| --- | --- | --- |
| travel_route_hostile_zone_check | 4182-41c5 | 43d6-4419 |
| the travel dispatch (companion aboard) | 35e9-3636 | 3889-38d6 |
| the warning with nobody aboard | 3637-366c | 38d7-3905 |
| the menu for pending_room_action 3 / 4 | 3551-3592 | 37f7-3834 |
| travel_pick_speaking_companion | 366f | 3908 |
| the ORNYCAB cabin | 368b | 3924 |
| the fly-over line (block 16 list 4) | 96d8 | a195 |
| the sighting scan | 40f9 | 4353 |
| the warning menu record | ds:1f9e | ds:2620 |
| CHANGE DESTINATION | 497a | 51fd |

### What the original does

The two releases run the same code. On each step of an ornithopter
flight, the check (4182) looks at the destination and the terrain. A
destination that is Atreides (5d36: a sietch, a village, or a place with
status bit 3) resets the accumulator ds:4726. Otherwise a step over a cell
of stage 0x30 takes 0x20 from ds:4726. The first such step arms
pending_room_action 4. SKIP TO DESTINATION stays greyed while the count
runs. When the accumulator wraps to 0, on the eighth step in a row, the
ornithopter is shot down (ds:46d9 = 2). The check also runs inside the
SKIP TO DESTINATION fast-forward (CD 4ffb, floppy 5de3), so the warning
stops the skip.

The flight pump then waits (ds:11ca) until a row is taken:

- **A companion aboard** (ds:1152). The ORNYCAB cabin comes up with the
  companion's head. The companion is the first slot when the second is
  empty or bit 7 of ds:0 is set, else the second slot; the engine had
  this the wrong way round. The line is the first in block 16 list 4 whose
  condition holds with ds:23 = 4. That is "Watch out! We are entering the
  Harkonnen zone...! We'd better get out of here fast!" (CD condition 703;
  floppy 697, with "... fast!"). The sentence mask is 0x20 (9f8b), so a line
  already said is never skipped.
- **Nobody aboard.** The cockpit is drawn (map_screen_draw_base: the sky and
  ORNYPAN). COMMAND 0xbf on the CD (0xb3 on the floppy), "  ****  WARNING
  ****\r\rENTERING HARKONNEN ZONE", goes at (0x66, 0x4e) in colour word
  0x200c.
- **The menu.** CHANGE DESTINATION opens the cockpit over the flight, and
  its Cancel carries on. IGNORE WARNING flies on. The CD's record also has
  " WHAT ? ": greyed with nobody aboard (3662), lit for a companion (357c).
  The floppy's record has only the two rows. There is no BACK TO STARTING
  POINT, which the engine used to offer.

**The sighting lines** come from the same block, with ds:23 = 3. The scan
stages the place's kind in name slot 4 and its side in slot 5.

- **The floppy** has three sides, from bearing + 0x60: below 0x58 "on the
  left", below 0x68 "ahead", else "on the right" (COMMAND 0xc2-0xc4). It
  says "Wait a minute, I'm not sure... I think I've just seen [4] [5]."
  when bit 3 of ds:0 is set (condition 695). Otherwise it says "It looks
  like [4], there [5]." (condition 696).
- **The CD** has two sides: below 0x60 "on the left" (0xce), else "on the
  right" (0xd0), with ds:e1 = 1. The "Wait a minute" line is the right
  side's (condition 701) and "It looks like" the left side's (702).
- **The menu** is GO TOWARDS THIS PLACE. The CD's (ds:1f92) adds " WHAT ? ".

### Built

- `askHostileZone` (scene.cpp) returns CHANGE DESTINATION or IGNORE
  WARNING. With a companion it shows the cabin, the line and the rows
  (`_cabinWarning`). With nobody aboard it draws the cockpit with the
  warning text. That text had never drawn: the lookup searched for the
  record's leading spaces.
- `flyoverSpeaker` and `flyoverLine` (cockpit.cpp) are shared with
  `showSighting`, which now takes its line from the dialogue with its
  release's sides.
- The speedrun bot still turns back to its starting point. That is the
  CHANGE DESTINATION pick a player would make.
- Check `scripts/check_hostile_zone.sh`: floppy and CD, nobody aboard and
  Gurney aboard, run in real time from `dune_story_setup=hostile-zone[-gurney]`.
  It checks the rows, the greyed " WHAT ? ", the text in the window, the
  companion's line, CHANGE DESTINATION -> Cancel, and IGNORE WARNING ->
  shot down.

## A fort turned sietch moves its people (2026-09-28)

A captured fort becomes a sietch on a new day (floppy `sub_9A58`, 9A6C–9A7E): its type byte becomes `type & 7`, and ds:27 is incremented. It then calls `sub_99F3`, which the engine had skipped.
- **Characters.** The routine walks the 12 character records at ds:FD8. A record whose second word is (0x80, place + 1), from `sub_61D8`, takes the new type byte, and its room becomes 1 or 2.
- **Paul.** If Paul is there, ds:4, ds:0B and ds:8 change the same way.
- **Why it matters.** Presence is a byte-for-byte test against ds:4–7 (`loc_136EE`). Without the fix-up, a character left there with STAY HERE keeps the fort's type byte (0x28), while Paul's position has the sietch's (0x00), so the character is absent. That is why the war council never started for Thufir at place 2 in the full speedrun run: persons in room was 0xb2, without Thufir's bit.
- **The troops.** The routine's troop callback (bp 7B77, at 9A4C) also clears flag 0x20 in the troop's word +10h and sets 0x1000 in +12h; the engine had only done the second.

With `battle.cpp`'s conversion completed, `check_speedrun.sh full` passes on floppy with no FORCED step (19 forts, the end on day 58). The CD run still stops at the palace for want of atomics: its atomics troops are captured.

**Speedrun CD campaign, seed 1 (2026-09-29).** It failed at stage 4 with 953 (x 10) men with atomics at locations 2-4: two troops with atomics stood at place 55 in occupation 0x22 (freed after a capture, apologizing: `75af`), and a troop in that state does not march. The end-game rules are not what changed the run: a build with each of them switched back (ds:d5, the lesson's charisma, the ds:c2 reset, the atomics gift, the message senders, the captive's counter, ds:2b) gives the same bot log line for line. The run parts from the Sep 28 pass on day 6 (troop 16's army skill 36, not 35), from changes to the troops and the landing's room rotation roll made before the end-game work (a 01:53 engine without them ends on day 30 with 12 forts). The bot now gives such troops a new job before gathering the atomics troops. A job writes the whole occupation byte (`6aea`), which frees them, as CHANGE TROOP OCCUPATION does for a player. The run then ends on day 43 with 14 forts.

## Map and globe

`MAP.HSQ` is a 50681-byte picture whose low nibbles are the terrain and whose
bits 4-5 flag spice fields; the flat map draws it in 4-pixel bands over 36
rows with `TABLAT.BIN`'s per-row offsets and lengths and a 16.16 rotation,
interpolating vertically with error accumulators (dune-rust
`map_renderer.rs`, `tablat.rs`; `map.cpp` is a port). Colours are
`(b >> 4 & 0xf) + 0x10` under ONMAP.HSQ's palette. Places project as in
`sub_1B647`: `x = cx + ((Δlongitude · 2 · rowLength) >> 16)`,
`y = (latitude - viewLatitude) + cy`, four times larger on the globe (centre
160, 76). ONMAP frames 122-127 are the sietch, Atreides palace, village,
fortress, Harkonnen palace and far-sietch icons, 58-70 the green troop
ornithopters, 141 the 170x108 blue frame; the panel's arrows are ICONES 37-41
and 49-53. The DUNE MAP title popup (`map_show_rallied_troops_popup`,
seg000:5bb0) is the panel record ds:194a, (10,10)-(190,64) filled 0xfb in a
one-pixel 0xf5 frame, with phrase 0xe2 at the corner + (10, 8), 10-pixel
lines, centred by its own spaces; the rallied-troop count ds:28 overwrites
the phrase's first number as a three-character right-aligned field
(seg000:d03c, e2e3, capped 999). It shows when the map opens from the room
(not on the GIVE ORDERS detour, which bumps map_view_reentry_count), and
either mouse button or 1000 timer ticks (5 s, seg000:5c03) take it away.
The map menu (`map_setup_main_menu` seg000:878c, record ds:20f2) is EXIT
MAPS; GIVE ORDERS TO TROOP while the contact range ds:1176 is under 2
(greyed without a troop where Paul stands), else CONTACT FREMEN TROOPS
(greyed without rallied troops); SEE SPICE DENSITY (greyed before phase 5);
TAKE AN ORNITHOPTER (greyed without one parked here); FIND PROSPECTORS (only
from phase 5). The left panel on the flat map is ICONES 6 (the eye frame
around a transparent oval) with ICONES 0x0d, the planet, at (22,161): the
frieze record ds:1ae6 with the map variant ds:1c66
(`ui_set_and_draw_frieze_sides_map`, seg000:d792). The globe is `drawDuneGlobe` over the
FRESK frame, with the game menu in the command box: EXIT GLOBE, SEE RESULTS
(STANDARD VISION while the results are up: the menu push, floppy CS:B840 /
CD seg000:b941, patches row 2 from the results flag), SAVE GAME, LOAD GAME,
OPTIONS & QUIT GAME. Hot zones
from the CD executable at 0x111A0: the book (24, 155)-(69, 176), the five
command rows 92..228 x 159 + 8 i, Paul's head on the box's top edge.

**SEE RESULTS: the globe's colours (built 2026-09-29).** Research, addresses
and captures: `notes/globe-results.md`. The VGA driver draws each globe pixel
from the live map cell through one of two paths, chosen by the results flag
(floppy ds:FEF2, CD ds:DD02; `vga_globe_init` patches NOP NOP or a jump at
DUNEVGA 1D24 / DNVGA 1E4A). SEE RESULTS (DUNEVGA 1DA3, DNVGA 1EC9): stage 0
`0x10 + t`, stage 0x10/0x20 (Atreides) `0x20 + t` (FRESK's reds), stage 0x30
(Harkonnen) `0x30 + t` (the blues); no place, owner or troop is read.
STANDARD VISION (DUNEVGA 1D26, DNVGA 1E4C): `0x10 + t`, but sprouting sand
(stage 0x10, t < 8) 12 higher, on FRESK's greens 0x20-0x23. The engine:
`globeCellColour` (scene.cpp). The colours change after the panels slide open
and before they slide shut. Orientation (map.cpp): the globe seeds from the
flat view's centre when it opens from the flat map's planet (ds:2144/2146),
else from Paul's place, phase = 398 * longitude >> 16, tilt = latitude with at
least +-32 (CS:B974); LOAD GAME from the globe opens SEE RESULTS and centres
on Paul with the tilt only within +-98 (`centreOnPlayer`, CD seg000:ba9e;
also the centre arrow); the arrows step 0x20 phases and 8 tilt rows (right
and up add); the globe creeps one phase a pass, about every 520 ms, in real
play only (never in dump or harness runs, whose pictures must not depend on
time). The results screen's charisma is half the byte (CS:BD0B `shr ax,1`).
Checked pixel for pixel against the original at three story stages and with
greening (`scripts/check_globe_results.sh`, `scripts/globe_ref.py`).

**Open:** what gfx vtable function 9 does for the frieze flag 0x40 (the
recording rules out a black fill); FIND PROSPECTORS (seg000:5b1e).

## Save files

The original's four logs are `DUNE21S<n>.SAV` (floppy) and `DUNE37S<n>.SAV`
(CD): a header of game time (u16), RLE marker word (low byte 0xF7) and length
- 2, then RLE with `marker, count, value` runs (verified on the five floppy
saves shipped with the data; dune-rust `crates/savegame`). The body is the map
picture's flag bits four to a byte (0x317F bytes), 0xC6 (floppy) or 0xA2 (CD)
bytes of the executable's own variables, DIALOGUE.HSQ with its said flags and
the data segment (4718 / 4705 bytes): 22051 bytes on the floppy. `saves.cpp`
writes and reads the same layout into ScummVM's save directory (and reads DOS
saves left in the game folder), so logs stay exchangeable with the DOS game.
The engine's own block is kept from the last load and otherwise zero. The
book's journal is rebuilt from the said flags on load (by character, not in
the order heard). On the floppy COMMAND 259-264 are the log rows ("Log 1: DAY  0 / 12.00
a.m."), SAVE SUCCESSFUL and SAVE ERROR, 172-175 and 254-258 the exit and
music options; the CD numbers them 271-276, 184-187 and 266-270 (with
SHUFFLE), has no "OPTIONS & QUIT GAME" (its entry 181 is a placeholder) and
says "NO I WISH TO CONTINUE". Rows are looked up by text, never by number.

**Decision:** the clock is read as 24 units a day starting at midnight for the
day counter and the log rows; the original's reading of the time word
(`sub_138B4`'s callers) is not decoded, so this is a placeholder.

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

## Other releases

**Amiga** — decoded; see "Amiga release" below.

**Sega Mega CD** — supported as a separate release: see the next section.

**One build for all four (2026-09-28).** The Amiga and Sega CD work (done on
a branch on 2026-09-25) is merged into the current engine: the same desktop
and iOS build detects the DOS floppy, the DOS CD, the Amiga and the Sega CD.
Only the platform hooks were carried over; the game logic is the current
one. Since the branch, the shared code gained screens the Amiga had never
seen: the desert walk, the cockpit and the cabin now draw the Amiga's sky
gradient over its sand (`amigaDesertView`, DUNES3 is not ported), and the
globe's ownership colours (SEE RESULTS) are DOS palette ramps, so the Amiga
globe keeps its ONMAP terrain colours there. The flat map's latitude fix
(terrain 18 rows north of the places) applies to the Amiga and the Sega CD
too, and a click on Paul's head is inert on every release (the globe opens
from the box's edge under it). `scripts/check_other_releases.sh` runs the
Amiga and Sega CD scenarios (`tests/regression/other-releases.json`) and
skips a release whose data is missing.

## Sega CD / Mega CD release

*Dune* on the Sega CD (Cryo / Virgin, USA T-70065 built 1994-08-30, Europe
1994-04). No public reverse engineering of this release was found (OpenRakis,
the dune2k forum, Sega Retro and romhacking.net searched on 2026-09-25), so
everything here was worked out from the disc. The research tools are in
`Cryogenic/scripts/`: `segacd_iso.py` (disc reader and extractor),
`m68k_dis.py` (capstone 68000 disassembler), `segacd_img.py` (tile screens),
`segacd_sprites.py` (sprite banks), `segacd_ds_align.py` and
`segacd_ds_convert.py` (the data-segment conversion). Sub-CPU addresses below
are in the game program (file 0, loaded at 0x7000).

### Disc and storage

**Tracks** — verified. Track 1 is MODE1/2352 data (541,369,248 bytes in the
Redump rip); track 2 is a 6-second audio track. All game data, speech
included, is on track 1: the game does not stream CD audio for music or
speech.

**ISO 9660** — verified. Four files: `ABS.TXT`, `BIB.TXT`, `CPY.TXT`
(Sega's sample-disc placeholders) and `DUNE.DAT`, 471,040,000 bytes (230,000
sectors) from disc sector 21.

**Boot area** — verified. Sector 0: `SEGADISCSYSTEM`, the IP at 0x200 (0x600
bytes) and the SP at 0x800 (0x2800 bytes, `MAIN DUNESP`, loaded at sub-CPU
0x6000). The SP's init (0x6cd0) writes index entry 0 = (sector 0, 0x1ec44
bytes), loads file 0 to 0x7000 (PRG-RAM) and file 1 to 0xc0000 (word RAM),
and jumps to 0xa69e.

**Index** — verified. `DUNE.DAT` has no header and no names. The index is
part of file 0: 0x202d2-0x25444 (file offset 0x192d2), 3,475 entries of six
bytes, big-endian, a 24-bit sector relative to `DUNE.DAT` and a 24-bit byte
size. The SP's reader (0x6032) multiplies the file number by 6 and adds 0x15,
the sector where `DUNE.DAT` starts. Files start on sector boundaries, back to
back without gaps.

**The European disc** — verified for storage only. Same size and file count,
but another program: file 0 is 0x1ecc6 bytes, the index at 0x20354-0x254c6,
and file 0 differs from offset 0xdf0 on. The engine reads the layout from the
SP's code when it has the track image (`clr.w (a0)+` / `move.l #size0,(a0)`;
`lea index.l,a0` followed by the multiply by 6; `move.l #end,$c(a6)`) and
otherwise tries the known revisions, keeping the one whose index tiles
`DUNE.DAT`. Everything else below was checked on the USA disc only.

**No compression** — verified. No file has an HSQ header; the text, map and
table files are byte-identical to the PC's unpacked ones.

### What the files are

"Verified" rows were decoded; the others are guesses from headers and sizes.

| Files | What | Confidence |
| --- | --- | --- |
| 0 | the game program (sub CPU): code, the PC-style data segment at 0x90c8, the index | verified |
| 1 | the main-CPU program (17 KB): copies itself to 0xff0000; a VDP and input driver | verified (loader), guessed (role) |
| 2 | `TABLAT.BIN` | verified (identical) |
| 3, 1857 | `MAP.HSQ`, `MAP2.HSQ` (unpacked) | verified (identical) |
| 7 | `CONDIT.HSQ` with one condition added | verified |
| 8 | `DIALOGUE.HSQ`, 291 bytes changed near its end | verified |
| 9, 10 | `DNCHAR.BIN`, `DNCHAR2.BIN` (the font) | verified (identical) |
| 12-17 | `COMMAND1`-`6`: English, French, German, English, Italian, Spanish | verified |
| 18-29 | `PHRASE11`-`62`, same languages, two parts each | verified |
| 1859 | `GLOBDATA.HSQ` | verified (identical) |
| 11, 43 | language screens (three planes: the choice, then each flag alone) | verified |
| 30 | a dialogue frame (text box and portrait frame) | verified |
| 38, 44-61, 1613-1615, 1627-1677 | desert views, the planet, intro backdrops | verified (drawn) |
| 1678-1743 | room screens (see Rooms) | verified |
| 1744-1809 | their second layers (room screen + 66) | verified |
| 1728 | the title logo, revealed over 27 planes | verified (drawn) |
| 1853 | blue text-box frames and a sand pattern | verified (drawn) |
| 1854 | the Atreides / Harkonnen balance panels | verified (drawn) |
| 1910 | the ending: Jessica and Paul; "THE END" | verified (drawn) |
| 1911-1925 | the end credits' cards | verified (drawn) |
| 1782-1850 | talking portraits: Leto, Paul, Jessica, Stilgar, Kynes, Chani, Harah, the Harkonnens, Fremen chiefs and troops | verified (frames drawn) |
| 1716-1718 | full-body figures that stand in rooms, at several depth scales | verified (frames drawn) |
| 1616 | the ornithopter and its dust | verified (frames drawn) |
| 1620, 1719, 1724, 1738, 1743, 1852, 1855 | small sprite sets: map icons, buttons, labels | verified (parsed) |
| 1860-1908, 1928 | bitmap sets of 60 x 60 pictures (see Bitmap sets); 1928 holds the map dialogue's portraits (Harkonnen captain, Stilgar, Feyd-Rautha, the Baron, the Emperor); the others probably the globe's frames | verified (1928 drawn), guessed (globe) |
| 1735, 1856, 1858 | bitmap sets: 1858 the map screen's items (ornithopter, troop icons, the 168 x 104 viewport frame, the gauntlet pointer, worm frames) | verified (drawn) |
| 1929 | a byte-coded script (0xf7 opcodes), loaded into RAM with the data files | guessed |
| 62-70 | 30 frames of 39 tiles each, an animation (five files identical) | guessed |
| 39 and 3,077 others | `Megative Voice File` headers: speech and sound samples | guessed |
| 31-37, 40-42, 71, 72, 1607-1612, 1926, 1927, 3465-3474 | large streams (0.2 to 12 MB), most starting with CRAM words: video | guessed |

The program loads files through a few routines, which sorts them by kind:
0x1c66e takes full screens (30, 38, 44, 47, 1615, 1647, 1677, 1687, 1728,
1734, 1737, 1742, 1853, 1854, 1910, 1911, 1925); 0x1f27a the big streams (31,
33, 37, 41, 42, 1608, 1927); 0x1dcd0 sounds; 0x1c3ba sprite sets; 0x6806 reads
data files into RAM (2-8, 1859, 1929).

### Tile screens

**Format** — verified on all 175 files that have it (`segacd_gfx.cpp`).
Big-endian:

| Offset | Content |
| --- | --- |
| +0 | word: offset T of the tile block |
| +2 | 64 words: CRAM, four lines of 16 colours, `0000 BBB0 GGG0 RRR0` |
| +0x82 | word L: length of the plane list that starts here (2, 4, 6 for 1, 2, 3 planes; 54 in the title logo) |
| +0x84 | (when L is above 2) each further plane's offset from 0x82 |
| plane | byte width, byte height (in tiles), then width x height VDP name-table words |
| +T | word: tile count, then 32 bytes per 8 x 8 tile, 4 bits per pixel, high nibble first |

Name-table words are the VDP's: priority 0x8000, palette line 0x6000,
vertical flip 0x1000, horizontal flip 0x0800, tile 0x07ff. Colour 0 of each
line is transparent; the backdrop is colour 0 of line 0. Rooms are 40 x 21
tiles (320 x 168), full screens 40 x 25. The file size always equals
T + 2 + 32 x count, the check the engine uses.

**Colours** — measured. The engine scales the 3-bit levels linearly
(level x 255 / 7). The VDP's real output is not linear; if pictures look dark
on the device, this is the place to change.

### Rooms

**Room tables** — verified. The Sega CD keeps the PC's room tables: per place
type, 5-byte records of a room code and four exits, with the same exit
numbers, the same 252-254 "leave" values and the same bit 7 for doors the
story opens. The pointer table (0x8394, 0x34 entries of 32-bit addresses) and
the tables (0x8230-0x8393) sit in the program's constants, except the
palace's, which is in the data segment as on the PC.

**The room code is a screen number** — verified. The interior routine
(0x10204) draws screen 1678 + code (`addi.w #$68e`) and places characters from
a marker table at 0x39b08. Screens from 1678 on get a second layer 66 files
later (0x1016c, `addi.w #$42`); earlier ones get 1614. Palace codes 0x00-0x0c
are screens 1678-1690 (the throne room is code 0), the sietch rock 0x0d is
1691 and the caves 0x0e-0x19 are 1692-1703; villages use 0x1b-0x20,
fortresses 0x21-0x23, the Harkonnen palace 0x24 and 0x25 (the Emperor's
court, with the Baron, Feyd-Rautha and the Emperor drawn into the picture).

**Exteriors** — verified from the code, not implemented. The routine at
0xfd9c picks the outdoor screen: a sietch's own view 1627 + n (n from the
location), and for the open desert 1632 + hash mod 19, or 1667 + hash mod 10
once the place is green, with hash = ((ds:6 << 8) xor ds:4) + 1.

### Sprite banks

**Format** — verified for the frame layout (`segacd_sprites.py`); the
portraits' second tile block is not decoded.

| Offset | Content |
| --- | --- |
| +0 | word: offset T of the tile block |
| +2 | word: offset F of the frame table |
| +4 | (banks with a palette) word 1, two unknown words, 15 CRAM colours (colour 0 transparent), 0xffff |
| F | words: frame offsets relative to F (the first gives the count) |
| frame | one word: the frame's size, (width - 1) << 8 or (height - 1), measured on the pieces; then 4-byte pieces: x in pixels; y in tiles in the high nibble and the VDP sprite size, (width - 1) << 2 or (height - 1), in the low one; the VDP attribute word |
| +T | word: tile count, then the tiles; a piece's tiles are column-major, as the VDP's sprites |

Palette-less banks (1719, 1724, 1738, 1743, 1852) parse to their exact size.
The portrait banks have about 7 KB after the counted tiles: more tiles
(the eye and mouth frames of the talking animation, loaded per frame).
Frames carry no position, so where the head, eyes and mouth sit on the body
comes from elsewhere (the program's tables, not found yet).

### Bitmap sets

**Format** — verified on 1928 (`segacd_bitmaps.py`). Word 0 is the offset
(2) of a table of words; the first entry also gives the table's length.
Each entry, relative to the table, points to a bitmap: word width, word
height, then the pixels, 4 bits each, high nibble first, in columns one byte
(two pixels) wide: byte (x / 2) x height + y. That is the Sega CD's word-RAM
"dot image" order, drawn by the graphics ASIC, not the VDP. No palette is
stored with them.

### Panel

**Verified** (found from the code at sub-CPU 0xab56, which loads file 5 to
0x3a844 and then draws file 30; compared with the longplay's frames):

- file 30, a tile screen of 29 x 7 cells, is the panel's right part: the
  command box and the direction pad's frame, drawn from x = 88 on the lower
  56 lines;
- file 5 is a set of tile blocks: word 0 = 2, a table of word offsets
  relative to the table, and per block a byte width, a byte height (tiles)
  and the tiles, **column-major**. Block 0 is the left part (the book, the day
  box, two companion slots; 88 x 56); 1 the map's left part (a globe between
  two figures); 3-5 the open "STORY" book; 7-15 the companions' 16 x 16
  faces; 16, 17 the sun and the moon; 19-34 the direction pad for every exit
  mask (19 + mask: bit 0 up, 1 right, 2 down, 3 left; open exits lavender,
  closed ones blue) with a red centre; 35-47 an incomplete set without it;
  36 and 48 the map's pad; 49 the flight's.
- Geometry (measured on the tiles): pad at (256, 184), 40 x 32; command box
  (96, 176)-(231, 215), five rows of 8 lines; day box (8, 200), 24 x 16;
  face slots (40, 200) and (64, 200).
- Colours: the panel uses VDP palette line 0 and every room screen's CRAM
  line 0 is its place's tint (bronze palace, violet desert); pads and faces
  use line 1 (file 30's). The map's blue tint is taken from file 1853's
  line 0 (a guess that matches the recording).
- The command text is the PC font's small set (`DNCHAR.BIN`, identical on the
  disc): the recording's rows are 8 lines apart in that font. File 1738 is
  a second font (91 glyphs, ASCII 32-122 with e-acute, a-acute, c-cedilla,
  e-grave in the slots of `[ \ ] ^`), 8 x 16 with a vertical gradient; where
  the game uses it is not known yet.
- Which pad has the red centre is guessed: a room with a way out of the
  place (a 252-254 exit); the recording's palace room without one shows
  pad 39. Faces 7-15 are taken as characters 0-8 in DIALOGUE order, also a
  guess from the pictures.

### Dialogue

**Measured** from the longplay (`notes/segacd-gameplay/longplay/`): the
character's portrait fills the left of the view over a close-up backdrop
(the room screen + 66 files, which are exactly these close-ups); the line
is in a box at (160, 16)-(311, 127), black text, justified, on a colour that
depends on the place (palace orange, sietch beige, village blue, fortress
pale, remote messages magenta); the command box shows `>>>> TALK <<<<` over
the verbs (" COME WITH ME ", " WHAT ? ", STOP TALKING). The conversations
themselves are the PC's (DIALOGUE, CONDIT, PHRASE from the disc, driven by
the engine's `Conversation`).

**Portrait banks** — identified by masked template matching of every
bank's frames against the longplay's close-ups
(`scripts/segacd_portrait_match.py`, OpenCV): 1782 Leto, 1783 Jessica, 1784
Thufir, 1786 Gurney, 1789 Chani, 1791 the Baron, 1793 the Emperor, 1794 the
Harkonnen captain, 1795-1800 smugglers, 1806 Stilgar, 1833-1849 Fremen
chiefs' bodies. 1801-1815 are heads (Fremen, chiefs) whose palettes differ
from their banks' CRAM, so they did not match; Duncan, Kynes, Harah and
Feyd-Rautha were not identified. Frames have no position: each portrait is a
rest pose of several frames whose placement was measured
(`scripts/segacd_portrait_pose.py`, table `kPortraits` in `segacd_game.cpp`);
the program's own placement and animation tables are not found yet.

### Map

**Measured.** The Sega CD's map is a zoomed flat map: the same terrain
(`MAP.HSQ`), about twice the PC's scale, in two main sand tones (yellow
231,230,98 and tan 229,197,130) inside a blue frame, with rock, palace and
troop icons (bitmap set 1858), a tooltip box and the map panel (block 1 and
pad 48; rows EXIT MAPS, CONTACT FREMEN TROOPS, SEE SPICE DENSITY, TAKE AN
ORNITHOPTER, FIND PROSPECTORS). The engine renders it with the PC's
`MapRenderer` from the world's live map, zooms twice and thresholds the 16
shades into the Sega CD's tones (the thresholds are guessed); the places are
drawn as placeholder markers. There is no separate globe screen in the
longplay.

**Bitmap sets** in word-RAM order (see below) hold the map's pieces: 1858 the
map items (ornithopter, troop icons, the 168 x 104 viewport frame, the
gauntlet pointer, worm frames), 1928 the map dialogue's 60 x 60 portraits,
1860-1908 probably more of them.

### Data segment

**Where** — verified. The program keeps the PC's data segment at 0x90c8
(file 0 offset 0x20c8), recognisable by its first bytes, the PC's
`00 00 02 00 0a 20 80 01 20 00 00 0a` stored big-endian.

**How it differs** — verified by aligning it with `DNCDPRG.EXE`'s
(`segacd_ds_align.py`: byte agreement allowing for swapped words):

- words are big-endian: the position words (ds:2-7), the locations'
  longitude, latitude and map offset (+2, +4, +6), the troops' +16 and +18,
  the characters' first two words, the name table (ds:11eb) and a few
  variables;
- the 68000 needs words on even addresses, so records of odd length gained a
  pad byte at their end: troops 27 to 28 bytes (68 records), smugglers 17 to 18
  (6 records). Everything after the troop table is 0x44 bytes later;
- after the smugglers the variables were reordered: the PC's pointer at
  0x113f and its word at 0x1156 are gone and a 120-byte table of 0xff was
  added. That part is mapped variable by variable (`segacd_world.cpp`,
  `kRegions`);
- values changed on purpose: Arrakeen starts with one ornithopter
  (location 0, byte 21), two location statuses are 0x80 instead of 0xa0, two
  troop bytes differ, and the characters' +4 word holds an index (0, 6, 12, ...)
  instead of a PC data pointer.

After conversion 78 bytes of 0x0000-0x1260 differ from the PC CD's initial
data, all listed above or PC-only constants the engine does not read
(0x11bd-0x1221, left zero).

**Decision: convert to the PC layout once, at a new game.** The whole engine
(`World`, dialogue conditions, saves) reads the data segment by PC CD
offsets, so rebuilding that layout lets the Sega CD share all of it. Saves
are therefore the engine's PC-format saves, not the Sega CD's backup-RAM ones.

### Text

**Verified.** COMMAND and PHRASE are the PC's format (little-endian offset
tables, strings ended by 0xff, the same inline codes) and mostly
byte-identical to the PC CD's. COMMAND1 differs only in entry 275
(`* NO BACK-UP RAM AVAILABLE *` instead of ` *** SAVE ERROR `). The USA disc
has all six languages; which one the US game uses is not known yet (the
engine uses 1, English). Location names come out as the shared
`World::locationName` gives them (the palace is "Carthag-(Atreides)"): that is
the same on the PC and agrees with dune-rust's and odrade's tables.

### Presentation

**Measured** from the Let's Play part 24 video (`notes/segacd-gameplay/route.md`):
the picture is 320 x 224, the room view the upper 168 lines and the panel the
lower 56. The panel holds the day counter under a sun or moon, two companion
portraits, the command list (white capitals) and a direction pad; its frame is
violet outdoors, bronze in the palace and ivory in battle. Dialogue shows a
full-screen close-up portrait with the line in a box. The map is its own
screen with a globe at the left and a red and yellow pad at the right.

The panel's pieces are in files 5 and 30 (see Panel).

### Decisions

- **Detection**: `DUNE.DAT` (471,040,000 bytes; md5 of the first 5,000 bytes
  per revision) or the Redump-named data track. A raw MODE1/2352 `.bin` and a
  2048-byte `.iso` or `.img` both work: `SegaCdArchive` finds `DUNE.DAT`
  through the image's ISO 9660 directory, so users need not extract anything.
- **One engine**: the release branches once, in `DuneEngine::run()`
  (`platform == kPlatformSegaCD`), into `runSegaCd()`. The shared code gained
  two small hooks: `Resource::setSegaCd()` (the PC names served from the disc)
  and `World::setSegaCdProgram()` (the initial data from the program).
- **Screen size**: 320 x 224, the Sega CD's own, rather than squeezing rooms
  into the PC's 152-line view.

### Open questions and next steps

1. The command verbs in conversations (COME WITH ME, WHAT ?), the panel's
   pressed states and the pointer shapes (file 4, four 32 x 32 pointers).
2. The program's portrait placement and talking-animation tables; the heads
   of banks 1801-1815; Duncan, Kynes, Harah and Feyd-Rautha.
3. The room figures (1716-1718) and the marker table at 0x39b08.
4. The video format of the large streams; the intro and the endings (1910
   holds the ending's stills).
5. The `Megative Voice File` sample format; the music.
6. The map's icons (1858) and menus, the ornithopter flight (the cockpit
   view with the minimap), battles, worms.
7. The exterior selection (0xfd9c) for sietches and the open desert.
8. The endings: the longplay (US, 7 h) shows only the military victory
   (the Emperor, the Baron and Feyd exiled, "Long live the Emperor Paul
   Atreides and Chani his Empress", THE END, credits); the "secret ending"
   clip stops before its ending, so the secret ending is still unseen.

## Platform and workflow decisions

- **ScummVM engine rather than DOSBox or the C# hybrid**: `README_PLAN.MD`. The
  DOSBox variant is parked (the sideloaded app failed with registration error 181).
- **Desktop first**: every change is checked on the SDL build with screenshot
  dumps before a device build, because a device round trip needs the user
  (sideloading, AirDrop of logs).
- **Device facts**: iPhone 14 Pro, iOS 16.4.1. `devicectl` can never pair
  (CoreDevice needs iOS 17). `pymobiledevice3` reads the syslog over USB but
  cannot install an ad-hoc-signed IPA or open the app container (the sideloading
  tool registers the app as a System app). The IPA is served over HTTP with an
  install link; logs come back by AirDrop.
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

## Amiga release

The Amiga release (Cryo/Virgin 1992, three disks, English, executable version
"1.0" by its save name `dune10s0.sav`) is played by converting its data into
the DOS layouts at load time (`amiga.cpp`, `amiga_gfx.cpp`); decoders and game
logic stay shared. Addresses "code 0x...." are offsets in the first hunk of the
68000 executable `dune` (disassembled with capstone,
`notes/amiga-port/tools/dis_game.py`). Evidence tools are in
`notes/amiga-port/tools/`.

**Sources.** The three images `Dune 1 (Cryo + Virgin) A/B/C.adf` (original,
copy-protected) and the TOSEC set in `Dune_Amiga_EN.zip` (cracked disk 1 plus
the same disks 2 and 3). The data files are identical in all of them; the
executables differ only in two bytes of the protection check (file offsets
72813 and 76109, `beq` patched to `bra`).

**Disks** — verified. Disk 1 has a small OFS file system (`dune`, `dir.0`,
`dir.1`, `disk_to_hd`, `tiroir`); disks 2 and 3 have an empty one. The game
data sits in raw sectors. `dir.0` is 103 big-endian records of 14 bytes:
unpacked size, stored size (u32 each), first sector on disk 1, 2 and 3 (u16,
0 = absent). A sector is a 16-bit checksum (sum of the following 255
big-endian words) and 510 data bytes; a file fills consecutive sectors. The
names are not on the disks: they are the tables `disk_fic`, `disk2`, `disk3`
of the installer `disk_to_hd`, which ships with its symbol table
(`read_disk`, `read_secteur_510`, ...). There is no HNM, VOC or HERAD file:
`m1-m3.hsq` are the music, `.sam` files replace `.sal`. **Decision:** the
engine reads loose files as that installer writes them (lower-case names and
`dune`); `scripts/dune_amiga_extract.py` writes the same from the ADFs with
the Python standard library. Detection: `dune` (first 5000 bytes, 137264
bytes) and `dunechar.hsq`.

**Files** — verified. HSQ is the DOS format, with one extension: the third
header byte holds bits 16-19 of the unpacked size (high nibble) and of the
packed size (low nibble), so the music files can exceed 64 KB (`m1.hsq`: 0x21
for 0x244ce and 0x18ae0). `map.hsq`, `map2.hsq`,
`tablat.bin`, `globdata.hsq` are byte-identical to DOS. `command2.hsq`,
`phrase21.hsq`, `phrase22.hsq` are the English text in the DOS layout (the
Amiga numbers English "2"), an earlier wording ("I am the Duke Leto
Atreides, your father.") with some different indices (COMMAND "Paul
Atreides" is 0xfc, CD 0x108). `dialogue.hsq` and `condit.hsq` are the DOS
layout with small content differences. `m1-m3.hsq` unpack to sound banks
(titles WORMSIGN, ECOLOVE, FREMENS; named instruments, wave tables and
samples) for the executable's own Paula replayer, not decoded.

**Sprite sheets** — verified. The DOS structure with big-endian words;
palette blocks hold 12-bit colour words (`0RGB`); the sprite header is
width word, palette offset, height; 4-bit pixels have the left pixel in the
high nibble. The palette offset and the RLE are the DOS ones (blitter at code
0x14b46, RLE unpacking at 0x13d52). Other entries:

- **Pictures**: first word 0, second word a size, then an HSQ body (no
  header) of 30400 bytes: 320x152, five interleaved bitplanes (per row plane
  0 to 4, 40 bytes each). The intro sheets `cryo`, `death`, `sun`,
  `ornycab` have the body without the two words; `mirror.hsq` is only a
  picture. Rooms are these pictures plus sprites: there are no polygons.
- **Palette records** (`sky`, `sunrs`, `attack`, `balcon`): zero, size 0x6c,
  54 colours (see the sky below).
- **Animation blocks**: the DOS layout with big-endian words.
- **Planar sprites** (`back`, `back1`, `credits`, `fresk`, `orny`, `ornytk`,
  `shai`, `shai1`, `shai2`, `stars`, `stars1`, `ver`), drawn by the blitter
  at code 0x15238 when `ds:14a9` is set: a word (bit 15 compressed, width),
  a word (bit 8 colours 16-31, height), four bitplanes one after the other,
  rows of `((width + 31) / 16) * 2` bytes. The compression (code 0x13dd0, two
  jump tables into 127 copies and 128 fills): 0 ends, 1-127 copies `128 - n`
  bytes, 128-255 repeats the next byte `129 - (n & 0x7f)` times. The sheets
  were found by which decoding ends exactly at every sprite's end.

**Decision:** every sheet is converted to the DOS layout on load: colours to
6-bit VGA (`v * 63 / 15`), header and nibbles swapped, planar sprites to
8-bit RLE sprites with palette byte 255. Pictures do not fit the 16-bit
offsets: they become an 8-byte stub (palette byte 253, a 32-bit offset) with
the 8-bit pixels after the sheet; `Sprite::getFrameInfo()` follows the stub.
On the Amiga colour 0 is a real colour, so `setPalette()` applies a (0, 1)
block there.

**Rooms (`.SAM`)** — verified. The `.SAL` stream with big-endian words and
no polygons or lines: marker count, then 5-byte commands (command word,
`y << 8 | x`, palette byte) until FFFF. An interior room's first command draws
the picture at (0, 0). The room byte of the room tables picks the sheet as on
DOS, file `0x13 + slot` (`por prouge comm equi balcon corr siet0 sas dunes2
fort bunk harko serre bota`), except slot 14 = `siet1`-`siet12` by SAL room
and slot 15 = `vilg1`-`vilg6` (code 0x5546); a village's picture follows its
region (code 0x5328, table `f1 f1 f2 f2 f3 f4 f4 f4 f5 f5 f6 f6` by the
place's first name). The room bytes differ from DOS (sietch rooms `e2-ed`
for `72-7d`, village `f1` for `81`, Harkonnen palace `b7/b8` for `95/a8`).

**Palettes, sky, panel** — verified. Five bitplanes; the copper switches to
a second 32-colour palette at line 152 for the panel. The engine keeps view
colours 0-31 and panel colours 32-63 (`icone.hsq` gets 32 added to its
palette offsets). Colour 1 of the exterior pictures is the sky: the copper
list (built at code 0x13782) repaints it every three lines from 26 gradient
entries, the last from line 75 down (`amigaSkyGradient`). A time-of-day
record (code 0x5296): words 0-13 are colours 2-15 (the landscape, kept for
the palace and villages, `ds:1582`), 14-27 colours 33-46 (the panel tint),
28-53 the gradient, bottom band first. The record is
`table[time & 15] + ((time >> 2) & 0x1c)`, table
`08 08 09 09 09 09 09 09 09 09 09 0a 0a 0b 0b 0b` (code 0x5254): sunrise,
day, sunset, night, four records for each day of the week. Colours 47-63 are
fixed interface colours (as POR.HSQ sets them); the shared drawing code's
DOS interface indices 224-255 are filled from the Amiga's
(`amigaMirrorUiColours`). The talk balloon is a plain box in colour 16 with
text in colour 29 (measured on the recording).

**Ornithopter** — verified (code 0x5406, 0x5420). Pad (0x95, 0x39) at a
sietch, (0xb5, 0x49) elsewhere (DOS 0xca); further ornis 50 pixels apart (DOS
70); the cropped wing frames are shifted by the table at code 0x5498.

**Data segment** — verified. It lies in the first hunk from 0x1a900 (the
CD's first bytes, big-endian). The layout is the CD's with the port's rules:
16-bit fields big-endian; pointers into the data segment (dune-chani's
`ofs16`) are 32-bit relocated addresses; records with words padded to an even
size (Troop 27 to 28, Smuggler 17 to 18); RoomPerson's handler is a 16-bit
index. A walk over dune-chani's CD field types predicts every Amiga offset
(two anchors: CD 0x11eb = Amiga 0x12e8, CD 0x1225 = 0x1322) and leaves only
content differences (version 1.0 data, room bytes, COMMAND ids, interface
lists). **Decision:** `amigaInitialDataSegment` converts to the CD layout
(table `amiga_ds_table.h`, generated by `notes/amiga-port/tools/build_conv.py`).
The runtime variables at CD 0x113e-0x11ea take the CD's initial values
(sentinels and pointers the shared logic expects). The world runs with the CD
layout and CD save format, the presentation follows the floppy
(`World::floppy()` is true, `World::amiga()` selects the Amiga details). The
scripted scenes are the CD's bytes, 0xed3 further (found by the map lesson
`0e 10 ff 00 02 03 05 07`).

**Other screens** — measured on the recording unless stated.
- The flat map and the globe: the Amiga's ONMAP keeps its sand ramp at 21-29
  (20 is a dark blue), so terrain value `t` shows as `clip(t + 17, 21, 29)`
  where DOS uses `t + 16` (`terrainColour`); the executable's table is not
  located. The globe wears ONMAP's colours 16-31 inside FRESK's frame
  (FRESK's own 16-31 are other colours).
- The mirror: `mirror.hsq` includes the gilt frame; Paul's portrait is drawn
  at its talking position, clipped to the glass (12, 12)-(306, 138).
- The ending (code 0x239a, 0x23f2, 0x250c): picture 1 is FINAL frame 5 (the
  worm's head) with "THE END" (frame 1) at (0x5a, 0x40); picture 2 is frame
  0 ("with / (in order of Appearance)") at (0x40, 0x34).
- The book: BOOK.HSQ holds an ornament (0), the drop capitals (1-9) and the
  cover (13) and page (14) as pictures.
- The telepathic messages are on black (there is no VIS.HSQ).

**Not ported yet.** The intro (the game starts in the throne room; the
recording also starts there, at the mirror), music and sound (Paula), the
desert landscape of code 0x5094 (a flat sand colour, colour 2 of the
time-of-day record, stands in), the talk background's coarser zoom, the
copy-protection check (not needed).


## Amiga cockpit dashboard and destination controls (2026-09-30)

The Amiga's `ORNYPAN` sheet differs from the DOS sheet. Hunk 0 `5e18`
loads resource `2a`; `5e20-5e22` selects frame 3 through the full-picture
decoder `1a7fc`. That frame is the complete 320 by 152 dashboard, including
its opaque sky and black pixels. Frames 0 and 1 are 16 by 1 palette
placeholders. Drawing those DOS frame indices omitted the dashboard. The
engine now draws frame 3 and retains the frame 2 window overlay, which the
original draws at `5d5e-5d6c` through the icon list at `1bf22`.

Destination hover follows hunk 0 `604a/60ba` (CD `4586/45de`, floppy
`4d9f/4df7`): a visible marker shows its kind and place name, other map
points show DESERT with a compass direction near the principal bearings,
and leaving the window restores the SELECT DESTINATION caption (`6164`).
Hover uses the same location hit test as a destination click, including
inside the nested CHANGE DESTINATION screen during flight.

The Amiga cursor classifier at `11356-113dc`, using rectangle `1bf06`,
recognizes scroll bands beside the map: up to 50 pixels horizontally and
25 pixels vertically, restricted to the view above row 155. `102ca`
dispatches these directional cursor clicks to the ordinary navigation
arrows (`e612/e61a/e622/e62a`). The engine now accepts taps in these bands
as well as the existing lower navigation pad. Held-button repeat timing
is not covered by this change.

`scripts/check_cockpit.sh` drives actual mouse and keyboard events after
placing Paul in the palace cockpit. Its 14 assertions cover the dashboard
structure, named and desert hover, caption restoration, the inert current
palace, map-edge and navigation-pad scrolling, recentring, Cancel and ESC,
the direct destination click, takeoff/arrival and sietch entry, plus a
real-time CHANGE DESTINATION/hover/Cancel/resume/skip sequence. The shared
hover and destination interaction was also checked on DOS floppy and CD.
Animation timing, palette fidelity and the destination-label blink remain
separate work.


## Amiga arrival home-room conversion (2026-09-30)

The Amiga arrival shuffle (hunk 0 `3332–33fc`, corresponding to CD
`2170–21f9`) reads the nine character home-room bytes at hunk 0 `1beae`.
The lookup at `33a0` uses A6 minus `0xd3e`; A6 is hunk 0 plus `1cbec`.
With the saved-state base at hunk 0 `1a900`, the table is at Amiga state
`15ae`, matching CD `ds:144d`. Its bytes are
`0a 09 08 04 06 05 05 09 02` in both executables and in captured Amiga RAM.

The conversion previously stopped before this table, leaving room zero
for characters returned home. Gurney's phase `2e` conversation then became
unreachable. The explicitly traced byte run `{0x144d, 0x15ae, 9, 0}` now
restores the table; the generator preserves it without extending generic
conversion past its established boundary. Existing arrival logic retains
Gurney's room-6 to room-10 substitution before phase `24` and skips followers.

`check_amiga_arrival.sh` covers the phase `23`, `24`, and `2e` room assignments
and preservation of a following companion. The corrected Amiga campaign bot
reaches the Emperor throne-room ending at phase `c8` and final attack stage 7.
This is assisted engine-logic coverage: the observed run uses 33 direct troop
order fallbacks plus battle save/reload retries. Completion entirely through
player controls remains unverified; absence of a `FORCED` log marker alone
must not be presented as proof of an unassisted run.

## DOS floppy map troop sprites and contact (2026-09-30)

The native floppy routines are verified against the unpacked executable. The
older annotated disassembly's labels in this block are displaced by `0x1ed0`;
addresses below are actual code offsets, not those labels.

- `CS:74af-74f0` walks visible places' troop chains, then moving troops. Hidden
  troops are excluded. Unhired Fremen show a question mark only at visited places.
- `CS:750a-75c0` selects the native script by occupation, stopped/captured state,
  carried harvester/ornithopter equipment, battle status and slot. Soldiers and
  Harkonnen have slot-dependent variants. ONMAP frame **0 is valid** (stopped
  miners), although zero inside a multi-frame script terminates its loop.
- `CS:75c1-7607` chooses moving sprites from the dominant destination-minus-GPS
  direction and the occupation class. `CS:7608-766a` positions stationary icons
  with signed slot offsets and moving icons at their GPS position.
- `CS:b544-b57f` projects that position using signed longitude displacement,
  keeping fractional pixels through the multiply. `CS:7069-7091` admits place
  markers inside `(4,4)-(316,148)`; marching icons have the wider `(-16,-16)` to
  `(328,160)` anchor limit, then clip to the map window.
- `CS:c4b3-c52f` spawns the first sprite, centres its rectangle and chooses an
  animation cursor. `CS:78ce-7923` steps ordinary icons every fourth 15-tick task
  (about 300 ms). The engine uses a separate animation seed; opening the map
  cannot alter the gameplay random generator. Full-map redraw preserves each
  icon's script cursor and relative movement, including under contact popups.
- `CS:c71b-c73e` draws by unsigned right-plus-bottom depth, with later insertion
  winning equal-depth ties. `CS:76e0-7715` tests strict rectangle interiors in
  reverse insertion order and rejects unhired troops. `CS:9426-944a` allows
  remote contact only when range is at least two; otherwise the troop must be
  at Paul's current place. Icon clicks now open the same real contact/orders
  UI as the command row, including for marching troops.

The native script block is `DS:169b-195e`. It is beyond the saved game state's
`0x1500` bytes and is cached separately from the user's executable. It must not
be obtained from save-state bytes or embedded as asset data in the engine.
Only the verified DOS floppy block is enabled by this change; CD and Amiga use
separate asset layouts.

Validation uses the original chapter-20 map: four visible troops (8, 3, 1, 2)
with exact native icon bounds, direct contact, MOVE TROOP to Carthag-Harg, a
north-facing march and arrival after one period. Additional labelled save
fixtures exercise hidden/unhired/local-range guards and equipment, battle,
army, ecology and Harkonnen variants. `scripts/check_map_troops.sh` drives real
pointer input and checks the live animation task. The local fidelity save is
required; it and the screenshots are not distributed as engine source.

Known limits: original and engine animation phases are independent. The existing
march simulation takes a slightly different intermediate longitude (two screen
pixels on the checked route); the icon displays the actual engine GPS, and the
arrival and stationed position agree. Selected-troop rings and animation timing
fidelity remain separate work.
