# Tests

Run `make release` and `make test`. All tests are native DuckDB SQLLogicTests.
Inputs in `testdata/atif/` are static fixtures owned by this repository.

The conversion test calls only `convert_agent_trajectory` to write datasets.
It then reads them with the independent `lance` extension and compares typed
metadata, decoded text, tool calls, and image/audio bytes against those fixtures.
The reader is a test-only dependency pinned in `test/extension_config.cmake`,
including Blob v2 materialization support. `make release` builds it by default;
`BUILD_EXTENSION_TEST_DEPS=none` omits test dependencies from runtime-only builds.
CI passes `--require lance` so a missing reader fails instead of skipping tests.

Writing and verification use separate databases because the two extensions
register the same COPY format. Neither `read_atif` nor the dependency's COPY API
is used by the test to create datasets.

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
