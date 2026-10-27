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

CALL convert_agent_trajectory(
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
Job/trial ingestion into typed DuckDB tables is a separate PR.

## Tests

`make test` runs native SQLLogicTests in `test/sql/` against repository-owned
`testdata/` fixtures, including text, gzip, and image/audio trajectories. There
is no Python integration test runner. Optional S3 tests use the variables listed
in `test/README.md`. CI runs them against a disposable RustFS service.

Use `make format-all` to format C++, SQL tests, and CMake files.
