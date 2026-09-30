# Save compatibility

The DOS floppy and CD engines retain the original save formats and names.
Engine Amiga saves use a separate name and format tag, so saving an Amiga
slot cannot replace a CD slot in a shared ScummVM Save path.

| Release | Log 1 filename | Current unpacked body | Header marker word |
| --- | --- | --- | --- |
| DOS floppy | `DUNE21S1.SAV` | 22051 bytes | `0x02F7` |
| DOS CD | `DUNE37S1.SAV` | 22138 bytes | `0x00F7` (original files also use `0x02F7`) |
| Engine Amiga | `DUNEAMS1.SAV` | 22122 bytes | `0xA1F7` |

Logs 2–4 replace the final digit with 2–4. The final two slots are the
last-place and last-new-sietch autosaves. Per-game **Game Options > Paths >
Save path** can additionally separate installations of the same release.
The filenames separate releases; they do not separate multiple targets
using the same release and save directory.

Before offering a slot for loading or changing game state, the engine checks
the file length, marker, complete RLE stream, exact allowed body length and
all immutable dialogue-list offsets against the installed release. Expansion
is bounded by that release's save size. A foreign or damaged slot is disabled
and logged as incompatible or damaged. No state is copied on a failed check.

## Older saves and imports

- Original DOS floppy/CD saves remain readable and writable. The historical
  engine floppy layout with 36 buffer bytes before the raw dialogue table,
  and historical CD saves with no 104-byte dialogue gap, remain readable.
  These alternatives require their exact sizes and matching dialogue header.
- If an Amiga `DUNEAMS` slot is absent, the engine can read a matching older
  engine Amiga save under its former `DUNE37S` filename. Loading never moves,
  rewrites or deletes that file. The next save writes `DUNEAMS` and leaves
  the legacy file intact. A present but invalid `DUNEAMS` slot is rejected;
  it does not silently fall back to the older slot.
- Native Amiga `DUNE10S` saves are a different format and are not imported.
  The supplied native save uses a big-endian six-byte header and expands to
  22392 bytes. Renaming it is not a conversion. Emulator snapshots remain
  the reference workflow for returning to an original Amiga game state.
- Earlier engines could overwrite CD saves with Amiga saves, or accept a CD
  save while interpreting its state at the wrong offset. This fix cannot
  recover an overwritten file or repair state already saved after a wrong
  import. Ambiguous/nonmatching saves are rejected; there is no bulk migration.
- Explicit engine Amiga fixtures must contain the Amiga dialogue table and
  engine layout. Exporters should use `DUNEAMS` and marker word `0xA1F7`;
  native DOS saves are not interchangeable Amiga fixtures.

## Evidence and checks

The DOS layout follows the CD executable's `sub_1B427`/`sub_1B473` and the
original saves, cross-checked with the public `dune-rust` savegame decoder.
The packed map flags occupy `0x317F` bytes, followed by `0xA2` bytes of
variables, the release's dialogue table, its buffer gap, and the state.
Dialogue tables are 4464 bytes (floppy), 4496 (CD), and 4480 (Amiga); the
immutable 272-byte header identifies their list layout. The DOS pointer
bases are `0xCFE9` (floppy) and `0xAA76` (CD). Engine Amiga retains the
CD-derived pointer encoding for its own table and adds its explicit tag.
All 36 checked DOS reference fixtures match these complete dialogue headers.

`scripts/check_save_compatibility.sh` runs actual engine save/load menus in
private temporary directories. It verifies three-release coexistence and
round trips, nondestructive legacy import, rejection in both CD/Amiga
directions (including files made to have the expected total size), native
DOS imports when their private reference fixtures are installed, historical
engine layouts, native Amiga rejection when its supplied save is present, malformed
files, and current-slot precedence. Set `DUNE_DATA_AMIGA` if the installed
Amiga data is outside `data/amiga`. It never drives the original game's
broken save/load menus and never touches a user's save directory.

On iPhone, choose one Save path for CD/floppy/Amiga, save Log 1 in each,
restart each target and load its own Log 1. Check a second Save path in
Game Options > Paths. Then copy a matching legacy engine Amiga `DUNE37S1`
into a private test directory, load it, save again, and confirm both the
unchanged legacy file and new `DUNEAMS1` exist. Desktop checks do not replace
these device checks.
