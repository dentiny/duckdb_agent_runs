CREATE TYPE retry_config AS STRUCT (
    "preserve_scored_agent_failures"   BOOLEAN,
    "scored_agent_failure_exceptions"  VARCHAR[],
    "max_retries"                      BIGINT,
    "include_exceptions"               VARCHAR[],
    "exclude_exceptions"               VARCHAR[],
    "wait_multiplier"                  DOUBLE,
    "min_wait_sec"                     DOUBLE,
    "max_wait_sec"                     DOUBLE,
    "retry_scored_trials"              BOOLEAN
);

CREATE TYPE tpu_spec AS STRUCT (
    "type"      VARCHAR,
    "topology"  VARCHAR
);

CREATE TYPE service_volume_bind AS STRUCT (
    "create_host_path"  BOOLEAN
);

CREATE TYPE service_volume_volume AS STRUCT (
    "subpath"  VARCHAR
);

CREATE TYPE service_volume_image AS STRUCT (
    "subpath"  VARCHAR
);

CREATE TYPE service_volume_config AS STRUCT (
    "type"       VARCHAR,
    "source"     VARCHAR,
    "target"     VARCHAR,
    "read_only"  BOOLEAN,
    "bind"       service_volume_bind,
    "volume"     service_volume_volume,
    "image"      service_volume_image
);

CREATE TYPE environment_config AS STRUCT (
    "type"                       VARCHAR,
    "import_path"                VARCHAR,
    "force_build"                BOOLEAN,
    "delete"                     BOOLEAN,
    "cpu_enforcement_policy"     VARCHAR,
    "memory_enforcement_policy"  VARCHAR,
    "override_cpus"              BIGINT,
    "override_memory_mb"         BIGINT,
    "override_storage_mb"        BIGINT,
    "override_gpus"              BIGINT,
    "override_tpu"               tpu_spec,
    "mounts"                     service_volume_config[],
    "extra_docker_compose"       VARCHAR[],
    "env"                        MAP(VARCHAR, VARCHAR),
    "kwargs"                     MAP(VARCHAR, VARIANT),
    "extra_allowed_hosts"        VARCHAR[]
);

CREATE TYPE verifier_config AS STRUCT (
    "max_retries"                 BIGINT,
    "transport_retry_budget_sec"  DOUBLE,
    "n_concurrent"                BIGINT,
    "override_timeout_sec"        DOUBLE,
    "min_timeout_sec"             DOUBLE,
    "max_timeout_sec"             DOUBLE,
    "include_logs"                VARCHAR[],
    "exclude_logs"                VARCHAR[],
    "env"                         MAP(VARCHAR, VARCHAR),
    "import_path"                 VARCHAR,
    "kwargs"                      MAP(VARCHAR, VARIANT),
    "disable"                     BOOLEAN,
    "allow_detached"              BOOLEAN
);

CREATE TYPE metric_config AS STRUCT (
    "type"    VARCHAR,
    "kwargs"  MAP(VARCHAR, VARIANT)
);

CREATE TYPE egress_proxy_endpoint AS STRUCT (
    "address"     VARCHAR,
    "admin_port"  BIGINT,
    "ca_pem"      VARCHAR,
    "token_env"   VARCHAR
);

CREATE TYPE pinned_host AS STRUCT (
    "host"     VARCHAR,
    "address"  VARCHAR
);

CREATE TYPE egress_job_extras AS STRUCT (
    "proxy"          egress_proxy_endpoint,
    "hosts"          VARCHAR[],
    "pinned"         pinned_host[],
    "routes"         MAP(VARCHAR, VARCHAR[]),
    "policies_toml"  VARCHAR
);

CREATE TYPE mcp_server_config AS STRUCT (
    "name"                  VARCHAR,
    "transport"             VARCHAR,
    "url"                   VARCHAR,
    "command"               VARCHAR,
    "args"                  VARCHAR[],
    "bearer_token_env_var"  VARCHAR
);

CREATE TYPE agent_config AS STRUCT (
    "name"                        VARCHAR,
    "import_path"                 VARCHAR,
    "model_name"                  VARCHAR,
    "n_concurrent"                BIGINT,
    "concurrency_group"           VARCHAR,
    "skills"                      VARCHAR[],
    "override_timeout_sec"        DOUBLE,
    "override_setup_timeout_sec"  DOUBLE,
    "min_timeout_sec"             DOUBLE,
    "max_timeout_sec"             DOUBLE,
    "timeout_grace_sec"           DOUBLE,
    "resume_trajectory"           BOOLEAN,
    "load_trajectory"             VARCHAR,
    "extra_allowed_hosts"         VARCHAR[],
    "egress_job"                  egress_job_extras,
    "include_logs"                VARCHAR[],
    "exclude_logs"                VARCHAR[],
    "durable_cache_logs"          VARCHAR[],
    "kwargs"                      MAP(VARCHAR, VARIANT),
    "env"                         MAP(VARCHAR, VARCHAR),
    "mcp_servers"                 mcp_server_config[]
);

CREATE TYPE dataset_config AS STRUCT (
    "path"                VARCHAR,
    "name"                VARCHAR,
    "version"             VARCHAR,
    "ref"                 VARCHAR,
    "registry_url"        VARCHAR,
    "registry_path"       VARCHAR,
    "repo"                VARCHAR,
    "overwrite"           BOOLEAN,
    "download_dir"        VARCHAR,
    "task_names"          VARCHAR[],
    "exclude_task_names"  VARCHAR[],
    "n_tasks"             BIGINT
);

CREATE TYPE runtime_addon AS STRUCT (
    "project"  VARCHAR,
    "name"     VARCHAR,
    "version"  VARCHAR
);

CREATE TYPE root_gate AS STRUCT (
    "name"   VARCHAR,
    "check"  VARCHAR
);

CREATE TYPE proxy_release AS STRUCT (
    "version"  VARCHAR,
    "digest"   VARCHAR
);

CREATE TYPE network_allow AS STRUCT (
    "host"     VARCHAR,
    "address"  VARCHAR
);

CREATE TYPE dataset_network AS STRUCT (
    "egress"         VARCHAR,
    "proxy"          proxy_release,
    "profile"        VARCHAR,
    "allow"          network_allow[],
    "local_mirrors"  VARCHAR[]
);

CREATE TYPE dataset_runtime_config AS STRUCT (
    "source"       VARCHAR,
    "version"      VARCHAR,
    "environment"  VARIANT,
    "addons"       runtime_addon[],
    "root_gates"   root_gate[],
    "network"      dataset_network
);

CREATE TYPE task_config AS STRUCT (
    "dataset_runtime"  dataset_runtime_config,
    "dataset_network"  dataset_network,
    "dataset_ref"      VARCHAR,
    "path"             VARCHAR,
    "git_url"          VARCHAR,
    "git_commit_id"    VARCHAR,
    "name"             VARCHAR,
    "ref"              VARCHAR,
    "overwrite"        BOOLEAN,
    "download_dir"     VARCHAR,
    "source"           VARCHAR
);

CREATE TYPE source_job_config AS STRUCT (
    "action"  VARCHAR,
    "type"    VARCHAR,
    "job_id"  UUID,
    "path"    VARCHAR
);

CREATE TYPE job_config AS STRUCT (
    "jobs_dir"                              VARCHAR,
    "n_attempts"                            BIGINT,
    "install_only"                          BOOLEAN,
    "timeout_multiplier"                    DOUBLE,
    "agent_timeout_multiplier"              DOUBLE,
    "verifier_timeout_multiplier"           DOUBLE,
    "agent_setup_timeout_multiplier"        DOUBLE,
    "environment_build_timeout_multiplier"  DOUBLE,
    "debug"                                 BOOLEAN,
    "n_concurrent_trials"                   BIGINT,
    "n_concurrent_prepare"                  BIGINT,
    "quiet"                                 BOOLEAN,
    "retry"                                 retry_config,
    "environment"                           environment_config,
    "verifier"                              verifier_config,
    "metrics"                               metric_config[],
    "agents"                                agent_config[],
    "datasets"                              dataset_config[],
    "tasks"                                 task_config[],
    "artifacts"                             VARIANT[],
    "metadata"                              MAP(VARCHAR, VARIANT),
    "extra_instruction_paths"               VARCHAR[],
    "source_jobs"                           source_job_config[]
);

CREATE TYPE agent_dataset_stats AS STRUCT (
    "n_trials"         BIGINT,
    "n_errors"         BIGINT,
    "metrics"          MAP(VARCHAR, VARIANT)[],
    "pass_at_k"        MAP(BIGINT, DOUBLE),
    "reward_stats"     MAP(VARCHAR, MAP(VARCHAR, VARCHAR[])),
    "exception_stats"  MAP(VARCHAR, VARCHAR[])
);

CREATE TYPE job_stats AS STRUCT (
    "n_completed_trials"  BIGINT,
    "n_errored_trials"    BIGINT,
    "n_running_trials"    BIGINT,
    "n_pending_trials"    BIGINT,
    "n_cancelled_trials"  BIGINT,
    "n_retries"           BIGINT,
    "evals"               MAP(VARCHAR, agent_dataset_stats),
    "n_input_tokens"      BIGINT,
    "n_cache_tokens"      BIGINT,
    "n_output_tokens"     BIGINT,
    "cost_usd"            DOUBLE
);

CREATE TYPE job_result AS STRUCT (
    "started_at"      TIMESTAMPTZ,
    "updated_at"      TIMESTAMPTZ,
    "finished_at"     TIMESTAMPTZ,
    "n_total_trials"  BIGINT,
    "stats"           job_stats
);

CREATE TYPE source_trial_config AS STRUCT (
    "action"    VARCHAR,
    "type"      VARCHAR,
    "trial_id"  UUID,
    "path"      VARCHAR
);

CREATE TYPE trial_config AS STRUCT (
    "task"                                  task_config,
    "trials_dir"                            VARCHAR,
    "install_only"                          BOOLEAN,
    "timeout_multiplier"                    DOUBLE,
    "agent_timeout_multiplier"              DOUBLE,
    "verifier_timeout_multiplier"           DOUBLE,
    "agent_setup_timeout_multiplier"        DOUBLE,
    "environment_build_timeout_multiplier"  DOUBLE,
    "agent"                                 agent_config,
    "environment"                           environment_config,
    "verifier"                              verifier_config,
    "artifacts"                             VARIANT[],
    "extra_instruction_paths"               VARCHAR[],
    "source_trial"                          source_trial_config
);

CREATE TYPE model_info AS STRUCT (
    "name"      VARCHAR,
    "provider"  VARCHAR
);

CREATE TYPE agent_info AS STRUCT (
    "name"        VARCHAR,
    "version"     VARCHAR,
    "model_info"  model_info
);

CREATE TYPE rollout_detail AS STRUCT (
    "prompt_token_ids"      BIGINT[][],
    "completion_token_ids"  BIGINT[][],
    "logprobs"              DOUBLE[][],
    "extra"                 MAP(VARCHAR, VARIANT[])
);

CREATE TYPE execution_error AS STRUCT (
    "schema_version"  VARCHAR,
    "error_id"        VARCHAR,
    "code"            VARCHAR,
    "message"         VARCHAR,
    "source"          VARCHAR,
    "tool_name"       VARCHAR,
    "execution_id"    VARCHAR,
    "tool_call_id"    VARCHAR,
    "inference_id"    VARCHAR,
    "session_id"      VARCHAR,
    "evidence"        MAP(VARCHAR, VARIANT)
);

CREATE TYPE agent_context AS STRUCT (
    "n_input_tokens"    BIGINT,
    "n_cache_tokens"    BIGINT,
    "n_output_tokens"   BIGINT,
    "cost_usd"          DOUBLE,
    "rollout_details"   rollout_detail[],
    "metadata"          MAP(VARCHAR, VARIANT),
    "execution_errors"  execution_error[]
);

CREATE TYPE verifier_result AS STRUCT (
    "rewards"  MAP(VARCHAR, VARIANT)
);

CREATE TYPE exception_info AS STRUCT (
    "exception_type"       VARCHAR,
    "exception_message"    VARCHAR,
    "exception_traceback"  VARCHAR,
    "occurred_at"          TIMESTAMPTZ
);

CREATE TYPE timing_info AS STRUCT (
    "started_at"   TIMESTAMPTZ,
    "finished_at"  TIMESTAMPTZ
);

CREATE TYPE step_result AS STRUCT (
    "step_name"        VARCHAR,
    "agent_result"     agent_context,
    "verifier_result"  verifier_result,
    "exception_info"   exception_info,
    "agent_execution"  timing_info,
    "verifier"         timing_info
);

CREATE TYPE trial_result AS STRUCT (
    "task_name"                  VARCHAR,
    "trial_uri"                  VARCHAR,
    "task_id"                    VARIANT,
    "source"                     VARCHAR,
    "task_checksum"              VARCHAR,
    "trial_lock_digest"          VARCHAR,
    "agent_info"                 agent_info,
    "agent_result"               agent_context,
    "verifier_result"            verifier_result,
    "verifier_environment_mode"  VARCHAR,
    "exception_info"             exception_info,
    "started_at"                 TIMESTAMPTZ,
    "finished_at"                TIMESTAMPTZ,
    "environment_setup"          timing_info,
    "agent_setup"                timing_info,
    "agent_execution"            timing_info,
    "verifier"                   timing_info,
    "step_results"               step_result[],
    "surface_lint"               MAP(VARCHAR, VARIANT),
    "teardown_incomplete"        BOOLEAN,
    "teardown_errors"            VARCHAR[]
);

-- Table constraints enforce catalog identity and required root fields.
-- Validate native defaults, nested collection members and model validators with
-- the producer's Harbor models before projecting into these normalized STRUCTs.
-- struct_extract is needed here: dotted STRUCT access is not bound in CHECKs.
CREATE TABLE jobs (
    job_id       UUID PRIMARY KEY,
    job_name     VARCHAR NOT NULL UNIQUE CHECK (regexp_matches(job_name, '\S')),
    owner_email  VARCHAR NOT NULL CHECK (regexp_matches(owner_email, '\S')),
    config       job_config NOT NULL,
    result       job_result NOT NULL,
    CHECK (
        struct_extract(config, 'jobs_dir') IS NOT NULL
        AND struct_extract(config, 'n_attempts') IS NOT NULL
        AND struct_extract(config, 'install_only') IS NOT NULL
        AND struct_extract(config, 'timeout_multiplier') IS NOT NULL
        AND struct_extract(config, 'debug') IS NOT NULL
        AND struct_extract(config, 'n_concurrent_trials') IS NOT NULL
        AND struct_extract(config, 'quiet') IS NOT NULL
        AND struct_extract(config, 'retry') IS NOT NULL
        AND struct_extract(config, 'environment') IS NOT NULL
        AND struct_extract(config, 'verifier') IS NOT NULL
        AND struct_extract(config, 'metrics') IS NOT NULL
        AND struct_extract(config, 'agents') IS NOT NULL
        AND struct_extract(config, 'datasets') IS NOT NULL
        AND struct_extract(config, 'tasks') IS NOT NULL
        AND struct_extract(config, 'artifacts') IS NOT NULL
        AND struct_extract(config, 'metadata') IS NOT NULL
        AND struct_extract(config, 'extra_instruction_paths') IS NOT NULL
        AND struct_extract(config, 'source_jobs') IS NOT NULL
        AND struct_extract(config, 'n_concurrent_trials') >= 1
        AND (struct_extract(config, 'n_concurrent_prepare') IS NULL OR struct_extract(config, 'n_concurrent_prepare') >= 1)
    ),
    CHECK (
        struct_extract(result, 'started_at') IS NOT NULL
        AND struct_extract(result, 'n_total_trials') IS NOT NULL
        AND struct_extract(result, 'stats') IS NOT NULL
        AND struct_extract(result, 'finished_at') IS NOT NULL
        AND struct_extract(result, 'finished_at') >= struct_extract(result, 'started_at')
        AND struct_extract(result, 'n_total_trials') >= 0
    )
);

CREATE TABLE trials (
    trial_id     UUID PRIMARY KEY,
    job_id       UUID NOT NULL REFERENCES jobs(job_id),
    trial_name   VARCHAR NOT NULL CHECK (regexp_matches(trial_name, '\S')),
    config       trial_config NOT NULL,
    result       trial_result NOT NULL,
    dataset_uri  VARCHAR NOT NULL CHECK (regexp_matches(dataset_uri, '\S')),
    CHECK (
        struct_extract(config, 'task') IS NOT NULL
        AND struct_extract(config, 'trials_dir') IS NOT NULL
        AND struct_extract(config, 'install_only') IS NOT NULL
        AND struct_extract(config, 'timeout_multiplier') IS NOT NULL
        AND struct_extract(config, 'agent') IS NOT NULL
        AND struct_extract(config, 'environment') IS NOT NULL
        AND struct_extract(config, 'verifier') IS NOT NULL
        AND struct_extract(config, 'artifacts') IS NOT NULL
        AND struct_extract(config, 'extra_instruction_paths') IS NOT NULL
    ),
    CHECK (
        struct_extract(result, 'task_name') IS NOT NULL
        AND struct_extract(result, 'trial_uri') IS NOT NULL
        AND struct_extract(result, 'task_id') IS NOT NULL
        AND struct_extract(result, 'task_checksum') IS NOT NULL
        AND struct_extract(result, 'agent_info') IS NOT NULL
        AND struct_extract(result, 'teardown_incomplete') IS NOT NULL
        AND regexp_matches(struct_extract(result, 'task_name'), '\S')
        AND struct_extract(struct_extract(result, 'agent_info'), 'name') IS NOT NULL
        AND struct_extract(struct_extract(result, 'agent_info'), 'version') IS NOT NULL
        AND (struct_extract(result, 'started_at') IS NULL OR struct_extract(result, 'finished_at') IS NULL OR struct_extract(result, 'finished_at') >= struct_extract(result, 'started_at'))
    )
);

CREATE MACRO trial_agent_name(config, result) AS
    CASE WHEN nullif(config.agent.import_path, '') IS NOT NULL
         THEN coalesce(nullif(config.agent.name, ''), config.agent.import_path)
         ELSE result.agent_info.name END;

CREATE INDEX jobs_owner_email_idx ON jobs (owner_email);
CREATE INDEX jobs_started_at_idx ON jobs ((result.started_at));

CREATE INDEX trials_task_name_idx ON trials ((result.task_name));
CREATE INDEX trials_agent_name_idx ON trials ((trial_agent_name(config, result)));
CREATE INDEX trials_model_name_idx ON trials ((result.agent_info.model_info.name));
CREATE INDEX trials_started_at_idx ON trials ((result.started_at));
CREATE INDEX trials_source_idx ON trials ((result.source));
CREATE INDEX trials_model_provider_idx ON trials ((result.agent_info.model_info.provider));

CREATE MACRO job_summaries(relation) AS TABLE
SELECT
    job_id, job_name, owner_email,
    result.started_at AS started_at,
    result.finished_at AS finished_at,
    result.n_total_trials AS n_total_trials,
    coalesce(result.stats.n_completed_trials, 0) AS n_completed_trials,
    coalesce(result.stats.n_errored_trials, 0) AS n_errored_trials,
    result.stats.cost_usd AS cost_usd,
    CASE WHEN result.stats.n_input_tokens IS NOT NULL
         THEN greatest(0, result.stats.n_input_tokens - coalesce(result.stats.n_cache_tokens, 0))
    END AS total_input_tokens,
    result.stats.n_cache_tokens AS total_cached_input_tokens,
    result.stats.n_output_tokens AS total_output_tokens,
    config.environment.type AS environment_type,
    list_sort(list_distinct(list_transform(config.agents, lambda a: a.name))) AS agents,
    list_sort(list_distinct(list_transform(config.agents, lambda a:
        CASE WHEN strpos(a.model_name, '/') > 0 THEN split_part(a.model_name, '/', 1) END
    ))) AS providers,
    list_sort(list_distinct(list_transform(config.agents, lambda a:
        regexp_replace(a.model_name, '^[^/]*/', '')
    ))) AS models,
    list_sort(list_distinct(list_concat(
        list_transform(config.datasets, lambda d: CASE WHEN d.path IS NOT NULL
            THEN coalesce(nullif(regexp_extract(rtrim(d.path, '/'), '[^/]+$'), ''), d.path)
            ELSE d.name END),
        list_transform(config.tasks, lambda t: coalesce(t.name,
            nullif(regexp_extract(rtrim(t.path, '/'), '[^/]+$'), ''), t.path))
    ))) AS datasets
FROM query_table(relation);

CREATE MACRO trial_summaries(relation) AS TABLE
WITH contexts AS (
    SELECT *, CASE WHEN result.agent_result IS NOT NULL
        THEN [result.agent_result]
        ELSE list_transform(result.step_results, lambda s: s.agent_result) END AS contexts
    FROM query_table(relation)
), totals AS (
    SELECT *,
        list_sum(list_transform(contexts, lambda c: c.n_input_tokens)) AS n_input,
        list_sum(list_transform(contexts, lambda c: c.n_cache_tokens)) AS n_cache,
        list_sum(list_transform(contexts, lambda c: c.n_output_tokens)) AS n_output,
        list_sum(list_transform(contexts, lambda c: c.cost_usd)) AS cost
    FROM contexts
)
SELECT
    t.trial_id, t.job_id, j.job_name, j.owner_email, t.trial_name,
    t.result.task_name AS task_name,
    t.result.source AS source,
    trial_agent_name(t.config, t.result) AS agent_name,
    t.result.agent_info.model_info.provider AS model_provider,
    t.result.agent_info.model_info.name AS model_name,
    t.result.started_at AS started_at,
    t.result.finished_at AS finished_at,
    t.result.verifier_result.rewards AS rewards,
    CASE WHEN isfinite(try_cast(t.result.verifier_result.rewards['reward'] AS DOUBLE))
         THEN try_cast(t.result.verifier_result.rewards['reward'] AS DOUBLE) END AS reward,
    t.result.exception_info.exception_type AS exception_type,
    t.result.exception_info IS NOT NULL AS has_error,
    CASE WHEN t.n_input IS NOT NULL
         THEN greatest(0, t.n_input - coalesce(t.n_cache, 0)) END AS input_tokens,
    t.n_cache AS cached_input_tokens,
    t.n_output AS output_tokens,
    t.cost AS cost_usd,
    t.n_input AS total_input_tokens,
    t.dataset_uri
FROM totals AS t
JOIN jobs AS j ON j.job_id = t.job_id;
