# Catalog schema and evolution

`migrations/schema.sql` creates the `agent_runs` schema and persistent
`schema_migrations(version, sql)` table. `migrations/0001.sql` adapts the Harbor catalog schema from
`ml-infra` commit `d7af6649c51790c78afc1ed4d5af990a1ec634b7`,
`trajectory_data/schema/0001.sql`, qualifying objects and their cross-references
with `agent_runs`. The migration runner follows that repository's
`trajectory_data/src/schema.rs` and numbered SQL files.

```sql
LOAD agent_runs; -- initializes or upgrades the catalog
SELECT max(version) FROM agent_runs.schema_migrations;
```

Extension initialization reads the largest recorded version from
`agent_runs.schema_migrations` and compares it with the latest migration
available in the SQL migration directory. It applies only newer migrations, in order, storing the exact executed SQL alongside each version. A fresh database starts at
version 0; a catalog already at the latest version requires no schema writes.

The check runs in `LoadInternal` before the extension's functions are registered.
It uses a dedicated connection and one transaction against the database
instance's default database, in the fixed `agent_runs` schema. All metadata
tables, types, indexes and summary macros are created there. Neither the
caller's current schema/search path nor an unrelated `main.schema_migrations`
selects the migration namespace. Reopening the database and loading the
extension checks versions again. DuckDB initializes an extension
once per database instance; another `LOAD` after successful initialization is a
no-op. There is no separate migration function to call before ingestion.

All pending SQL and the corresponding version records commit together. Failure
rolls back the batch; retrying does not require manual cleanup. A database with a
newer recorded version fails extension initialization. Concurrent migration
attempts can conflict; retry after the other caller finishes. The runner never rewrites or drops existing tables to
force initialization to succeed.

The job/trial table definitions adapt ml-infra's schema, omitting its
service-specific owner email. This extension isolates the tables in
`agent_runs` rather than the service's `main` schema. The migration ledger
additionally stores the executed SQL. Existing
`main.jobs`, `main.trials` and `main.schema_migrations` are not adopted, moved or
modified. Importing such a catalog requires an explicit data/schema migration;
a version recorded in another schema must not cause extension migrations to be
skipped.

## Table design

- `agent_runs.jobs` stores one native job UUID, unique job name, typed
  `job_config` and `job_result` STRUCTs.
- `agent_runs.trials` stores native trial UUIDs, their parent job UUID (foreign
  key), trial name, typed `trial_config` and `trial_result`, and the exact trajectory
  `dataset_uri`. Trial names are not globally unique.
- Known producer fields remain typed, including nested STRUCTs, lists, timestamps
  and UUIDs. Dynamic maps and producer-defined payloads use VARIANT to preserve
  mixed JSON types and integer precision.
- CHECK constraints enforce required root fields and basic timestamp/count
  invariants. This projection is not a replacement for Harbor model validation.
- Indexes support time, task, agent, model, source and provider lookups.
  Summary macros provide query projections without duplicating stored data.
- These are job/trial metadata tables. Trajectory steps and media remain in the
  Lance datasets referenced by `dataset_uri`.

Harbor-specific config/result types belong to this first adapter. Supporting a
new producer requires an explicit schema migration and adapter design; it must
not silently cast an unrelated format into Harbor's STRUCTs.

## Adding a migration

Add the next unused four-digit SQL filename, for example `0002.sql`. Qualify
metadata objects, custom type names and cross-references with `agent_runs`; do
not depend on the caller's search path or persist unqualified macro references.
Applied files are immutable after deployment. This pre-release baseline changed
while there was no production data; recreate any development database initialized
with the earlier `0001.sql`. The extension reads filenames and SQL at load time,
orders migrations by their numeric filename, and applies versions newer than the
database's recorded maximum. Adding a SQL migration does not require generated
C++ or a rebuild.

CMake installs the SQL files to `<CMAKE_INSTALL_PREFIX>/share/agent_runs/migrations`.
Install them together with the extension; DuckDB's binary-only `INSTALL` command
does not copy these companion files. Set `DUCKDB_AGENT_RUNS_MIGRATIONS` before
loading to use another directory. Repository Make targets set this variable to
`schema/migrations` in the checkout, so development tests do not depend on the
working directory. The compiled default is the configured installation prefix;
use the environment override when relocating an installation.

Open a file-backed DuckDB database to persist the ledger and metadata together
in that one file. The migration connection shares the extension's database
instance and does not attach or create another database. An in-memory DuckDB
database remains in memory. Lance trajectory datasets are external files.

Use an additive migration where possible: new nullable fields or columns must
leave existing rows readable. For incompatible type changes, explicitly
transform and validate old values and recreate affected indexes, constraints and
summary macros in the same transaction. Named STRUCT definitions, table column
types and query helpers must be upgraded together; changing a type alias alone
must not be assumed to rewrite stored columns. The ingestion adapter must target
the resulting schema version.

Every new migration needs native SQL tests that start with a populated prior
version, verify transformed values and constraints, repeat loading across a
file-backed database reopen, and exercise a failed upgrade followed by a retry. Do not add a placeholder migration solely to increment
the version. There is no automatic downgrade; restore a backup to roll back an
incompatible deployed schema.
