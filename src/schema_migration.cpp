#include "agent_runs_embedded_migrations.hpp"
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

struct MigrationSet {
	string schema_sql;
	map<int32_t, string> migrations;
};

//! Read a SQL file as an owning string before its query result is released.
string ReadMigrationSQL(Connection &connection, const string &path) {
	auto result = connection.Query("SELECT content FROM read_text(?)", Value(path));
	result->ThrowIfError();
	return result->Fetch()->GetValue(0, 0).GetValue<string>();
}

//! Read either the SQL bundled in the extension or an explicitly selected directory.
MigrationSet LoadMigrations(Connection &connection, const string &directory) {
	MigrationSet result;
	if (directory.empty()) {
		result.schema_sql = string(AGENT_RUNS_SCHEMA_SQL);
		for (const auto &migration : AGENT_RUNS_MIGRATIONS) {
			auto filename = string(migration.filename);
			auto version = Value(filename.substr(0, filename.size() - 4)).GetValue<int32_t>();
			result.migrations.emplace(version, string(migration.sql));
		}
	} else {
		auto &fs = FileSystem::GetFileSystem(*connection.context);
		result.schema_sql = ReadMigrationSQL(connection, fs.JoinPath(directory, SCHEMA_FILE));
		fs.ListFiles(directory, [&](const string &filename, bool is_directory) {
			if (is_directory || filename == SCHEMA_FILE || fs.ExtractExtension(filename) != "sql") {
				return;
			}
			auto version = Value(fs.ExtractBaseName(filename)).GetValue<int32_t>();
			if (version <= 0 || result.migrations.find(version) != result.migrations.end()) {
				throw InvalidInputException("Invalid or duplicate schema migration version: %s", filename);
			}
			result.migrations.emplace(version, ReadMigrationSQL(connection, fs.JoinPath(directory, filename)));
		});
	}
	if (result.migrations.empty()) {
		throw IOException("No schema migrations found in %s", directory);
	}
	return result;
}

//! Execute all statements and surface any failure so the owning transaction rolls back.
void ExecuteMigrationSQL(Connection &connection, const string &sql) {
	auto result = connection.Query(sql);
	for (auto current = result.get(); current; current = current->next.get()) {
		current->ThrowIfError();
	}
}

//! Apply pending SQL and version records within the owning transaction.
void ApplySchemaMigrations(Connection &connection, const MigrationSet &migration_set) {
	auto name = GetAgentRunsName(*connection.context, "schema_migrations");
	if (!Catalog::GetEntry<TableCatalogEntry>(*connection.context, name, OnEntryNotFound::RETURN_NULL)) {
		ExecuteMigrationSQL(connection, migration_set.schema_sql);
	}
	auto result = connection.Query(StringUtil::Format("SELECT coalesce(max(version), 0) FROM %s", name.ToString()));
	result->ThrowIfError();
	auto applied = result->Fetch()->GetValue(0, 0).GetValue<int32_t>();
	const auto &migrations = migration_set.migrations;
	auto latest = migrations.rbegin()->first;
	if (applied > latest) {
		throw InvalidInputException("Catalog schema version %d is newer than supported version %d", applied, latest);
	}
	for (auto it = migrations.upper_bound(applied); it != migrations.end(); ++it) {
		const auto &[version, sql] = *it;
		ExecuteMigrationSQL(connection, sql);
		auto record =
		    connection.Query(StringUtil::Format("INSERT INTO %s (version, sql) VALUES (?, ?)", name.ToString()),
		                     Value::INTEGER(version), Value(sql));
		record->ThrowIfError();
	}
}

} // namespace

QualifiedName GetAgentRunsName(ClientContext &context, std::string_view name) {
	return QualifiedName(DatabaseManager::Get(context).GetDefaultDatabase(), /*schema=*/Identifier("agent_runs"),
	                     Identifier(name));
}

void MigrateAgentRunsSchema(DatabaseInstance &database) {
	auto directory = FileSystem::GetEnvVariable("DUCKDB_AGENT_RUNS_MIGRATIONS");
	Connection connection(database);
	connection.BeginTransaction();
	// Connection destruction rolls back the entire batch on failure, including its version records.
	ApplySchemaMigrations(connection, LoadMigrations(connection, directory));
	connection.Commit();
}

} // namespace duckdb
