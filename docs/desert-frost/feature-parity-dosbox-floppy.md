# Feature parity: Cryo Dune (DOSBox-X floppy) vs Desert Frost engine

Scope: desktop ScummVM engine, DOS floppy release as the reference (`data/floppy`). Source build `Dune build 2026-10-26 20:30:54 #000196`.

## Verified in this run
- Build: `scripts/test_dune_scummvm_sdl.sh build` succeeded (SDL desktop).
- Regression harness: `cd.script` and `floppy.script` completed, all manifest checkpoints captured (cd 16, floppy 42) with exit status 0.
- Campaign: `check_speedrun.sh campaign 1` -> PASS (floppy, assisted completion; 0 direct order fallbacks; 2 declared FORCED setup markers).
- Ecology win: `check_ecology_win.sh` -> PASS (floppy + CD): vegetation takes the last fort, final-attack stage 1, Thufir's line, no Emperor demand; floppy palace taken and Baron's hall ends the game; CD palace spared and Paul shot.
- Live telemetry: `engine_demo`/`live_state` captures accompany the campaign runs under `sdl-run/`.

## Feature status (floppy-original first)
| Feature | Status | Notes |
|---|---|---|
| Resources (floppy HSQ, scripts) | Done | `resource.cpp`, verified in FINDINGS.md |
| Sprite sheets (4/8-bit, RLE, scale/flip) | Done | `sprite.cpp` |
| Rooms (.SAL polygons/lines) | Done | line dithering partial (`room.cpp`) |
| Sky/time of day | Done | `sky.cpp`, follows game clock |
| Control panel | Done | command list partly decoded (throne room) |
| Mouse pointer / touch | Done | `cursor.cpp` |
| Places/exits/characters from DUNEPRG.EXE | Done | `world.cpp` |
| Floppy intro, credits scroll, prologue | Done | `intro.cpp`, `intro_scenes.cpp` (33 scenes) |
| Landing/throne room | Done | `scene.cpp` |
| Dialogue engine | Done-ish | original CONDIT/DIALOGUE; selection still limited |
| Book | Done-ish | pagination ours |
| Map/globe | Done | `map.cpp` |
| Saves | Done | floppy `DUNE21S` format |
| Clock/flight/spice/rallying/results | Done | gameplay rules verified |
| Spice shipment to Emperor | Implemented/needs visual verify | `story.cpp` `shipSpice()`, COMM room entry; star-field animation "not shown" (see FINDINGS.md) |
| End credits (final cast list) | Partial | `GameScreen::drawEnding()` shows cast names; lacks animated/scrolling presentation over the original artwork |
| End game sequence | Done (floppy) | ecology win takes Baron's hall and ends game; verified |
| Vegetation sprites/growth | Done-ish | tufts on sprouting cells; discs grow radius 4->12; needs visual comparison to original growth rate |
| Animated sprites in front of sietches | Partial/unknown | room exterior backdrops use frozen HNM last frame (CD) / SIET-VILG-FORT sheets (floppy); no idle animation sprites decoded yet |
| Battles in front of fortresses | Partial | fort battles resolve logically; decorative front sprites not confirmed |
| Amiga port | Partial | intro + desert landscape not ported; independent SFX not |
| Sega CD port | Partial | verbs/videos/sound/flight/battles missing |

## Remaining gaps toward 100% faithful reproduction
1. Confirm and, if needed, implement animated exterior/facade sprites (sietches and fortresses) per floppy references.
2. Match final-credits presentation (scroll, timing, background) to DOSBox-X.
3. Validate shipment star-field/departure animation and Emperor demand timing against capture.
4. Validate vegetation growth coverage and tuft animation over a long irrigation run against captures.
5. Complete command lists per room and smugglers/raids/equipment verbs (see README "Not yet").
6. Combine results with Live Testing agent and Live Dashboard telemetry screenshots before release.

## Handoff
Substantial engine-assisted verification passes above; requesting GitHub Repo Manager to prepare/push a release candidate with this build's uncommitted changes and linked telemetry/evidence.
