# Tests

Run `make release` and `make test`. All tests are native DuckDB SQLLogicTests.
Inputs in `testdata/atif/` and `testdata/harbor/` are static fixtures owned by
this repository. Metadata tests initialize the ml-infra-derived `agent_runs` schema with
`LOAD agent_runs` and cover nested types, precise VARIANT values,
caller rollback, constraint rollback, and local metadata sources. Deliberately
invalid trajectory files verify that metadata ingestion never reads ATIF.

The conversion test calls only `import_agent_trajectory` to write datasets.
It then reads them with the independent `lance` extension and compares typed
metadata, decoded text, tool calls, and image/audio bytes against those fixtures.
The reader is a test-only dependency pinned in `test/extension_config.cmake`,
including Blob v2 materialization support. `make release` builds it by default;
`BUILD_EXTENSION_TEST_DEPS=none` omits test dependencies from runtime-only builds.
Pass `--require lance` to the native test runner so a missing reader fails
instead of skipping tests.

Writing and verification use separate databases because the two extensions
register the same COPY format. Neither `read_atif` nor the dependency's COPY API
is used by the test to create datasets.
