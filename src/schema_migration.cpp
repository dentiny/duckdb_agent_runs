#include "agent_runs_schema_migrations.hpp"
#include "duckdb/catalog/catalog.hpp"
#include "duckdb/common/sql_identifier.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "schema_migration.hpp"

namespace duckdb {
namespace {

struct MigrationState : GlobalTableFunctionState {
	bool done = false;
};

//! Declare the resulting schema version without applying migrations during binding.
unique_ptr<FunctionData> BindSchemaMigration(ClientContext &, TableFunctionBindInput &, vector<LogicalType> &types,
                                             vector<Identifier> &names) {
	types = {LogicalType::INTEGER};
	names = {"schema_version"};
	return nullptr;
}

//! Run the migration batch once per invocation.
unique_ptr<GlobalTableFunctionState> InitSchemaMigration(ClientContext &, TableFunctionInitInput &) {
	return make_uniq<MigrationState>();
}

//! Execute all statements and surface any failure so the owning transaction rolls back.
void ExecuteMigrationSQL(Connection &connection, const string &sql) {
	auto result = connection.Query(sql);
	for (auto current = result.get(); current; current = current->next.get()) {
		current->ThrowIfError();
	}
}

//! Apply pending SQL and version records within the owning transaction.
int ApplySchemaMigrations(Connection &connection) {
	ExecuteMigrationSQL(connection, "CREATE TABLE IF NOT EXISTS schema_migrations "
	                                "(version INTEGER PRIMARY KEY CHECK (version > 0))");
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
	return latest;
}

//! Apply pending versions atomically on a dedicated connection, as in ml-infra's catalog migrator.
void MigrateSchema(ClientContext &context, TableFunctionInput &input, DataChunk &output) {
	auto &state = input.global_state->Cast<MigrationState>();
	if (state.done) {
		return;
	}
	if (!context.transaction.IsAutoCommit()) {
		throw TransactionException("Call migrate_agent_runs outside an explicit transaction");
	}
	auto &catalog = Catalog::GetCatalog(context, Identifier::InvalidCatalog());
	Connection connection(DatabaseInstance::GetDatabase(context));
	ExecuteMigrationSQL(connection, StringUtil::Format("USE %s.main", SQLIdentifier::ToString(catalog.GetName())));
	connection.BeginTransaction();
	// Connection destruction rolls back the entire batch on failure, including its version records.
	auto latest = ApplySchemaMigrations(connection);
	connection.Commit();
	output.SetValue(0, 0, Value::INTEGER(latest));
	output.SetCardinality(1);
	state.done = true;
}

} // namespace

void RegisterSchemaMigration(ExtensionLoader &loader) {
	TableFunction function("migrate_agent_runs", {}, MigrateSchema, BindSchemaMigration, InitSchemaMigration);
	loader.RegisterFunction(function);
}

} // namespace duckdb
