# Dune engine proposal for ScummVM

This is a copy-ready technical brief for the ScummVM development team and
Developer Central/wiki. It describes the current Cryogenic work without
presenting the engine as ready for upstream merge.

## Summary

Cryogenic is developing a new native ScummVM engine for Cryo Interactive's
*Dune* (1992), initially targeting the English DOS CD and floppy releases. The
engine is a clean-room reimplementation: it reads original game data supplied
by the user, replaces the original executable, and does not redistribute
copyrighted game data.

The current code lives at:

```text
third_party/scummvm/engines/dune/
```

The project is being developed desktop-first, with an iOS build used for
device testing. The purpose of this proposal is to invite review of the
architecture, format work, legal/credit requirements, and the path toward an
upstream-quality engine.

## Current implementation

The current prototype can:

- detect the English DOS CD release by `DUNE.DAT` and the floppy release by
  `DUNES.HSQ`;
- read the CD archive or a floppy directory through one `Resource` interface;
- decode HSQ-compressed resources with bounds checks;
- decode the 4-bit and 8-bit sprite sheets, palettes, transparency, scaling,
  and flipping;
- render `.SAL` rooms, sky tiles, polygons, lines, room sprites, palace exits,
  and the 320x200 control panel;
- play CD first-generation HNM videos and their embedded VOC audio;
- play the HERAD OPL2/AdLib music format through ScummVM's OPL emulator;
- draw the original pointer and accept direct touch on iOS;
- reproduce the first portion of the floppy intro through the DUNE title; and
- run native/null, SDL screenshot, audio-capture, and iOS device checks.

These are real format and renderer milestones, not a claim of complete game
compatibility. Game logic, dialogue, command scripts, characters, saves,
location flow, and many intro scenes remain incomplete.

## Source map

| Module | Purpose |
| --- | --- |
| `detection.cpp`, `metaengine.cpp` | ScummVM detection and launcher integration |
| `resource.cpp` | CD archive/floppy resource loading and HSQ decoding |
| `sprite.cpp` | Palette and sprite-sheet decoding/blitting |
| `room.cpp`, `sky.cpp`, `palace.cpp` | Room composition, sky, and palace exits |
| `panel.cpp`, `cursor.cpp` | Control panel, font, pointer, and touch zones |
| `hnm.cpp` | CD HNM video and VOC soundtrack playback |
| `music.cpp` | HERAD AdLib/OPL2 playback |
| `intro.cpp`, `scene.cpp` | Intro sequencing and the current game-screen shell |
| `debug.cpp` | Device log, OSD messages, screenshot dumps, and developer options |

Detailed format findings are documented in
[`engines/dune/FINDINGS.md`](../third_party/scummvm/engines/dune/FINDINGS.md),
and the module status/build workflow is in
[`engines/dune/README.md`](../third_party/scummvm/engines/dune/README.md).

## Verification evidence

The repository contains the following repeatable checks:

```sh
./scripts/test_dune_scummvm_native.sh
./scripts/test_dune_scummvm_runtime.sh
./scripts/test_dune_scummvm_sdl.sh dump
./scripts/build_dune_scummvm_ios.sh
```

The SDL dump produces intro and room screenshots. The audio smoke test
confirms that a real Dune CD resource is decoded and submitted to the mixer.
The iOS build is ad-hoc signed for development and sideloading; the
device evidence is retained under
`notes/temp/dune_scummvm_engine_20260916/`.

## Proposed upstream path

Before an upstream pull request, the engine should be reviewed against the
usual ScummVM requirements for a new engine:

1. Confirm that every contributed source file has a compatible licence and
   that all external reverse-engineering references are credited.
2. Remove repository-specific build patches and keep any required ScummVM
   changes small, reviewable, and separately justified.
3. Replace measured/placeholder scene scripts with decoded executable tables
   or clearly documented compatibility fallbacks.
4. Add detector entries and game-data documentation for each supported DOS
   release, including exact hashes and required files.
5. Add regression tests or deterministic dump checks for resource, sprite,
   HNM, music, room, and panel decoding.
6. Complete enough game flow to start, save, reload, and finish the supported
   releases, then test on more than one backend.
7. Submit the engine for maintainer review before advertising it as supported.

The project is deliberately keeping the renderer release-neutral. `Resource`
is the storage seam, while sprite, room, HNM, and music decoders operate on
bytes. This should allow Amiga and Sega Mega CD support to share game logic
without spreading release checks throughout the renderer.

## Planned release work

### Amiga

The Amiga disk images show resource names and structures close to the DOS
floppy release. Work is needed on ADF file-system access, 32-colour palette
handling, Amiga music, and release-specific data/detection. The Amiga
disassembly may also help recover save-game and game-state structures.

### Sega Mega CD

The available disc has an ISO 9660 data track and a large `DUNE.DAT`, but its
layout is not the PC archive format. The boot program/system area must be
examined to find the index before a Mega CD `Resource` implementation can be
written. CD-quality audio, video, and redrawn resources need separate
validation.

### High-quality music

The official *Dune Spice Opera 2024 remaster* by Stéphane Picq with Philippe
Ulrich is available from
[Stéphane Picq's Bandcamp](https://stphanepicq.bandcamp.com/album/dune-spice-opera-2024-remaster-lp).
The implementation goal is an optional user-supplied integration, not
bundling commercial recordings. Licensing, file identification, timing, and
the distinction between original AdLib/MT-32 playback and the remaster must
be settled before upstream discussion.

## Help wanted

The most useful contributions right now are:

- ScummVM maintainers willing to review the engine structure and licensing
  before more code accumulates;
- reverse engineers who can decode the executable's location, command,
  dialogue, save, and intro tables;
- Amiga and Mega CD owners who can provide non-copyrighted format observations,
  hashes, boot-sector analysis, or test reports;
- audio specialists who can document HERAD, AGD, MT-32, HNM/VOC, and optional
  remastered-music integration; and
- testers with different ScummVM backends and original hardware for visual and
  audio comparison.

Please open one issue per question or failure, include the release, platform,
ScummVM revision, and data hashes, and do not upload proprietary game data.
Code contributions should include a focused test or reproducible evidence.

## Important status note

This document is a proposal and engineering handoff, not an upstream request
for immediate inclusion. The engine is advanced but incomplete. The ScummVM
team's feedback on architecture, licensing, detector conventions, data-file
documentation, and minimum compatibility expectations would determine the
next step.
