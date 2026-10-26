#include "duckdb/function/table_function.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/main/settings.hpp"
#include "run_ingest.hpp"

namespace duckdb {
namespace {
struct RunBindData : public TableFunctionData {
	RunOptions options;
};
struct RunState : public GlobalTableFunctionState {
	bool initialized = false;
	idx_t offset = 0;
	RunPlan plan;
};
string RequiredString(const Value &value, const char *name) {
	if (value.IsNull()) {
		throw InvalidInputException("%s must not be NULL", name);
	}
	auto result = StringValue::Get(value);
	if (result.find_first_not_of(" \t\r\n") == string::npos || result.find('\0') != string::npos) {
		throw InvalidInputException("%s must be a nonblank path without NUL bytes", name);
	}
	return result;
}
RunFormat ParseRunFormat(const string &format) {
	if (format == "harbor") {
		return RunFormat::HARBOR;
	}
	throw InvalidInputException("Unsupported run format: %s", format);
}
unique_ptr<FunctionData> BindRun(ClientContext &, TableFunctionBindInput &input, vector<LogicalType> &types,
                                 vector<Identifier> &names) {
	auto data = make_uniq<RunBindData>();
	data->options.source = RequiredString(input.inputs[0], "job_directory");
	for (auto &parameter : input.named_parameters) {
		auto name = parameter.first.GetIdentifierName();
		if (name == "job_destination") {
			data->options.job_destination = RequiredString(parameter.second, "job_destination");
		} else if (name == "trajectory_destination") {
			data->options.trajectory_destination = RequiredString(parameter.second, "trajectory_destination");
		} else if (name == "text_compression") {
			data->options.text_compression = RequiredString(parameter.second, "text_compression");
		} else if (name == "format") {
			data->options.format = ParseRunFormat(RequiredString(parameter.second, "format"));
		} else if (name == "dry_run") {
			if (parameter.second.IsNull()) {
				throw InvalidInputException("dry_run must not be NULL");
			}
			data->options.dry_run = BooleanValue::Get(parameter.second);
		}
	}
	if (data->options.job_destination.empty()) {
		throw InvalidInputException("job_destination is required");
	}
	if (data->options.text_compression != "lz4" && data->options.text_compression != "zstd" &&
	    data->options.text_compression != "none") {
		throw InvalidInputException("text_compression must be lz4, zstd, or none");
	}
	names = {"job_destination", "file_count",        "total_bytes",           "status",
	         "trial_name",      "trajectory_source", "trajectory_destination"};
	types = {LogicalType::VARCHAR, LogicalType::UBIGINT, LogicalType::UBIGINT, LogicalType::VARCHAR,
	         LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR};
	return std::move(data);
}
unique_ptr<GlobalTableFunctionState> InitRun(ClientContext &, TableFunctionInitInput &) {
	return make_uniq<RunState>();
}
void IngestRun(ClientContext &context, TableFunctionInput &input, DataChunk &output) {
	auto &state = input.global_state->Cast<RunState>();
	const auto &options = input.bind_data->Cast<RunBindData>().options;
	if (!state.initialized) {
		if (!Settings::Get<EnableExternalAccessSetting>(context)) {
			throw PermissionException("ingest_agent_run requires enable_external_access");
		}
		state.plan = PlanHarborRun(context, options);
		ValidateDestinations(context, options, state.plan);
		PreflightTrajectories(context, options, state.plan);
		UploadRun(context, options, state.plan);
		if (!options.dry_run) {
			ConvertTrajectories(context, options, state.plan);
		}
		state.initialized = true;
	}
	const auto &plan = state.plan;
	const auto count = MaxValue<idx_t>(plan.trajectories.size(), 1);
	idx_t rows = 0;
	while (state.offset < count && rows < STANDARD_VECTOR_SIZE) {
		const bool has_trajectory = !plan.trajectories.empty();
		output.data[0].Append(Value(options.job_destination));
		output.data[1].Append(Value::UBIGINT(plan.files.size()));
		output.data[2].Append(Value::UBIGINT(plan.total_bytes));
		output.data[3].Append(Value(options.dry_run ? "planned" : (has_trajectory ? "ingested" : "uploaded")));
		if (has_trajectory) {
			const auto &trajectory = plan.trajectories[state.offset];
			output.data[4].Append(Value(trajectory.trial_name));
			output.data[5].Append(
			    Value(FileSystem::GetFileSystem(context).JoinPath(plan.source, trajectory.relative_source)));
			output.data[6].Append(Value(FileSystem::GetFileSystem(context).JoinPath(options.trajectory_destination,
			                                                                        trajectory.relative_dataset)));
		} else {
			for (idx_t col = 4; col < 7; col++) {
				output.data[col].Append(Value(LogicalType::VARCHAR));
			}
		}
		state.offset++;
		rows++;
	}
	output.CheckCardinality(rows);
}
} // namespace

void RegisterRunIngest(ExtensionLoader &loader) {
	TableFunction function("ingest_agent_run", {LogicalType::VARCHAR}, IngestRun, BindRun, InitRun);
	function.GetSignature().WithTypedKwargs("options", [](TypedKwargs &options) {
		options.Add("format", LogicalType::VARCHAR);
		options.Add("job_destination", LogicalType::VARCHAR);
		options.Add("dry_run", LogicalType::BOOLEAN);
		options.Add("trajectory_destination", LogicalType::VARCHAR);
		options.Add("text_compression", LogicalType::VARCHAR);
	});
	loader.RegisterFunction(function);
}
} // namespace duckdb
