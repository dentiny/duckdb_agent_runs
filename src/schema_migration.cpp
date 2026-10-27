#include "agent_runs_schema_migrations.hpp"
#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/catalog_entry/table_catalog_entry.hpp"
#include "duckdb/main/connection.hpp"
#include "schema_migration.hpp"

namespace duckdb {
namespace {

//! Execute all statements and surface any failure so the owning transaction rolls back.
void ExecuteMigrationSQL(Connection &connection, const string &sql) {
	auto result = connection.Query(sql);
	for (auto current = result.get(); current; current = current->next.get()) {
		current->ThrowIfError();
	}
}

//! Apply pending SQL and version records within the owning transaction.
void ApplySchemaMigrations(Connection &connection) {
	auto name =
	    QualifiedName(Identifier::InvalidCatalog(), Identifier::DefaultSchema(), Identifier("schema_migrations"));
	if (!Catalog::GetEntry<TableCatalogEntry>(*connection.context, name, OnEntryNotFound::RETURN_NULL)) {
		ExecuteMigrationSQL(connection, "CREATE TABLE schema_migrations "
		                                "(version INTEGER PRIMARY KEY CHECK (version > 0))");
	}
	auto result = connection.Query("SELECT coalesce(max(version), 0) FROM schema_migrations");
	result->ThrowIfError();
	auto applied = result->Fetch()->GetValue(0, 0).GetValue<int32_t>();
	constexpr auto latest = LATEST_SCHEMA_VERSION;
	if (applied > latest) {
		throw InvalidInputException("Catalog schema version %d is newer than supported version %d", applied, latest);
	}
	for (const auto &migration : SCHEMA_MIGRATIONS) {
		if (migration.version <= applied) {
			continue;
		}
		ExecuteMigrationSQL(connection, migration.sql);
		ExecuteMigrationSQL(connection,
		                    StringUtil::Format("INSERT INTO schema_migrations VALUES (%d)", migration.version));
	}
}

} // namespace

void MigrateAgentRunsSchema(DatabaseInstance &database) {
	Connection connection(database);
	connection.BeginTransaction();
	// Connection destruction rolls back the entire batch on failure, including its version records.
	ApplySchemaMigrations(connection);
	connection.Commit();
}

} // namespace duckdb
