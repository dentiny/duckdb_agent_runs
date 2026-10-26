#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/catalog_entry/table_catalog_entry.hpp"
#include "duckdb/catalog/catalog_entry/type_catalog_entry.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/appender.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/main/settings.hpp"
#include "duckdb/planner/binder.hpp"
#include "run_ingest.hpp"
#include "schema_migration.hpp"

#include <string_view>

namespace duckdb {
namespace {

constexpr std::string_view JOBS_TABLE_NAME = "jobs";
constexpr std::string_view TRIALS_TABLE_NAME = "trials";

struct RunBindData : TableFunctionData {
	RunOptions options;
};

struct RunState : GlobalTableFunctionState {
	bool done = false;
};

//! Look up the existing schema type, including its nested config/result fields.
LogicalType GetMetadataType(ClientContext &context, std::string_view name) {
	return Catalog::GetEntry<TypeCatalogEntry>(context, GetAgentRunsName(context, name)).user_type;
}

//! Bind to the existing ml-infra table layout instead of creating or replacing user tables.
TableCatalogEntry &GetMetadataTable(ClientContext &context, std::string_view name, const vector<string> &columns,
                                    const vector<LogicalType> &types) {
	auto &table = Catalog::GetEntry<TableCatalogEntry>(context, GetAgentRunsName(context, name));
	if (table.GetColumns().GetColumnNames() != columns || table.GetTypes() != types) {
		throw BinderException("%s must use the typed ml-infra jobs/trials schema", string(name));
	}
	return table;
}

//! Require the pinned job table layout before appending.
TableCatalogEntry &GetJobsTable(ClientContext &context) {
	return GetMetadataTable(context, JOBS_TABLE_NAME, {"job_id", "job_name", "owner_email", "config", "result"},
	                        {LogicalType::UUID, LogicalType::VARCHAR, LogicalType::VARCHAR,
	                         GetMetadataType(context, "job_config"), GetMetadataType(context, "job_result")});
}

//! Require the pinned trial table layout before appending.
TableCatalogEntry &GetTrialsTable(ClientContext &context) {
	return GetMetadataTable(
	    context, TRIALS_TABLE_NAME, {"trial_id", "job_id", "trial_name", "config", "result", "dataset_uri"},
	    {LogicalType::UUID, LogicalType::UUID, LogicalType::VARCHAR, GetMetadataType(context, "trial_config"),
	     GetMetadataType(context, "trial_result"), LogicalType::VARCHAR});
}

//! Borrow a non-NULL VARCHAR argument while its Value remains alive.
std::string_view GetStringArgument(const Value &value, std::string_view name) {
	if (value.IsNull()) {
		throw InvalidInputException("%s must not be NULL", string(name));
	}
	return StringValue::Get(value);
}

//! Resolve the public format name to its internal adapter enum.
RunFormat ParseRunFormat(std::string_view format) {
	if (format == "harbor") {
		return RunFormat::HARBOR;
	}
	throw InvalidInputException("Unsupported run format: %s", string(format));
}

//! Bind arguments and declare writes without reading files or changing tables.
unique_ptr<FunctionData> BindRun(ClientContext &context, TableFunctionBindInput &input, vector<LogicalType> &types,
                                 vector<Identifier> &names) {
	auto data = make_uniq<RunBindData>();
	data->options.source = GetStringArgument(input.inputs[0], "job_directory");
	for (auto &parameter : input.named_parameters) {
		std::string_view name = parameter.first.GetIdentifierName();
		if (name == "owner_email") {
			data->options.owner_email = GetStringArgument(parameter.second, "owner_email");
		} else if (name == "trajectory_destination") {
			data->options.trajectory_destination = GetStringArgument(parameter.second, "trajectory_destination");
		} else if (name == "format") {
			data->options.format = ParseRunFormat(GetStringArgument(parameter.second, "format"));
		}
	}
	if (data->options.owner_email.empty()) {
		throw InvalidInputException("owner_email is required");
	}
	auto &jobs = GetJobsTable(context);
	GetTrialsTable(context);
	if (input.binder) {
		auto &catalog = jobs.ParentCatalog();
		input.binder->GetStatementProperties().RegisterDBModify(catalog, context,
		                                                        DatabaseModificationType::INSERT_DATA);
	}
	types = {LogicalType::UUID, LogicalType::VARCHAR, LogicalType::UBIGINT};
	names = {"job_id", "job_name", "trials_ingested"};
	return std::move(data);
}

//! Execute each ingestion call once, regardless of output chunk requests.
unique_ptr<GlobalTableFunctionState> InitRun(ClientContext &, TableFunctionInitInput &) {
	return make_uniq<RunState>();
}

//! Append metadata with the current context so the caller's COMMIT/ROLLBACK governs both tables.
void InsertRunMetadata(ClientContext &context, const RunOptions &options, const RunMetadata &metadata) {
	auto &jobs = GetJobsTable(context);
	auto job_types = jobs.GetTypes();
	InternalAppender job_appender(context, jobs);
	job_appender.AppendRow(metadata.job.id, Value(metadata.job.name), Value(options.owner_email),
	                       metadata.job.config.CastAs(context, job_types[3]),
	                       metadata.job.result.CastAs(context, job_types[4]));
	job_appender.Close();

	auto &trials = GetTrialsTable(context);
	auto trial_types = trials.GetTypes();
	InternalAppender trial_appender(context, trials);
	auto &fs = FileSystem::GetFileSystem(context);
	for (const auto &trial : metadata.trials) {
		auto dataset_uri =
		    fs.JoinPath(options.trajectory_destination, StringUtil::Format("%s.lance", trial.id.ToString()));
		trial_appender.AppendRow(trial.id, metadata.job.id, Value(trial.name),
		                         trial.config.CastAs(context, trial_types[3]),
		                         trial.result.CastAs(context, trial_types[4]), Value(dataset_uri));
	}
	trial_appender.Close();
}

//! Read and append one job with its trials, returning one summary row.
void IngestRun(ClientContext &context, TableFunctionInput &input, DataChunk &output) {
	auto &state = input.global_state->Cast<RunState>();
	if (state.done) {
		return;
	}
	if (!Settings::Get<EnableExternalAccessSetting>(context)) {
		throw PermissionException("ingest_agent_run requires enable_external_access");
	}
	const auto &options = input.bind_data->Cast<RunBindData>().options;
	auto metadata = ReadHarborRun(context, options);
	if (!metadata.trials.empty() && options.trajectory_destination.empty()) {
		throw InvalidInputException("trajectory_destination is required to populate trials.dataset_uri");
	}
	InsertRunMetadata(context, options, metadata);
	// TODO: Convert each trial's ATIF trajectory to its Lance dataset in parallel in a follow-up PR.
	output.data[0].Append(metadata.job.id);
	output.data[1].Append(Value(metadata.job.name));
	output.data[2].Append(Value::UBIGINT(metadata.trials.size()));
	output.CheckCardinality(1);
	state.done = true;
}

} // namespace

void RegisterRunIngest(ExtensionLoader &loader) {
	TableFunction function("ingest_agent_run", {LogicalType::VARCHAR}, IngestRun, BindRun, InitRun);
	function.GetSignature().WithTypedKwargs("options", [](TypedKwargs &options) {
		options.Add("format", LogicalType::VARCHAR);
		options.Add("owner_email", LogicalType::VARCHAR);
		options.Add("trajectory_destination", LogicalType::VARCHAR);
	});
	loader.RegisterFunction(function);
}

} // namespace duckdb
