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
ascending group order, goes to slot `(group + ds:0xC7) mod markers`, or to the
first free slot; `_sub_13B59_draw_SAL` hands the slots to the markers from the
last one down, and `sub_13D2F` draws PERS frame 2 x group (groups from 15 on
as 15). ds:0xC7 is 0 in both executables, so Duke Leto (group 0) lands on
the throne room's last marker, which the recording confirms. At a sietch
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

Not transcribed yet: harvester breakdowns and saboteurs, military training,
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
  offset, tilt added to the lookup, colour (map & 0x0f) + 0x10, centre
  (159,79). A wrong tilt sign produced a striped half-black ball.
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
for MNT1 to come round before the approach clip; the engine cuts to it.

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
dune2k players say. Not built: troop marches (GO & SEARCH FOR EQUIPMENT, the
final attack's march), battles and the night attack, so the palace garrison
cannot yet be removed in play and the green route ends at the arrival rule
unless the garrison is gone; the SEE RESULTS globe colours; the credits'
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
- **Not transcribed.** `sub_9A58` also runs `sub_9934` (the sietch's byte 0x0B counter) and a daily motivation decay (`sub_9BCB(1)` when more than 8 days have passed since troop byte 0x14; below 5 the troop sulks with bit 5).
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
  - Only 2 harvesters (×4 output, `seg000:708a`) were found lying free all game. Buying more from the smugglers is not built yet.

## A fort turned sietch moves its people (2026-09-28)

A captured fort becomes a sietch on a new day (floppy `sub_9A58`, 9A6C–9A7E): its type byte becomes `type & 7`, and ds:27 is incremented. It then calls `sub_99F3`, which the engine had skipped.
- **Characters.** The routine walks the 12 character records at ds:FD8. A record whose second word is (0x80, place + 1), from `sub_61D8`, takes the new type byte, and its room becomes 1 or 2.
- **Paul.** If Paul is there, ds:4, ds:0B and ds:8 change the same way.
- **Why it matters.** Presence is a byte-for-byte test against ds:4–7 (`loc_136EE`). Without the fix-up, a character left there with STAY HERE keeps the fort's type byte (0x28), while Paul's position has the sietch's (0x00), so the character is absent. That is why the war council never started for Thufir at place 2 in the full speedrun run: persons in room was 0xb2, without Thufir's bit.
- **The troops.** The routine's troop callback (bp 7B77, at 9A4C) also clears flag 0x20 in the troop's word +10h and sets 0x1000 in +12h; the engine had only done the second.

With `battle.cpp`'s conversion completed, `check_speedrun.sh full` passes on floppy with no FORCED step (19 forts, the end on day 58). The CD run still stops at the palace for want of atomics: its atomics troops are captured.

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
FRESK frame, with the game menu in the command box (swift-dune Fresk.swift:
EXIT GLOBE, SEE MAP OF THIS AREA, SAVE GAME, LOAD GAME, OPTIONS & QUIT GAME;
SEE RESULTS replaces the map row on the results screen, not done). Hot zones
from the CD executable at 0x111A0: the book (24, 155)-(69, 176), the five
command rows 92..228 x 159 + 8 i, Paul's head on the box's top edge.

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
