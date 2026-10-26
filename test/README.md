# Tests

All tests are DuckDB SQLLogicTests in `sql/`, run with `make test` after
`make release`. There is no separate Python test runner.

Read-only input fixtures live in `testdata/harbor/` and `testdata/atif/`. Local
outputs use `{TEST_DIR}`, which DuckDB isolates and cleans up per test. The
multimodal conversion assertions inspect written Lance fragments for the exact
PNG/WAV bytes referenced by both messages and tool observations.

## Optional S3 test

`sql/ingest_s3.test` uses the same native test runner. It skips unless all four
variables are set:

```sh
export AGENT_RUNS_S3_ENDPOINT=127.0.0.1:9000
export AGENT_RUNS_S3_BUCKET=agent-runs-test
export AGENT_RUNS_S3_KEY_ID=agent_runs_test
export AGENT_RUNS_S3_SECRET=agent_runs_test_password
make test
```

Install `httpfs` for the tested DuckDB version before running this test. The
endpoint must be a disposable S3-compatible service with HTTP and path-style
access, and the bucket must already exist. Each run writes to a random
`agent-runs-<uuid>/` prefix; remove the bucket or prefixes after testing.
The ingestion CI workflow provisions and destroys its own RustFS container.
