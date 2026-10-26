# Uninterrupted Player-Input Speedrun Policy

An uninterrupted run completes Dune through a single playthrough without the
engine's battle save/reload retry path.

Rules:

- No saveSlot(1)/loadSlot(1) battle retries; `dune_speedrun_uninterrupted=1`
  makes `speedrunFight` use one attempt and fail that attempt on a loss.
- No direct campaign/order fallbacks; refusals without an enabled row are not bypasses.
- No FORCED progression shortcuts. Declared setup shortcuts are allowed only in
  campaign mode and only the two documented setup markers.
- Player UI/input only: navigation, combat, orders, and save/load must use real
  controls.
- The RNG/progression seed must be disclosed in evidence.
- Slot-0 troop-approach retries may occur after a failed battle attempt; they
  are disclosed as `slot0_reload_count` / `troop_approach_retries`.

Classification:

- `interrupted`: any slot-1 battle save/reload retry occurred (`reload_count > 0`).
- `uninterrupted`: no slot-1 reloads, no fatal `BLOCKED` markers, no progression
  shortcuts, no direct order fallbacks, and a complete `player-ui-v1` coverage
  declaration.

Engine controls:

- `dune_speedrun_uninterrupted=1` disables `saveSlot(1)`/`loadSlot(1)` retries in
  `speedrunFight`; a lost battle is logged as a retry and the run continues
  without a slot-1 reload.
- `DUNE_SPEEDRUN_REQUIRE_UNINTERRUPTED=1` makes `check_speedrun.sh` use
  `--require-uninterrupted`.

Evidence:

- Disclosed seed: 30 (campaign part).
- Platforms: DOS floppy (18 forts), DOS CD (13 forts), Amiga (18 forts).
- Artifacts: notes/speedrun/uninterrupted-evidence/seed30/{floppy,cd,amiga}/evidence.json
- WS1 PASS captured in notes/speedrun/uninterrupted-evidence/seed30/ws1-pass-lines.txt
