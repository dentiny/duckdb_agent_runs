#include "agent_runs_embedded_migrations.hpp"
#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/catalog_entry/table_catalog_entry.hpp"
#include "duckdb/common/map.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/main/database_manager.hpp"
#include "schema_migration.hpp"

namespace duckdb {
namespace {

//! Order the bundled migration SQL by version without copying its contents.
map<int32_t, std::string_view> GetSchemaMigrations() {
	map<int32_t, std::string_view> migrations;
	for (const auto &migration : AGENT_RUNS_MIGRATIONS) {
		auto version = Value(string(migration.filename.substr(0, migration.filename.size() - 4))).GetValue<int32_t>();
		migrations.emplace(version, migration.sql);
	}
	return migrations;
}

//! Execute all statements and surface any failure so the owning transaction rolls back.
void ExecuteMigrationSQL(Connection &connection, std::string_view sql) {
	auto result = connection.Query(string(sql));
	for (auto current = result.get(); current; current = current->next.get()) {
		current->ThrowIfError();
	}
}

//! Apply pending SQL and version records within the owning transaction.
void ApplySchemaMigrations(Connection &connection) {
	auto name = GetAgentRunsName(*connection.context, "schema_migrations");
	if (!Catalog::GetEntry<TableCatalogEntry>(*connection.context, name, OnEntryNotFound::RETURN_NULL)) {
		ExecuteMigrationSQL(connection, AGENT_RUNS_SCHEMA_SQL);
	}
	auto result = connection.Query(StringUtil::Format("SELECT coalesce(max(version), 0) FROM %s", name.ToString()));
	result->ThrowIfError();
	auto applied = result->Fetch()->GetValue(0, 0).GetValue<int32_t>();
	auto migrations = GetSchemaMigrations();
	auto latest = migrations.rbegin()->first;
	if (applied > latest) {
		throw InvalidInputException("Catalog schema version %d is newer than supported version %d", applied, latest);
	}
	for (auto it = migrations.upper_bound(applied); it != migrations.end(); ++it) {
		const auto &[version, sql] = *it;
		ExecuteMigrationSQL(connection, sql);
		auto record =
		    connection.Query(StringUtil::Format("INSERT INTO %s (version, sql) VALUES (?, ?)", name.ToString()),
		                     Value::INTEGER(version), Value(string(sql)));
		record->ThrowIfError();
	}
}

} // namespace

QualifiedName GetAgentRunsName(ClientContext &context, std::string_view name) {
	return QualifiedName(DatabaseManager::Get(context).GetDefaultDatabase(), /*schema=*/Identifier("agent_runs"),
	                     Identifier(name));
}

void MigrateAgentRunsSchema(DatabaseInstance &database) {
	Connection connection(database);
	connection.BeginTransaction();
	// Connection destruction rolls back the entire batch on failure, including its version records.
	ApplySchemaMigrations(connection);
	connection.Commit();
}

} // namespace duckdb
