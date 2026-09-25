# Sourced by the build scripts: provides stage_scummvm_source DEST.
#
# Fills DEST with a ScummVM source tree, leaving an existing engines/dune/
# alone (the scripts rsync the engine in afterwards). Two sources, in order:
#
#   1. third_party/scummvm-source.tar, if present (the maintainer's offline copy);
#   2. a shallow git clone of $SCUMMVM_REPO at $SCUMMVM_REF, cached under
#      $local_root/scummvm-upstream so it is fetched only once.
#
# SCUMMVM_REF defaults to the upstream commit the engine is developed and
# tested against. Newer ScummVM usually works; if it does not, this pin is the
# known-good baseline.

SCUMMVM_REPO="${SCUMMVM_REPO:-https://github.com/scummvm/scummvm.git}"
SCUMMVM_REF="${SCUMMVM_REF:-430653b98d75e86d15a293c0aa3b3dbed56b499d}"

stage_scummvm_source() {
	dest=$1
	mkdir -p "$dest"
	if [ -f "$repo_root/third_party/scummvm-source.tar" ]; then
		tar -xf "$repo_root/third_party/scummvm-source.tar" -C "$dest"
		return
	fi
	upstream="$local_root/scummvm-upstream"
	if [ ! -f "$upstream/.dune-ref-$SCUMMVM_REF" ]; then
		rm -rf "$upstream"
		mkdir -p "$upstream"
		git -C "$upstream" init -q
		git -C "$upstream" remote add origin "$SCUMMVM_REPO"
		git -C "$upstream" fetch -q --depth 1 origin "$SCUMMVM_REF"
		git -C "$upstream" checkout -q FETCH_HEAD
		touch "$upstream/.dune-ref-$SCUMMVM_REF"
	fi
	rsync -a --exclude '/.git/' --exclude '/.dune-ref-*' --exclude '/engines/dune/' \
		"$upstream/" "$dest/"
}
