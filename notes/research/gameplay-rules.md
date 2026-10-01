# Dune (1992): gameplay rules recovered from the CD executable

Research log, 2026-09-23. Sources, in order of weight:

1. **madmoose's `dune-chani`** annotation database for DNCDPRG.EXE v3.7
   (<https://github.com/madmoose/dune-chani>, HTML listing at
   <https://thomas.fach-pedersen.net/dune/cryo-dune-3.7-cd-dncdprg.html>):
   routine and data names, struct layouts, prose comments. Extracts per topic
   are next to this file (`chani-*.txt`, made with `scripts/chani_grep.py`).
2. OpenRakis' annotated `DNCDPRG_RECENT.ASM`, read at the addresses the
   database names (OpenRakis labels a seg000 offset `x` as `sub_1x` / `loc_1x`;
   its data names are 0x1F4B0 + ds offset).
3. The initial data segment of the player's DNCDPRG.EXE (file offset 63152).
4. Lionel Debroux' odrade (troop/NPC/smuggler table offsets) and the dune2k
   forum thread "Dune cheats" (John2022, hugslab) it cites.

Addresses below are seg000 code offsets / ds data offsets of the CD 3.7
executable. "Verified" = read in the disassembly; "annotated" = taken from the
database's comment without re-reading every instruction.

## Clock (verified)

- The PIT is reprogrammed to divisor 0x1745 = 5957: 1193182 / 5957 = 200.3 Hz
  (seg000:e85c, the divisor is the offset of `unk_20BF5`).
- The PIT handler (seg000:ef6a) decrements the countdown ds:46db each tick; on
  underflow it reloads it from ds:146e (= 12000 in the data segment),
  increments `game_time` (ds:2) and raises `new_hour_flag` (ds:46dd).
  One time period = 12000 / 200.3 = **59.9 s of real time**. The clock stops
  while `game_suspend_count` (ds:2788) is nonzero (dialogues, menus, load).
- 16 periods per day: day = time >> 4, period = time & 15 (seg000:1ac5/1ae0).
- `run_events_for_current_time_period` (seg000:1b23) runs once per period:
  date indicator, sky palette, new-day hook, troop occupation events
  (`run_troop_occupation_events`, seg000:6c6f), the vegetation walk, and the
  per-period action table at seg000:1db3 (period 3 = spice shipment counter,
  4 = Harkonnen raid scheduler, 8 = shipment reminder, 15 = Harkonnen growth).
- WAIT FOR EVENING / WAIT FOR MORNING advance to a target time, running every
  skipped period's events.

### Time on Dune: save-file observations and conversion

Added 2026-09-30 from research supplied by the user. The basic clock was
already documented above; the signatures, boundary values and editing examples
below were not. Preserve the distinction between observed save values and
independently verified executable behavior. This note concerns the DOS saves;
do not apply their compressed signatures to native Amiga or Sega CD files.

**Reported timing and display limits.** A day contains 16 periods of 90
in-game minutes. One period takes approximately 60 real seconds, making an
uninterrupted day approximately 16 real minutes. The executable-derived value
above is about 59.9 seconds per period; menus and other suspended states stop
that clock. The supplied research identifies Day 1 00:00 as the minimum,
Day 1 04:30 as the zero/default time, and Day 1 09:00 as the gameplay start.
WAIT FOR MORNING targets 04:30, and WAIT FOR EVENING targets 22:30. Travel
advances game time through its own travel steps; see Flight below.

The supplied observations distinguish the ordinary date indicator, which
cycles after Day 365 22:30, the results display, which reaches Day 999 22:30,
and the 16-bit time value, whose last pre-wrap evening is Day 4096 22:30.
The CD date routine at `seg000:1ad1` adds three with 16-bit arithmetic before
shifting by four; `seg000:1a6b` applies the 365-day display cycle and adds one.
The results display's exact saturation behavior and the start-time differences
between releases remain separate verification points; these observations do
not establish a game-over deadline.

**Reported compressed-byte landmarks.** The user found these prefixes before
the saved state time, including in a save taken well after the last fortress
was captured, during further spice mining and vegetation growth. In those
observations the runs represented by the `F7` tokens continued to contain zero.

```text
v2.1  C3 89 B2 FF FF F7 24 00 xx xx DT DT
v2.3  C4 89 B2 FF FF F7 30 00 xx xx DT DT
v3.7  C9 85 D6 FF FF F7 68 00 xx xx DT DT
```

The v3.7 sequence was also reported in `DUNE38Sx.SAV`. The v2.3 and v3.8
signatures have not been independently checked in this workstream.
`F7 count value` is a run-length token: for example `F7 68 00` expands to
104 zero bytes. It is not a fixed-position separator. A following zero byte
can join the run, increasing its count and moving the subsequent compressed
bytes. Decode the RLE body before locating a field; these signatures alone
are not a reliable general-purpose patching method.

**Correction to the proposed subhour interpretation.** The supplied note
tentatively called `xx xx` a counter from `0000` through `FFFF` and suggested
zeroing it when editing the date. The recovered CD state layout instead calls
the word immediately before `game_time` **`rand_bits` at `ds:0000`**; the
time word is at `ds:0002`. This is also the engine's `kRandomBits` /
`kGameTime` layout and is consumed by `World::rollRandom()`. It must not be
described as a verified fractional clock. Zeroing it changes random state;
preserve it when only changing the date. The real timer countdown is the
separate `ds:46db` field described above.

**Reported Log 1 / Log 2 values.** The original table wrote the hexadecimal
word most-significant byte first. The file stores its bytes in the opposite,
little-endian order:

| Time word | Bytes written to the file | Reported label |
| --- | --- | --- |
| `0xFFFD` | `FD FF` | Day 1 00:00 |
| `0xFFFE` | `FE FF` | Day 1 01:30 |
| `0xFFFF` | `FF FF` | Day 1 03:00 |
| `0x0000` | `00 00` | Day 1 04:30 |
| `0x000D` | `0D 00` | Day 2 00:00 |
| `0x16CC` | `CC 16` | Day 365 22:30 |
| `0x3E6C` | `6C 3E` | Day 999 22:30 |
| `0xFFFC` | `FC FF` | Day 4096 22:30 |

For a desired numbered day and a 90-minute slot counted from midnight,
multiply the day by 16, add the slot, subtract 19, and keep the low 16 bits.
The following is literal Python for calculating and encoding that word:

```python
def encode_time(day, slot_from_midnight):
    assert 1 <= day <= 4096
    assert 0 <= slot_from_midnight < 16
    time_word = (day * 16 + slot_from_midnight - 19) & 0xffff
    return time_word.to_bytes(2, "little")

assert encode_time(2, 0).hex(" ") == "0d 00"
assert encode_time(10, 8).hex(" ") == "95 00"  # Day 10, noon
```

The supplied four-byte example `00 00 95 00` zeros the preceding word as
well as setting Day 10 noon; preserve that word instead for a date-only
change. DOS saves also duplicate the time in their first, uncompressed
header word (`load_save_game_timestamp`, CD `seg000:b30f`; stamping at
`seg000:b389`). Updating only the state copy can leave the menu's date
different from the date loaded into play. Edit the decoded state time and
header consistently, then re-encode RLE and update the packed length; see
[save compatibility](../../third_party/scummvm/engines/dune/SAVES.md).

**Implementation follow-up discovered while recording this note.**
`GameScreen::slotLabel()` still contains an old placeholder that treats a
time unit as an hour and divides by 24. The ordinary date calculation also
needs a check that the three-period addition wraps at 16 bits near `FFFF`.
These are recorded fidelity gaps, not changes made by this documentation
update. Reproduce the original DOS load-menu labels and boundary dates
before correcting them; do not use the original Amiga save/load menus.
### External save and asset references (2026-09-30)

The user supplied two further historical references. Read them alongside the
executable-backed findings above; their unresolved guesses are not engine rules.

- [Dune Editor: Savegame Hex editing](https://sites.google.com/site/duneeditor/savegame-editing)
  collects community notes on save slots, RLE, 28-byte place records,
  27-byte troop records, equipment, spice stocks, contact range and the
  troop position field used for map icons. It credits Rymoah and John2022
  and references earlier forum/wiki work. Useful leads for save fixtures and
  map-troop investigations; its decimal file offsets describe particular
  compressed examples, not universal offsets. Its save-slot list includes
  restart slot 0, manual slots 1/2 and automatic slots 3/4. Its troop-position
  table and record fields must be checked against each release's native
  coordinate/animation routines before implementation.
- [Dune Editor: bigs_fr mirror](https://sites.google.com/site/duneeditor/bigs_fr-mirror)
  preserves historical floppy/CD file inventories and format research on HSQ,
  sprite sheets, SAL scene commands and map resources. It describes sprite
  offset tables, palette blocks, packed pixels/RLE, and scene placement,
  transformations, shapes and character markers. Its music/sound sections
  mainly list files; its save section only identifies the filename extension.
  Those sections are not complete audio or save-format specifications.

**Local cross-checks and limits.** The engine already has separate HSQ and
save-RLE decoders (`resource.cpp`, `saves.cpp`); do not substitute one for the
other. A save's six-byte header is outside the RLE body, which resolves some
apparent marker exceptions in older editing notes. The location status bit
`0x01` means exhausted for harvesting in the recovered rules; vegetation can
cause exhaustion, but the bit alone is not a vegetation record. Palette and
SAL format notes are retained as references without changing the user's
current functionality/completion priority. The executable and verified
original captures remain the authority for fixes.

### Swift-Dune wiki: central research reference

The user designated the [Swift-Dune wiki](https://github.com/codingstyle/swift-dune/wiki)
as the central reference on 2026-09-30. Consult it first to locate documented
formats, resources, animations and game logic, then cross-check the relevant
release's executable and captures when implementing behavior.

- [Savegame](https://github.com/codingstyle/swift-dune/wiki/Savegame): RLE,
  location/troop records, map positions, occupations, equipment and flags; useful
  for map UI and save fixtures. Header/state sections remain unfinished. The
  listed automatic slot numbers 4/5 differ from this engine's verified 3/4;
  preserve release-specific evidence instead of changing filenames from the table.
- [Game phases](https://github.com/codingstyle/swift-dune/wiki/Game-Phases): story
  milestones through the ending; use to organize progression tests, checking
  actual prerequisites/callbacks in each binary.
- [Songs](https://github.com/codingstyle/swift-dune/wiki/Songs) and
  [Sounds](https://github.com/codingstyle/swift-dune/wiki/Sounds): song/resource
  mappings and explicitly floppy sound-file descriptions. These are useful DOS
  audio leads, not evidence that Amiga has the same banks or cue rules.
- [Versions](https://github.com/codingstyle/swift-dune/wiki/Versions) and
  [Compression](https://github.com/codingstyle/swift-dune/wiki/Compression): PC
  release overview and compression entry points; retain separate HSQ, sprite
  RLE and save-RLE handling.

The home page and six topic pages above were read. This is a navigation/reference
record, not a claim that every wiki page or uncertain field has been verified.

## Flight (verified / annotated)

- A travel steps every 0x300 PIT ticks = **3.83 s** (`travel_pump`,
  seg000:4f0c). Each step moves **one map cell** along the dominant axis of the
  heading (`travel_step_position` seg000:5206, `travel_heading_deltas`
  seg000:5198: the major axis gets 0x20 = one cell).
- Every 16th step runs one time period (`travel_advance_step` seg000:4b3b).
  A crossing of N cells takes N x 3.83 s and N / 16 periods.
- Distances are Chebyshev in cells: max(|dlng| / lng_units_per_cell[|lat|],
  |dlat|) (seg000:7c8f).
- SKIP TO DESTINATION fast-forwards up to 200 steps (seg000:4ffb), each still
  checking the hostile-zone warning; BACK TO STARTING POINT, TOWARDS NEAREST
  PLACE, CHANGE DESTINATION re-aim the flight.
- The view: CD plays the MNT1-4 HNM loops chosen by the terrain ahead (sand /
  rock / transitions, seg000:4ec6) with a minimap showing a trail (ICONES 0x2f)
  and the position (ICONES 0x30); the floppy has no MNT videos and draws the
  ORNY cockpit. There is no heading-dependent ornithopter sprite on the map.
- Flying within the 9x9 cells around a hidden sietch that the story phase makes
  discoverable (bearing within +-0x60 of the heading) arms the "travelling
  companions" action: a companion comments and the verbs become GO TOWARDS
  THIS PLACE / RESUME FLIGHT (seg000:40f9, 3555).

## Discovering places (verified)

- Location status (byte 10): 0x80 hidden, **0x40 prospected** (not "known"),
  0x02 battle, 0x01 exhausted (verified in the mining test); 0x10 is set on
  the palace, 0x08 is tested by the new-day troop hook and 0x20 by the
  irrigation test (meanings not yet pinned down).
- A hidden place is only discovered by arriving there (`location_mark_discovered`
  seg000:425b): clear 0x80, zero discoverable_at_phase, count sietches in
  ds:27, phase 0x10 on Tuono-Harg. Story phase callbacks also reveal specific
  places (Sihaya-Clam at 0x28, Oxtyn-Tabr at 0x44, ...).
- Sietches whose `discoverable_at_phase` <= phase feed the "there is a sietch
  very near / not far" lines (nearest distance in ds:d6, seg000:5274).

## Hiring (rallying) a troop (verified)

- The troop's leader stands in the sietch as person 14 (unrallied troop,
  occupation bit 7) and hired troops' chiefs as person 15, 16, ... (room entry
  classification seg000:316e).
- COME WITH ME to the leader (seg000:95c1) runs a charisma check: pass when the
  Harkonnen population sum ds:ac < 1000, or charisma > 100, or
  (100 - charisma) / 4 <= the troop's motivation modifier (seg000:6efd).
- `troop_rally_troop` (seg000:66ce): ds:28 (rallied troops) + 1, charisma + 1
  (seg000:6f78, capped 200; every 4 points raise all troops' motivation),
  occupation = (occupation & 0x20) | **2 (waiting for orders)**, occupation
  clocks restarted, rally day stored; a location with discoverable phase 0
  gets 2 and a map circle of radius 2 marked as explored.

## The stillsuits (verified, 2026-09-25)

`scripts/dune_dialogue_dump.py DUNE.DAT regex` lists the DIALOGUE entries
with their CONDIT expressions (CD numbering).

1. Phase 1 (after Gurney), exactly two troops rallied (ds:28 == 2): Leto asks
   for stillsuits (event 11, phase 2).
2. Phase 2: Gurney remembers the Fremen who told him of a stillsuit maker.
3. Carthag-Tuek's chief (place 12; the location block's ds:4e == 0x205, its
   names) says "I know a stillsuit maker... fly eastwards... fly with
   someone" (event 11, phase 3).
4. Tuono-Tuek (place 15, names 0x303, hidden, discoverable from phase 2, east
   of Carthag-Tuek) is found by flying past it; its Fremen, troop 5 (ds:2e ==
   5 once the troop is staged), explain their stillsuits and say "I know of
   two more sietchs..." with event 12: chapter 4, and the stillsuits are the
   Atreides'.
5. Entering palace room 2 with Gurney from phase 4 (pending_room_action 5, the
   room-entry scan): "Look! The stillsuits have been stored here!"; Leto
   (phases 4-7): "We've got the stillsuits. Good work Paul!"

The location block (prepare_location_data_for_condit, seg000:331e) and the
room-entry scan (seg000:35b4) were missing from the engine: without the first
the chief's line never matched, and without the second no room-entry line was
said. Both are in now (`World::stageLocationForConditions`,
`GameScreen::roomEntryScan`).

## Contact range and the map menu (verified, 2026-09-24)

- `location_visibility_distance` (ds:1176, init 1) is how far, in map cells,
  Paul reaches troops. Jessica's lines carrying dialogue event 8 raise it
  (seg000:a186): the first from 1 to 30 with charisma + 10, each later one
  + 20; with ds:0a bit 1 set, charisma + 40 and no limit. ds:d5 then holds
  0x80 - range / 6 (0 from 100 on).
- The map menu (seg000:878c) offers GIVE ORDERS TO TROOP (the troop where
  Paul stands) while the range is under 2, and CONTACT FREMEN TROOPS (cycle
  through the rallied troops, seg000:86cc) after. SEE SPICE DENSITY is greyed
  and FIND PROSPECTORS hidden before phase 5; TAKE AN ORNITHOPTER needs one
  parked at Paul's place.
- The DUNE MAP title popup closes on either mouse button or after 1000 timer
  ticks (5 s).

## Occupations (verified)

- Classes by bits 2-3: spice 0-1, army 4-5, ecology 8-10. SPECIALIZE IN SPICE
  gives 0 (troop 3 at seg001:08e0 gives 1, prospecting), ARMY 4, ECOLOGY 8
  (becomes 10, bulb growing, where the place has no bulbs). The class verbs
  switch between the two jobs of a class (low bit).
- Per period (seg000:6c26 table): 0 spice mining, 1 prospecting, 4 military
  training, 5 espionage, 6 attacking, 8 irrigation, 9 wind-trap, 10 bulbs.
- Motivation modifier (seg000:6efd): motivation (+20 with ds:fa), +30 for an
  attacking troop at Paul's location, else capped at 100 (irrigation/wind-trap
  count as 100); in phases 0x64-0x67 minus 40 (floor 10).
- **Spice mining** (seg000:6fe5, 708a): needs a prospected, unexhausted place
  with density >= 1 and a working harvester. Harvest per period in kg:
  `P = (mod + (spice_skill & 0xF0)) * population_byte`, `P /= 4` without a
  harvester (equipment bit 7), `kg = (((density & 0xF0) + 1) * (P >> 8)) >> 7`
  (9 bits). The stock ds:a0 counts 10 kg batches (remainder in ds:46e1); the
  place's density drops by (kg + byte 19) / byte 17 (spice amount). The
  troop's running total raises its spice skill every 128 kg (cap 0x5f).
  Harvesters break (worm / saboteur events by region, ds:1141) and are
  repaired in the period whose number equals the troop id's low nibble.
- **Prospecting** (seg000:70cc): duration = spice_amount * 16 /
  (motivation + spice_skill) periods after the job started; then the place
  becomes prospected (status 0x40) and the troop's skill rises.
- Every 64 periods skills of the unused classes decay by 1 (seg000:6d7b).

## Spice shipments (verified with capstone, 2026-09-24)

The OpenRakis listing drops some instructions here (the `cmp al, 0c0h`
family at seg000:2595), so these rules were read from the executable with
`scripts/dune_disasm.py`; raw disassemblies are in `disasm-*.txt` next to
this file.

- **Start.** Phase 0x14's long idle alone in the open desert (current_scene
  ds:8 = 0xff, no companions, 1000 ticks; WAIT FOR EVENING/MORNING count as
  the wait, seg000:0f8e) runs sub_11071: phase 0x15, Paul's first vision
  (ds:0a bit 0, vision message 1 queued), and sub_12090 arms the shipments
  with the first demand at once.
- **Demand** (seg000:20d2): `(ds:c3 * 150 + 100) * (rand & 63 + 224) >> 8`
  in 10 kg batches; when the last shipment fell short (ds:be < 0x80) it is
  multiplied by `(511 - 2 * be) / 256` (up to twice). ds:c3 + 1, ds:cf = 0,
  ds:bf |= 0x90, and the Emperor's COMM message 0x20b (last shipment met) or
  0x30b.
- **Period 3** (seg000:20a4), while armed: paused by ds:c2 (the final attack)
  it only updates the days left; with a demand pending, 1-3 days late posts the
  reminder message `table[days * 4 + min(class, 2)]` from cs:2165 (4 5 6 0 /
  5 6 0 0 / 6 0 0 0; 0 = the end), 4 days late ends the game; an unpaid
  flag (ds:11bb) ends it too; otherwise it counts the days to the event day
  (ds:118d) and rolls the next demand on it.
- **Period 8** (seg000:1dda): with a demand pending, Duncan not travelling with
  Paul, Paul not in the COMM room and no final attack, vision message 0x30b
  ("Paul! Do not forget the spice shipments").
- **Bargaining with Duncan.** His event-8 line (seg000:2239) builds the offers
  ds:b4..ba from the demand D and the stock S: S < D: S, 3/4 S, 1/2 S, 1/4 S;
  else D, S, then 3/4 S and 1/2 S (S < 1.5 D, flag 2), 3/4 S and 1.5 D (S < 2 D,
  flag 4), or 1.5 D and 2 D (flag 6). It also stages the smuggler with the
  oldest unpaid bill (ds:1c..20, ds:11f7). His event-4 line opens ARGUE /
  ACCEPT / REFUSE / WHAT ? (ds:1ffe); a choice sets ds:9f (1, 2, 3) and ds:1a
  + 1, and the dialogue goes on. His event-9 line on ACCEPT agrees the offer
  `(ds:1a - 1) & 3` (ds:c0) and arms ds:1158.
- **Shipping** (seg000:135ad, sub_12566): entering the COMM room (room 8) with
  Duncan present and an agreed amount pays it out of the stock; ds:be =
  amount * 128 / demand (1..255); the next event day is 7 days on (>= 1.5 x),
  6 (> 1 x), 5 (exactly), or 4 (short; short twice running zeroes ds:be),
  plus `rand & ((c3 >> 1) & 3)`.
- **Duncan's event 15** (seg000:24a3): before phase 0x10 a flag; after, the
  talk ends and the Emperor's verdict comes by COMM as message
  `((class + 7) << 8) | 0x0b` (class 5, nothing shipped, marks the unpaid flag).
- **The end** (pending_room_screen_request 7): COMMAND 0xc1 "As Paul Atreides
  failed to respond to my spice demands...".

## COMM room and visions (verified, 2026-09-24)

- Messages are words `(variant << 8) | person` in ds:1179 (10, the oldest
  dropped), count ds:c8, unread ds:c9, bit 7 = read. From phase 0x38 a new one
  away from the COMM room queues vision 0x201 ("A message has arrived").
- In palace room 8 the verbs are VIEW NEW MESSAGES (greyed with none unread)
  and Messages already seen; the list shows the senders newest first, then
  Cancel; a sender presents their topic-4 line with ds:24 = the variant
  (reading an Emperor's demand sets ds:bf bit 5).
- Visions are `(sender << 8) | type` (+ a place) in ds:1190, 10 at most, only
  after the first vision. In a room, after 0x32 idle ticks the head message is
  spoken by its sender if present (DIALOGUE character 16 topic 4, ds:ea =
  type); after 0x1c2 ticks it comes as the dream over VIS.HSQ.

## Scripted scenes (verified, 2026-09-24)

The " Continue..." sequences are byte scripts in the code segment (CD
seg000:12db-1391; the floppy's are 0x3ca later): each byte is twice the index
into the action table cs:1475. 00 [room, count, cast...] the shot with an
explicit cast; 02 [speaker] the speaker's next topic-7 line; 04 redraw; 06
[speaker] the head, silent; 08 wait; 0a the evening and CHANKISS sprite 0; 0c
CHANKISS sprite 1; 0e/10 the prospector's density-map lesson; 12 a shot
through a transition; 14 the final scene; ff the end (back to the room).
Dialogue event 3 picks 0x12f8 (< 0x14), 0x134f (< 0x18), 0x1370 (< 0x30) or
0x12db; phases 0x0c, 0x48 and 0x58 start 0x1321, 0x1313 and 0x12fb.

## Stilgar (verified, 2026-09-24)

- Event 8 arms seg000:2ccf after the line: ds:0a bit 3 (heard of the Water of
  Life); if Paul accepted (ds:9f = 1) he drinks: charisma >= 100 survives
  (ds:0a bit 1, three periods pass), otherwise the ending COMMAND 0xbd.
- Event 9 (seg000:2d2c): the final attack; ds:c2 + 1 stops the shipments and
  the army troops with atomics (occupation 4, equipment bit 2) march on the
  Harkonnen palace (seg000:84a6, the march not built yet).

## Ecology and the green route (verified with capstone, 2026-09-24)

Sources: the executable (disassemblies `disasm-vegetation-63f0.txt`,
`disasm-ecology-jobs-7660.txt`, `disasm-7443.txt`, `disasm-equipment-7cbb.txt`,
`disasm-final-14c9.txt`), dune-chani's names, and the dune2k thread
"Alternative Ecology Ending" (forum.dune2k.com/topic/24592): vegetation grown
up to the Harkonnen palace lets Paul fly there safely; the final dialogues are
those of the military victory. The binary agrees: there is one ending scene,
and ecology is a second way to reach it.

- **The map's cells.** Low nibble terrain; bits 4-5 the stage: 0x10 sprouting
  (drawn as ONMAP tufts 0x78/0x79), 0x20 Atreides land, 0x30 Harkonnen land
  (10344 cells at the start, 102 Atreides); bit 6 marks a place's cell (the
  new-game pass, seg000:01a1). The saves keep bits 4-5 of every cell.
- **Ecology troops.** SPECIALIZE IN ECOLOGY is greyed until Kynes is met
  (ds:0a bit 5). It sets job 8 (irrigation); only at place 62 (record 0x7c8)
  without bulbs does it become 10 (bulb growing). The ecology menu (ds:21a6):
  GO & SEARCH FOR EQUIPMENT, ASSEMBLY WIND-TRAP (job 9), SPECIALIZE IN SPICE /
  ARMY.
- **Wind trap** (seg000:7711): each period location byte 0x1b gains
  `max(1, min(255, 2 * motivation + troop byte 0x16) * men >> 12)`; on the
  wrap the place gets its wind trap (status bit 5, appearance | 8), 5 water,
  and the troop goes back to irrigating.
- **Bulbs** (seg000:767d): ds:ec counts 256 periods, then 16 bulbs (location
  byte 0x1a); the troop then irrigates.
- **Irrigation** (seg000:7693) needs water, a wind trap and bulbs carried by
  the troop (equipment bit 1; MODIFY EQUIPMENT takes them from the place). The
  first period: status bit 0 (vegetation), no more spice (density 0), a disc of
  radius 4 on the place. Then the ecology skill / 4 fills a progress byte;
  each wrap: ecology skill + 1, 12 water (none left: the troop stops), the
  radius + 1 up to 12, the disc's centre 2 rows north (down to -82), and the
  disc again.
- **The disc** (seg000:6515 / 653a): every cell not sprouting becomes Atreides
  land, a quarter of the sand cells sprout (the rotating mask 0x44 on terrain <
  8); a place under it loses its spice; a Harkonnen fortress under it (not the
  two palaces) falls without a battle (seg000:7443): Atreides land of radius 5
  round it, charisma + 4, motivation + 1 for all, held (status bit 3), its
  Harkonnens freed as Fremen (up to 8) or gone. With one Harkonnen place left,
  the final attack begins (ds:c2 = 1).
- **Every new day** (seg000:63f0): water behind every wind trap + 1 + half the
  sprouting cells next to the place (up to 250); then a 0x46-step LFSR walk
  (taps 0x402, state in cs:65b4) turns visited sprouting cells into Atreides
  land.
- **Flights** (seg000:4182): to a place not held by the Atreides, each step
  over Harkonnen land warns ("ENTERING HARKONNEN ZONE", COMMAND 0xbf) and the
  eighth in a row brings the ornithopter down (room screen 2, COMMAND 0xbc).
  Green land under the route is what makes the palace reachable.
- **Arrival** (seg000:503c): at a place in battle or not held, Fremen
  attacking it start the night battle; otherwise any Harkonnen troop there and
  Paul is shot (room screen 4, COMMAND 0xbe).
- **The end** (sub_14057): location_and_room 0x3002, room 2 of the Harkonnen
  palace: phase 0xc8 and the scene cs:128f: the Baron's hall with Duncan,
  Jessica, Chani, Thufir, Gurney, Stilgar, seven chiefs, Feyd, the Emperor and
  the Baron; their topic-7 lines; then FINAL.HSQ (sprites 0-2, then 3 and 4:
  "THE END") and the cast (COMMAND 0x122-0x12e over portrait scenes), and the
  program exits.

## Harkonnens (annotated)

- Period 15: each Harkonnen troop with 1..199 population gains 1 on a set
  random bit (seg000:1d10).
- New day: Harkonnen production ds:a8 = sum(density / 8) over non-Atreides
  places + rand(sum / 16) (seg000:1cda).
- Period 4: raids on even days once armed (seg000:1f64): target = a known
  sietch near a Harkonnen area (seg000:2017); up to two troops move there,
  the place becomes a battle site, and if Paul is there the night attack
  starts.

## Results (verified layout)

SEE RESULTS (seg000:b96b) slides the FRESK panels open and draws the stats
overlay (UI records at ds:2482): "N day on DUNE" (16,6), "CHARISMA = N"
(216,6); controlled area percentages at (20,69) Harkonnen colour 0x3f and
(48,69) Atreides colour 0x25 under "CONTROLLED AREAS" (8,80); spice production
(240,60)/(272,60) under "SPICE PRODUCTION" (236,71); number of men
(240,131)/(272,131) under "NUMBER OF MEN" (236,142); "ATREIDES" (35,125),
"HARKONNENS" (35,139). Six gauges (ICONES 0x37 / 0x38 bars, 0x39 cap, height
<= 30) grow at (26,62), (54,62), (252,54), (280,54), (252,125), (280,125)
toward: area / 2 + 1, potential spice >> 4 + 1, today's spice >> 4 + 1, the
population sums' high bytes + 1; a trend glyph (rose / fell / unchanged) sits
on each.

## Smugglers, battles (to do)

Six smuggler records at ds:10d8 (region, haggling, stock and prices of
harvesters, ornithopters, krys knives, laser guns, weirding modules), restocked
each new day. Battles weigh both sides' skills and populations
(seg000:60f8); the attacking occupation's per-period resolution is at
seg000:739e. Not yet read in detail.

### Phase flowchart and ending branches

The [DOS floppy phase flowchart](game-phases/dune-game-phases-floppy.svg)
shows story transitions, their conditions, final-attack stages, ecology victory
and continued play. [Evidence and corrections](game-phases/game-phase-flowchart.md)
explain omitted phases `0x04` and `0x2E`, reachable `0x6C`, the unverified ordinary
route into `0x07`, and the separate story/final-attack state variables. The SVG,
editable generator and JSON are retained together; no original game data is
included. It is a source-audited milestone map, not a claim of complete UI coverage.
