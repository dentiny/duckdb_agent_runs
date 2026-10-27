# Catalog schema and evolution

`migrations/0001.sql` is the existing Harbor catalog schema copied verbatim from
`ml-infra` commit `d7af6649c51790c78afc1ed4d5af990a1ec634b7`,
`trajectory_data/schema/0001.sql`. The migration runner follows that repository's
`trajectory_data/src/schema.rs` and build-time migration embedding.

```sql
LOAD agent_runs;
CALL migrate_agent_runs(); -- returns schema_version = 1
```

Call this explicitly before ingestion, outside an explicit transaction. It uses
its own connection and transaction against the caller's current database, in the
`main` schema. It does not use the caller's search path. Loading the extension
and binding/preparing the call do not modify the catalog. Repeated calls apply
only pending migrations and preserve existing rows.

All pending SQL and the corresponding `schema_migrations(version INTEGER PRIMARY
KEY CHECK (version > 0))` records commit together. Failure rolls back the batch;
retrying does not require manual cleanup. A database with a newer recorded
version is rejected. Concurrent migration attempts can conflict; retry after the
other caller finishes. The runner never rewrites or drops existing tables to
force initialization to succeed.

The version table and initial SQL match ml-infra, so a catalog already migrated
by that service can be reused. An existing unversioned catalog must be verified
against the initial schema before an administrator explicitly records version 1;
the extension does not infer compatibility or silently adopt unknown tables.

## Table design

- `jobs` stores one native job UUID, unique job name, owner email, typed
  `job_config` and `job_result` STRUCTs.
- `trials` stores native trial UUIDs, their parent job UUID (foreign key), trial
  name, typed `trial_config` and `trial_result`, and the exact trajectory
  `dataset_uri`. Trial names are not globally unique.
- Known producer fields remain typed, including nested STRUCTs, lists, timestamps
  and UUIDs. Dynamic maps and producer-defined payloads use VARIANT to preserve
  mixed JSON types and integer precision.
- CHECK constraints enforce required root fields and basic timestamp/count
  invariants. This projection is not a replacement for Harbor model validation.
- Indexes support owner, time, task, agent, model, source and provider lookups.
  Summary macros provide query projections without duplicating stored data.
- These are job/trial metadata tables. Trajectory steps and media remain in the
  Lance datasets referenced by `dataset_uri`.

Harbor-specific config/result types belong to this first adapter. Supporting a
new producer requires an explicit schema migration and adapter design; it must
not silently cast an unrelated format into Harbor's STRUCTs.

## Adding a migration

Add the next unused four-digit SQL filename, for example `0002.sql`. Applied
files are immutable. CMake embeds all files in numeric order and regenerates the
extension when a file changes or is added. No Python runner or runtime schema
file installation is required.

Use an additive migration where possible: new nullable fields or columns must
leave existing rows readable. For incompatible type changes, explicitly
transform and validate old values and recreate affected indexes, constraints and
summary macros in the same transaction. Named STRUCT definitions, table column
types and query helpers must be upgraded together; changing a type alias alone
must not be assumed to rewrite stored columns. The ingestion adapter must target
the resulting schema version.

Every new migration needs native SQL tests that start with a populated prior
version, verify transformed values and constraints, repeat the migration call,
and exercise a failed upgrade followed by a retry. Test a file-backed database
across reopen as well. Do not add a placeholder migration solely to increment
the version. There is no automatic downgrade; restore a backup to roll back an
incompatible deployed schema.
