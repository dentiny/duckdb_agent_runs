#include "duckdb/common/exception.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/common/path.hpp"
#include "duckdb/common/unordered_map.hpp"
#include "duckdb/common/unordered_set.hpp"
#include "duckdb/main/client_context.hpp"
#include "run_ingest.hpp"
#include <algorithm>

namespace duckdb {
namespace {

bool IsRegularFile(FileSystem &fs, const string &path) {
	const auto metadata = fs.GetStatsIfExists(path);
	return metadata && metadata->file_type == FileType::FILE_TYPE_REGULAR;
}

//! Validate a listed local entry before using its metadata or descending into it.
FileMetadata InspectDirectoryEntry(FileSystem &fs, const string &path) {
	if (fs.CanonicalizePath(path) != Path::Normalize(path)) {
		throw InvalidInputException("Symlinks are not supported: %s", path);
	}
	auto metadata = fs.GetStatsIfExists(path);
	if (!metadata) {
		throw IOException("Source changed during planning: %s", path);
	}
	if (metadata->file_type != FileType::FILE_TYPE_DIR && metadata->file_type != FileType::FILE_TYPE_REGULAR) {
		throw InvalidInputException("Only regular files are supported: %s", path);
	}
	if (metadata->file_type == FileType::FILE_TYPE_REGULAR && metadata->file_size < 0) {
		throw IOException("Cannot determine source file size: %s", path);
	}
	return *metadata;
}

//! Append an inventoried file and update the job's total byte count.
void AppendRunFile(RunPlan &plan, string relative_path, idx_t size) {
	plan.files.push_back({std::move(relative_path), size});
	plan.total_bytes += size;
}

//! Traverse local directories; entry validation and file accounting are handled separately.
vector<string> ScanRunDirectory(ClientContext &context, FileSystem &fs, RunPlan &plan) {
	vector<string> directories;
	vector<string> pending {""};
	while (!pending.empty()) {
		context.InterruptCheck();
		auto relative_directory = std::move(pending.back());
		pending.pop_back();
		auto directory = relative_directory.empty() ? plan.source : fs.JoinPath(plan.source, relative_directory);
		const bool listing_succeeded = fs.ListFiles(directory, [&](const string &name, bool) {
			context.InterruptCheck();
			auto relative = relative_directory.empty() ? name : StringUtil::Format("%s/%s", relative_directory, name);
			auto metadata = InspectDirectoryEntry(fs, fs.JoinPath(plan.source, relative));
			if (metadata.file_type == FileType::FILE_TYPE_DIR) {
				directories.push_back(relative);
				pending.push_back(std::move(relative));
				return;
			}
			AppendRunFile(plan, std::move(relative), idx_t(metadata.file_size));
		});
		if (!listing_succeeded) {
			throw IOException("Cannot list Harbor directory: %s", directory);
		}
	}
	return directories;
}

//! Resolve a listed object beneath the job prefix; directory markers return an empty path.
string GetRelativeObjectPath(const string &prefix, const string &path) {
	if (!StringUtil::StartsWith(path, prefix)) {
		throw IOException("Listed object is outside the job prefix: %s", path);
	}
	auto relative = path.substr(prefix.size());
	if (relative.empty() || relative.back() == '/') {
		return "";
	}
	if (Path::FromString(relative).IsAbsolute() || relative.find('\\') != string::npos ||
	    Path::Normalize(relative) != relative) {
		throw InvalidInputException("Source object key must be a normalized job-relative path: %s", path);
	}
	return relative;
}

//! Derive implicit object-store directories from a job-relative file path.
void CollectParentDirectories(const string &relative_path, unordered_set<string> &directories) {
	for (auto separator = relative_path.find('/'); separator != string::npos;
	     separator = relative_path.find('/', separator + 1)) {
		directories.insert(relative_path.substr(0, separator));
	}
}

//! List remote files and collect the parent prefixes used to discover trials and steps.
vector<string> ScanRunObjects(ClientContext &context, FileSystem &fs, RunPlan &plan) {
	unordered_set<string> directories;
	const auto prefix = StringUtil::Format("%s/", plan.source);
	for (const auto &entry : fs.Glob(StringUtil::Format("%s**", prefix))) {
		context.InterruptCheck();
		auto relative = GetRelativeObjectPath(prefix, entry.path);
		if (relative.empty()) {
			continue;
		}
		auto metadata = fs.GetStatsIfExists(entry);
		if (!metadata || metadata->file_size < 0 || metadata->file_type != FileType::FILE_TYPE_REGULAR) {
			throw IOException("Cannot determine source object metadata: %s", entry.path);
		}
		CollectParentDirectories(relative, directories);
		AppendRunFile(plan, std::move(relative), idx_t(metadata->file_size));
	}
	return vector<string>(directories.begin(), directories.end());
}

//! Scan either source backend and return a stable file inventory and sorted relative directories.
vector<string> ScanRunFiles(ClientContext &context, FileSystem &fs, RunPlan &plan) {
	auto directories = Path::FromString(plan.source).IsRemote() ? ScanRunObjects(context, fs, plan)
	                                                            : ScanRunDirectory(context, fs, plan);
	std::sort(plan.files.begin(), plan.files.end(),
	          [](const RunFile &a, const RunFile &b) { return a.relative_path < b.relative_path; });
	std::sort(directories.begin(), directories.end());
	return directories;
}

RunPlan InspectHarborRoot(FileSystem &fs, const RunOptions &options) {
	RunPlan plan;
	const auto remote = Path::FromString(options.source).IsRemote();
	plan.source = remote ? options.source : fs.CanonicalizePath(options.source);
	if (remote) {
		while (!plan.source.empty() && plan.source.back() == '/') {
			plan.source.pop_back();
		}
	} else if (!fs.DirectoryExists(plan.source)) {
		throw InvalidInputException("job_directory must be a directory");
	}
	for (auto name : {"config.json", "result.json"}) {
		if (!IsRegularFile(fs, fs.JoinPath(plan.source, name))) {
			throw InvalidInputException("Harbor job requires %s", name);
		}
	}
	return plan;
}

string Normalize(FileSystem &fs, const string &path) {
	auto value = Path::FromString(path).IsRemote() ? path : fs.CanonicalizePath(path);
#ifdef _WIN32
	if (Path::FromString(path).IsLocal()) {
		value = StringUtil::Replace(value, "\\", "/");
	}
#endif
	while (value.size() > 1 && value.back() == '/') {
		value.pop_back();
	}
	return value;
}

//! Test normalized paths at directory boundaries, so /job does not contain /job-backup.
bool IsSameOrDescendantPath(const string &root, const string &path) {
	auto prefix = StringUtil::EndsWith(root, "/") ? root : StringUtil::Format("%s/", root);
	return root == path || StringUtil::StartsWith(path, prefix);
}

//! Reject equal or nested source/destination roots before any ingestion writes.
//! Both paths must already be normalized; names identify the offending SQL arguments.
void CheckSourceDestOverlap(const string &source, const string &destination, const char *source_name,
                            const char *destination_name) {
	if (IsSameOrDescendantPath(source, destination) || IsSameOrDescendantPath(destination, source)) {
		throw InvalidInputException("%s and %s must not overlap", source_name, destination_name);
	}
}

void FindTrajectory(FileSystem &fs, const string &root, const string &relative_directory, const string &trial,
                    const string &dataset, RunPlan &plan) {
	vector<string> found;
	for (auto name : {"trajectory.json", "trajectory.jsonl", "trajectory.json.gz", "trajectory.jsonl.gz"}) {
		for (const auto &parent : {relative_directory + "/agent", relative_directory}) {
			auto relative = parent + "/" + name;
			if (IsRegularFile(fs, fs.JoinPath(root, relative))) {
				found.push_back(std::move(relative));
			}
		}
	}
	if (found.size() > 1) {
		throw InvalidInputException("Multiple primary trajectories for %s", fs.JoinPath(root, relative_directory));
	}
	if (!found.empty()) {
		plan.trajectories.push_back({trial, std::move(found[0]), dataset});
	}
}

} // namespace

RunPlan PlanHarborRun(ClientContext &context, const RunOptions &options) {
	auto &fs = FileSystem::GetFileSystem(context);
	auto plan = InspectHarborRoot(fs, options);
	auto directories = ScanRunFiles(context, fs, plan);

	unordered_map<string, vector<string>> steps_by_trial;
	for (const auto &directory : directories) {
		auto marker = directory.rfind("/steps/");
		if (marker != string::npos && directory.find('/', marker + 7) == string::npos) {
			steps_by_trial[directory.substr(0, marker)].push_back(directory);
		}
	}
	for (const auto &trial : directories) {
		auto parts = StringUtil::Split(trial, '/');
		auto is_trial = (parts.size() == 1 && trial != "trials") || (parts.size() == 2 && parts[0] == "trials");
		if (!is_trial || !IsRegularFile(fs, fs.JoinPath(plan.source, trial, "config.json"))) {
			continue;
		}
		if (!IsRegularFile(fs, fs.JoinPath(plan.source, trial, "result.json"))) {
			throw InvalidInputException("Harbor trial requires result.json: %s", fs.JoinPath(plan.source, trial));
		}
		FindTrajectory(fs, plan.source, trial, trial, trial + ".lance", plan);
		auto steps = steps_by_trial.find(trial);
		if (steps != steps_by_trial.end()) {
			for (const auto &step : steps->second) {
				FindTrajectory(fs, plan.source, step, trial, step + ".lance", plan);
			}
		}
	}
	std::sort(plan.trajectories.begin(), plan.trajectories.end(),
	          [](const RunTrajectory &a, const RunTrajectory &b) { return a.relative_source < b.relative_source; });
	return plan;
}

void ValidateDestinations(ClientContext &context, const RunOptions &options, const RunPlan &plan) {
	auto &fs = FileSystem::GetFileSystem(context);
	auto source = Normalize(fs, plan.source);
	auto job = Normalize(fs, options.job_destination);
	CheckSourceDestOverlap(source, job, "job_directory", "job_destination");
	if (!plan.trajectories.empty() && options.trajectory_destination.empty()) {
		throw InvalidInputException("trajectory_destination is required when the run contains trajectories");
	}
	if (!options.trajectory_destination.empty()) {
		auto trajectory = Normalize(fs, options.trajectory_destination);
		CheckSourceDestOverlap(source, trajectory, "job_directory", "trajectory_destination");
		CheckSourceDestOverlap(job, trajectory, "job_destination", "trajectory_destination");
		if (Path::FromString(trajectory).IsRemote() && !StringUtil::StartsWith(trajectory, "s3://")) {
			throw InvalidInputException("trajectory_destination supports only local paths and s3:// URIs");
		}
	}
}
} // namespace duckdb
