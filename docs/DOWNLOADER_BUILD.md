# Downloader build procedure

The public GitHub package is intentionally source-only. It does not contain a
full ScummVM checkout, SDL/DOSBox dependencies, iOS frameworks, original Dune
data, commercial music, or generated compiler output. The directory
`third_party/scummvm/engines/dune/` is the Cryogenic Dune engine overlay; it is
not a copy of the upstream ScummVM tree.

## 1. Obtain the public package

Clone the repository or unpack the source package and enter its root:

```sh
git clone https://github.com/OpenRakis/Cryogenic.git
cd Cryogenic
```

Keep the original game files outside the repository. The engine tests accept a
legally obtained DOS CD or floppy data directory supplied with `--path` or by
the local test layout; no game data should be committed or uploaded.

## 2. Download the build inputs locally

Use the ScummVM revision selected by the maintainer and obtain it from the
official ScummVM source repository or release archive. Save the archive as:

```text
third_party/scummvm-source.tar
```

The archive must contain the ScummVM source root, including `configure`,
`engines/`, and `devtools/create_project/`. Check it before running a build:

```sh
tar -tf third_party/scummvm-source.tar | sed -n '1,20p'
```

For an iOS build, obtain the compatible iOS framework bundle used by the
selected ScummVM revision and place it at:

```text
third_party/scummvm-ios-libs/scummvm-ios7-libs-v4.zip
```

These downloads are local build inputs, not repository contents. Confirm their
licences and provenance before using or redistributing them. The downloader
should not fetch copyrighted Dune game files or commercial soundtrack files.

## 3. Build on the local fast drive

The scripts keep large intermediate trees outside the repository by default.
Set `DUNE_LOCAL_BUILD_ROOT` to a local SSD/work volume when necessary:

```sh
export DUNE_LOCAL_BUILD_ROOT=/path/to/local/dune-build
./scripts/test_dune_scummvm_native.sh
./scripts/test_dune_scummvm_runtime.sh
```

The iOS build uses the same downloaded source and creates a development IPA
and evidence under `notes/temp/`:

```sh
./scripts/build_dune_scummvm_ios.sh
```

For SDL screenshot/audio checks, use the desktop SDL test after the native
build has succeeded:

```sh
./scripts/test_dune_scummvm_sdl.sh dump
```

The C# track has its own .NET 8 prerequisites and also requires user-supplied
DOS data. See the root README and `CONTRIBUTING.md` for the hash and command.

## 4. Keep generated material out of GitHub

Do not add `third_party/scummvm-source.tar`, the iOS framework archive, full
upstream dependency trees, `notes/temp/`, `build-*`, `obj/`, `bin/`, Xcode
derived data, object files, IPAs, device logs, ISO/ADF images, or game data.
Use `scripts/prepare_github_package.sh` to regenerate a clean source bundle
after local builds; review its `.source-manifest.txt` before upload.
