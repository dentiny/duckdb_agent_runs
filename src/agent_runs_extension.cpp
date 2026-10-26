#define DUCKDB_EXTENSION_MAIN
#include "agent_runs_extension.hpp"
#include "duckdb/main/database.hpp"
#include "lance_conversion_extension.hpp"
#include "run_ingest.hpp"
namespace duckdb {
namespace {
void LoadInternal(ExtensionLoader &loader) {
	DuckDB db(loader.GetDatabaseInstance());
	db.LoadStaticExtension<LanceConversionExtension>();
	RegisterRunIngest(loader);
	loader.SetDescription("Upload agent run artifacts and convert trajectories to Lance.");
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
