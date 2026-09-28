# Credits

This engine stands on many years of reverse engineering by the Dune community.
Nothing here would exist without the people below. If you add knowledge from a
new source, add it to this file in the same change.

*Dune* was created by Cryo Interactive (designed by Rémi Herbulot, music by
Stéphane Picq and Philippe Ulrich) and published by Virgin Games in 1992. The
engine contains no game data; you need your own copy of the game.

## Projects this engine builds on

| Project | Authors | Licence | What we took from it |
| --- | --- | --- | --- |
| [OpenRakis](https://github.com/OpenRakis/OpenRakis) | the OpenRakis contributors (xcomcmdr, madmoose and others) | Apache 2.0 | The annotated disassemblies `asm/cd/DNCDPRG_RECENT.ASM` and `asm/floppy/DUNEPRG.ASM`: the location and exit table (`sub_13EFE`, `sub_13F27`, `sub_15E4F`), the room drawing routine (`sub_13B59`), the sky routine (`sub_138B4`), the dialogue engine (`sub_19F9E`, `sub_1A03F`, `sub_1A396`, `sub_1CF70`, `sub_193DF`, `sub_1CFB9`, the driver at `loc_19472`), the string builder (`sub_18944`) and the routine names quoted in our comments |
| [Cryogenic](https://github.com/OpenRakis/Cryogenic) and [Spice86](https://github.com/OpenRakis/Spice86) | Kevin Ferrare, Maximilien Noal and the OpenRakis contributors | Apache 2.0 | The repository this work lives in; the C# overrides are a behavioural reference for the original's routines |
| [dune-rust](https://github.com/madmoose/dune-rust) | Thomas Fach-Pedersen (madmoose) | none stated; **reuse permitted by the author** (told to the project owner, 2026-09-22) | `.SAL` command layout, the sprite blitter's rules (8-bit sprites, scale factors, flips, palette offset), the polygon fill (gradient, Galois noise, edge tracing), the sky palette layout, the globe renderer, the night-attack simulation, the CONDIT.HSQ expression decoder (`crates/condit`), the save-game / data-segment layout and RLE (`crates/savegame`), the flat map renderer and TABLAT rows (`map_renderer.rs`, `tablat.rs`), the HSQ decoder (`hsq.rs`, also our Python tooling) and the lip-sync header of the character sheets. `room.cpp`, `Sprite::drawFrame`, `scene.cpp`'s globe, `attack.cpp`, `dialogue.cpp`, `map.cpp` and `saves.cpp` follow it closely |
| [madmoose/dune](https://github.com/madmoose/dune) (unfinished ScummVM engine) | Thomas Fach-Pedersen (madmoose) | GPL 2.0 or later (file headers) | The intro's scene table and the order of the CD intro videos |
| [chani](https://github.com/madmoose/chani) and [dune-chani](https://github.com/madmoose/dune-chani) | Thomas Fach-Pedersen (madmoose) | MIT (chani); dune-chani states no licence | dune-chani's annotation database of DNCDPRG.EXE 3.7 (routine names, struct layouts, comments) is the main source of the gameplay rules: clock, flight, spice mining and prospecting, rallying, occupations, discovery, the new-game pass and the results screen (`world.cpp`, `scene.cpp`, `notes/research/gameplay-rules.md`), and the names of the shipment, COMM, vision and scripted-scene routines (`story.cpp`, `story_scene.cpp`) |
| [FED2k / dune2k forum](https://forum.dune2k.com/topic/24592-alternative-ecology-ending/) | the thread's players | forum posts (facts only, not copied) | The ecology route as players found it: vegetation grown up to the Harkonnen palace makes the flight safe, and the ending is the military one; checked against the executable (`ecology.cpp`) |
| [Capstone](https://www.capstone-engine.org/) | Nguyen Anh Quynh and contributors | BSD-3-Clause | Tool only (not linked): `scripts/dune_disasm.py` disassembles the executable's code for the rules the listings mis-decode (the shipments, the dialogue events, the scenes) |
| [swift-dune](https://github.com/codingstyle/swift-dune) | Christophe Buguet (codingstyle) | none stated; **reuse permitted by the author** (told to the project owner, 2026-09-22) | The whole floppy intro, credits and prologue as scene logic: order, durations, transitions, sprite numbers and positions (Intro.swift, Prologue.swift and one file per scene), the character animation player, the sky tile layout and time-of-day palettes, the dissolve pattern, the water ripple, the control panel layout and font rules (UI.swift, Font.swift), and the room-marker to character mapping. `intro_scenes.cpp`, `attack.cpp` and `panel.cpp` are transcriptions of it |
| [AdPlug](https://github.com/adplug/adplug), HERAD player | Stas'M | LGPL 2.1 or later | `music.cpp` is a reduced port of its playback logic (version 1 / OPL2) |
| hnm1dump | VAG, with Rymoah's console version | "Feel free to use this code for any purposes as long as the [credit] line is kept": *Cryo HNM 1 Dumper v.0.2 by VAG, ARR. vagsoft@mail.ru* | HNM chunk and tag layout, palette block rules. Found in dune_revival's `tools/hnm1dump` |
| [dune_revival](https://github.com/mberdev/dune_revival) / [dunerevival-code](https://github.com/sonicpp/dunerevival-code) | Bigs, Honza, Pierro, mberdev, sonicpp and the Dune Revival contributors | GPL 3.0 | First reference for the HSQ, sprite and `.SAL` formats (`bigs_c/test_NDS`) |
| [odrade](https://github.com/debrouxl/odrade) | Lionel Debroux (with Dmitri Fatkin, John2022, hugslab) | GPL 2.0 | The troop record layout (27 bytes: occupation codes, skills, equipment bits, population) and the NPC/smuggler table positions, used by `World::troop` and FINDINGS.md |
| UNLZEXE | Mitugu Kurizono (1990) | stated as freely usable in its source header; **verify the exact terms before publishing** | The LZEXE 0.91 decompression algorithm (bit queue reloaded after the sixteenth bit, the two match encodings) that `World::unpackLzexe` reimplements to read the floppy's DUNEPRG.EXE |
| [ScummVM](https://www.scummvm.org/) | the ScummVM team | GPL 3.0 or later | The framework, the iOS backend and the OPL emulators |
| Cryo's `disk_to_hd` (on the Amiga disk 1) | Cryo Interactive (1992) | part of the game; read, not copied | Its symbol table and code gave the Amiga disk layout (`dir.0`, 510-byte sectors with a checksum, the file-name tables): `scripts/dune_amiga_extract.py` reimplements the reading |
| [amitools](https://github.com/cnvogelg/amitools) (`xdftool`) | Christian Vogelgsang | GPL 2.0 | Tool only: listing and unpacking the Amiga disks' OFS file system during the investigation; the extractor has its own small OFS reader |
| [Capstone](https://www.capstone-engine.org/) (again) | Nguyen Anh Quynh and contributors | BSD-3-Clause | Tool only: the 68000 disassembly of the Amiga executable `dune` (`notes/amiga-port/tools/dis_game.py`), the source of the Amiga formats in `amiga.cpp` |
| "DUNE Full Game Speedrun (Amiga)" video | MilkToast (runner) | video, facts only | The reference for the Amiga port's look (talk balloon colours, mirror, ending layout) and the route in `notes/amiga-speedrun/route.md` |

For the Sega CD release no earlier reverse engineering was found; its
formats were decoded from the disc (FINDINGS.md, "Sega CD / Mega CD
release"). General Mega Drive / Mega CD hardware facts (the VDP's 4-bit tiles,
name-table words, sprite sizes and CRAM colours; the disc's boot header and
the IP/SP layout) are public knowledge from the console's documentation and
emulator sources such as Charles MacDonald's VDP notes and the Genesis Plus GX
and MAME Mega CD drivers; no code was taken from them. The gameplay references
are a Let's Play recording ("Let's Play Dune (Sega CD) 24 - The Secret
Ending") and a full US longplay ("Sega CD Longplay 047 - Dune (US)"), used
only to compare screens and to measure layouts (portrait placements, box
colours); `scripts/segacd_portrait_match.py` uses
[OpenCV](https://opencv.org/) (Apache 2.0; tool only, not linked) for the
template matching.

Also consulted: Zwomp's ["Exploring the Dune files"](https://zwomp.com/tags/dune/)
(HSQ format), Bigs' [Dune pages](https://www.bigs.fr/dune_old/), the
[OpenRakis wiki](https://github.com/OpenRakis/OpenRakis/wiki),
[hsqLib](https://github.com/jeancallisti/hsqLib) by Jean Callisti, and xcomcmdr's
[r/dune post](https://www.reddit.com/r/dune/comments/1sqh5ja/) that pointed us to
several of the above. The Dune Reborn Discord linked from swift-dune's README is
where this community meets.

## Licensing status — read before publishing

The engine is meant to be GPL 3.0 or later, like ScummVM. Two sources above
carry **no licence file**; their authors have given the project owner
permission to reuse their code (2026-09-22). A written note or a licence on
their repositories would still make this durable for anyone who forks this
engine:

- **dune-rust**: the polygon rasteriser, blitter rules, globe renderer and
  night-attack simulation are close re-expressions of its code (itself a
  transcription of the original game's routines).
- **swift-dune**: the intro, credits, prologue and panel are transcriptions
  of its scene code.

AdPlug's LGPL code may be combined into a GPL work; keep its attribution in
`music.h`. VAG's credit line must stay with the HNM player (`hnm.h`).
