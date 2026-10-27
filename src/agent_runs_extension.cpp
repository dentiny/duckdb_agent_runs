#define DUCKDB_EXTENSION_MAIN
#include "agent_runs_extension.hpp"
#include "duckdb/main/database.hpp"
#include "json_extension.hpp"
#include "lance_conversion_extension.hpp"
#include "run_ingest.hpp"
#include "schema_migration.hpp"

namespace duckdb {
namespace {
void LoadInternal(ExtensionLoader &loader) {
	DuckDB db(loader.GetDatabaseInstance());
	db.LoadStaticExtension<JsonExtension>();
	db.LoadStaticExtension<LanceConversionExtension>();
	MigrateAgentRunsSchema(loader.GetDatabaseInstance());
	RegisterRunIngest(loader);
	loader.SetDescription(
	    "Import ATIF trajectories into Lance and ingest job/trial metadata into a versioned catalog.");
}
} // namespace

void AgentRunsExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}
std::string AgentRunsExtension::Name() {
	return "agent_runs";
}
std::string AgentRunsExtension::Version() const {
	return "0.1.0";
}
} // namespace duckdb
extern "C" {
DUCKDB_CPP_EXTENSION_ENTRY(agent_runs, loader) {
	duckdb::LoadInternal(loader);
}
}
