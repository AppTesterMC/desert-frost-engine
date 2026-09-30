# Original arguments and ScummVM game options

The DOS release's useful runtime choices are in **Game Options > Game**.
Select the game in the launcher, open Game Options and scroll down in Game.
Language choices are offered only when the installed data contains a command
bank and both phrase banks. The selected language applies when starting the
game, including its command rows, dialogue, book and intro text resources.

| Original argument | ScummVM equivalent / implementation |
| --- | --- |
| default, FRA, GER | English, French and German text; available on floppy and CD when installed |
| ENG, ITA, SPA | CD English variant, Italian and Spanish text |
| DUT | CD French text with the alternate alien font; **not Dutch** in this executable/data set |
| ADL | AdLib music checkbox, rendered by ScummVM's OPL emulator; Volume controls its level; Audio selects the OPL emulator |
| SDB / SBP sampled sound | Sampled sounds checkbox; covers the implemented effects and CD video soundtracks; Volume controls their levels |
| JOY | ScummVM controller support and the per-game Keymaps tab; left stick moves the pointer, A selects, B cancels, direction pad uses Dune's navigation keys |
| WRI | ScummVM's per-game **Paths > Save path**; saves use the existing ScummVM save-file manager |
| 386 | No option needed: ScummVM runs native engine code, not the DOS 8086/386 rendering drivers |
| EMS, XMS, HIM, EM4 | No option needed: ScummVM manages host memory, not DOS memory managers |
| SAF, VSF | DOS rendering-driver compatibility paths; no equivalent alternate driver in this engine |
| ADP, ADG / AGD, MID | Original Sound Blaster Pro, AdLib Gold and MT-32 music drivers are not implemented; they are not offered as working choices |
| NOM | Original mouse-driver disable is not exposed until the keyboard can reach every pointer-only action; disabling touch currently would make the game unusable on a phone |
| REC, DEM | Original floppy event recorder/player not yet ported; the developer input-script harness is a different format |
| MON | Original monochrome mode remains deferred; the user currently prioritizes functionality over colour changes |
| ALN | Not present in either game executable's argument table examined here |

Original defaults disabled audio until a driver was selected. The port preserves
its existing enabled-audio defaults. The new checkboxes persist per game:
`dune_no_music=false` and `dune_no_sound=false` enable their streams;
`true` disables them. An explicitly false music key now works (previously the
presence of any `dune_no_music` value disabled music). Global mute, volumes and
the Audio tab's **No music** device still apply. The OPL stream now follows
Music volume too (it previously bypassed that volume as a plain mixer stream).
Muting sampled sound keeps CD video timing intact because its audio stream is still processed by the mixer.

The `dune_language` values correspond to physical text banks: 1 default English,
2 French, 3 German, 4 the CD's ENG variant, 5 Italian, 6 Spanish, 7 alien-font
French. A manually selected missing bank fails with an actionable startup error;
it does not mix another language into the session. These options apply to the
DOS releases. Amiga options and their limits are described below; Sega CD
retains its existing handling.

## Binary evidence

The offsets below refer to the unpacked executable code segment unless marked
as raw file offsets. The CD trace was checked against OpenRakis
`DNCDPRG_RECENT.ASM` and the original executable. The SPD13 recompilation
`StaticDefinitions.cs` independently identifies the parser at `0xE4AD`;
`dune-re`'s `main.rs` also identifies that entry point and replaces it with
native argument handling. Resource-bank comparisons use the original game data.

The floppy argument table starts at unpacked code offset `0xDF00`: 16 seven-byte
records for `386 MON FRA GER NOM REC DEM JOY ADL SDB MID AGD XMS EMS EM4 HIM`.
Its parser follows at `0xDF70`. Thus this floppy executable does not accept the
extra language, animation or write-path tokens found in the CD executable.

The CD table at code `0xE40C` contains 23 records and the parser starts at
`0xE4AD`. The supplied raw CD file offset `0xE60C` includes the executable header.
The language selection at `0xE610` extracts bits 2–4, and `0xCFE4` / `0xD00F`
select the corresponding command/phrase bank. At `0xCFE4`, language index 6
loads `DNCHAR2.BIN`. `COMMAND7`, `PHRASE71` and `PHRASE72` contain French text.
The original spelling is `AGD` on floppy and `ADG` on CD. CD `0xE76A` separately
selects PCM and music drivers. DOS I/O-port suffixes are hardware addresses,
not device addresses on iOS, so the port does not expose them.

## Verification and device checks

Run `scripts/check_original_options.sh`. It starts the actual desktop engine
with each installed language, opens Leto's conversation, compares its text with
the original phrase bank, checks that non-English command panels differ, exercises map navigation and
save/load through the localized menus in a private save path, and
captures actual mixer output with the music/sound choices enabled and disabled.
The full pre-IPA graphics/audio preflight remains required.

The real launcher Edit Game dialog was also checked in a disposable desktop
build at logical GUI sizes 640 by 480 and 320 by 200: all three floppy and seven
CD language choices selected, audio checkboxes toggled through their mouse
handlers, and changes saved and verified in a newly opened dialog. The engine
contains no GUI-test hooks.

On the iPhone, select a DOS game and open **Game Options > Game**. Check that
only installed languages appear; choose French or German, start the game and
check the command rows and Leto's dialogue. On CD also try Italian and Spanish.
Return to the launcher, restore English, and confirm that the setting persists.
Turn music off and on across starts, then sampled sound off and on while the CD
intro plays. Check that video timing remains normal. Confirm that both existing
Leto/Celimyn-Tuek fix checkboxes remain available. With a paired controller,
check pointer motion, selection, cancellation and direction-pad navigation;
inspect their per-game Keymaps bindings. Use an alternate writable Save path and
save/load an **engine** log there; do not enter the original Amiga save menu.

## Amiga applicability

The detected Amiga release contains one language: English in `COMMAND2.HSQ`,
`PHRASE21.HSQ` and `PHRASE22.HSQ`. DOS bank numbers do not apply: bank 2 is
French on DOS but English on Amiga. No alternate-language choice is offered
without supported alternate data. DOS targets use the Text language control;
the unused generic ScummVM language selector is disabled on newly detected
targets to avoid presenting a second, ineffective language setting. The
unsupported MIDI/MT-32 tabs are also suppressed for DOS; its Audio tab still
provides the implemented AdLib emulator and No music choices.

Amiga **Game Options > Game** exposes **Fix the Leto loop** and **Fix
Celimyn-Tuek**, both off by default. These options already work in the shared
world implementation; they are now reachable through the launcher. Existing
Amiga targets with a stored platform are recognized even before re-detection.
Controller bindings remain in **Keymaps**, and the engine's working save/load
uses **Paths > Save path**. This does not use the original Amiga game's broken
disk-save menu.

The original Amiga music banks are `M1.HSQ`, `M2.HSQ` and `M3.HSQ`, with the
song titles WORMSIGN, ECOLOVE and FREMENS and an executable Paula replayer.
The current engine's music player implements DOS HERAD through OPL only;
`amigaFileName` rejects its DOS song names. Its sampled-sound player decodes
VOC, which these Amiga disks do not contain. Amiga music and sound remain
unimplemented, so this menu exposes neither an AdLib switch nor a pretend
Paula switch. Detection disables unused volume sliders and MIDI tabs.
ScummVM's generic Audio device chooser remains a host setting and cannot
enable the missing Amiga player. The `.SAM` files are room command streams,
not audio samples.

The original Amiga executable provides independent evidence for the two
optional fixes. The phase dispatcher at hunk 0 `0x20C6–0x20F0` indexes the table
at `0x205A`; phase `0x4C` selects the pointer at `0x20A2` to `0x1FA6`. That
callback increments a counter, moves Jessica and queues vision `0x105`,
without clearing Leto, matching CD `0x1166`. Celimyn-Tuek is location 68 at
Amiga state `0x870`, hunk 0 `0x1B170`: its original bytes include names
`0C 05`, hidden status `80` and discovery phase `FF` at record offset `0x0B`.
The English text banks and static palace references independently identify
this as the Amiga release rather than a DOS language variant.

Run `scripts/check_amiga_options.sh` with `DUNE_DATA_AMIGA` set to installed
Amiga data. It checks both story options off and on, their actual room and
conversation behavior, the discovery phase boundary, save/reload, keyboard
navigation and an isolated writable save path. On iPhone, open an Amiga
target's Game Options, toggle each fix, save and reopen the dialog; confirm
that no DOS language or AdLib control appears. Check Keymaps with a paired
controller and save/load an engine log under an alternate Save path.
