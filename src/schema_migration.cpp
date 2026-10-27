#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/catalog_entry/table_catalog_entry.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/common/map.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/main/database_manager.hpp"
#include "schema_migration.hpp"

namespace duckdb {
namespace {

constexpr const char *SCHEMA_FILE = "schema.sql";

//! Read numbered SQL filenames in version order from the installed migration directory.
map<int32_t, string> FindSchemaMigrations(FileSystem &fs, const string &directory) {
	map<int32_t, string> migrations;
	fs.ListFiles(directory, [&](const string &filename, bool is_directory) {
		if (is_directory || filename == SCHEMA_FILE || fs.ExtractExtension(filename) != "sql") {
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

//! Read a SQL file as an owning string before its query result is released.
string ReadMigrationSQL(Connection &connection, const string &path) {
	auto result = connection.Query("SELECT content FROM read_text(?)", Value(path));
	result->ThrowIfError();
	return result->Fetch()->GetValue(0, 0).GetValue<string>();
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
	if (!Catalog::GetEntry<TableCatalogEntry>(*connection.context, name, OnEntryNotFound::RETURN_NULL)) {
		auto path = FileSystem::GetFileSystem(*connection.context).JoinPath(directory, SCHEMA_FILE);
		ExecuteMigrationSQL(connection, ReadMigrationSQL(connection, path));
	}
	auto result = connection.Query(StringUtil::Format("SELECT coalesce(max(version), 0) FROM %s", name.ToString()));
	result->ThrowIfError();
	auto applied = result->Fetch()->GetValue(0, 0).GetValue<int32_t>();
	auto migrations = FindSchemaMigrations(FileSystem::GetFileSystem(*connection.context), directory);
	auto latest = migrations.rbegin()->first;
	if (applied > latest) {
		throw InvalidInputException("Catalog schema version %d is newer than supported version %d", applied, latest);
	}
	for (auto it = migrations.upper_bound(applied); it != migrations.end(); ++it) {
		const auto &[version, path] = *it;
		auto sql = ReadMigrationSQL(connection, path);
		ExecuteMigrationSQL(connection, sql);
		auto record =
		    connection.Query(StringUtil::Format("INSERT INTO %s (version, sql) VALUES (?, ?)", name.ToString()),
		                     Value::INTEGER(version), Value(sql));
		record->ThrowIfError();
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
