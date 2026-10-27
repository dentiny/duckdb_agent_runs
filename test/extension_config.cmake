# Test-only reader with Blob v2 materialization, built against our pinned
# DuckDB.
duckdb_extension_load(
  lance GIT_URL https://github.com/lance-format/lance-duckdb.git GIT_TAG
  004b8d7888d8dbc3005c37e85816e2b6cb729f51)
