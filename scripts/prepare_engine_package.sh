#!/usr/bin/env bash
# Builds the upload package for the public engine repository
# (github.com/AppTesterMC/desert-frost-engine): the ScummVM engine, the scripts
# and tests needed to build and check it, and the documentation. Nothing else
# from this working tree goes in. It never touches git or the network.
#
#   ./scripts/prepare_engine_package.sh [OUTPUT_DIR]
#
# OUTPUT_DIR (default release/github/desert-frost-<timestamp>) receives:
#   repo/          the tree to copy over a clone of the repository
#   repo.tar.gz    the same tree as an archive
#   MANIFEST.txt   every file with its SHA-256
#   UPLOAD.md      how to publish it
# The script refuses to finish if the tree contains game data, private paths
# or oversized files.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUT="${1:-$ROOT/release/github/desert-frost-$STAMP}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac
if [ -e "$OUT" ]; then
	printf 'Destination already exists; choose a new path: %s\n' "$OUT" >&2
	exit 2
fi
REPO="$OUT/repo"
ENGINE=third_party/scummvm/engines/dune
mkdir -p "$REPO"

put() { # put SOURCE [DEST]: copy one file, keeping its mode
	local src="$1" dest="${2:-$1}"
	mkdir -p "$REPO/$(dirname -- "$dest")"
	cp -p "$ROOT/$src" "$REPO/$dest"
}

tree() { # tree DIR [rsync filters...]
	local dir="$1"; shift
	mkdir -p "$REPO/$dir"
	rsync -a --exclude '.DS_Store' --exclude '__pycache__/' --exclude '*.pyc' "$@" \
		"$ROOT/$dir/" "$REPO/$dir/"
}

# The engine, exactly as it is built. ENGINE_SRC may name a frozen copy (for
# example a snapshot taken at a verified point while work continues in the tree).
if [ -n "${ENGINE_SRC:-}" ]; then
	mkdir -p "$REPO/$ENGINE"
	rsync -a --exclude '.DS_Store' "$ENGINE_SRC/" "$REPO/$ENGINE/"
else
	tree "$ENGINE"
fi

# Front page, build guide and ignore rules written for the engine repository.
put docs/desert-frost/README.md README.md
put docs/desert-frost/BUILDING.md BUILDING.md
put docs/desert-frost/gitignore .gitignore
for f in LICENSE NOTICE CONTRIBUTING.md Makefile; do put "$f"; done

# Build, check and packaging scripts for the ScummVM engine (the DOSBox and
# Spice86 tracks, and the maintainer's device-report helpers, stay private).
for f in \
	scummvm_source.sh \
	test_dune_scummvm_sdl.sh test_dune_scummvm_native.sh test_dune_scummvm_runtime.sh \
	check_dune_build.sh check_speedrun.sh build_dune_scummvm_ios.sh install_dune_scummvm_ios_devicectl.sh \
	ScummVM-iOS.xcscheme PREFLIGHT.md README-local-builds.md \
	dune_regress.py dune_accept_golden.py dune_audio_level.py \
	dune_sprite_sheet.py dune_hnm_frames.py dune_room.py dune_dat.py dune_unlzexe.py chani_grep.py dune_disasm.py asm_range.py dune_dialogue_dump.py \
	prepare_engine_package.sh; do
	put "scripts/$f"
done
tree scripts/patches

# Regression scenarios; the golden images are game frames and stay out.
tree tests/regression

# Documentation and README media; of the research notes only the write-ups the
# engine and its checks cite (raw extracts, disassembly dumps and videos stay private).
put notes/research/gameplay-rules.md
put notes/speedrun/route.md
put notes/speedrun/battle-worm-spec.md
tree doc --include dune-intro.gif --include "screen-*.png" --exclude "*"
tree docs --exclude 'desert-frost/'

# ---- Checks: fail rather than publish something that must not be public.
fail=0
data=$(cd "$REPO" && find . -type f \( -iname '*.dat' -o -iname '*.hsq' -o -iname '*.hnm' \
	-o -iname '*.exe' -o -iname '*.sav' -o -iname '*.iso' -o -iname '*.bin' -o -iname '*.adf' \
	-o -iname '*.ipa' -o -iname '*.mov' -o -iname '*.mp3' -o -iname '*.voc' -o -iname '*.log' \) -print)
if [ -n "$data" ]; then
	printf 'Game data or build output in the package:\n%s\n' "$data" >&2
	fail=1
fi
big=$(cd "$REPO" && find . -type f -size +2M -print)
if [ -n "$big" ]; then
	printf 'Files over 2 MB:\n%s\n' "$big" >&2
	fail=1
fi
# Extra patterns (e.g. personal names) come from an untracked local file, so
# the check itself publishes nothing personal: one extended regex per line.
extra=$(grep -v '^#' "$ROOT/.package-private-patterns" 2>/dev/null | paste -sd'|' - || true)
private=$(grep -rIilE "/Volumes/|/Users/[a-z]|192\\.168\\.[0-9]+\\.[0-9]+|/private/tmp/claude${extra:+|$extra}" "$REPO" \
	--exclude='prepare_engine_package.sh' || true)
if [ -n "$private" ]; then
	printf 'Private paths or addresses in:\n%s\n' "$private" >&2
	fail=1
fi
unlicensed=$(cd "$REPO/$ENGINE" && grep -L 'SPDX-License-Identifier: GPL-3.0-or-later' -- *.cpp *.h || true)
if [ -n "$unlicensed" ]; then
	printf 'Engine files without the GPL-3.0-or-later header:\n%s\n' "$unlicensed" >&2
	fail=1
fi
[ -f "$REPO/$ENGINE/COPYING" ] || { echo "Missing $ENGINE/COPYING" >&2; fail=1; }
if [ "$fail" -ne 0 ]; then
	printf 'Package NOT ready; fix the above. Partial output left in %s\n' "$OUT" >&2
	exit 1
fi

# ---- Manifest, archive and upload notes.
{
	printf 'Desert Frost engine upload package\n'
	printf 'Generated: %s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
	printf 'Excludes game data, golden images, logs, IPAs and build output.\n\n'
	(cd "$REPO" && find . -type f -print | LC_ALL=C sort | sed 's|^\./||' | while read -r f; do
		printf '%s  %s\n' "$(shasum -a 256 "$f" | cut -d' ' -f1)" "$f"
	done)
} > "$OUT/MANIFEST.txt"
COPYFILE_DISABLE=1 tar -czf "$OUT/repo.tar.gz" -C "$REPO" .

cat > "$OUT/UPLOAD.md" <<'EOF'
# Publishing this package

`repo/` is the new content of github.com/AppTesterMC/desert-frost-engine.
Copy it over a clone (this adds and updates files and deletes nothing; files that
exist only on GitHub, such as `doc/intro-paul.bmp`, stay):

```sh
git clone git@github.com:AppTesterMC/desert-frost-engine.git
cd desert-frost-engine
git switch -c engine-update
rsync -a /path/to/this/package/repo/ ./
git status                      # review: no game data, no logs, no golden/
git add -A
git commit -m "Update Dune engine, add build instructions and scripts"
git push -u origin engine-update  # then open a pull request, or merge to main
```

After pushing, check that the README renders, that BUILDING.md's links resolve,
and that the repository's licence shown by GitHub still reads Apache-2.0 (the
root LICENSE). The engine directory has its own GPL-3.0-or-later COPYING.
EOF

printf 'Package ready: %s\n' "$OUT"
printf '  %s files, %s\n' "$(cd "$REPO" && find . -type f | wc -l | tr -d ' ')" \
	"$(du -sh "$OUT/repo.tar.gz" | cut -f1) compressed"
