#!/usr/bin/env python3
"""Record/replay Dune engine input (DUNEREC1), using an isolated config and saves.

F12 finishes recording; Escape aborts playback. Original DOS demo files belong
with windows/Start-DOSBoxX-Stream.ps1 -DemoMode Playback, not this launcher.
"""
from __future__ import annotations
import argparse
from datetime import datetime
import os
from pathlib import Path
import subprocess
import sys
import uuid


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('rec', 'dem'))
    parser.add_argument('file', type=Path)
    parser.add_argument('--engine', type=Path, required=True)
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--skip-intro', action='store_true', help='REC only; this setting is stored in the recording')
    parser.add_argument('--script', type=Path, help='REC only: record a developer input script, without period jumps')
    parser.add_argument('--headless', action='store_true', help='DEM or scripted REC only; no interactive display')
    parser.add_argument('--language', type=int, choices=range(1,8), default=1)
    args = parser.parse_args()
    args.file = args.file.expanduser().resolve()
    args.engine = args.engine.expanduser().resolve()
    args.data = args.data.expanduser().resolve()
    if not args.engine.is_file() or not args.data.is_dir():
        parser.error('engine executable and game data directory must exist')
    if args.mode == 'rec' and args.file.exists():
        parser.error('recording already exists; choose a new filename')
    if args.mode == 'dem' and not args.file.is_file():
        parser.error('recording does not exist')
    if args.mode == 'dem' and (args.script or args.skip_intro):
        parser.error('DEM uses the recorded startup settings; do not supply a script or --skip-intro')
    if args.script and not args.script.is_file():
        parser.error('input script does not exist')
    if args.headless and args.mode == 'rec' and not args.script:
        parser.error('headless REC needs --script')
    args.file.parent.mkdir(parents=True, exist_ok=True)
    run = args.file.parent / '.demo-sessions' / (datetime.now().strftime('%Y%m%d-%H%M%S-') + uuid.uuid4().hex[:8])
    (run/'saves').mkdir(parents=True)
    config = {'savepath': run/'saves', f'dune_{args.mode}': args.file}
    if args.mode == 'rec':
        config['dune_language'] = args.language
    if args.skip_intro:
        config.update(dune_floppy_start=99, dune_intro_start=99, dune_skip_cd_story=1)
    if args.script:
        config['dune_input'] = args.script.resolve()
    if args.headless:
        config['dune_checkpoint_dir'] = run/'frames'
        (run/'frames').mkdir()
    for value in config.values():
        if '\n' in str(value) or '\r' in str(value):
            parser.error('paths cannot contain line breaks')
    ini = run/'scummvm.ini'
    ini.write_text('[scummvm]\n'+''.join(f'{key}={value}\n' for key,value in config.items()), encoding='utf-8')
    env=os.environ.copy()
    if args.headless:
        env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
    print('Session:',run,flush=True)
    print('F12 finishes recording; Escape aborts replay.',flush=True)
    with (run/'console.log').open('w',encoding='utf-8') as log:
        result=subprocess.run([str(args.engine),'-c',str(ini),'--gfx-mode=surface','--no-fullscreen','--path='+str(args.data),'dune'],cwd=run,env=env,stdout=log,stderr=subprocess.STDOUT)
    print('Engine exit:',result.returncode,'; log:',run/'console.log')
    return result.returncode


if __name__=='__main__':
    sys.exit(main())
