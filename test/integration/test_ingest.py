"""Run against the built CLI: python3 test/integration/test_ingest.py --duckdb ... --extension ..."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

parser = argparse.ArgumentParser()
parser.add_argument("--duckdb", default="build/release/duckdb")
parser.add_argument(
    "--extension",
    default="build/release/extension/agent_runs/agent_runs.duckdb_extension",
)
args, unittest_args = parser.parse_known_args()
CLI = str(Path(args.duckdb).resolve())
EXTENSION = str(Path(args.extension).resolve())


def literal(value):
    return "'" + str(value).replace("'", "''") + "'"


class IngestTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="agent-runs-'quoted-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "job"
        self.source.mkdir()
        (self.source / "config.json").write_text("{}")
        (self.source / "result.json").write_text("{}")
        self.trial = self.source / "task__abc"
        (self.trial / "agent").mkdir(parents=True)
        (self.trial / "config.json").write_text("{}")
        (self.trial / "result.json").write_text("{}")
        (self.trial / "agent" / "output.bin").write_bytes(bytes(range(256)) * 9000)
        (self.source / "empty.log").touch()
        self.destination = self.root / "uploaded"

    def sql(self, sql, error=None):
        result = subprocess.run(
            [CLI, "-unsigned", "-json", "-c", f"LOAD {literal(EXTENSION)}; {sql}"],
            text=True,
            capture_output=True,
            timeout=120,
            env={
                k: v
                for k, v in os.environ.items()
                if k not in ("CODEX_CI", "CODEX_THREAD_ID")
            },
        )
        if error:
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn(error.lower(), result.stderr.lower())
            return
        self.assertEqual(result.returncode, 0, result.stderr)
        if sql.lstrip().upper().startswith("EXPLAIN"):
            return result.stdout
        return json.loads(result.stdout) if result.stdout.strip() else None

    def ingest(self, **options):
        parts = {"job_destination": self.destination, **options}
        named = ", ".join(
            f"{key} := {str(value).lower() if isinstance(value, bool) else literal(value)}"
            for key, value in parts.items()
        )
        return f"SELECT * FROM ingest_agent_run({literal(self.source)}, {named})"

    def test_upload_byte_exact_and_duplicate(self):
        result = self.sql(self.ingest())
        self.assertEqual(result[0]["status"], "uploaded")
        originals = {
            p.relative_to(self.source): p.read_bytes()
            for p in self.source.rglob("*")
            if p.is_file()
        }
        copied = {
            p.relative_to(self.destination): p.read_bytes()
            for p in self.destination.rglob("*")
            if p.is_file()
        }
        self.assertEqual(copied, originals)
        self.assertEqual(int(result[0]["file_count"]), len(originals))
        self.assertEqual(
            int(result[0]["total_bytes"]), sum(map(len, originals.values()))
        )
        self.sql(self.ingest(), error="already exists")
        self.assertEqual((self.destination / "config.json").read_bytes(), b"{}")

    def test_dry_run_and_explain_have_no_effects(self):
        self.assertEqual(self.sql(self.ingest(dry_run=True))[0]["status"], "planned")
        self.assertFalse(self.destination.exists())
        self.sql("EXPLAIN " + self.ingest())
        self.assertFalse(self.destination.exists())

    def test_invalid_inputs(self):
        self.sql("SELECT * FROM ingest_agent_run(NULL)", error="must not be NULL")
        self.sql(
            f"SELECT * FROM ingest_agent_run({literal(self.source)})",
            error="job_destination is required",
        )
        self.sql(self.ingest(format="unknown"), error="Unsupported run format")
        self.sql(self.ingest(job_destination=" "), error="nonblank")
        self.sql(
            self.ingest(job_destination=self.source / "inside"),
            error="must not overlap",
        )
        (self.source / "result.json").unlink()
        self.sql(self.ingest(), error="Harbor job requires result.json")
        self.assertFalse(self.destination.exists())

    def test_symlink_rejected(self):
        (self.source / "link").symlink_to(self.root / "secret")
        self.sql(self.ingest(), error="Symlinks are not supported")
        self.assertFalse(self.destination.exists())


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0], *unittest_args])
