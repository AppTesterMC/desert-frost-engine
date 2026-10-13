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

# Stamp each staged Dune build with an ID that is visible in its screen
# watermark. The counter gives each local build a readable number.
stamp_dune_build_id() {
	stamp_dir=$1
	counter_file="$stamp_dir/.dune-build-counter"
	counter=0
	if [ -r "$counter_file" ]; then
		IFS= read -r counter < "$counter_file" || true
		case "$counter" in ''|*[!0-9]*) counter=0 ;; esac
	fi
	counter=$((counter + 1))
	printf '%s\n' "$counter" > "$counter_file"
	build_number=$(printf '%06d' "$counter")
	stamp_id="DUNE build $(date '+%Y-%m-%d %H:%M:%S') #$build_number"
	cat > "$stamp_dir/build_id.h" <<EOF
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef ENGINES_DUNE_BUILD_ID_H
#define ENGINES_DUNE_BUILD_ID_H

#define DUNE_BUILD_ID "$stamp_id"

#endif
EOF
	printf '%s\n' "$stamp_id"
}

stage_scummvm_source() {
	dest=$1
	mkdir -p "$dest"
	# A complete checkout is included in private Windows handoff bundles so
	# their first WSL build does not need to clone ScummVM over the network.
	if [ -x "$repo_root/third_party/scummvm/configure" ] &&
			[ -f "$repo_root/third_party/scummvm/engines/access/detection.cpp" ]; then
		rsync -a --exclude '/.git/' --exclude '/engines/dune/' \
			"$repo_root/third_party/scummvm/" "$dest/"
		return
	fi
	if [ -f "$repo_root/third_party/scummvm-source.tar" ]; then
		# Older offline archives include a prototype engine. Preserve the
		# authoritative tree, just as the git fallback below does. Archives
		# may spell member paths with or without a leading ./.
		tar --exclude 'engines/dune' --exclude './engines/dune' \
			-xf "$repo_root/third_party/scummvm-source.tar" -C "$dest"
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
