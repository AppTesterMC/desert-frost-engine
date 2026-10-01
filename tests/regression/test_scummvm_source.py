#!/usr/bin/env python3
"""Exercise real tar/rsync staging without game data, downloads or engine builds."""
import io
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[2]
HELPER = Path(os.environ.get("SCUMMVM_SOURCE_HELPER", REPO / "scripts/scummvm_source.sh")).resolve()


class SourceStagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="dune-source-stage-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.repo = self.root / "repo"
        self.local = self.root / "build"
        self.dest = self.root / "destination"
        (self.repo / "third_party").mkdir(parents=True)
        self.local.mkdir()
        self.dest.mkdir()

    def seed_engine(self):
        engine = self.dest / "engines/dune"
        (engine / "nested").mkdir(parents=True)
        (engine / "dune.cpp").write_bytes(b"authoritative gameplay engine\n")
        (engine / "nested/local-only.h").write_bytes(b"local-only header\n")
        (engine / "dune.cpp").chmod(0o600)
        engine.chmod(0o700)
        return self.engine_snapshot()

    def engine_snapshot(self):
        engine = self.dest / "engines/dune"
        return {
            str(p.relative_to(engine)): (p.stat().st_mode & 0o777, p.read_bytes() if p.is_file() else None)
            for p in [engine, *sorted(engine.rglob("*"))]
        }

    def make_archive(self, prefix, directory_headers=True):
        with tarfile.open(self.repo / "third_party/scummvm-source.tar", "w") as archive:
            if directory_headers:
                for name in ("engines", "engines/dune", "engines/dune/nested", "engines/agi"):
                    entry = tarfile.TarInfo(prefix + name + "/")
                    entry.type = tarfile.DIRTYPE
                    entry.mode = 0o755
                    archive.addfile(entry)
            for name, body in {
                "configure": b"upstream configure\n",
                "engines/agi/agi.cpp": b"upstream other engine\n",
                "engines/dune/dune.cpp": b"obsolete prototype\n",
                "engines/dune/module.mk": b"obsolete four-object module\n",
                "engines/dune/nested/archive-only.h": b"must never appear\n",
            }.items():
                entry = tarfile.TarInfo(prefix + name)
                entry.size = len(body)
                entry.mode = 0o644
                archive.addfile(entry, io.BytesIO(body))

    def stage(self):
        # Positional shell arguments preserve spaces and avoid path interpolation.
        command = 'set -eu; repo_root=$1; local_root=$2; . "$3"; stage_scummvm_source "$4"'
        process = subprocess.run(
            ["/bin/sh", "-c", command, "stage-test", str(self.repo), str(self.local), str(HELPER), str(self.dest)],
            env={**os.environ, "SCUMMVM_REF": "fixture-ref"}, text=True, capture_output=True,
        )
        self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
        self.assertEqual((self.dest / "configure").read_bytes(), b"upstream configure\n")
        self.assertEqual((self.dest / "engines/agi/agi.cpp").read_bytes(), b"upstream other engine\n")

    def check_archive_existing(self, prefix, directory_headers=True):
        expected = self.seed_engine()
        self.make_archive(prefix, directory_headers)
        self.stage()
        self.assertEqual(self.engine_snapshot(), expected, "archive replaced or added authoritative engine files")
        self.stage()
        self.assertEqual(self.engine_snapshot(), expected, "repeated staging changed the engine")

    def test_bare_archive_preserves_engine(self):
        self.check_archive_existing("")

    def test_dot_prefixed_archive_preserves_engine(self):
        self.check_archive_existing("./")

    def test_bare_file_only_archive_preserves_engine(self):
        self.check_archive_existing("", directory_headers=False)

    def test_dot_prefixed_file_only_archive_preserves_engine(self):
        self.check_archive_existing("./", directory_headers=False)

    def test_archive_does_not_install_prototype_into_empty_destination(self):
        for prefix in ("", "./"):
            with self.subTest(prefix=prefix):
                self.make_archive(prefix)
                self.stage()
                self.assertFalse((self.dest / "engines/dune").exists(), "prototype entered an empty staging tree")

    def test_cached_git_fallback_keeps_engine(self):
        expected = self.seed_engine()
        upstream = self.local / "scummvm-upstream"
        (upstream / "engines/dune").mkdir(parents=True)
        (upstream / "engines/agi").mkdir(parents=True)
        (upstream / ".git").mkdir()
        (upstream / ".git/config").write_text("must not stage\n")
        (upstream / ".dune-ref-fixture-ref").touch()
        (upstream / "engines/dune/dune.cpp").write_text("unrelated upstream engine\n")
        (upstream / "engines/agi/agi.cpp").write_bytes(b"upstream other engine\n")
        (upstream / "configure").write_bytes(b"upstream configure\n")
        self.stage()
        self.assertEqual(self.engine_snapshot(), expected)
        self.assertFalse((self.dest / ".git").exists())
        self.assertFalse((self.dest / ".dune-ref-fixture-ref").exists())


if __name__ == "__main__":
    print("tar:", shutil.which("tar"), flush=True)
    print(subprocess.check_output(["tar", "--version"], text=True).strip(), flush=True)
    unittest.main(verbosity=2)
