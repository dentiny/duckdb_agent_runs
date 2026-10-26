# Test fixtures

`harbor/` contains static Harbor job directories for artifact upload, missing
metadata, and POSIX symlink rejection. Each complete job contains config/result
metadata and a trial with its own metadata. Binary and empty artifacts exercise
byte-exact copies.

`atif/text/trajectory.json` is a synthetic ATIF v1.8 text fixture copied from
`duckdb_lance_conversion` commit `0c27b7cfa5746c8a3b99cf2712b83cec4588a3ae`.
The upload-only binary artifact is the generated PNG conformance fixture from
the same commit. Tests use repository-owned data without loading submodule test
files or constructing ATIF documents at runtime.
