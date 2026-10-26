"""Run against the built CLI: python3 test/integration/test_ingest.py --duckdb ... --extension ..."""

import argparse
import json
import os
import gzip
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

parser = argparse.ArgumentParser()
parser.add_argument("--duckdb", default="build/release/duckdb")
parser.add_argument("--httpfs")
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
        self.trajectory_destination = self.root / "trajectories"

    def sql(self, sql, error=None):
        result = subprocess.run(
            [
                CLI,
                "-unsigned",
                "-json",
                "-c",
                f'LOAD {literal(EXTENSION)}; {getattr(self, "setup_sql", "")} {sql}',
            ],
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
        remaining = result.stdout.strip()
        parsed = None
        while remaining:
            parsed, end = json.JSONDecoder().raw_decode(remaining)
            remaining = remaining[end:].strip()
        return parsed

    def ingest(self, **options):
        parts = {
            "job_destination": self.destination,
            "trajectory_destination": self.trajectory_destination,
            **options,
        }
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

    def add_trajectory(self, parent=None, compressed=False):
        parent = parent or self.trial / "agent"
        parent.mkdir(parents=True, exist_ok=True)
        data = {
            "schema_version": "ATIF-v1.8",
            "trajectory_id": "test",
            "agent": {"name": "example", "version": "1"},
            "steps": [{"step_id": 1, "source": "user", "message": "hello 你好🙂"}],
        }
        name = "trajectory.json.gz" if compressed else "trajectory.json"
        content = json.dumps(data, ensure_ascii=False).encode()
        (parent / name).write_bytes(gzip.compress(content) if compressed else content)
        return parent / name

    def test_trajectory_conversion_and_duplicate(self):
        self.add_trajectory()
        result = self.sql(
            self.ingest(
                text_compression="zstd",
                trajectory_destination=str(self.trajectory_destination) + "/",
            )
        )
        dataset = self.trajectory_destination / (self.trial.name + ".lance")
        self.assertEqual(result[0]["status"], "ingested")
        self.assertEqual(result[0]["trajectory_destination"], str(dataset))
        self.assertTrue(list((dataset / "_versions").glob("*.manifest")))
        self.sql(
            self.ingest(job_destination=self.root / "another-upload"),
            error="already exists",
        )
        self.assertFalse((self.root / "another-upload").exists())

    def test_multistep_and_trials_container(self):
        container = self.source / "trials"
        container.mkdir()
        self.trial.rename(container / self.trial.name)
        self.trial = container / self.trial.name
        self.add_trajectory(self.trial / "steps" / "step-1" / "agent")
        self.add_trajectory(self.trial / "steps" / "step-2" / "agent", compressed=True)
        result = self.sql(self.ingest())
        self.assertEqual(len(result), 2)
        self.assertEqual(
            {Path(row["trajectory_destination"]).name for row in result},
            {"step-1.lance", "step-2.lance"},
        )
        for row in result:
            self.assertTrue(Path(row["trajectory_destination"]).is_dir())

    def test_trajectory_validation_and_preview(self):
        trajectory = self.add_trajectory()
        result = self.sql(self.ingest(dry_run=True))
        self.assertEqual(result[0]["status"], "planned")
        self.assertTrue(result[0]["trajectory_destination"].endswith(".lance"))
        self.assertFalse(self.destination.exists())
        self.assertFalse(self.trajectory_destination.exists())
        self.sql(
            self.ingest(trajectory_destination=self.destination / "nested"),
            error="must not overlap",
        )
        self.sql(
            self.ingest(text_compression="invalid"), error="text_compression must be"
        )
        self.sql(
            self.ingest(trajectory_destination="gs://bucket/out"), error="supports only"
        )
        self.add_trajectory(compressed=True)
        self.sql(self.ingest(), error="Multiple primary trajectories")
        self.assertFalse(self.destination.exists())

    def test_missing_media_surfaces_failure(self):
        fixture = Path(
            "external/duckdb_lance_conversion/test/data/trajectory_missing_media.json"
        )
        shutil.copyfile(fixture, self.trial / "agent" / "trajectory.json")
        self.sql(self.ingest(), error="media")

    def test_limit_does_not_skip_later_conversion_failures(self):
        self.add_trajectory()
        other = self.source / "zzz-last-trial"
        (other / "agent").mkdir(parents=True)
        (other / "config.json").write_text("{}")
        (other / "result.json").write_text("{}")
        (other / "agent" / "trajectory.json").write_text("not ATIF")
        self.sql(self.ingest() + " LIMIT 1", error="ATIF")

    def test_media_payloads_are_embedded(self):
        fixtures = Path("external/duckdb_lance_conversion/test/data")
        for name in ["trajectory_media.json", "atif-image.png", "atif-audio.wav"]:
            target = "trajectory.json" if name.startswith("trajectory") else name
            shutil.copyfile(fixtures / name, self.trial / "agent" / target)
        result = self.sql(self.ingest())
        dataset = Path(result[0]["trajectory_destination"])
        shutil.rmtree(self.source)
        shutil.rmtree(self.destination)
        payloads = [file.read_bytes() for file in dataset.rglob("*.lance")]
        for name in ["atif-image.png", "atif-audio.wav"]:
            expected = (fixtures / name).read_bytes()
            # The fixture references each payload from both message and observation.
            self.assertEqual(sum(data.count(expected) for data in payloads), 2)

    def test_external_access_disabled(self):
        self.sql(
            "SET enable_external_access = false; " + self.ingest(),
            error="enable_external_access",
        )
        self.assertFalse(self.destination.exists())

    def test_no_catalog_tables(self):
        self.sql(self.ingest())
        self.assertEqual(
            self.sql("SELECT count(*) AS n FROM information_schema.tables")[0]["n"], 0
        )


@unittest.skipUnless(
    args.httpfs, "pass --httpfs to run S3 integration tests (requires moto[server])"
)
class S3Test(unittest.TestCase):
    sql = IngestTest.sql
    ingest = IngestTest.ingest
    add_trajectory = IngestTest.add_trajectory

    @classmethod
    def setUpClass(cls):
        from moto.server import ThreadedMotoServer

        cls.server = ThreadedMotoServer(ip_address="127.0.0.1", port=0, verbose=False)
        cls.server.start()
        host, port = cls.server.get_host_and_port()
        cls.endpoint = f"{host}:{port}"

    @classmethod
    def tearDownClass(cls):
        cls.server.stop()

    def setUp(self):
        import boto3
        import uuid

        IngestTest.setUp(self)
        self.bucket = "agent-runs-" + uuid.uuid4().hex
        self.s3 = boto3.client(
            "s3",
            endpoint_url="http://" + self.endpoint,
            aws_access_key_id="test",
            aws_secret_access_key="test",
            region_name="us-east-1",
        )
        self.s3.create_bucket(Bucket=self.bucket)
        self.setup_sql = f"""LOAD {literal(Path(args.httpfs).resolve())};
        CREATE SECRET s3_test (TYPE S3, KEY_ID 'test', SECRET 'test', REGION 'us-east-1',
            ENDPOINT {literal(self.endpoint)}, URL_STYLE 'path', USE_SSL false,
            SCOPE {literal('s3://' + self.bucket + '/')});"""
        self.destination = f"s3://{self.bucket}/jobs/my-job"
        self.trajectory_destination = f"s3://{self.bucket}/trajectories/my-job"

    def objects(self):
        return {
            obj["Key"]
            for obj in self.s3.list_objects_v2(Bucket=self.bucket).get("Contents", [])
        }

    def test_s3_upload_and_convert(self):
        fixtures = Path("external/duckdb_lance_conversion/test/data")
        for name in ["trajectory_media.json", "atif-image.png", "atif-audio.wav"]:
            target = "trajectory.json" if name.startswith("trajectory") else name
            shutil.copyfile(fixtures / name, self.trial / "agent" / target)
        result = self.sql(self.ingest())
        self.assertEqual(result[0]["status"], "ingested")
        for source in self.source.rglob("*"):
            if source.is_file():
                key = "jobs/my-job/" + source.relative_to(self.source).as_posix()
                actual = self.s3.get_object(Bucket=self.bucket, Key=key)["Body"].read()
                self.assertEqual(actual, source.read_bytes())
        self.assertTrue(
            any(
                "/_versions/" in key and key.endswith(".manifest")
                for key in self.objects()
            )
        )
        payloads = [
            self.s3.get_object(Bucket=self.bucket, Key=key)["Body"].read()
            for key in self.objects()
            if key.startswith("trajectories/") and key.endswith(".lance")
        ]
        for name in ["atif-image.png", "atif-audio.wav"]:
            self.assertEqual(
                sum(data.count((fixtures / name).read_bytes()) for data in payloads), 2
            )
        before = self.objects()
        self.sql(
            self.ingest(job_destination=f"s3://{self.bucket}/another"),
            error="already exists",
        )
        self.assertEqual(self.objects(), before)

    def test_s3_preview_does_not_upload(self):
        self.add_trajectory()
        self.sql(self.ingest(dry_run=True))
        self.assertEqual(self.objects(), set())


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0], *unittest_args])
