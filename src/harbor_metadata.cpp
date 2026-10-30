#include "duckdb/common/file_system.hpp"
#include "duckdb/common/path.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/types/uuid.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/connection.hpp"
#include "schema_migration.hpp"
#include "mbedtls_wrapper.hpp"
#include "run_ingest.hpp"

#include <algorithm>
#include <string_view>
#include <utility>

namespace duckdb {
namespace {

Value ReadMetadataDocument(ClientContext &context, FileSystem &fs, const string &path) {
	auto file = fs.OpenFile(path, FileFlags::FILE_FLAGS_READ | FileFlags::FILE_FLAGS_NULL_IF_NOT_EXISTS);
	if (!file) {
		return Value(LogicalType::VARIANT());
	}
	auto size = file->GetFileSize();
	if (size < 0) {
		throw IOException("Cannot determine metadata file size: %s", path);
	}
	string contents(idx_t(size), '\0');
	file->Read(contents.data(), idx_t(size), 0);
	return Value(std::move(contents)).CastAs(context, LogicalType::JSON()).CastAs(context, LogicalType::VARIANT());
}

Value ReadField(ClientContext &context, const Value &document, std::string_view name) {
	if (document.IsNull()) {
		return Value(LogicalType::VARCHAR);
	}
	auto projected = document.CastAs(context, LogicalType::STRUCT({{Identifier(name), LogicalType::VARCHAR}}));
	return StructValue::GetChildren(projected)[0];
}

//! Same URL-namespace UUIDv5 rule as the catalog service; generated IDs are not producer IDs.
Value CatalogID(const string &kind, const string &source) {
	static const unsigned char url_namespace[] = {0x6b, 0xa7, 0xb8, 0x11, 0x9d, 0xad, 0x11, 0xd1,
	                                              0x80, 0xb4, 0x00, 0xc0, 0x4f, 0xd4, 0x30, 0xc8};
	duckdb_mbedtls::MbedTlsWrapper::SHA1State sha;
	sha.AddString(string(reinterpret_cast<const char *>(url_namespace), sizeof(url_namespace)));
	sha.AddString(StringUtil::Format("harbor-catalog:%s:%s", kind, source));
	auto hash = sha.Finalize();
	hash[6] = char((static_cast<unsigned char>(hash[6]) & 0x0f) | 0x50);
	hash[8] = char((static_cast<unsigned char>(hash[8]) & 0x3f) | 0x80);
	return Value::UUID(UUID::FromBlob(const_data_ptr_cast(hash.data())));
}

string Provenance(FileSystem &fs, const string &path) {
	if (FileSystem::IsRemoteFile(path)) {
		return path;
	}
	auto canonical = StringUtil::Replace(fs.CanonicalizePath(path), fs.PathSeparator(path), "/");
	return StringUtil::Format("file://%s", StringUtil::URLEncode(canonical, false));
}

RunRecord ReadMetadataRecord(ClientContext &context, FileSystem &fs, const string &directory, const string &source_uri,
                             const string &kind) {
	RunRecord record;
	record.source_uri = source_uri;
	record.config = ReadMetadataDocument(context, fs, fs.JoinPath(directory, "config.json"));
	record.result = ReadMetadataDocument(context, fs, fs.JoinPath(directory, "result.json"));
	auto id = ReadField(context, record.result, "id");
	record.id = id.IsNull() ? CatalogID(kind, source_uri) : id.CastAs(context, LogicalType::UUID);
	return record;
}

vector<string> FindTrajectories(FileSystem &fs, const string &source) {
	vector<string> paths;
	for (const auto &entry : fs.Glob(fs.JoinPath(source, "**", "trajectory.json"))) {
		paths.push_back(entry.path);
	}
	// Metadata-only trials still surface missing trajectories as conversion errors.
	for (const auto &pattern :
	     {fs.JoinPath(source, "*", "config.json"), fs.JoinPath(source, "trials", "*", "config.json")}) {
		for (const auto &entry : fs.Glob(pattern)) {
			auto directory = Path::FromString(entry.path).Parent().ToString();
			if (Path::FromString(directory).GetPathSegments().back() == "trials") {
				continue;
			}
			auto root = fs.JoinPath(directory, "trajectory.json");
			auto agent = fs.JoinPath(directory, "agent", "trajectory.json");
			if (std::find(paths.begin(), paths.end(), root) == paths.end() &&
			    std::find(paths.begin(), paths.end(), agent) == paths.end()) {
				paths.push_back(agent);
			}
		}
	}
	std::sort(paths.begin(), paths.end());
	paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
	return paths;
}

} // namespace

RunMetadata ReadHarborRun(ClientContext &context, const RunOptions &options) {
	auto &fs = FileSystem::GetFileSystem(context);
	if (options.source.empty()) {
		throw InvalidInputException("job_directory must not be empty");
	}
	// Glob returns expanded, normalized paths; trial names are suffixes of this root.
	auto source = fs.CanonicalizePath(options.source);
	StringUtil::RTrim(source, "/");
	if (source.empty()) {
		throw InvalidInputException("job_directory must not be the root directory");
	}
	RunMetadata metadata;
	metadata.job = ReadMetadataRecord(context, fs, source, Provenance(fs, source), "job");
	auto name = ReadField(context, metadata.job.config, "job_name");
	metadata.job.name = name.IsNull() ? Path::FromString(source).GetPathSegments().back() : StringValue::Get(name);
	auto trajectories = FindTrajectories(fs, source);
	if (trajectories.empty() && metadata.job.config.IsNull() && metadata.job.result.IsNull()) {
		throw InvalidInputException("No trajectories or Job metadata found: %s", source);
	}
	Connection summaries(*context.db);
	summaries.context->config = context.config;
	const auto summary_sql =
	    StringUtil::Format("SELECT %s(?::VARIANT)", GetAgentRunsName(context, "atif_metadata").ToString());
	for (const auto &trajectory : trajectories) {
		auto directory = Path::FromString(trajectory).Parent();
		if (directory.GetPathSegments().back() == "agent") {
			directory = directory.Parent();
		}
		if (directory.ToString().size() <= source.size()) {
			throw InvalidInputException("trajectory.json must be inside a Trial directory: %s", trajectory);
		}
		auto relative = directory.ToString().substr(source.size() + 1);
		auto trial = ReadMetadataRecord(context, fs, directory.ToString(), Provenance(fs, trajectory), "trial");
		auto trial_name = ReadField(context, trial.result, "trial_name");
		trial.name = trial_name.IsNull() ? relative : StringValue::Get(trial_name);
		trial.trajectory_path = trajectory;
		auto document = ReadMetadataDocument(context, fs, trajectory);
		if (!document.IsNull()) {
			auto summary = summaries.Query(summary_sql, document);
			summary->ThrowIfError();
			trial.trajectory = summary->Fetch()->GetValue(0, 0);
		}
		metadata.trials.emplace_back(std::move(trial));
	}
	return metadata;
}

} // namespace duckdb
