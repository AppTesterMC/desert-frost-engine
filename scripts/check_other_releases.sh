#!/bin/sh
# The other releases on the desktop build: the Amiga (DUNE_DATA_AMIGA, the
# files scripts/dune_amiga_extract.py writes from the three ADFs) and the
# Sega CD (DUNE_DATA_SEGACD, the folder with the bin/cue rip). Each scenario
# of tests/regression/other-releases.json is compared with its golden; a
# release whose data is missing is skipped. The floppy and CD scenarios stay
# in the default manifest (the IPA's gate).
set -eu
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export DUNE_DATA_AMIGA="${DUNE_DATA_AMIGA:-$HOME/dune-amiga-data/game}"
export DUNE_DATA_SEGACD="${DUNE_DATA_SEGACD:-$HOME/dune-segacd-data}"
export SDL_AUDIODRIVER=dummy
exec python3 "$script_dir/dune_regress.py" verify --manifest tests/regression/other-releases.json
