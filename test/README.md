# Tests

All tests are DuckDB SQLLogicTests in `sql/`. Run `make release` followed by
`make test`; there is no separate Python test runner.

Read-only fixtures live in `testdata/harbor/` and `testdata/atif/`. Tests cover
byte-exact artifact upload, empty files, duplicate protection, dry-run,
EXPLAIN/PREPARE, argument/path validation, and POSIX symlink rejection.
Outputs use `{TEST_DIR}`, isolated and cleaned up by the DuckDB test runner.
