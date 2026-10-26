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

Use the bundled conversion extension directly:

```sql
LOAD agent_runs;

COPY (SELECT * FROM read_atif('/data/trial/agent/trajectory.json'))
TO 's3://bucket/trajectories/job-id/trial-id.lance'
(FORMAT LANCE, STORAGE_VERSION '2.2', COLUMN_COMPRESSION {
    'steps.message.text': 'lz4',
    'steps.observation.results.content.text': 'lz4'
});
```

The destination is the exact dataset URI supplied to `COPY`. Text codecs are
`lz4`, `zstd`, and `none`. The reader accepts JSON/JSONL and gzip, with local,
S3, or HTTP(S) sources. Relative image/audio references resolve beside the
trajectory and their bytes are embedded in Lance. Configure DuckDB S3 secrets
before reading or writing S3 datasets. Other dataset formats can use the bundled
conversion extension's readers and Lance writer directly as well.

This extension does not copy job directories or upload arbitrary artifacts.
Job/trial ingestion into typed DuckDB tables is a separate PR.

## Tests

`make test` runs native SQLLogicTests in `test/sql/` against repository-owned
`testdata/` fixtures, including text, gzip, and image/audio trajectories. There
is no Python integration test runner. Optional S3 tests use the variables listed
in `test/README.md`. CI runs them against a disposable RustFS service.

Use `make format-all` to format C++, SQL tests, and CMake files.
