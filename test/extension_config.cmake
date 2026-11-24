# Test-only reader with Blob v2 materialization, built against our pinned
# DuckDB.
duckdb_extension_load(
  lance GIT_URL https://github.com/lance-format/lance-duckdb.git GIT_TAG
  daa5b46291543c42428866989e2704285e9cb8a6)
