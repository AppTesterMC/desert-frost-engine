#!/usr/bin/env python3
"""Exercise native Amiga tracker PCM, controls and original room cue routing.

Uses the user's M1/M2/M3 files, never an emulator or system audio. Fixtures
are temporary. PCM output proves the mixer path, not physical speaker output.
"""
import array
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(sys.argv[1]).resolve()
DATA = Path(os.environ.get('DUNE_DATA_AMIGA', ROOT / 'data/amiga'))
BUILD = Path(os.environ.get('DUNE_LOCAL_BUILD_ROOT', '/tmp/dune-scummvm-native-build'))
BINARY = BUILD / 'build-sdl-dune/scummvm'
RUN = Path(os.environ.get('DUNE_RUN_ROOT', BUILD / 'sdl-run'))
RUN.mkdir(parents=True, exist_ok=True)
OUT = Path(tempfile.mkdtemp(prefix='amiga-music-', dir=RUN))
ENV = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='disk')


def play(name, options=None, replacement=None, script='wait 2500\nquit\n', save=None):
    case = OUT / name
    case.mkdir()
    for folder in ('saves', 'frames'):
        (case / folder).mkdir()
    if save is not None:
        (case / 'saves/DUNEAMS1.SAV').write_bytes(save)
    data = DATA
    if replacement is not None:
        data = case / 'data'
        data.mkdir()
        for original in DATA.iterdir():
            if original.is_file() and original.name.lower() != 'm2.hsq':
                (data / original.name).symlink_to(original.resolve())
        (data / 'm2.hsq').write_bytes(replacement)
    ini = {'savepath': case / 'saves', 'dune_input': case / 'input.script',
           'dune_checkpoint_dir': case / 'frames', 'music_volume': 192,
           'dune_no_music': 'false', **(options or {})}
    (case / 'input.script').write_text(script)
    (case / 'game.ini').write_text('[scummvm]\n' + ''.join(f'{k}={v}\n' for k, v in ini.items()))
    env = dict(ENV, SDL_DISKAUDIOFILE=str(case / 'audio.raw'))
    with (case / 'stdout.log').open('wb') as stdout:
        subprocess.run([str(BINARY), '-c', str(case / 'game.ini'), '--gfx-mode=surface',
                        '--no-fullscreen', '--path=' + str(data), 'dune'], env=env,
                       stdout=stdout, stderr=subprocess.STDOUT, timeout=60, check=True)
    log = (case / 'saves/dune-ios.log').read_text(errors='replace')
    assert 'Startup complete: throne room displayed' in log, (name, log[-500:])
    samples = array.array('h')
    samples.frombytes((case / 'audio.raw').read_bytes())
    if sys.byteorder != 'little':
        samples.byteswap()
    assert len(samples) > 44100, (name, 'short capture')
    return case, log, samples


def level(samples):
    return {'peak': max(map(abs, samples)), 'nonzero': sum(x != 0 for x in samples),
            'rms': math.sqrt(sum(x*x for x in samples) / len(samples)), 'samples': len(samples)}


def fixture():
    """Original-format test composition: amplitude distinguishes loop order.

    Three one-tick patterns; restart1 must repeat quiet/medium and never the
    initial loud pattern. This catches a generic MOD player's restart0 bug.
    """
    data = bytearray(1084 + 3*1024 + 256)
    data[:20] = b'SYNTHETIC DUNE TEST '.ljust(20)
    struct.pack_into('>H', data, 42, 128)
    data[45] = 64
    struct.pack_into('>HH', data, 46, 0, 128)
    data[950:955] = bytes([3, 1, 0, 1, 2])
    data[1080:1084] = b'FLT4'
    for pattern, volume in enumerate((64, 16, 32)):
        offset = 1084 + pattern*1024
        data[offset:offset+4] = bytes([1, 172, 0x1c, volume])  # sample1, period428, Cxx
        data[offset+4:offset+8] = bytes([0, 0, 15, 1])       # F01: one tick per row
    for i in range(256):
        data[-256+i] = round(math.sin(i*math.tau/32)*100) & 255
    return data


def rms_at(samples, start, duration=0.25):
    chunk = samples[int(start*44100)*2:int((start+duration)*44100)*2]
    assert chunk, 'capture did not reach the required loop'
    return math.sqrt(sum(x*x for x in chunk) / len(chunk))


def main():
    if not (DATA / 'dune').is_file():
        print('SKIP Amiga music: set DUNE_DATA_AMIGA to installed Amiga data')
        return
    results = {}
    def original(track):
        source = DATA / f'm{track}.hsq'
        case, log, samples = play(f'module-{track}', replacement=source.read_bytes())
        assert 'Music: M2.HSQ started' in log, track
        stats = level(samples)
        assert stats['peak'] > 100 and stats['nonzero'] > 10000, (track, stats)
        stats['input_sha256'] = hashlib.sha256(source.read_bytes()).hexdigest()
        return track, stats
    with ThreadPoolExecutor(max_workers=3) as pool:
        for track, stats in pool.map(original, (1, 2, 3)):
            results[f'module-{track}'] = stats
            print(f'PASS Amiga music: original M{track} stereo PCM, peak {stats["peak"]}', flush=True)
    assert len({results[f'module-{i}']['rms'] for i in (1, 2, 3)}) == 3

    for name, options in [('disabled', {'dune_no_music': 'true'}),
                          ('zero-volume', {'music_volume': 0}),
                          ('muted', {'mute': 'true'}),
                          ('null-driver', {'music_driver': 'null'})]:
        case, log, samples = play(name, options)
        stats = level(samples)
        assert stats['nonzero'] == 0, (name, stats)
        results[name] = stats
        print(f'PASS Amiga music: {name} produces silence', flush=True)
    case, log, samples = play('low-volume', {'music_volume': 48})
    stats = level(samples)
    assert 0 < stats['peak'] < results['module-2']['peak'] / 2, stats
    results['low-volume'] = stats
    print('PASS Amiga music: standard Music volume attenuates native PCM', flush=True)

    case, log, samples = play('restart-byte', replacement=fixture(), script='wait 9000\nquit\n')
    first = next(i//2 for i, x in enumerate(samples) if abs(x) > 100) / 44100
    # Read well inside each pattern to exclude mixer/filter startup transients.
    segments = [rms_at(samples, first + 0.35 + i*(64*4*3581/709379)) for i in range(6)]
    assert 3.5 < segments[0]/segments[1] < 4.5, segments
    assert 1.8 < segments[2]/segments[1] < 2.2, segments
    for i in (3, 5):
        assert 0.85 < segments[i]/segments[1] < 1.15, segments
    assert 0.85 < segments[4]/segments[2] < 1.15, segments
    results['restart-byte'] = {'rms_segments': segments}
    print('PASS Amiga music: nonzero restart skips initial pattern on successive loops', flush=True)

    assert max(map(abs, samples[::2])) > 100 and not any(samples[1::2]), 'channel0 stereo routing'
    results['stereo-routing'] = {'channel0_left': True}
    print('PASS Amiga music: native channel0 reaches only the left output', flush=True)

    effects = fixture()
    effects[1088:1092] = bytes([0, 0, 15, 6])     # Hold opening note six ticks.
    effects[1092:1096] = bytes([0, 0, 13, 3])     # D03 must enter pattern1 row0.
    effects[1084+1024+4:1084+1024+8] = bytes([0, 0, 15, 255])  # FFF clamps to31.
    effects[1084+1024+16:1084+1024+20] = bytes([0, 0, 12, 64]) # Next row is loud.
    case, log, samples = play('native-effects', replacement=effects)
    first = next(i//2 for i, x in enumerate(samples) if abs(x) > 100) / 44100
    opening = rms_at(samples, first+0.02, 0.04)
    quiet = rms_at(samples, first+0.25, 0.2)
    next_row = rms_at(samples, first+0.9, 0.2)
    assert 3.5 < opening/quiet < 4.5 and 3.5 < next_row/quiet < 4.5, (opening, quiet, next_row)
    results['native-effects'] = {'rms_opening': opening, 'rms_break_row0': quiet, 'rms_speed31_next_row': next_row}
    print('PASS Amiga music: D03 means row0; FFF clamps speed to31', flush=True)

    invalid = {}
    good = fixture()
    invalid['truncated'] = good[:-10]
    invalid['bad-signature'] = bytearray(good); invalid['bad-signature'][1080:1084] = b'BAD!'
    invalid['bad-restart'] = bytearray(good); invalid['bad-restart'][951] = 127
    invalid['bad-sample'] = bytearray(good); invalid['bad-sample'][1084] |= 0xf0
    invalid['bad-repeat'] = bytearray(good); struct.pack_into('>H', invalid['bad-repeat'], 46, 129)
    invalid['unsupported-effect'] = bytearray(good); invalid['unsupported-effect'][1086] = 0x13
    def reject(item):
        name, data = item
        case, log, samples = play(name, replacement=data, script='wait 700\nquit\n')
        assert 'Music: M2.HSQ not started' in log and not any(samples), name
        return name
    with ThreadPoolExecutor(max_workers=3) as pool:
        for name in pool.map(reject, invalid.items()):
            results[name] = {'rejected': True}
            print(f'PASS Amiga music: {name} safely rejected, game remains playable', flush=True)

    cockpit = ('wait 400\ncheckpoint cockpit\nclick left 181 102\nwait 1000\n'
               'checkpoint arrived\nclick left 275 164\nwait 800\ncheckpoint inside\n'
               'key DOWN\nwait 500\ncheckpoint outside\nclick left 160 171\nwait 500\n'
               'checkpoint return-cockpit\nclick left 139 76\nwait 1200\ncheckpoint palace-arrived\n'
               'click left 275 164\nwait 800\ncheckpoint palace-inside\nquit\n')
    case, log, samples = play('room-cues', {'dune_test_cockpit': 1}, script=cockpit)
    assert log.count('Music: M2.HSQ started') == 2, log
    assert 'Flight: place 0 -> 12' in log and 'Room 2 of place 12' in log, log
    assert log.count('Music: M3.HSQ started') == 1, log
    assert log.index('checkpoint arrived') < log.index('Music: M3.HSQ started'), log
    assert log.index('checkpoint palace-arrived') < log.rindex('Music: M2.HSQ started'), log
    results['room-cues'] = level(samples)
    print('PASS Amiga music: flights preserve music, sietch/palace entries select M3/M2 once', flush=True)

    menu = ('wait 300\nclick left 160 163\nwait 300\nclick left 45 170\nwait 300\n'
            'click left 160 195\nwait 300\nclick left 160 163\nwait 1200\n')
    case, log, samples = play('menu-off', script=menu+'quit\n')
    assert 'Options: music off' in log, log
    assert not any(samples[-44100:]), 'menu music off left audio playing'
    results['menu-off'] = {'tail_silent': True}
    print('PASS Amiga music: real in-game MUSIC OFF stops the native stream', flush=True)
    case, log, samples = play('menu-on', script=menu+'click left 160 195\nwait 300\nclick left 160 171\nwait 1500\nquit\n')
    assert 'Options: music on' in log and log.count('Music: M2.HSQ started') == 2, log
    assert max(map(abs, samples[-44100:])) > 100, 'menu music on did not resume PCM'
    results['menu-on'] = {'tail_audible': True}
    print('PASS Amiga music: real in-game MUSIC ON restarts current native track', flush=True)

    enable_menu = ('wait 300\nclick left 160 163\nwait 300\nclick left 45 170\nwait 300\n'
                   'click left 160 195\nwait 300\nclick left 160 171\nwait 1500\nquit\n')
    for name, extra, silent in [('launcher-disabled-on', {}, False),
                                 ('launcher-disabled-muted-on', {'mute': 'true'}, True),
                                 ('launcher-disabled-zero-volume-on', {'music_volume': 0}, True)]:
        case, log, samples = play(name, {'dune_no_music': 'true', **extra}, script=enable_menu)
        assert 'Options: music on' in log and 'Music: M2.HSQ started' in log, log
        assert (not any(samples)) if silent else max(map(abs, samples[-44100:])) > 100, name
        results[name] = {'silence': silent}
        print(f'PASS Amiga music: {name} honors current checkbox, global mute and volume', flush=True)

    # Declare one private location seed, then exercise real load and movement.
    # No original emulator or original game save menu is involved.
    seed_script = ('wait 300\nclick left 160 163\nwait 200\nclick left 45 170\nwait 200\n'
                   'click left 160 179\nwait 200\nclick left 160 163\nwait 300\nquit\n')
    case, log, samples = play('village-cue-seed', script=seed_script)
    from dune_save_patch import unpack, pack
    packed = (case / 'saves/DUNEAMS1.SAV').read_bytes()
    body = unpack(packed)
    base = len(body) - 4705
    village = next(i for i in range(70) if 0x21 <= body[base+0x100+i*28+8] <= 0x27)
    place_type = body[base+0x100+village*28+8]
    for offset, value in ((4, 1), (5, place_type), (7, village+1), (8, place_type), (11, 1)):
        body[base+offset] = value
    village_script = ('wait 300\nclick left 160 163\nwait 200\nclick left 45 170\nwait 200\n'
                      'click left 160 187\nwait 200\nclick left 160 163\nwait 300\n'
                      'click left 160 163\nwait 200\nclick left 160 163\nwait 200\n'
                      'checkpoint village-outside\nclick left 275 164\nwait 1000\n'
                      'checkpoint village-walk-out\nquit\n')
    case, log, samples = play('village-cue', script=village_script, save=pack(body, packed))
    assert 'Saves: slot 0 loaded' in log and f'Room 1 of place {village}' in log, log
    assert f'Desert: Paul walks out of place {village}' in log, log
    assert log.count('Music: M2.HSQ started') == 1 and 'Music: M3.HSQ started' not in log, log
    assert max(map(abs, samples[-44100:])) > 100, 'village navigation lost its existing music'
    results['village-cue'] = {'place': village, 'type': place_type, 'preserves_M2': True}
    print('PASS Amiga music: real village load and desert exit preserve the existing M2 stream', flush=True)

    rapid = 'wait 300\nclick left 160 163\nwait 150\nclick left 45 170\nwait 150\n'
    for i in range(16):
        row = 163 if i % 2 == 0 else 171
        rapid += f'click left 160 195\nwait 60\nclick left 160 {row}\nwait 60\n'
    rapid += 'wait 1200\nquit\n'
    case, log, samples = play('rapid-toggle', script=rapid)
    assert log.count('Options: music off') == 8 and log.count('Options: music on') == 8, log
    assert log.count('Music: M2.HSQ started') == 9, log
    assert max(map(abs, samples[-44100:])) > 100, 'rapid toggles lost final native stream'
    results['rapid-toggle'] = {'stop_start_pairs': 8, 'final_PCM': True, 'clean_exit': True}
    print('PASS Amiga music: eight rapid stop/start pairs retain final PCM and exit cleanly', flush=True)

    report = {'binary_sha256': hashlib.sha256(BINARY.read_bytes()).hexdigest(),
              'checks': results, 'count': len(results), 'evidence': str(OUT)}
    (OUT/'results.json').write_text(json.dumps(report, indent=2)+'\n')
    print(f'PASS Amiga music: {len(results)} checks; PCM evidence {OUT}')


if __name__ == '__main__':
    try:
        main()
    except (AssertionError, OSError, ValueError, StopIteration, subprocess.SubprocessError) as exc:
        print(f'FAIL Amiga music: {exc}; evidence {OUT}', file=sys.stderr)
        sys.exit(1)
