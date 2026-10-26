#include "duckdb/function/table_function.hpp"
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
	bool done = false;
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
	names = {"job_destination", "file_count", "total_bytes", "status"};
	types = {LogicalType::VARCHAR, LogicalType::UBIGINT, LogicalType::UBIGINT, LogicalType::VARCHAR};
	return std::move(data);
}
unique_ptr<GlobalTableFunctionState> InitRun(ClientContext &, TableFunctionInitInput &) {
	return make_uniq<RunState>();
}
void IngestRun(ClientContext &context, TableFunctionInput &input, DataChunk &output) {
	auto &state = input.global_state->Cast<RunState>();
	if (state.done) {
		return;
	}
	if (!Settings::Get<EnableExternalAccessSetting>(context)) {
		throw PermissionException("ingest_agent_run requires enable_external_access");
	}
	auto &options = input.bind_data->Cast<RunBindData>().options;
	auto plan = PlanHarborRun(context, options);
	ValidateDestinations(context, options, plan);
	UploadRun(context, options, plan);
	output.data[0].Append(Value(options.job_destination));
	output.data[1].Append(Value::UBIGINT(plan.files.size()));
	output.data[2].Append(Value::UBIGINT(plan.total_bytes));
	output.data[3].Append(Value(options.dry_run ? "planned" : "uploaded"));
	output.CheckCardinality(1);
	state.done = true;
}
} // namespace

void RegisterRunIngest(ExtensionLoader &loader) {
	TableFunction function("ingest_agent_run", {LogicalType::VARCHAR}, IngestRun, BindRun, InitRun);
	function.GetSignature().WithTypedKwargs("options", [](TypedKwargs &options) {
		options.Add("format", LogicalType::VARCHAR);
		options.Add("job_destination", LogicalType::VARCHAR);
		options.Add("dry_run", LogicalType::BOOLEAN);
	});
	loader.RegisterFunction(function);
}
} // namespace duckdb
