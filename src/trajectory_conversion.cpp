#include "duckdb/common/sql_identifier.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/planner/binder.hpp"
#include "duckdb/planner/operator/logical_copy_to_file.hpp"
#include "trajectory_conversion.hpp"
#include "util.hpp"

#include <string_view>

namespace duckdb {
namespace {

enum class TextCompression : uint8_t { LZ4, ZSTD, NONE };

//! Borrow a non-NULL VARCHAR argument while its Value remains alive.
std::string_view GetStringArgument(const Value &value, std::string_view name) {
	if (value.IsNull()) {
		throw InvalidInputException("%s must not be NULL", string(name));
	}
	return StringValue::Get(value);
}

//! Resolve the supported text codecs to the internal compression enum.
TextCompression ParseTextCompression(std::string_view name) {
	if (CIEquals(name, "lz4")) {
		return TextCompression::LZ4;
	}
	if (CIEquals(name, "zstd")) {
		return TextCompression::ZSTD;
	}
	if (CIEquals(name, "none")) {
		return TextCompression::NONE;
	}
	throw InvalidInputException("Unsupported text_compression: %s", string(name));
}

//! Render only validated enum values into the dependency's COPY options.
std::string_view GetTextCompressionName(TextCompression compression) {
	switch (compression) {
	case TextCompression::LZ4:
		return "lz4";
	case TextCompression::ZSTD:
		return "zstd";
	case TextCompression::NONE:
		return "none";
	default:
		throw InternalException("Unknown text compression");
	}
}

//! Plan conversion with the bundled reader/writer in the caller's context, preserving its secrets and settings.
unique_ptr<LogicalOperator> BindTrajectoryConversion(ClientContext &context, TableFunctionBindInput &input,
                                                     TableIndex bind_index, vector<Identifier> &names) {
	auto source = GetStringArgument(input.inputs[0], "source");
	std::string_view destination;
	auto compression = TextCompression::LZ4;
	for (const auto &parameter : input.named_parameters) {
		std::string_view name = parameter.first.GetIdentifierName();
		if (name == "trajectory_destination") {
			destination = GetStringArgument(parameter.second, "trajectory_destination");
		} else if (name == "text_compression") {
			compression = ParseTextCompression(GetStringArgument(parameter.second, "text_compression"));
		}
	}
	if (destination.empty()) {
		throw InvalidInputException("trajectory_destination is required");
	}
	// Expand to a native COPY plan to reuse the caller's secrets, settings, and execution context.
	auto compression_name = string(GetTextCompressionName(compression));
	auto sql = StringUtil::Format("COPY (SELECT * FROM read_atif(%s)) TO %s "
	                              "(FORMAT LANCE, STORAGE_VERSION '2.2', COLUMN_COMPRESSION {"
	                              "'steps.message.text': '%s', 'steps.observation.results.content.text': '%s'})",
	                              SQLString::ToString(string(source)), SQLString::ToString(string(destination)),
	                              compression_name, compression_name);
	Parser parser(context);
	parser.ParseQuery(sql);
	auto binder = Binder::CreateBinder(context, input.binder);
	auto statement = binder->Bind(*parser.statements[0]);
	statement.plan->Cast<LogicalCopyToFile>().table_index = bind_index;
	names = {"trajectories_converted"};
	return std::move(statement.plan);
}

} // namespace

void RegisterTrajectoryConversion(ExtensionLoader &loader) {
	TableFunction function("import_agent_trajectory", {LogicalType::VARCHAR}, nullptr, nullptr);
	function.bind_operator = BindTrajectoryConversion;
	function.GetSignature().WithTypedKwargs("options", [](TypedKwargs &options) {
		options.Add("trajectory_destination", LogicalType::VARCHAR);
		options.Add("text_compression", LogicalType::VARCHAR);
	});
	loader.RegisterFunction(function);
}

} // namespace duckdb
