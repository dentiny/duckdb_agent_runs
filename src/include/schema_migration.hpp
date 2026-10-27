#pragma once

namespace duckdb {

class ClientContext;
class DatabaseInstance;
struct QualifiedName;

//! Qualify metadata in the database instance's default catalog and fixed agent_runs schema.
QualifiedName GetAgentRunsName(ClientContext &context, const char *name);

//! Check the recorded schema version on extension load and atomically apply pending migrations.
void MigrateAgentRunsSchema(DatabaseInstance &database);

} // namespace duckdb
