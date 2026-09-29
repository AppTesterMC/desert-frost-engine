#!/bin/sh

# Desktop (SDL) build of ScummVM with the Dune engine, for checking rendering
# on the Mac instead of on a device. With "dump" as the first argument the
# engine writes title/menu/scene1 BMPs into the evidence folder and exits by itself.
# With "harness SCRIPT OUTPUT" it runs the headless scripted-input regression
# target; the engine writes named PNG checkpoints into OUTPUT.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
repo_source_root="$repo_root/third_party/scummvm"
local_root="${DUNE_LOCAL_BUILD_ROOT:-/private/tmp/dune-scummvm-native-build}"
. "$script_dir/scummvm_source.sh"
source_root="$local_root/scummvm"
build_root="$local_root/build-sdl-dune"
run_root="$local_root/sdl-run"
evidence_root="$repo_root/notes/temp/dune_scummvm_engine_20260916"

mkdir -p "$source_root"
if [ ! -f "$source_root/.dune-local-source-ready" ]; then
	stage_scummvm_source "$source_root"
	touch "$source_root/.dune-local-source-ready"
fi
rsync -a "$repo_source_root/engines/dune/" "$source_root/engines/dune/"
mkdir -p "$build_root" "$run_root/saves"
cd "$build_root"

if [ ! -f Makefile ]; then
	"$source_root/configure" \
		--backend=sdl \
		--disable-all-engines \
		--enable-engine=dune \
		--enable-debug
fi

make -j"$(sysctl -n hw.ncpu)"

# Homebrew can replace sdl2 with sdl2-compat (SDL 3 underneath; it happened on
# 2026-09-26). With it the dummy video driver cannot make a renderer and every
# headless run quits before the engine starts, with no error. When the real
# SDL2 is still in the Cellar, the binary is pointed at it. A DYLD_LIBRARY_PATH
# would not work because perl and env are SIP-protected and drop it.
sdl2_real=$(ls -d /opt/homebrew/Cellar/sdl2/*/lib/libSDL2-2.0.0.dylib 2>/dev/null | tail -1)
sdl2_linked=$(otool -L scummvm | awk '/libSDL2-2.0.0.dylib/ {print $1}')
if [ -n "$sdl2_real" ] && [ -n "$sdl2_linked" ] && [ "$sdl2_linked" != "$sdl2_real" ]; then
	install_name_tool -change "$sdl2_linked" "$sdl2_real" scummvm 2>/dev/null
	codesign --force -s - scummvm >/dev/null 2>&1
fi

config="$run_root/scummvm.ini"
if [ "${1:-}" = "dump" ]; then
	dump_root="$evidence_root/results/sdl-dump"
	mkdir -p "$dump_root" "$evidence_root/logs"
	printf '[scummvm]\nsavepath=%s\ndune_dump=%s\n' "$run_root/saves" "$dump_root" > "$config"
	# The explicit mode keeps SDL's headless backend from attempting its default
	# OpenGL transaction before the dump surface exists. Older ScummVM SDL
	# builds may warn and fall back to their default mode; that fallback is fine.
	SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}" \
	perl -e 'alarm 180; exec @ARGV' -- ./scummvm -c "$config" --gfx-mode=surface --no-fullscreen \
		--path="${DUNE_DATA:-$repo_root}" dune >"$evidence_root/logs/sdl-dump.log" 2>&1 || true
	ls -l "$dump_root"
elif [ "${1:-}" = "speedrun" ]; then
	# The speedrun check (notes/speedrun/route.md): a bot plays the route
	# through the game's own actions and logs each milestone ("Speedrun:").
	part=${2:-campaign}
	seed=${3:-1}
	mkdir -p "$evidence_root/logs"
	printf '[scummvm]\nsavepath=%s\ndune_speedrun=%s\ndune_rng_seed=%s\ndune_no_music=1\n' \
		"$run_root/saves" "$part" "$seed" > "$config"
	log="$evidence_root/logs/speedrun-$part-$(basename -- "${DUNE_DATA:-cd}").log"
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
		perl -e 'alarm 900; exec @ARGV' -- ./scummvm -c "$config" --gfx-mode=surface --no-fullscreen \
		--path="${DUNE_DATA:-$repo_root}" dune >"$log" 2>&1 || true
	grep -E "Speedrun:|Battle:|BLOCKED" "$log" | tail -60
	echo "log: $log"
elif [ "${1:-}" = "harness" ]; then
	input_script=${2:?harness requires an input script}
	checkpoint_dir=${3:?harness requires a checkpoint directory}
	extra_config=${4:-}
	# ScummVM resolves config paths from the build directory after this script's
	# cd. Make direct invocations behave like dune_regress.py's absolute paths.
	case "$input_script" in
		/*) input_script=$(CDPATH= cd -- "$(dirname -- "$input_script")" && pwd)/$(basename -- "$input_script") ;;
		*) input_script=$(CDPATH= cd -- "$repo_root/$(dirname -- "$input_script")" && pwd)/$(basename -- "$input_script") ;;
	esac
	mkdir -p "$checkpoint_dir"
	# The random generators are pinned (the landing's room rotation draws from
	# them); an extra config's own dune_rng_seed comes later and wins.
	printf '[scummvm]\nsavepath=%s\ndune_input=%s\ndune_checkpoint_dir=%s\ndune_rng_seed=1\n' \
		"$run_root/saves" "$input_script" "$checkpoint_dir" > "$config"
	if [ -n "$extra_config" ]; then
		cat "$extra_config" >> "$config"
	fi
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE="$checkpoint_dir/audio.raw" \
		perl -e 'alarm 300; exec @ARGV' -- ./scummvm -c "$config" --gfx-mode=surface \
		--no-fullscreen --path="${DUNE_DATA:-$repo_root}" dune
else
	printf '[scummvm]\nsavepath=%s\n' "$run_root/saves" > "$config"
	exec ./scummvm -c "$config" --no-fullscreen --path="$repo_root" dune
fi
