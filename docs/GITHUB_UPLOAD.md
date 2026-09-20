# GitHub upload package

This repository contains both public source material and local build/device
artifacts. Do not upload the working tree wholesale: original Dune data,
disc images, executables, IPA files, compiler output, and device logs must
remain outside the public repository.

## Build a clean source package

From the repository root, run:

```sh
./scripts/prepare_github_package.sh
```

The script creates a timestamped directory below `release/github/`. Pass an
explicit destination when a fixed path is useful:

```sh
./scripts/prepare_github_package.sh release/github/Cryogenic-upload
```

The output is a source/documentation package, not a release build. It includes
the C# implementation, the Cryogenic Dune engine overlay, build/test scripts,
GitHub metadata, documentation, and the selected README media. It does not
include the full ScummVM/SDL/DOSBox dependency trees, original game files, or
an IPA.

The downloader workflow for obtaining those build inputs locally is documented
in [`DOWNLOADER_BUILD.md`](DOWNLOADER_BUILD.md).

## Before publishing

1. Review `README.md`, `CONTRIBUTING.md`, and
   `docs/scummvm-engine-proposal.md` for claims that match the current build.
2. Run the native and runtime smoke tests that are possible on the machine.
3. Inspect the generated `.source-manifest.txt` and confirm that no game data,
   music, disc image, executable, private path, or device log is present.
4. Review the staged diff and file modes; only intentional executable bits
   should remain on scripts.
5. Commit the reviewed source changes, then push through the normal GitHub
   review process. This project does not publish copyrighted Dune data.

## Public versus private material

Public source package:

- `src/` and `third_party/scummvm/engines/dune/` (the Cryogenic engine overlay,
  not the full ScummVM checkout);
- `scripts/`, `.github/`, `README.md`, `CONTRIBUTING.md`, licenses, and notices;
- `doc/` screenshots and the selected intro animation used for presentation;
- `docs/`, including the ScummVM proposal and this checklist.

Private/local material:

- `DUNE.DAT`, `DUNES.HSQ`, `DNCDPRG.EXE`, `INTDS.HSQ`, and other game data;
- ISO/ADF/Mega CD dumps, commercial soundtrack files, and extracted assets;
- the full ScummVM, SDL, DOSBox, and iOS framework source/dependency trees;
- IPA/app bundles, signing material, Xcode derived data, object files, and
  build caches;
- device logs, recordings, and temporary test runs under `notes/temp/`.

The ScummVM engine is still an incomplete development track. The package must
retain that status and should be sent to the ScummVM developers as a review
proposal, not presented as an upstream-ready engine.
