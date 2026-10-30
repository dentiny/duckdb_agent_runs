-- Internal miles publication metadata is separate from native Harbor config/result.
-- Trials inherit publication metadata through agent_runs.trials.job_id = agent_runs.job_publications.job_id.
CREATE TABLE agent_runs.job_publications (
    job_id UUID PRIMARY KEY REFERENCES agent_runs.jobs(job_id),
    producer_job_id UUID NOT NULL,
    source_uri VARCHAR NOT NULL,
    manifest JSON NOT NULL,
    status_snapshot JSON,
    owner_email VARCHAR,
    wandb_run_id VARCHAR,
    wandb_url VARCHAR,
    completed_trials BIGINT CHECK (completed_trials >= 0),
    total_trials BIGINT CHECK (total_trials >= 0),
    imported_at TIMESTAMPTZ NOT NULL DEFAULT current_timestamp,
    CHECK (completed_trials <= total_trials)
);

CREATE VIEW agent_runs.jobs_with_publications AS
SELECT j.*, p.owner_email
FROM agent_runs.jobs j LEFT JOIN agent_runs.job_publications p USING (job_id);

CREATE MACRO agent_runs.published_job_summaries(relation) AS TABLE
SELECT s.* EXCLUDE (n_completed_trials, n_total_trials), p.owner_email,
    CASE WHEN p.completed_trials IS NOT NULL OR p.total_trials IS NOT NULL THEN p.completed_trials ELSE s.n_completed_trials END AS n_completed_trials,
    CASE WHEN p.completed_trials IS NOT NULL OR p.total_trials IS NOT NULL THEN p.total_trials ELSE s.n_total_trials END AS n_total_trials,
   p.wandb_run_id, p.wandb_url,
   CASE WHEN p.completed_trials IS NOT NULL OR p.total_trials IS NOT NULL
        THEN 'publication' ELSE 'job' END AS progress_source
FROM agent_runs.job_summaries(relation) s LEFT JOIN agent_runs.job_publications p USING (job_id);
