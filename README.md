# agent_runs

`agent_runs` imports ATIF (Agent Trajectory Interchange Format) trajectories
into Lance and stores Harbor job/trial metadata in DuckDB. It provides two SQL
calls:

- `ingest_agent_run` reads a Harbor job, converts each trial's trajectory, and
  inserts the job and trials into `agent_runs.jobs` and `agent_runs.trials`.
- `import_agent_trajectory` converts one ATIF trajectory to a Lance dataset
  without inserting job/trial metadata.

## Ingest a Harbor job

The job directory needs `config.json` and `result.json`. Each trial needs its
own metadata pair and `agent/trajectory.json`. Trials can be directly under
the job directory, as shown here with multimodal data, or under a `trials/`
directory.

```text
my-job/
├── config.json
├── result.json
└── task__abc/
    ├── config.json
    ├── result.json
    └── agent/
        ├── trajectory.json
        ├── atif-image.png
        └── atif-audio.wav
```

Media files are needed only when the trajectory references them. ATIF media
paths are relative to `trajectory.json`; ingestion embeds their bytes in the
Lance dataset.

Open a file-backed DuckDB database if you want to keep the metadata between
sessions. Loading the extension creates or updates its tables in that database.

```sh
duckdb agent_runs.duckdb
```

```sql
FORCE INSTALL agent_runs FROM community;
LOAD agent_runs;

CALL ingest_agent_run(
    '/data/jobs/my-job',
    owner_email := 'agent@example.com',
    trajectory_destination := '/data/datasets/my-job'
);
```

The call returns `job_id`, `job_name`, and `trials_ingested`. It writes each
trial's Lance dataset to `<trajectory_destination>/<trial UUID>.lance` and saves that URI in
`agent_runs.trials.dataset_uri`. Conversion of different trials runs in
parallel. You can query the persisted metadata immediately:

```sql
SELECT job_id, job_name, owner_email FROM agent_runs.jobs;
SELECT trial_id, trial_name, dataset_uri FROM agent_runs.trials;
SELECT job_name, config.n_attempts, result.stats.n_completed_trials
FROM agent_runs.jobs;
```

Known metadata fields remain typed, including nested fields like those in
the last query.
Use a Lance reader to query the datasets at those URIs; `agent_runs` writes
Lance datasets but does not provide a Lance dataset reader.

`owner_email` is required. `trajectory_destination` is required when the job
has trials; a job without trials can omit it. `format := 'harbor'` is optional
and is the only supported format today. Job and trial UUIDs come from the
Harbor result files. Existing rows are not replaced, so importing the same
job again fails on its unique ID. The extension reads metadata and trajectories;
it does not copy other job files, logs, or artifacts.

## Schema management

`LOAD agent_runs` creates the fixed `agent_runs` schema and its jobs/trials tables
in the default DuckDB database on first load. On later loads, it compares the
recorded version with the available numbered SQL migrations and applies only
newer versions. No separate migration command is needed. The version and the
SQL that was applied are stored in `agent_runs.schema_migrations`:

```sql
SELECT version FROM agent_runs.schema_migrations ORDER BY version;
```

The tables and migration history persist together in a file-backed DuckDB
database; Lance datasets remain at their external destinations. Migration SQL
files must be available when the extension loads. If they are outside its
configured install directory, set `DUCKDB_AGENT_RUNS_MIGRATIONS` to their
directory before starting DuckDB. A failed migration rolls back as a unit.
See [schema design and evolution](schema/README.md) for the table layout and
migration policy.

## Import one ATIF trajectory

Use this call when you only need a Lance dataset:

```sql
LOAD agent_runs;

CALL import_agent_trajectory(
    '/data/trial/agent/trajectory.json',
    trajectory_destination := '/data/datasets/trial.lance'
);
```

`trajectory_destination` names the complete `.lance` dataset, not a parent
directory. The call returns `trajectories_converted`; it does not add a job or
trial row. ATIF input can be JSON, JSONL, or gzip-compressed, from local
storage, S3, or HTTP(S). Relative image and audio references are resolved
beside the trajectory and embedded in Lance. `text_compression` defaults to
`'lz4'`; `'zstd'` and `'none'` are also accepted.

## Storage and failures

For S3 sources or destinations, configure the appropriate DuckDB secret and
load `httpfs` when required. A Harbor job source may be a local directory or
an S3 prefix. The Lance datasets live outside the DuckDB database file.

If a trial conversion fails during `ingest_agent_run`, its job/trial inserts
are rolled back. By default, the extension also waits for started conversions
to stop and removes files under this call's dataset destinations, even if no
valid Lance dataset was committed. With cleanup enabled, it refuses to start
when a destination already contains files, so cleanup cannot remove a
preexisting dataset. A
deletion error is returned to the caller; datasets not yet deleted may remain.

If the destination credentials lack delete permission, disable cleanup before
ingestion:

```sql
SET agent_runs_failure_cleanup = false;
```

With cleanup disabled, a failed call can leave partial datasets. This setting
applies to `ingest_agent_run`, not to standalone `import_agent_trajectory`.
An explicit `ROLLBACK` after a successful ingestion rolls back its DuckDB
metadata inserts, but does not delete the external Lance datasets.
