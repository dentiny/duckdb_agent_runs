# Tests

Run `make release` and `make test`. All tests are native DuckDB SQLLogicTests.
Inputs in `testdata/atif/` are static fixtures owned by this repository.

The optional S3 test requires `httpfs` and these environment variables:

```sh
export AGENT_RUNS_S3_ENDPOINT=127.0.0.1:9000
export AGENT_RUNS_S3_BUCKET=agent-runs-test
export AGENT_RUNS_S3_KEY_ID=agent_runs_test
export AGENT_RUNS_S3_SECRET=agent_runs_test_password
make test
```

Use a disposable HTTP/path-style S3 service and an existing bucket. Tests write
under a random prefix. CI creates and removes its own service and bucket.
