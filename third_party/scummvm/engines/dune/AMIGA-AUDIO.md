# Amiga music

The engine plays the Amiga release's own `M1.HSQ`, `M2.HSQ` and `M3.HSQ`
through ScummVM's Paula mixer. It does not use the DOS HERAD soundtrack or
replacement recordings. The user supplies the original game files; this
source package contains no module, sample or recording.

The normal game starts with M2, **ECOLOVE**. Entering a sietch
from its exterior selects M3, **FREMENS**. Villages and fortresses retain the
current song. Entering the Atreides palace
selects M2 again. Moving around a location, viewing the map and flying keep
the current song running. An unchanged selection does not restart its song.
The engine's in-game MUSIC OFF / MUSIC ON controls stop and restart the
current native track. ScummVM's Music volume, global mute and per-game music
toggle also apply.

The Amiga introduction is still unported. M1, **WORMSIGN**, is supported and
tested by the backend, but the normal startup skips that introduction and
therefore starts at the original post-intro M2 cue.

## Resource format and replay evidence

The files use Cryo's extended HSQ header: its third byte supplies upper size
bits, since each unpacked module exceeds 64 KiB. The existing resource loader
already handles this. Their decompressed form is four-channel StarTrekker
`FLT4`, with ordinary signed 8-bit PCM instruments.

| Resource | Unpacked bytes | Song orders | Patterns | Restart order | PCM bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| M1 | 148686 | 26 | 25 | 0 | 122002 |
| M2 | 153424 | 17 | 12 | 4 | 140052 |
| M3 | 153194 | 38 | 29 | 3 | 122414 |

Header, pattern and sample sizes account for every byte. The music is the
only independently evidenced Amiga audio bank so far. The `.SAM` files are
room command data, not PCM. No separate DOS-style VOC/SAMPLE bank is present
in the complete 103-file disk directory; the original night-attack routine
has no equivalent of the DOS sampled battle-sound call. Independent sound
effects and speech are not advertised as implemented.

Addresses below are offsets in hunk 0 of the original Amiga executable,
not DOS offsets or relocated runtime addresses:

- `0x00d6–0x00d8`: M1 before the prologue; `0x0122–0x0124`: M2 afterward.
- `0x57b2–0x5826`: room navigation selects M3 for type below `0x20`, M2
  for type `0x20`, only when leaving exterior room 1 through a positive
  interior exit. Larger place types retain the current song.
- `0x127d0–0x1283a`: ignore an unchanged module selection; otherwise stop,
  load and initialize its replay.
- `0x1283c–0x128be`: sample offsets, clear four initial sample bytes,
  disregard finetune, set speed 6 and clear the song/row/tick counters.
- `0x11a1e–0x11a44`, `0x11c78–0x11c94`: continuous CIA B timer A,
  reload `0x0dfc`, replay once per four interrupts (about 49.52 Hz on PAL).
- `0x12936–0x12a80`: decode four events per row and honor the module's
  restart-order byte. M2 and M3 must not loop back to order zero.
- `0x12b1c–0x12c78`: sample selection, initial and looping DMA spans.
- `0x12d9c–0x12df4`, `0x12f12–0x12f60`: arpeggio and bounded pitch slides.
- `0x13042–0x1309a`: pattern break starts row zero regardless of the Dxx
  argument; Cxx volume is capped at 64; Fxx is speed 1–31, not BPM.

The dedicated sequencer implements the effects present in these three
modules, plus their original position-jump command. Unsupported effects
and malformed headers, orders, sample indices or sample ranges fail cleanly
before starting a stream. This intentionally is not a general-purpose MOD
player. It uses ScummVM's A500 Paula filtering and the original stereo
routing, channels 0/3 left and 1/2 right. The routing is described in the
[Commodore hardware manual](https://amigadev.elowar.com/read/ADCD_2.1/Hardware_Manual_guide/node00D9.html).

## Verification

`scripts/check_amiga_music.sh` captures the real engine's mixer output using
SDL's disk audio driver. It checks all three original modules, native Music
volume and mute controls, malformed-file rejection, original nonzero restart
behavior using a small synthetic composition, and real cockpit/room/menu
input. Its temporary fixtures are outside the source tree. No emulator
state, original game save menu or system speakers are used.

The menu also updates the current per-game music setting, so MUSIC ON can
re-enable a song disabled in the launcher. Global mute and zero Music volume
remain authoritative. This shared behavior is checked for DOS as well.

The tests prove that the native sequencer reaches the mixer and produces
non-silent PCM. They do not claim a bit-identical hardware recording or
physical iPhone speaker verification. On a device, start the Amiga game,
check the palace music, enter a sietch, return to the palace, and check music
off/on, volume, app suspension and return to the launcher. DOS floppy/CD
music must still work through their separate HERAD backend.
