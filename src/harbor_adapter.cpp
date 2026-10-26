#include "duckdb/common/exception.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/common/path.hpp"
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

//! Collect regular files beneath plan.source into plan.files and accumulate plan.total_bytes.
//! Return sorted job-relative subdirectory paths; reject resolvable symlinks before descending.
vector<string> ScanRunDirectory(ClientContext &context, FileSystem &fs, RunPlan &plan) {
	vector<string> directories;
	vector<string> pending {""};
	while (!pending.empty()) {
		context.InterruptCheck();
		auto relative_directory = std::move(pending.back());
		pending.pop_back();
		auto directory = relative_directory.empty() ? plan.source : fs.JoinPath(plan.source, relative_directory);
		auto listed = fs.ListFiles(directory, [&](const string &name, bool) {
			context.InterruptCheck();
			auto relative = relative_directory.empty() ? name : relative_directory + "/" + name;
			auto path = fs.JoinPath(plan.source, relative);
			// Listing follows resolvable links. Compare with the canonical path before descending.
			// Dangling links and special files omitted by DuckDB's listing are not ingested.
			if (fs.CanonicalizePath(path) != Path::Normalize(path)) {
				throw InvalidInputException("Symlinks are not supported: %s", path);
			}
			auto metadata = fs.GetStatsIfExists(path);
			if (!metadata) {
				throw IOException("Source changed during planning: %s", path);
			}
			if (metadata->file_type == FileType::FILE_TYPE_DIR) {
				directories.push_back(relative);
				pending.push_back(std::move(relative));
			} else if (metadata->file_type == FileType::FILE_TYPE_REGULAR) {
				if (metadata->file_size < 0) {
					throw IOException("Cannot determine source file size: %s", path);
				}
				auto size = idx_t(metadata->file_size);
				plan.total_bytes += size;
				plan.files.push_back({std::move(relative), size});
			} else {
				throw InvalidInputException("Only regular files are supported: %s", path);
			}
		});
		if (!listed) {
			throw IOException("Cannot list Harbor directory: %s", directory);
		}
	}
	std::sort(plan.files.begin(), plan.files.end(),
	          [](const RunFile &a, const RunFile &b) { return a.relative_path < b.relative_path; });
	std::sort(directories.begin(), directories.end());
	return directories;
}

//! Collect files beneath an object-storage prefix and derive their job-relative parent directories.
//! Object stores expose directory hierarchies through key prefixes rather than directory metadata.
vector<string> ScanRunObjects(ClientContext &context, FileSystem &fs, RunPlan &plan) {
	unordered_set<string> directories;
	const auto prefix = plan.source + "/";
	for (const auto &entry : fs.Glob(prefix + "**")) {
		context.InterruptCheck();
		if (!StringUtil::StartsWith(entry.path, prefix)) {
			throw IOException("Listed object is outside the job prefix: %s", entry.path);
		}
		auto relative = entry.path.substr(prefix.size());
		if (relative.empty() || relative.back() == '/') {
			continue;
		}
		if (Path::FromString(relative).IsAbsolute() || relative.find('\\') != string::npos ||
		    Path::Normalize(relative) != relative) {
			throw InvalidInputException("Source object key must be a normalized job-relative path: %s", entry.path);
		}
		auto metadata = fs.GetStatsIfExists(entry);
		if (!metadata || metadata->file_size < 0 || metadata->file_type != FileType::FILE_TYPE_REGULAR) {
			throw IOException("Cannot determine source object metadata: %s", entry.path);
		}
		auto size = idx_t(metadata->file_size);
		plan.total_bytes += size;
		plan.files.push_back({relative, size});
		for (auto separator = relative.find('/'); separator != string::npos;
		     separator = relative.find('/', separator + 1)) {
			directories.insert(relative.substr(0, separator));
		}
	}
	std::sort(plan.files.begin(), plan.files.end(),
	          [](const RunFile &a, const RunFile &b) { return a.relative_path < b.relative_path; });
	vector<string> result(directories.begin(), directories.end());
	std::sort(result.begin(), result.end());
	return result;
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

bool ContainsPath(const string &root, const string &path) {
	auto prefix = StringUtil::EndsWith(root, "/") ? root : root + "/";
	return root == path || StringUtil::StartsWith(path, prefix);
}

bool Overlaps(const string &a, const string &b) {
	return ContainsPath(a, b) || ContainsPath(b, a);
}

} // namespace

RunPlan PlanHarborRun(ClientContext &context, const RunOptions &options) {
	auto &fs = FileSystem::GetFileSystem(context);
	auto plan = InspectHarborRoot(fs, options);
	if (Path::FromString(plan.source).IsRemote()) {
		ScanRunObjects(context, fs, plan);
	} else {
		ScanRunDirectory(context, fs, plan);
	}
	return plan;
}

void ValidateDestinations(ClientContext &context, const RunOptions &options, const RunPlan &plan) {
	auto &fs = FileSystem::GetFileSystem(context);
	if (Overlaps(Normalize(fs, plan.source), Normalize(fs, options.job_destination))) {
		throw InvalidInputException("job_destination and job_directory must not overlap");
	}
}
} // namespace duckdb
