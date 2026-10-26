# Tests

SQLLogicTest smoke tests live in `sql/` and run with `make test`.
The Python integration suite in `integration/` exercises complete ingestion through
the built DuckDB CLI. From the repository root, run:

```sh
python3 test/integration/test_ingest.py -v
```

Use `--duckdb` and `--extension` to test alternate build locations. Integration tests
use temporary directories and do not write to cloud storage. Additional optional
S3 test instructions are in the repository README.
