#include "duckdb/common/file_system.hpp"
#include "duckdb/common/path.hpp"
#include "duckdb/common/set.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/types/uuid.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/parallel/task_executor.hpp"
#include "schema_migration.hpp"
#include "mbedtls_wrapper.hpp"
#include "run_ingest.hpp"

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
	// JSON-to-VARIANT is the only parse, and it yields NULL rather than an error for malformed input.
	auto document = Value(std::move(contents)).WithType(LogicalType::JSON()).CastAs(context, LogicalType::VARIANT());
	if (document.IsNull()) {
		throw InvalidInputException("Malformed JSON document: %s", path);
	}
	return document;
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
	set<string> paths;
	for (const auto &entry : fs.Glob(fs.JoinPath(source, "**", "trajectory.json"))) {
		paths.insert(entry.path);
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
			if (!paths.count(root) && !paths.count(agent)) {
				paths.insert(agent);
			}
		}
	}
	return vector<string>(paths.begin(), paths.end());
}

//! Read one trial's metadata and ATIF summary on its own connection using DuckDB's task scheduler.
class ReadTrialTask : public BaseExecutorTask {
public:
	ReadTrialTask(TaskExecutor &executor, ClientContext &context, string directory, string relative,
	              string trajectory_path, const string &summary_sql, RunRecord &trial)
	    : BaseExecutorTask(executor), db(*context.db), config(context.config), directory(std::move(directory)),
	      relative(std::move(relative)), trajectory_path(std::move(trajectory_path)), summary_sql(summary_sql),
	      trial(trial) {
	}

	void ExecuteTask() override {
		Connection connection(db);
		connection.context->config = config;
		auto &context = *connection.context;
		auto &fs = FileSystem::GetFileSystem(context);
		auto record = ReadMetadataRecord(context, fs, directory, Provenance(fs, trajectory_path), "trial");
		auto trial_name = ReadField(context, record.result, "trial_name");
		record.name = trial_name.IsNull() ? relative : StringValue::Get(trial_name);
		record.trajectory_path = trajectory_path;
		auto document = ReadMetadataDocument(context, fs, trajectory_path);
		if (!document.IsNull()) {
			auto summary = connection.Query(summary_sql, document);
			summary->ThrowIfError();
			record.trajectory = summary->Fetch()->GetValue(0, 0);
		}
		trial = std::move(record);
	}

	string TaskType() const override {
		return "ReadTrialTask";
	}

private:
	DatabaseInstance &db;
	const ClientConfig &config;
	string directory;
	string relative;
	string trajectory_path;
	const string &summary_sql;
	RunRecord &trial;
};

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
	if (!trajectories.empty() && options.trajectory_destination.empty()) {
		throw InvalidInputException("trajectory_destination is required to populate trials.dataset_uri");
	}
	const auto summary_sql =
	    StringUtil::Format("SELECT %s(?::VARIANT)", GetAgentRunsName(context, "atif_metadata").ToString());
	metadata.trials.resize(trajectories.size());
	TaskExecutor executor(context);
	vector<unique_ptr<BaseExecutorTask>> tasks;
	tasks.reserve(trajectories.size());
	for (idx_t i = 0; i < trajectories.size(); i++) {
		auto directory = Path::FromString(trajectories[i]).Parent();
		if (directory.GetPathSegments().back() == "agent") {
			directory = directory.Parent();
		}
		auto directory_path = directory.ToString();
		if (directory_path.size() <= source.size()) {
			throw InvalidInputException("trajectory.json must be inside a Trial directory: %s", trajectories[i]);
		}
		auto relative = directory_path.substr(source.size() + 1);
		tasks.emplace_back(make_uniq<ReadTrialTask>(executor, context, std::move(directory_path), std::move(relative),
		                                            trajectories[i], summary_sql, metadata.trials[i]));
	}
	executor.ScheduleTasks(std::move(tasks));
	executor.WorkOnTasks();
	return metadata;
}

} // namespace duckdb
