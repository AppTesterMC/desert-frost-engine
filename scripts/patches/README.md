# Patches to ScummVM

Changes to ScummVM outside `engines/dune/`. `build_dune_scummvm_ios.sh` applies
each `*.patch` once to the staged source tree (`-p1`, marker file
`.applied-<name>` in the tree). Keep them small, explain the reason in the
patch's own comments, and offer them upstream when they are generally useful.

| Patch | Why |
| --- | --- |
| `0001-ios7-audio-session-playback.patch` | iOS: ask for the Playback audio session so the ring/silent switch does not mute the game (the default SoloAmbient session is muted by it, which looked like "the build lost its audio"), and retry `AudioQueueStart`, whose single transient failure used to silence the whole session. |
