# 0.2.0

## Breaking

- The `0.1.0` schema was changed in place rather than by a new migration, so catalogs created by `0.1.0` are not upgraded. Ingest into a fresh database ([#16], [#17]).

## Added

- Job and trial `config.json` and `result.json` are now optional. Missing metadata stays NULL, names fall back to the job directory name or the trial's relative path, and stable UUIDv5 IDs are derived from the canonical source URI, now stored in the new `jobs.source_uri` and `trials.source_uri` columns. `trial_summaries` and `job_summaries` report `metadata_status = 'partial'` for such runs ([#16]).
- Trajectories are discovered recursively, including directly inside a trial and in nested phase directories, not only at `agent/trajectory.json` ([#16]).
- The new `trials.trajectory` column keeps an ATIF summary of each trial: agent, model provider and name, start and end time, token counts, and cost. The summary macros fall back to it when producer results are missing ([#17]).

## Fixed

- Job paths written with `~`, `file://`, `.`, `..`, or a trailing slash now produce the same trial names and IDs as the canonical path ([#19]).
- With `agent_runs_failure_cleanup` enabled, trial datasets are now also removed when `COMMIT` fails, not only when conversion fails or on `ROLLBACK` ([#20]).
- Malformed metadata files fail ingestion instead of being treated as missing ([#21]).
- `job_summaries` and `trial_summaries` now split model names into provider and model with the same rule: only `provider/model` with both parts non-empty ([#24]).
- A local `trajectory_destination` is stored in `trials.dataset_uri` as an absolute, normalized path, so the URI no longer depends on the working directory ([#27]).

## Changed

- Metadata is parsed into DuckDB `VARIANT`, and the extension no longer depends on the `json` extension ([#22]).

## Performance

- Trial metadata is read and summarized in parallel, and each metadata file is parsed once ([#21]).
- Destination existence checks run in parallel, avoiding one sequential round trip per trial on S3 ([#25]).
- `job_summaries` counts trials in its aggregate instead of a per-job subquery ([#26]).

[#16]: https://github.com/dentiny/duckdb_agent_runs/pull/16
[#17]: https://github.com/dentiny/duckdb_agent_runs/pull/17
[#19]: https://github.com/dentiny/duckdb_agent_runs/pull/19
[#20]: https://github.com/dentiny/duckdb_agent_runs/pull/20
[#21]: https://github.com/dentiny/duckdb_agent_runs/pull/21
[#22]: https://github.com/dentiny/duckdb_agent_runs/pull/22
[#24]: https://github.com/dentiny/duckdb_agent_runs/pull/24
[#25]: https://github.com/dentiny/duckdb_agent_runs/pull/25
[#26]: https://github.com/dentiny/duckdb_agent_runs/pull/26
[#27]: https://github.com/dentiny/duckdb_agent_runs/pull/27
