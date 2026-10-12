# Local build staging

The native and iOS build scripts stage their source trees in the system temporary
directory before compiling.
The finished native executable and packaged iOS outputs are synchronized back
into the repository only after the job finishes. Intermediate object files and
compiler caches stay in the local staging area for fast incremental rebuilds.

The staging roots can be overridden for a particular job with
`DUNE_LOCAL_BUILD_ROOT`. The default roots are:

- `${TMPDIR:-/tmp}/dune-scummvm-native-build`
- `${TMPDIR:-/tmp}/dune-scummvm-ios-build`
- `${TMPDIR:-/tmp}/dune-mxdos-ios-build`

Logs, scripts, test results, IPA files, and source changes remain under the
repository. The temporary directories contain only build working data and may
be recreated without affecting the tracked project.
