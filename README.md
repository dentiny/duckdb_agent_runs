# agent_runs

DuckDB extension for ATIF trajectory conversion and agent-run metadata ingestion.
The pinned `external/duckdb_lance_conversion` submodule supplies the ATIF reader,
media embedding, and Lance writer. `LOAD agent_runs` loads the bundled dependency.

## Build

```sh
git submodule update --init --recursive
GEN=ninja make release
make test
```

Requires CMake, C++17, Cargo, and protoc. DuckDB is pinned to v2.0.0; loadable
extensions require the exact DuckDB version they were built against.

## ATIF to Lance

Convert a trajectory with a single function call:

```sql
LOAD agent_runs;

CALL import_agent_trajectory(
    '/data/trial/agent/trajectory.json',
    trajectory_destination := 's3://bucket/trajectories/job-id/trial-id.lance',
    text_compression := 'lz4'
);
```

`trajectory_destination` is the exact Lance dataset URI. `text_compression`
defaults to `lz4`; `zstd` and `none` are also supported. The call returns
`trajectories_converted` and handles ATIF reading, media embedding, and Lance
writing internally using the bundled conversion dependency.

Sources may be JSON/JSONL or gzip, on local storage, S3, or HTTP(S). Relative
image/audio references resolve beside the trajectory and their bytes are
embedded in the dataset. Configure DuckDB S3 secrets before using S3 sources
or destinations.

This extension does not copy job directories or upload arbitrary artifacts.

## Job/trial metadata ingestion

Loading the extension initializes or upgrades the metadata catalog before ingestion:

```sql
LOAD agent_runs;

CALL ingest_agent_run(
    '/data/jobs/my-job',
    format := 'harbor',
    owner_email := 'agent@example.com',
    trajectory_destination := 's3://bucket/trajectories/job-id'
);
```

The call reads the job's `config.json` and `result.json`, then the same metadata
pair and `agent/trajectory.json` from each direct trial directory or
`trials/<trial>/` directory. Sources
can also be S3 prefixes when `httpfs` and the appropriate secret are available.
The `agent_runs.jobs` and `agent_runs.trials` tables preserve nested typed
config/result STRUCTs and dynamic VARIANT fields. Their namespace is fixed in
the database instance's default catalog; the caller's current catalog and search
path do not redirect ingestion. See `schema/README.md` for the schema design.

Job and trial UUIDs come from Harbor's result documents. `owner_email` is required.
For jobs with trials, `trajectory_destination` is required. The call converts each
trial's ATIF trajectory to `<trajectory_destination>/<trial UUID>.lance` in
parallel using DuckDB's task executor, then records that path in
`agent_runs.trials.dataset_uri`. Jobs without trials may omit it.

The call returns `job_id`, `job_name`, and `trials_ingested`. Inserts use the
caller's transaction, including explicit `BEGIN`/`ROLLBACK`. Duplicate IDs,
constraint violations, malformed metadata, or conversion failures fail the call
and roll back its inserts. Existing records are never replaced. Lance datasets
are external files and are not removed by a DuckDB transaction rollback.

## Tests

`make test` runs native SQLLogicTests in `test/sql/` against repository-owned
`testdata/` fixtures, including text, gzip, and image/audio trajectories.
Tests call our conversion API and use a separate, pinned `lance` test dependency
to read back and compare the stored values and media bytes with those fixtures.
The reader is built by default; use `BUILD_EXTENSION_TEST_DEPS=none` for builds
without test dependencies. There is no Python integration test runner.

Use `make format-all` to format C++, SQL tests, and CMake files.

## Catalog schema

`LOAD agent_runs` initializes or upgrades the job/trial metadata catalog. It
uses a fixed `agent_runs` schema for its tables, types, macros and version
records. It compares the largest recorded schema version with the latest SQL migration file
and applies only pending versions in one transaction. Existing rows are
preserved; an already up-to-date catalog needs no schema writes. A newer,
unsupported catalog version fails extension loading.

See [schema design and evolution](schema/README.md) for the typed tables,
ml-infra compatibility and upgrade policy.

SQL migrations are runtime resources. Install them with the extension using
CMake, or set `DUCKDB_AGENT_RUNS_MIGRATIONS` to their directory before loading.
Repository Make targets select the checkout's `schema/migrations` automatically.
