#include "duckdb/common/file_system.hpp"
#include "duckdb/common/path.hpp"
#include "duckdb/main/client_context.hpp"
#include "run_ingest.hpp"

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

namespace duckdb {
namespace {

//! Parse a complete metadata document without inferring or flattening its nested schema.
Value ReadMetadataDocument(ClientContext &context, FileSystem &fs, const string &path) {
	auto file = fs.OpenFile(path, FileFlags::FILE_FLAGS_READ);
	auto size = file->GetFileSize();
	if (size < 0) {
		throw IOException("Cannot determine metadata file size: %s", path);
	}
	string contents(idx_t(size), '\0');
	file->Read(contents.data(), idx_t(size), 0);
	return Value(std::move(contents)).CastAs(context, LogicalType::JSON()).CastAs(context, LogicalType::VARIANT());
}

//! Copy an identity field out of the temporary projection, retaining the complete config/result documents.
string ReadRequiredField(ClientContext &context, const Value &document, std::string_view name) {
	auto projected = document.CastAs(context, LogicalType::STRUCT({{Identifier(name), LogicalType::VARCHAR}}));
	const auto &field = StructValue::GetChildren(projected)[0];
	if (field.IsNull() || StringValue::Get(field).empty()) {
		throw InvalidInputException("Harbor metadata requires %s", string(name));
	}
	return StringValue::Get(field);
}

//! Read one native Harbor config/result pair and its result UUID.
RunRecord ReadMetadataRecord(ClientContext &context, FileSystem &fs, const string &directory) {
	RunRecord record;
	record.config = ReadMetadataDocument(context, fs, fs.JoinPath(directory, "config.json"));
	record.result = ReadMetadataDocument(context, fs, fs.JoinPath(directory, "result.json"));
	record.id = Value(ReadRequiredField(context, record.result, "id")).CastAs(context, LogicalType::UUID);
	return record;
}

//! Locate trial metadata only; logs, artifacts, media, and trajectory files are never scanned.
vector<string> FindTrialDirectories(FileSystem &fs, const string &source) {
	vector<string> directories;
	auto direct_pattern = fs.JoinPath(source, "*", "config.json");
	auto nested_pattern = fs.JoinPath(source, "trials", "*", "config.json");
	std::array<string, 2> patterns {std::move(direct_pattern), std::move(nested_pattern)};
	for (const auto &pattern : patterns) {
		for (const auto &entry : fs.Glob(pattern)) {
			auto directory = Path::FromString(entry.path).Parent();
			if (directory.GetPathSegments().back() != "trials") {
				directories.emplace_back(directory.ToString());
			}
		}
	}
	std::sort(directories.begin(), directories.end());
	return directories;
}

} // namespace

RunMetadata ReadHarborRun(ClientContext &context, const RunOptions &options) {
	auto &fs = FileSystem::GetFileSystem(context);
	RunMetadata metadata;
	metadata.job = ReadMetadataRecord(context, fs, options.source);
	metadata.job.name = ReadRequiredField(context, metadata.job.config, "job_name");
	auto directories = FindTrialDirectories(fs, options.source);
	metadata.trials.reserve(directories.size());
	for (const auto &directory : directories) {
		auto trial = ReadMetadataRecord(context, fs, directory);
		trial.name = ReadRequiredField(context, trial.result, "trial_name");
		trial.trajectory_path = fs.JoinPath(directory, "agent", "trajectory.json");
		metadata.trials.emplace_back(std::move(trial));
	}
	return metadata;
}

} // namespace duckdb
