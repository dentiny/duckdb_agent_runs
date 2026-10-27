#pragma once

namespace duckdb {

class DatabaseInstance;

//! Check the recorded schema version on extension load and atomically apply pending migrations.
void MigrateAgentRunsSchema(DatabaseInstance &database);

} // namespace duckdb
