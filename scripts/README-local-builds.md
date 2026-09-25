# Local build staging

The repository is stored on an external volume, so the long-running native and
iOS build scripts stage their source trees under `/private/tmp` before compiling.
The finished native executable and packaged iOS outputs are synchronized back
into the repository only after the job finishes. Intermediate object files and
compiler caches stay in the local staging area for fast incremental rebuilds.

The staging roots can be overridden for a particular job with
`DUNE_LOCAL_BUILD_ROOT`. The default roots are:

- `/private/tmp/dune-scummvm-native-build`
- `/private/tmp/dune-scummvm-ios-build`
- `/private/tmp/dune-mxdos-ios-build`

Logs, scripts, test results, IPA files, and source changes remain under the
repository. The temporary directories contain only build working data and may
be recreated without affecting the tracked project.
