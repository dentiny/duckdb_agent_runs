#include "duckdb/common/file_system.hpp"
#include "duckdb/common/sql_identifier.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/main/database.hpp"
#include "run_ingest.hpp"

namespace duckdb {
namespace {
const char *TextCompressionName(TextCompression compression) {
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
} // namespace
void PreflightTrajectories(ClientContext &context, const RunOptions &options, const RunPlan &plan) {
	auto &fs = FileSystem::GetFileSystem(context);
	for (const auto &trajectory : plan.trajectories) {
		auto target = fs.JoinPath(options.trajectory_destination, trajectory.relative_dataset);
		// S3 has no directories. Check Lance's manifest prefix as well as local paths.
		if (target.find("://") == string::npos) {
			if (fs.FileExists(target) || fs.DirectoryExists(target)) {
				throw IOException("Trajectory destination already exists: %s", target);
			}
		} else if (!fs.Glob(target + "/_versions/*").empty()) {
			throw IOException("Trajectory destination already exists: %s", target);
		}
	}
}
static void CheckQuery(unique_ptr<QueryResult> result) {
	if (result->HasError()) {
		result->ThrowError();
	}
	result->Complete();
	if (result->HasError()) {
		result->ThrowError();
	}
}
void ConvertTrajectories(ClientContext &context, const RunOptions &options, const RunPlan &plan) {
	if (plan.trajectories.empty()) {
		return;
	}
	// The caller's context lock is held by the table function. A separate connection
	// avoids recursive query deadlocks; storage secrets are shared by the database.
	Connection connection(DatabaseInstance::GetDatabase(context));
	for (auto suffix : {"timeout_ms", "io_timeout_ms", "retry_max_times", "retry_min_delay_ms", "retry_max_delay_ms",
	                    "retry_factor"}) {
		string setting = string("lance_conversion_storage_") + suffix;
		Value value;
		if (context.TryGetCurrentSetting(Identifier(setting), value)) {
			CheckQuery(connection.Query("SET " + setting + " = ?", value));
		}
	}
	for (const auto &trajectory : plan.trajectories) {
		context.InterruptCheck();
		auto &fs = FileSystem::GetFileSystem(context);
		const auto source = fs.JoinPath(plan.source, trajectory.relative_source);
		const auto target = fs.JoinPath(options.trajectory_destination, trajectory.relative_dataset);
		// Quote literals, never interpolate a path as SQL syntax. Compression is validated at bind.
		auto sql = "COPY (SELECT * FROM read_atif(" + SQLString::ToString(source) + ")) TO " +
		           SQLString::ToString(target) + " (FORMAT LANCE, STORAGE_VERSION '2.2', COLUMN_COMPRESSION {" +
		           "'steps.message.text': '" + TextCompressionName(options.text_compression) + "', " +
		           "'steps.observation.results.content.text': '" + TextCompressionName(options.text_compression) +
		           "'})";
		CheckQuery(connection.Query(sql));
	}
}
} // namespace duckdb
