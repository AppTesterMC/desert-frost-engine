# Preflight: check every build locally before making an IPA

Installing an IPA costs the user a manual TrollStore transfer and a screen
recording. Never ask for that until the build has passed this check on the Mac.

## One command

```sh
cd path/to/your/checkout
./scripts/check_dune_build.sh
```

Takes a few minutes. It builds the desktop (SDL) engine, screenshots every
screen the engine can reach for both the CD and floppy releases, and captures
the audio the mixer really produces. Everything lands in
`notes/temp/dune_scummvm_engine_20260916/results/preflight/`.

## What you must do with the results

**1. Look at the pictures.** The script converts every screen to PNG. Open each
one (`Read` the file) and judge it yourself — the script only counts files, it
cannot see them. Expect roughly 15 CD PNGs and 62 floppy PNGs:

- `cd-*.png` — available intro video frames (the missing CRYO2 entry is logged)
  and 11 palace rooms
- `floppy-*.png` — every intro scene from the logo to the night flight, the
  credits, the nine prologue cards (`floppy-prologue-*`) and 11 palace rooms;
  `floppy-room-10.png` is the landing screen (throne room with Duke Leto);
  `floppy-talk-1..4.png` are Duke Leto's opening lines, `floppy-book-cover.png`
  and `floppy-book-topic-0-page-1.png` the book; `*-map-flat.png` and
  `*-map-globe.png` the map and the globe; `*-place-12-room-1..2.png` a sietch
  (entrance under the sky, then the cave), `*-place-9-room-1.png` a village,
  `*-place-2-room-1..3.png` a fortress and its bunker, `*-place-1-room-1.png`
  the Harkonnen palace; `*-sietch-chief.png` (the hired troop's chief), `*-troop*.png`
  (orders, occupation list, new occupation), `*-map-density.png`, `*-results.png` and
  `*-evening-balcony.png` (the sky at sunset); `*-menu-save.png`, `*-menu-saved.png`,
  `*-menu-load.png`, `*-menu-options.png` and `*-menu-quit.png` the game menu
  (the save writes `DUNE21S2.SAV` / `DUNE37S2.SAV` into the run's save folder
  and loads it back). On the CD the exteriors are the last picture of the
  arrival videos and need `DNCDPRG.EXE` next to `DUNE.DAT`; the floppy needs
  `DUNEPRG.EXE`

Compare anything suspicious against a recording of the original game (the
maintainer uses a screen capture of the floppy release, logo to first map
screen, which is not distributed):
`ffmpeg -ss <seconds> -i <mov> -frames:v 1 out.png`; the scene → seconds
timeline is in the header of `third_party/scummvm/engines/dune/intro.h`.

**2. Read the audio verdicts.** Three lines are printed:

```
cd-music audio: OK (22/23 s with sound, peak 8330)
cd-speech audio: OK (33/34 s with sound, peak 4154)
floppy-music audio: OK (23/24 s with sound, peak 1708)
```

`SILENT` means the engine produces no sound; fix it before building anything.
The matching `.wav` files are playable if a verdict looks wrong.

**3. Check the engine log.** `preflight/cd-dune.log` and `floppy-dune.log` are
what the game writes at runtime. Every room should say `drawn`, never `FAILED`,
and the `Audio:` line should say the mixer is ready.

## Then, and only then

```sh
./scripts/build_dune_scummvm_ios.sh     # ad-hoc signed IPA for TrollStore
```

Check the log ends in `** BUILD SUCCEEDED **`, note the printed SHA-256, and
give the user the IPA path and that checksum. Serve it for installation with:

```sh
cp notes/temp/dune_scummvm_engine_20260916/packages/ScummVM-ios-dune-prototype.ipa /tmp/serve/dune.ipa
cd /tmp/serve && python3 -m http.server 8642 --bind 0.0.0.0
```

and tell the user the `http://<LAN-ip>:8642/` link (the page offers a TrollStore
`apple-magnifier://install?url=...` link). Keep the previous IPA alongside the
new one so a regression can be compared.

## Notes

- Device-only things this cannot check: touch behaviour, the mouse pointer,
  real loudness, and performance. Say so plainly rather than implying the build
  was verified on the phone.
- Game data is copied to `/private/tmp/dune-data` (CD) and
  `/private/tmp/dune-data/floppy` on first run; the repository volume is slow.
- Developer config keys used by the check (`dune_dump`, `dune_intro_start`,
  `dune_speech_probe`, `dune_no_music`) are documented in
  `third_party/scummvm/engines/dune/debug.h`.
- Background a build that takes minutes; do not let it block the conversation.
