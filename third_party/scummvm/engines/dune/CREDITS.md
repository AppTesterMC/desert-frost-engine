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
| [OpenRakis](https://github.com/OpenRakis/OpenRakis) | the OpenRakis contributors (xcomcmdr, madmoose and others) | Apache 2.0 | The annotated disassemblies `asm/cd/DNCDPRG_RECENT.ASM` and `asm/floppy/DUNEPRG.ASM`: the location and exit table (`sub_13EFE`, `sub_13F27`, `sub_15E4F`), the room drawing routine (`sub_13B59`), the sky routine (`sub_138B4`), and the routine names quoted in our comments |
| [Cryogenic](https://github.com/OpenRakis/Cryogenic) and [Spice86](https://github.com/OpenRakis/Spice86) | Kevin Ferrare, Maximilien Noal and the OpenRakis contributors | Apache 2.0 | The repository this work lives in; the C# overrides are a behavioural reference for the original's routines |
| [dune-rust](https://github.com/madmoose/dune-rust) | Thomas Fach-Pedersen (madmoose) | **none stated** | `.SAL` command layout, the sprite blitter's rules (8-bit sprites, scale factors, flips, palette offset), the polygon fill (gradient, Galois noise, edge tracing) and the sky palette layout. `room.cpp` and `Sprite::drawFrame` follow it closely |
| [madmoose/dune](https://github.com/madmoose/dune) (unfinished ScummVM engine) | Thomas Fach-Pedersen (madmoose) | GPL 2.0 or later (file headers) | The intro's scene table and the order of the CD intro videos |
| [chani](https://github.com/madmoose/chani) | Thomas Fach-Pedersen (madmoose) | MIT | Consulted; nothing taken yet |
| [swift-dune](https://github.com/codingstyle/swift-dune) | Christophe Buguet (codingstyle) | **none stated** | Facts only, no code: the floppy intro's script (scene order, durations, transitions), sprite numbers and positions for the text cards, starfield, dunes and title, the sky tile layout and the palette numbers for day, night, sunrise and sunset, and the names of the palace rooms |
| [AdPlug](https://github.com/adplug/adplug), HERAD player | Stas'M | LGPL 2.1 or later | `music.cpp` is a reduced port of its playback logic (version 1 / OPL2) |
| hnm1dump | VAG, with Rymoah's console version | "Feel free to use this code for any purposes as long as the [credit] line is kept": *Cryo HNM 1 Dumper v.0.2 by VAG, ARR. vagsoft@mail.ru* | HNM chunk and tag layout, palette block rules. Found in dune_revival's `tools/hnm1dump` |
| [dune_revival](https://github.com/mberdev/dune_revival) / [dunerevival-code](https://github.com/sonicpp/dunerevival-code) | Bigs, Honza, Pierro, mberdev, sonicpp and the Dune Revival contributors | GPL 3.0 | First reference for the HSQ, sprite and `.SAL` formats (`bigs_c/test_NDS`) |
| [odrade](https://github.com/debrouxl/odrade) | Lionel Debroux | GPL 2.0 | Consulted for save-game and troop/location structures; nothing taken yet |
| [ScummVM](https://www.scummvm.org/) | the ScummVM team | GPL 3.0 or later | The framework, the iOS backend and the OPL emulators |

Also consulted: Zwomp's ["Exploring the Dune files"](https://zwomp.com/tags/dune/)
(HSQ format), Bigs' [Dune pages](https://www.bigs.fr/dune_old/), the
[OpenRakis wiki](https://github.com/OpenRakis/OpenRakis/wiki),
[hsqLib](https://github.com/jeancallisti/hsqLib) by Jean Callisti, and xcomcmdr's
[r/dune post](https://www.reddit.com/r/dune/comments/1sqh5ja/) that pointed us to
several of the above. The Dune Reborn Discord linked from swift-dune's README is
where this community meets.

## Licensing status — read before publishing

The engine is meant to be GPL 3.0 or later, like ScummVM. Two sources above
carry **no licence**, which legally means all rights are reserved by their
authors:

- **dune-rust**: our polygon rasteriser and blitter rules are close
  re-expressions of its code (itself a transcription of the original game's
  routines). **Ask madmoose for permission, or for a licence on the
  repository, before this engine is published or proposed to ScummVM.** His
  ScummVM attempt is GPL, so the intent is likely friendly, but it is his call.
- **swift-dune**: we used measurements and sequence facts, not code. A
  courtesy note to codingstyle is still the right thing, and a request for a
  licence would let future contributors port scenes directly.

AdPlug's LGPL code may be combined into a GPL work; keep its attribution in
`music.h`. VAG's credit line must stay with the HNM player (`hnm.h`).
