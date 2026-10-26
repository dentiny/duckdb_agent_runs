# agent_runs

DuckDB extension for uploading complete agent runs and converting their trajectories to Lance.
Harbor is the first input adapter. Job/Trial metadata catalog support is separate future work.

The pinned `external/duckdb_lance_conversion` Git submodule supplies ATIF reading,
media handling, and Lance writing. Both extensions build against the same DuckDB
submodule. `LOAD agent_runs` loads the statically bundled conversion dependency;
no runtime download or `INSTALL` is needed.

## Build

```sh
git clone --recurse-submodules https://github.com/dentiny/duckdb_agent_runs.git
cd duckdb_agent_runs
# Requires CMake, a C++17 compiler, Cargo, and protoc (or vcpkg protobuf).
GEN=ninja make
make test
```

For an existing checkout, run `git submodule update --init --recursive` first.
The loadable binary is `build/release/extension/agent_runs/agent_runs.duckdb_extension`.
DuckDB extensions require the exact DuckDB version they were built against.

## Review sequence

1. Initialize agent_runs and bundle the pinned Lance conversion submodule.
2. Add the single ingestion function, Harbor adapter, and hierarchy-preserving uploads.
3. Convert discovered ATIF trajectories to Lance and add end-to-end coverage.

## Upload a completed Harbor job

```sql
LOAD agent_runs;
SELECT * FROM ingest_agent_run(
    '/local/harbor/job',
    format := 'harbor',
    job_destination := 's3://bucket/jobs/exact-job-prefix',
    trajectory_destination := 's3://bucket/trajectories/exact-job-prefix',
    text_compression := 'lz4',
    dry_run := false
);
```

`job_destination` is the exact destination directory/prefix, with no generated
job ID or directory name appended. All regular files below the source are copied
byte-for-byte under their original relative paths, including logs and artifacts.
The source is a directory or object-storage prefix with `config.json` and
`result.json` at its root. Remote sources require a DuckDB filesystem that can
list and read objects; S3 sources use the configured `httpfs` secrets.
Planning and uploads use the calling connection's DuckDB filesystem. Resolvable
internal symlinks are rejected. On POSIX, DuckDB's directory listing omits dangling
symlinks and special files; these are not uploaded. Empty directories have no
object-storage representation. Local paths and DuckDB writable filesystem URIs are supported;
for S3, load `httpfs` and configure DuckDB secrets before ingesting.

`dry_run := true` checks directory layout and target conflicts without writes;
it does not decode ATIF or validate media contents. EXPLAIN and
PREPARE do not upload files. Existing target objects cause an error, rather than
an overwrite. Transfers stream through a 1 MiB buffer. External writes are not
transactional: failures may leave partial outputs, and concurrent writers must
use distinct destination prefixes. Use a fresh prefix when retrying.

The result contains one row per discovered trajectory, or a single row with NULL
trajectory fields if there are none. Columns are `job_destination`, `file_count`,
`total_bytes`, `status`, `trial_name`, `trajectory_source`, and
`trajectory_destination`. File/byte counts describe the whole job and repeat on
each trajectory row. Status is `planned`, `uploaded` (no trajectories), or
`ingested` (upload and all conversions completed). All conversions complete before
any success rows are emitted, including when the caller uses LIMIT. No catalog
tables are created.


## Trajectory destinations and supported Harbor layouts

`trajectory_destination` is the exact per-job output **root** you specify. No job
UUID or source directory name is appended. Each primary trajectory gets its own
Lance dataset beneath that root, using the trial's relative directory name:

| Input beneath job_directory | Output beneath trajectory_destination |
| --- | --- |
| `task__abc/agent/trajectory.json` | `task__abc.lance` |
| `trials/task__abc/agent/trajectory.jsonl` | `trials/task__abc.lance` |
| `task__abc/steps/solve/agent/trajectory.json` | `task__abc/steps/solve.lance` |

Direct trial-level `trajectory.json`/`trajectory.jsonl` and gzip variants are also
accepted. A trial must have `config.json` and `result.json`. Multiple primary
trajectory files for the same trial or step are rejected as ambiguous. Missing
trajectories are allowed: failed trials and their artifacts are still uploaded.
If any trajectories exist, `trajectory_destination` is required. Source, job
output, and trajectory output roots must not overlap.

ATIF is read from the original local path, so relative media paths continue to
resolve. The pinned conversion submodule writes the full native ATIF schema,
including provenance, raw JSON, and embedded image/audio Blob V2 content, with
Lance storage version 2.2. Text compression is `lz4` by default; `zstd` and `none`
are supported. A new dataset is created per trajectory; existing datasets are
rejected. Local paths and `s3://` are supported for Lance output.

For S3, configure a scoped DuckDB `TYPE S3` secret with the appropriate region,
endpoint, URL style, and credentials. The upload uses the calling connection's
DuckDB filesystem; the conversion uses the same database's secrets through the
bundled Lance writer. The six `lance_conversion_storage_*` retry/timeout settings
are forwarded to the conversion connection. Conversion runs on a separate
connection to avoid re-entering the calling query. Job upload and conversion are
external effects and are not rolled back by a SQL transaction or later failure.
Use distinct destination prefixes for concurrent ingestion and a fresh pair of
prefixes after an incomplete attempt. No automatic overwrite, catalog
registration, durable queue, or retry/resume protocol is provided.

## Tests

All ingestion tests are DuckDB SQLLogicTests in `test/sql/`. Build and run them
with the standard extension workflow:

```sh
make release
make test
```

The tests read checked-in Harbor job and ATIF fixtures from `testdata/`, covering
artifact byte fidelity, text/image/audio conversion with all supported codecs,
multi-step/gzip input, preview/EXPLAIN/PREPARE behavior, validation, symlink
rejection, and duplicate protection. They write local outputs only under the
DuckDB test runner's temporary directory.

The optional S3 SQLLogicTest requires `httpfs` and a disposable S3-compatible
service. See `test/README.md` for configuration. CI runs it against RustFS.
