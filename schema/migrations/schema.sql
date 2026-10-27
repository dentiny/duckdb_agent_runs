CREATE SCHEMA IF NOT EXISTS agent_runs;

CREATE TABLE agent_runs.schema_migrations (
    version INTEGER PRIMARY KEY CHECK (version > 0),
    sql VARCHAR NOT NULL
);
