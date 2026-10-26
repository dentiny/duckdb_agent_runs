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
    dry_run := false
);
```

`job_destination` is the exact destination directory/prefix, with no generated
job ID or directory name appended. All regular files below the source are copied
byte-for-byte under their original relative paths, including logs and artifacts.
The source is a local directory with `config.json` and `result.json` at its root.
Planning and uploads use the calling connection's DuckDB filesystem. Resolvable
internal symlinks are rejected. On POSIX, DuckDB's directory listing omits dangling
symlinks and special files; these are not uploaded. Empty directories have no
object-storage representation. Local paths and DuckDB writable filesystem URIs are supported;
for S3, load `httpfs` and configure DuckDB secrets before ingesting.

`dry_run := true` validates and plans the upload without writes. EXPLAIN and
PREPARE do not upload files. Existing target objects cause an error, rather than
an overwrite. Transfers stream through a 1 MiB buffer. External writes are not
transactional: failures may leave partial outputs, and concurrent writers must
use distinct destination prefixes. Use a fresh prefix when retrying.

The returned row contains the destination, file count, total source bytes, and
`planned` or `uploaded` status. No catalog tables are created.

```sh
make release
make test
```
