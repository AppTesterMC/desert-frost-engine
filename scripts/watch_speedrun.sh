#!/bin/sh

# Watch the speedrun bot play a Dune release in a window on this Mac.
# It uses the same bot as scripts/check_speedrun.sh (speedrun.cpp), slowed
# down so each dialogue page, room and battle can be read. Flights and
# animations run in real time unless --skip-flight-after is set. Milestones
# show as status messages, and the full log is written to the saves folder
# (dune-ios.log). Close the window to stop; endless exits after its tour.
#
# Usage: scripts/watch_speedrun.sh [campaign|full|ecology|endless] [options]
#   campaign  from Leto's death to the ending (the default; the story before is a logged shortcut)
#   full      from a new game to the ending (engine bot, seed 1 by default)
#   ecology   assisted whole-planet greening and the release-specific ecology outcome
#   endless   bypass the ending, recruit Liet Kynes, tour the garden and
#             every sietch room, then finish in the palace garden
#   --release floppy|cd|amiga  select the matching local game data
#   --music   play the game's music (off by default)
#   --pace    pause length in percent (100 = about 1.4 s per dialogue page)
#   --room-wait SECONDS  extra idle time in every visited room
#   --skip-flight-after SECONDS  automatically use SKIP TO DESTINATION after this long
#   --action-pause SECONDS  hold the screen before and after each scripted action/click
#   --action-pause-before SECONDS  hold the screen before each scripted action/click
#   --action-pause-after SECONDS   hold the screen after each scripted action/click
#   defaults: 4 seconds before, 5 seconds after, and a 6-second flight skip
#   --intro   play the intro first (skipped by default)
#   --auto-stop  exit the game when the route reaches its endpoint
#   --cd      shorthand for --release cd
#   --data    the game folder (default: the floppy data in data/floppy)
#   --seed    the random seed (the checks pass with --seed 1)
#   --stream  no window: the game sends its own frames and sound through
#             ffmpeg to URL, e.g. YouTube's rtmp://a.rtmp.youtube.com/live2/KEY
#             (or a local .mp4/.flv recording); only the game is shown. Local
#             captures close after the route reaches its endpoint.
#   --backup-stream  send the same encoded feed to a second RTMP ingest URL
#   -h        this help
#
# From the first worm ride (day 5-6) the bot travels by worm: CALL A WORM's
# map, the call (floppy: the SHAI worm; CD: VER.HNM) and the ride on the
# worm's back. To watch it: scripts/watch_speedrun.sh campaign [--cd]

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$script_dir/stream_limit.sh"
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
part=campaign
release=floppy
music=0
pace=100
intro=0
seed=1
data="$repo_root/data/floppy"
stream=""
backup_stream=""
stream_local=0
stream_format=flv
auto_stop=0
stream_session=0
room_wait=0
skip_flight_after=6
pause_before=4
pause_after=5
validate_seconds() {
	seconds_option=$1
	seconds_value=$2
	case "$seconds_value" in
		''|*[!0-9]*) echo "$seconds_option needs a whole number of seconds" >&2; exit 2 ;;
	esac
	seconds_digits=$(printf '%s' "$seconds_value" | sed 's/^0*//')
	[ -n "$seconds_digits" ] || seconds_digits=0
	if [ "${#seconds_digits}" -gt 7 ] || { [ "${#seconds_digits}" -eq 7 ] && [ "$seconds_digits" -gt 2147483 ]; }; then
		echo "$seconds_option is too large (maximum 2147483 seconds)" >&2
		exit 2
	fi
}
while [ $# -gt 0 ]; do
	case "$1" in
		campaign|full) part=$1 ;;
		green|ecology) part=green ;;
		endless-tour|endless) part=endless-tour ;;
		--release)
			case "${2:-}" in
				floppy) release=floppy; data="$repo_root/data/floppy" ;;
				cd) release=cd; data="$repo_root/data" ;;
				amiga) release=amiga; data="${DUNE_DATA_AMIGA:-$HOME/dune-amiga-data/game}" ;;
				*) echo "--release needs floppy, cd, or amiga" >&2; exit 2 ;;
			esac
			shift
			;;
		--music) music=1 ;;
		--pace) pace=$2; shift ;;
		--room-wait) validate_seconds "$1" "$2"; room_wait=$2; shift ;;
		--skip-flight-after) validate_seconds "$1" "$2"; skip_flight_after=$2; shift ;;
		--action-pause)
			validate_seconds "$1" "$2"
			pause_before=$2
			pause_after=$2
			shift
			;;
		--action-pause-before) validate_seconds "$1" "$2"; pause_before=$2; shift ;;
		--action-pause-after) validate_seconds "$1" "$2"; pause_after=$2; shift ;;
		--intro) intro=1 ;;
		--seed) seed=$2; shift ;;
		--data) data=$2; shift ;;
		--stream) stream=$2; shift ;;
		--backup-stream) backup_stream=$2; shift ;;
		--auto-stop) auto_stop=1 ;;
		--cd) release=cd; data="$repo_root/data" ;;
		-h|--help) sed -n '3,/^$/p' "$0" | sed -n '/^#/s/^# \{0,1\}//p'; echo; echo "Worm rides: scripts/watch_speedrun.sh campaign [--cd]"; exit 0 ;;
		*) echo "unknown option $1" >&2; exit 2 ;;
	esac
	shift
done

case "$stream" in
	""|*://*) ;;
	*) [ ! -e "$stream" ] || { echo "recording already exists: $stream; choose a new --stream path" >&2; exit 2; } ;;
esac
case "$backup_stream" in
	""|*://*) ;;
	*) echo "--backup-stream needs a complete RTMP URL" >&2; exit 2 ;;
esac
if [ -n "$backup_stream" ]; then
	case "$stream" in *://*) ;; *) echo "--backup-stream requires an RTMP --stream" >&2; exit 2 ;; esac
fi
case "${DUNE_LOCAL_BUILD_ROOT:-}" in "${TMPDIR%/}"/*|/tmp/*) unset DUNE_LOCAL_BUILD_ROOT ;; esac
case "${DUNE_RUN_ROOT:-}" in "${TMPDIR%/}"/*|/tmp/*) unset DUNE_RUN_ROOT ;; esac
local_root="${DUNE_LOCAL_BUILD_ROOT:-$HOME/Library/Caches/Cryogenic-local/dune-scummvm-native-build}"
run_root="${DUNE_RUN_ROOT:-$local_root/sdl-run}"
build_log="$local_root/build-sdl-dune/dune-build.log"
mkdir -p "$(dirname -- "$build_log")"
# Build (or bring up to date) the desktop ScummVM with the engine. Only the
# build: the headless screen tour of "dump" delayed the window by minutes.
if ! SDL_AUDIODRIVER=dummy DUNE_DATA="$data" DUNE_SKIP_BUILD="${DUNE_SKIP_BUILD:-0}" bash "$script_dir/test_dune_scummvm_sdl.sh" build >"$build_log" 2>&1; then
	echo "build failed; details from $build_log:" >&2
	tail -60 "$build_log" >&2 || true
	exit 1
fi
build_id_file="$local_root/scummvm/engines/dune/build_id.h"
build_id=$(sed -n 's/^#define DUNE_BUILD_ID "\([^"]*\)"/\1/p' "$build_id_file" | head -1)
case "$build_id" in
	''|*unstamped*)
		echo "The staged engine has no stamped build ID; rebuilding it before launch."
		if ! SDL_AUDIODRIVER=dummy DUNE_DATA="$data" DUNE_SKIP_BUILD=0 \
				bash "$script_dir/test_dune_scummvm_sdl.sh" build >"$build_log" 2>&1; then
			echo "build failed while stamping the engine; details from $build_log:" >&2
			tail -60 "$build_log" >&2 || true
			exit 1
		fi
		build_id=$(sed -n 's/^#define DUNE_BUILD_ID "\([^"]*\)"/\1/p' "$build_id_file" | head -1)
		;;
esac
if [ -z "$build_id" ] || [ "$build_id" = build_unstamped ]; then
	echo "Could not get a stamped engine build ID from $build_id_file" >&2
	exit 1
fi

if [ -n "$stream" ]; then
	# Keep per-run logs and saves separate. A sequence supervisor may provide
	# shared pipes so one encoder and RTMP connection can span several runs.
	run_root="$run_root/streams/run-$(date +%Y%m%d-%H%M%S)-$$"
fi
config="$run_root/watch.ini"
mkdir -p "$run_root/saves"
{
	printf '[scummvm]\nsavepath=%s\n' "$run_root/saves"
	printf 'dune_speedrun=%s\ndune_speedrun_watch=%s\ndune_rng_seed=%s\n' "$part" "$pace" "$seed"
	printf 'dune_speedrun_room_wait=%s\ndune_speedrun_skip_flight_after=%s\n' "$room_wait" "$skip_flight_after"
	printf 'dune_speedrun_pause_before=%s\ndune_speedrun_pause_after=%s\n' "$pause_before" "$pause_after"
	[ "$music" = 1 ] || printf 'dune_no_music=1\n'
	[ "$intro" = 1 ] || printf 'dune_floppy_start=99\ndune_intro_start=99\ndune_skip_cd_story=1\n'
} > "$config"

if [ -n "$stream" ]; then
	# The game writes its screen to a pipe and its sound to a growing file,
	# in real time (streamTick in debug.cpp); tail passes the file on to
	# ffmpeg, which encodes both as YouTube's ingest wants (H.264, a keyframe
	# every 2 s, AAC). The video pipe is the only place the game waits, and
	# ffmpeg does not probe the inputs (their formats are given), so ffmpeg
	# never waits for sound while the game waits for it to take a frame (two
	# pipes from the game deadlocked). The sound file grows by 0.3 GB an hour
	# and is removed on the next run.
	pipes="$run_root/stream"
	if [ -n "${DUNE_STREAM_SESSION_DIR:-}" ]; then
		case "$stream" in *://*) ;; *) echo "shared stream sessions require an RTMP URL" >&2; exit 2 ;; esac
		pipes=$DUNE_STREAM_SESSION_DIR
		[ -p "$pipes/video" ] && [ -p "$pipes/audio" ] && [ -f "$pipes/audio.raw" ] || {
			echo "shared stream pipes are not ready: $pipes" >&2
			exit 2
		}
		stream_session=1
	else
		rm -rf "$pipes"
		mkdir -p "$pipes"
		mkfifo "$pipes/video" "$pipes/audio"
	fi
	printf 'dune_stream=%s\ndisable_sdl_audio=true\n' "$pipes" >> "$config"
	case "$stream" in
		*://*) ;;
		*)
			stream_local=1
			case "$stream" in *.mp4) stream_format=mp4 ;; esac
			mkdir -p "$(dirname -- "$stream")"
			;;
	esac
	if [ "$stream_local" = 1 ] || [ "$auto_stop" = 1 ]; then
		printf 'dune_exit_after_ending=8\ndune_exit_after_speedrun=true\n' >> "$config"
	fi
	game_pid=""
	tail_pid=""
	ffmpeg_pid=""
	awake_pid=""
	write_milestones() {
		[ "$stream_local" = 1 ] || return 0
		case "$stream" in
			*.mp4) milestones="${stream%.mp4}.milestones.txt" ;;
			*.flv) milestones="${stream%.flv}.milestones.txt" ;;
			*) milestones="$stream.milestones.txt" ;;
		esac
		{
			printf 'Runtime: ScummVM engine bot (assisted game logic)\nRelease: %s\nRoute: %s\nSeed: %s\nProcess exit: %s\n' "$release" "$part" "$seed" "${status:-1}"
			printf 'Completion check: %s\n' "${capture_status:-not checked}"
			printf 'Game log: %s\n\n' "$run_root/saves/dune-ios.log"
			grep -E '^(Intro:|Speedrun:|Ecology:|Story setup:|Story:.*ending|Story:.*the end)' "$run_root/saves/dune-ios.log" 2>/dev/null || true
		} > "$milestones"
		echo "Milestones: $milestones"
	}
	stop_capture() {
		status=$1
		trap - EXIT INT TERM HUP
		stop_stream_limit
		[ -z "$game_pid" ] || kill -TERM "$game_pid" 2>/dev/null || true
		[ -z "$tail_pid" ] || kill "$tail_pid" 2>/dev/null || true
		if [ -n "$game_pid" ]; then
			# SDL may defer TERM while the game is blocked on a pipe. Release
			# the writer before waiting for FFmpeg to consume the final bytes.
			kill -KILL "$game_pid" 2>/dev/null || true
			wait "$game_pid" 2>/dev/null || true
		else
			# Before game startup the encoder shell may be waiting to open
			# the FIFO, with no MP4 output to finalize yet.
			[ -z "$ffmpeg_pid" ] || kill -TERM "$ffmpeg_pid" 2>/dev/null || true
		fi
		# The redirections open video before creating ffmpeg.log. A slow
		# cold start from an external drive may still leave the encoder shell
		# waiting to open that FIFO even though the game process exists.
		if [ ! -e "$run_root/ffmpeg.log" ]; then
			[ -z "$ffmpeg_pid" ] || kill -TERM "$ffmpeg_pid" 2>/dev/null || true
		fi
		# SIGINT makes FFmpeg write the MP4 trailer before it exits. Closing
		# its video reader also releases a game blocked on the video FIFO.
		[ -z "$ffmpeg_pid" ] || kill -INT "$ffmpeg_pid" 2>/dev/null || true
		[ -z "$ffmpeg_pid" ] || wait "$ffmpeg_pid" 2>/dev/null || true
		[ -z "$tail_pid" ] || wait "$tail_pid" 2>/dev/null || true
		[ -z "$awake_pid" ] || kill "$awake_pid" 2>/dev/null || true
		[ -z "$awake_pid" ] || wait "$awake_pid" 2>/dev/null || true
		write_milestones
		exit "$status"
	}
	trap 'stop_capture $?' EXIT
	trap 'stop_capture 130' INT
	trap 'stop_capture 143' TERM HUP
	if [ "$stream_session" = 0 ]; then
		tail -c +1 -F "$pipes/audio.raw" 2>/dev/null > "$pipes/audio" &
		tail_pid=$!
		set --
		if [ "$stream_format" = mp4 ]; then set -- -movflags +faststart; fi
		if [ -n "$backup_stream" ]; then
			fifo_options='queue_size=120:attempt_recovery=1:recover_any_error=1:recovery_wait_time=1:max_recovery_attempts=0:drop_pkts_on_overflow=1:restart_with_keyframe=1'
			tee_targets="[f=flv:onfail=ignore]$stream|[f=flv:onfail=ignore]$backup_stream"
			ffmpeg -hide_banner -loglevel warning -nostdin \
				-thread_queue_size 128 -f rawvideo -pix_fmt rgb24 -video_size 320x200 -framerate 30 -nofind_stream_info -i pipe:3 \
				-thread_queue_size 512 -f s16le -ar 22050 -ac 2 -nofind_stream_info -i "$pipes/audio" \
				-vf "scale=1152:720:flags=neighbor,pad=1280:720:64:0:black,format=yuv420p" \
				-map 0:v:0 -map 1:a:0 \
				-c:v libx264 -preset veryfast -b:v 4000k -minrate 4000k -maxrate 4000k -bufsize 8000k -g 60 -keyint_min 60 -sc_threshold 0 -x264-params nal-hrd=cbr:force-cfr=1 \
				-c:a aac -b:a 128k -ar 44100 -f tee -use_fifo 1 -fifo_options "$fifo_options" "$tee_targets" \
				3<"$pipes/video" >"$run_root/ffmpeg.log" 2>&1 &
		else
			ffmpeg -hide_banner -loglevel warning -nostdin \
			-thread_queue_size 128 -f rawvideo -pix_fmt rgb24 -video_size 320x200 -framerate 30 -nofind_stream_info -i pipe:3 \
			-thread_queue_size 512 -f s16le -ar 22050 -ac 2 -nofind_stream_info -i "$pipes/audio" \
				-vf "scale=1152:720:flags=neighbor,pad=1280:720:64:0:black,format=yuv420p" \
				-c:v libx264 -preset veryfast -b:v 4000k -minrate 4000k -maxrate 4000k -bufsize 8000k -g 60 -keyint_min 60 -sc_threshold 0 -x264-params nal-hrd=cbr:force-cfr=1 \
				-c:a aac -b:a 128k -ar 44100 \
			"$@" -f "$stream_format" "$stream" 3<"$pipes/video" >"$run_root/ffmpeg.log" 2>&1 &
		fi
		ffmpeg_pid=$!
		case "$stream" in *://*) start_stream_limit ;; esac
	fi
	echo "Action pauses: before ${pause_before}s, after ${pause_after}s; flight skip: ${skip_flight_after}s"
	echo "Build: ${build_id:-unknown}"
	if [ "$stream_local" = 1 ]; then
		if [ "$part" = endless-tour ]; then
			echo "Recording the $part route on $release to $stream; it closes in the palace garden after the sietch tour."
		elif [ "$part" = green ] && [ "$release" = cd ]; then
			echo "Recording the assisted CD ecology route to $stream; the palace is finished by the final military attack."
		else
			echo "Recording the $part route on $release to $stream; it closes after the story ending."
		fi
	elif [ "$stream_session" = 1 ]; then
		echo "Streaming the $part route on $release; the shared RTMP session remains open for the next route."
	elif [ "$auto_stop" = 1 ]; then
		echo "Streaming the $part route on $release (seed $seed); it will stop at the route endpoint."
	else
		echo "Streaming the $part route on $release (seed $seed); Ctrl-C to stop."
	fi
	[ -z "$backup_stream" ] || echo "The encoded feed uses matching primary and backup output settings."
	echo "Log: $run_root/saves/dune-ios.log  (grep Speedrun: for the milestones)"
	cd "$local_root/build-sdl-dune"
	status=0
	# caffeinate: the Mac must not sleep under a live stream.
	SDL_VIDEODRIVER=dummy ./scummvm -c "$config" --gfx-mode=surface --no-fullscreen \
		--path="$data" dune >"$run_root/stream-scummvm.log" 2>&1 &
	game_pid=$!
	if command -v caffeinate >/dev/null 2>&1; then
		caffeinate -i -w "$game_pid" >/dev/null 2>&1 &
		awake_pid=$!
	fi
	wait "$game_pid" || status=$?
	game_pid=""
	if [ -n "$awake_pid" ]; then
		kill "$awake_pid" 2>/dev/null || true
		wait "$awake_pid" 2>/dev/null || true
		awake_pid=""
	fi
	# tail follows the sound file without end (the game can go seconds
	# without writing, e.g. the campaign's story shortcut). Once the game has
	# closed the video pipe and tail has passed on the last sound, stopping
	# tail ends ffmpeg's sound input and ffmpeg ends the stream properly. A
	# game that failed before opening the pipe leaves ffmpeg waiting on it;
	# that is stopped.
	if [ "$stream_session" = 0 ]; then
		sleep 1
		kill "$tail_pid" 2>/dev/null || true
		wait "$tail_pid" 2>/dev/null || true
		tail_pid=""
	fi
	ffmpeg_status=0
	if [ "$stream_session" = 0 ]; then
		if [ "$status" = 0 ]; then
			wait "$ffmpeg_pid" || ffmpeg_status=$?
		else
			kill -INT "$ffmpeg_pid" 2>/dev/null || true
			wait "$ffmpeg_pid" 2>/dev/null || ffmpeg_status=$?
		fi
		ffmpeg_pid=""
	fi
	[ "$status" = 0 ] || echo "the game stopped ($status); see $run_root/stream-scummvm.log" >&2
	[ "$ffmpeg_status" = 0 ] || echo "ffmpeg did not finalize cleanly ($ffmpeg_status); see $run_root/ffmpeg.log" >&2
	if [ "$stream_local" = 1 ] && [ "$status" = 0 ] && [ "$ffmpeg_status" = 0 ]; then
		echo "Recording finalized: $stream"
	fi
	capture_status=$status
	if { [ "$stream_local" = 1 ] || [ "$auto_stop" = 1 ]; } && [ "$status" = 0 ] && [ "$ffmpeg_status" = 0 ]; then
		game_log="$run_root/saves/dune-ios.log"
		case "$part" in
			full|campaign)
				python3 "$script_dir/dune_speedrun_evidence.py" "$game_log" --part "$part" --release "$release" --seed "$seed" || capture_status=1
				;;
			green)
				if ! grep -qF 'Ecology: all mapped desert terrain is sprouting green' "$game_log" ||
						! grep -qF "Story: Paul enters the Baron's hall, the end" "$game_log"; then
					capture_status=1
				fi
				;;
			endless-tour)
				if ! grep -qF 'Tour: final palace garden (with Liet Kynes)' "$game_log" ||
						! grep -qF 'Tour: every sietch room visited; exiting the game in the palace garden' "$game_log" ||
						grep -q 'Speedrun:.*BLOCKED' "$game_log"; then
					capture_status=1
				fi
				;;
		esac
			[ "$capture_status" = 0 ] || echo "The stream closed, but the route did not meet its completion check; see $game_log" >&2
	fi
	write_milestones
	trap - EXIT INT TERM HUP
	[ "$capture_status" = 0 ] || exit "$capture_status"
	exit "$ffmpeg_status"
fi

echo "Build: ${build_id:-unknown}"
echo "Action pauses: before ${pause_before}s, after ${pause_after}s; flight skip: ${skip_flight_after}s"
echo "Watching the $part speedrun (seed $seed); close the window to stop."
echo "Log: $run_root/saves/dune-ios.log  (grep Speedrun: for the milestones)"
cd "$local_root/build-sdl-dune"
if [ "$music" = 1 ]; then
	exec ./scummvm -c "$config" --no-fullscreen --path="$data" dune
else
	SDL_AUDIODRIVER=dummy exec ./scummvm -c "$config" --no-fullscreen --path="$data" dune
fi
