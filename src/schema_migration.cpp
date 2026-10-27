#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/catalog_entry/table_catalog_entry.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/main/database_manager.hpp"
#include "schema_migration.hpp"

#include <iterator>

namespace duckdb {
namespace {

struct SchemaMigration {
	int version;
	const char *sql;
};

constexpr SchemaMigration SCHEMA_MIGRATIONS[] = {
#include "agent_runs_schema_migrations.inc"
};

//! Execute all statements and surface any failure so the owning transaction rolls back.
void ExecuteMigrationSQL(Connection &connection, const string &sql) {
	auto result = connection.Query(sql);
	for (auto current = result.get(); current; current = current->next.get()) {
		current->ThrowIfError();
	}
}

//! Apply pending SQL and version records within the owning transaction.
void ApplySchemaMigrations(Connection &connection) {
	auto name = GetAgentRunsName(*connection.context, "schema_migrations");
	auto &catalog = Catalog::GetCatalog(*connection.context, name.Catalog());
	if (!catalog.GetSchema(*connection.context, name.Schema(), OnEntryNotFound::RETURN_NULL)) {
		ExecuteMigrationSQL(connection, StringUtil::Format("CREATE SCHEMA %s", name.Parent().ToString()));
	}
	if (!Catalog::GetEntry<TableCatalogEntry>(*connection.context, name, OnEntryNotFound::RETURN_NULL)) {
		ExecuteMigrationSQL(connection, StringUtil::Format("CREATE TABLE %s "
		                                                   "(version INTEGER PRIMARY KEY CHECK (version > 0))",
		                                                   name.ToString()));
	}
	auto result = connection.Query(StringUtil::Format("SELECT coalesce(max(version), 0) FROM %s", name.ToString()));
	result->ThrowIfError();
	auto applied = result->Fetch()->GetValue(0, 0).GetValue<int32_t>();
	constexpr auto latest = SCHEMA_MIGRATIONS[std::size(SCHEMA_MIGRATIONS) - 1].version;
	if (applied > latest) {
		throw InvalidInputException("Catalog schema version %d is newer than supported version %d", applied, latest);
	}
	for (const auto &migration : SCHEMA_MIGRATIONS) {
		if (migration.version <= applied) {
			continue;
		}
		ExecuteMigrationSQL(connection, migration.sql);
		ExecuteMigrationSQL(connection,
		                    StringUtil::Format("INSERT INTO %s VALUES (%d)", name.ToString(), migration.version));
	}
}

} // namespace

QualifiedName GetAgentRunsName(ClientContext &context, const char *name) {
	return QualifiedName(DatabaseManager::Get(context).GetDefaultDatabase(), Identifier("agent_runs"),
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
