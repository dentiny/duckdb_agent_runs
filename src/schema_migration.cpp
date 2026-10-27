#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/catalog_entry/table_catalog_entry.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/main/database_manager.hpp"
#include "schema_migration.hpp"

#include <map>

namespace duckdb {
namespace {

//! Read numbered SQL filenames in version order from the installed migration directory.
std::map<int32_t, string> FindSchemaMigrations(FileSystem &fs, const string &directory) {
	std::map<int32_t, string> migrations;
	fs.ListFiles(directory, [&](const string &filename, bool is_directory) {
		if (is_directory || fs.ExtractExtension(filename) != "sql") {
			return;
		}
		auto version = Value(fs.ExtractBaseName(filename)).GetValue<int32_t>();
		if (version <= 0 || !migrations.emplace(version, fs.JoinPath(directory, filename)).second) {
			throw InvalidInputException("Invalid or duplicate schema migration version: %s", filename);
		}
	});
	if (migrations.empty()) {
		throw IOException("No schema migrations found in %s", directory);
	}
	return migrations;
}

//! Execute all statements and surface any failure so the owning transaction rolls back.
void ExecuteMigrationSQL(Connection &connection, const string &sql) {
	auto result = connection.Query(sql);
	for (auto current = result.get(); current; current = current->next.get()) {
		current->ThrowIfError();
	}
}

//! Apply pending SQL and version records within the owning transaction.
void ApplySchemaMigrations(Connection &connection, const string &directory) {
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
	auto migrations = FindSchemaMigrations(FileSystem::GetFileSystem(*connection.context), directory);
	auto latest = migrations.rbegin()->first;
	if (applied > latest) {
		throw InvalidInputException("Catalog schema version %d is newer than supported version %d", applied, latest);
	}
	for (const auto &[version, path] : migrations) {
		if (version <= applied) {
			continue;
		}
		auto sql = connection.Query("SELECT content FROM read_text(?)", Value(path));
		sql->ThrowIfError();
		ExecuteMigrationSQL(connection, sql->Fetch()->GetValue(0, 0).GetValue<string>());
		ExecuteMigrationSQL(connection, StringUtil::Format("INSERT INTO %s VALUES (%d)", name.ToString(), version));
	}
}

} // namespace

QualifiedName GetAgentRunsName(ClientContext &context, std::string_view name) {
	return QualifiedName(DatabaseManager::Get(context).GetDefaultDatabase(), Identifier("agent_runs"),
	                     Identifier(name));
}

void MigrateAgentRunsSchema(DatabaseInstance &database) {
	auto directory = FileSystem::GetEnvVariable("DUCKDB_AGENT_RUNS_MIGRATIONS");
	if (directory.empty()) {
		directory = AGENT_RUNS_MIGRATION_DIRECTORY;
	}
	Connection connection(database);
	connection.BeginTransaction();
	// Connection destruction rolls back the entire batch on failure, including its version records.
	ApplySchemaMigrations(connection, directory);
	connection.Commit();
}

} // namespace duckdb
