# Tests

Run `make release` and `make test`. All tests are native DuckDB SQLLogicTests.
Inputs in `testdata/harbor/` are static fixtures owned by this repository.
Metadata tests initialize the ml-infra-derived `agent_runs` schema with
`LOAD agent_runs` and cover nested types, precise VARIANT values,
caller rollback, constraint rollback, local metadata sources, and parallel
trajectory conversion. Conversion failures roll back metadata inserts.

The multimodal conversion test calls `ingest_agent_run` to write a trial's dataset.
It then reads it with the independent `lance` extension and compares typed
metadata, decoded text, tool calls, and image/audio bytes against those fixtures.
The reader is a test-only dependency pinned in `test/extension_config.cmake`,
including Blob v2 materialization support. `make release` builds it by default;
`BUILD_EXTENSION_TEST_DEPS=none` omits test dependencies from runtime-only builds.
Pass `--require lance` to the native test runner so a missing reader fails
instead of skipping tests.

Writing and verification use separate databases because the two extensions
register the same COPY format. The test writes datasets only through the
`agent_runs` API.
