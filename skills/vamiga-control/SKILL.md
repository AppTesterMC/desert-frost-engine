---
name: vamiga-control
description: Drive the vAmiga macOS emulator over its JSON-RPC/RetroShell interface for Amiga Dune verification. Use for vAmiga control, disk insertion, memory and ROM setup, and scripted input.
---

# vAmiga Control

vAmiga exposes a JSON-RPC 2.0 server on TCP port 8082. Enable it in RetroShell with `server rpc start`; it resets when vAmiga is re-set up.

## Protocol

- Plain TCP, not HTTP.
- One line per request: `{"jsonrpc":"2.0","id":N,"method":"retroshell","params":"<RetroShell command>"}`.
- `params` must be a string and the method must be `retroshell`.
- One connection at a time; close it after each reply.

## Python client

```python
import socket, json

def rpc(command, host="127.0.0.1", port=8082):
    req = {"jsonrpc": "2.0", "id": 1, "method": "retroshell", "params": command}
    with socket.create_connection((host, port), timeout=5) as s:
        s.sendall((json.dumps(req) + "\n").encode())
        return s.recv(65536).decode()
```

## Useful RetroShell commands

- `help <cmd>`
- `amiga power on|off`, `amiga reset`
- `mem`, `mem set CHIP_RAM|SLOW_RAM|FAST_RAM <KB>` (only while powered off)
- `df0 insert <path without spaces>`, `df1 set CONNECT true`
- `agnus`, `mem save`, `keyboard press`, `mouse1 press`
- `mem load rom <path>` (power off first; path without spaces)
- `server`

## Dune Amiga setup

- Disks: `~/Amiga/Dune/DuneA/B/C.adf` (originals in `~/dune-amiga-data/raw`).
- Kickstart 2.04: `~/Amiga/kick204.rom` (user-supplied; never fetch ROMs).
- Memory: 1 MB Chip + 512 KB Slow.
- Drives: DF0 = A, DF1 = B.

## Limitations

- No screenshot command and no mouse movement.
- The server listens on all interfaces (`*:8082`); treat it as local-only.
- Never download Kickstart ROMs; only use user-supplied or legally obtained ROMs.
