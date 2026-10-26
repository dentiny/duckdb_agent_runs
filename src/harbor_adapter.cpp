#include "run_ingest.hpp"
#include "duckdb/common/exception.hpp"
#include <algorithm>
#include <filesystem>
namespace duckdb {
namespace fs = std::filesystem;
RunPlan PlanHarborRun(const RunOptions &options) {
	RunPlan plan;
	auto root = fs::canonical(fs::path(options.source));
	if (!fs::is_directory(root)) {
		throw InvalidInputException("job_directory must be a local directory");
	}
	for (auto name : {"config.json", "result.json"}) {
		if (!fs::is_regular_file(root / name)) {
			throw InvalidInputException("Harbor job requires %s", name);
		}
	}
	plan.source = root.string();
	for (const auto &entry : fs::recursive_directory_iterator(root)) {
		const auto status = entry.symlink_status();
		if (fs::is_symlink(status)) {
			throw InvalidInputException("Symlinks are not supported: %s", entry.path().string());
		}
		if (fs::is_directory(status)) {
			continue;
		}
		if (!fs::is_regular_file(status)) {
			throw InvalidInputException("Only regular files are supported: %s", entry.path().string());
		}
		RunFile file {entry.path().lexically_relative(root).generic_string(), entry.file_size()};
		plan.total_bytes += file.size;
		plan.files.push_back(std::move(file));
	}
	std::sort(plan.files.begin(), plan.files.end(),
	          [](const RunFile &a, const RunFile &b) { return a.relative_path < b.relative_path; });
	return plan;
}
static bool IsAncestor(const fs::path &a, const fs::path &b) {
	auto ai = a.begin();
	auto bi = b.begin();
	for (; ai != a.end() && bi != b.end(); ++ai, ++bi) {
		if (*ai != *bi) {
			return false;
		}
	}
	return ai == a.end();
}
void ValidateDestinations(const RunOptions &options, const RunPlan &plan) {
	if (options.job_destination.find("://") != string::npos) {
		return;
	}
	const auto destination = fs::weakly_canonical(options.job_destination);
	const auto source = fs::path(plan.source);
	if (IsAncestor(source, destination) || IsAncestor(destination, source)) {
		throw InvalidInputException("job_destination and job_directory must not overlap");
	}
}
} // namespace duckdb
