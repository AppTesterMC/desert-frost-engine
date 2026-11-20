# Original floppy irrigation regression

This fixture compares growth at a working wind trap with the English DOS floppy executable. The input save was made at sietch 12 after 45 irrigation days. DOS and the engine each loaded that same save and advanced through player commands in the open desert. The in-game day and period identify comparison points because the original can ignore clicks during a sky transition.

To run the focused day-51 gate, supply the engine executable, the game data, the retained day-48 save and a new output directory.

```sh
python3 scripts/check_original_floppy_irrigation.py --replay-day51 \
  "$ENGINE" "$FLOPPY_DATA" "$IRRIGATION_DAY_48_SAVE" "$OUT"
```

The gate verifies the seed hash before launching. It compares the final game clock, last processed period, sietch 12's 28-byte location record, and SHA-256 of the 50,681-cell map against the original. To regenerate the DOS side, stage the same `DUNE21S1.SAV` in a scratch save directory and use the Spice86 Dune autoplay host with its standard new-game prelude.

```sh
AUTOPLAY_SAVES_IN="$START_SAVES" "$AUTOPLAY" \
  tests/regression/floppy-irrigation/original-week.script "$DOS_OUT" --memdump-full
python3 scripts/check_original_floppy_irrigation.py --progression \
  "$DOS_OUT" "$OUT" dawn-4
```

On day 51, the original disc widened from radius 6 to 7, moved north by two rows and held 19 units of water. The previous engine differed at 62 map cells. A corrected replay matched every cell, the water, and the saved clocks. Original snapshots at days 49, 52 and 53 also matched on these measures. Reference fingerprints include both the independently advanced DOS checkpoints and nine separate original renderings of prearranged irrigation saves. The prepared captures validate saved maps and projection. The player-input replays validate subsequent growth. The longest prepared sample reached radius 11 on day 123. The maximum campaign coverage remains unmeasured. [Reference fingerprints](reference.json)
