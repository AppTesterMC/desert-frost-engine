---
name: dosbox-x-control
description: Run and compare the original DOS Dune in DOSBox-X for golden-reference verification. Use for headless DOSBox-X runs, floppy/CD reference data, and original-versus-engine comparison.
---

# DOSBox-X Control

DOSBox-X comes from the Homebrew formula (not the disabled cask).

- Binary: `/opt/homebrew/bin/dosbox-x`

## Golden reference

- Directory: `~/Dune-DOS-reference/`
- Game copies: `C/DUNE` (floppy), `C/DUNECD` (CD), `Dune-CD_DOS_EN.iso`
- Configs: `dune-reference.conf`, `dune-floppy.command`, `dune-cd.command`
- Output: `capture/`

## Headless run

```sh
dosbox-x -silent -conf "$HOME/Dune-DOS-reference/dune-reference.conf" \
  -time-limit 30 -c "..."
```

Never start a windowed or audible run without the user asking.

## Scripted comparison

- `~/Dune-DOS-reference/autoplay/autoplay.sh SCRIPT OUTDIR [--video] [--memdump]`
  runs original floppy `DUNEPRG.EXE` on Spice86 11.1.0.
- `~/Dune-DOS-reference/autoplay/compare.sh SCRIPT NAME`
  runs original plus engine and writes `captures/compare/NAME`.

## ScummVM headless SDL pitfall

Homebrew replaced `sdl2` with `sdl2-compat` (SDL3), causing headless ScummVM runs to exit immediately. Relink the binary to `/opt/homebrew/Cellar/sdl2/*/lib` with `install_name_tool`, then `codesign -s -`. `DYLD_LIBRARY_PATH` does not help under SIP.

## Rules

- Use headless mode (`-silent`) for verification.
- Do not overwrite or modify the golden reference data.
- `.SAV` files are shared between the original and engine; use them for comparison.
