#include "run_ingest.hpp"
#include "duckdb/common/exception.hpp"
#include <algorithm>
#include <filesystem>
namespace duckdb {
namespace fs = std::filesystem;
static void FindTrajectory(const fs::path &root, const fs::path &directory, const string &trial, const string &dataset,
                           RunPlan &plan) {
	vector<fs::path> found;
	for (auto name : {"trajectory.json", "trajectory.jsonl", "trajectory.json.gz", "trajectory.jsonl.gz"}) {
		for (const auto &parent : {directory / "agent", directory}) {
			if (fs::is_regular_file(parent / name)) {
				found.push_back(parent / name);
			}
		}
	}
	if (found.size() > 1) {
		throw InvalidInputException("Multiple primary trajectories for %s", directory.string());
	}
	if (!found.empty()) {
		plan.trajectories.push_back({trial, found[0].lexically_relative(root).generic_string(), dataset});
	}
}
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
	vector<fs::path> candidates;
	for (const auto &entry : fs::directory_iterator(root)) {
		if (!entry.is_directory()) {
			continue;
		}
		if (entry.path().filename() == "trials") {
			for (const auto &trial : fs::directory_iterator(entry.path())) {
				if (trial.is_directory()) {
					candidates.push_back(trial.path());
				}
			}
		} else {
			candidates.push_back(entry.path());
		}
	}
	for (const auto &trial : candidates) {
		if (!fs::is_regular_file(trial / "config.json")) {
			continue;
		}
		if (!fs::is_regular_file(trial / "result.json")) {
			throw InvalidInputException("Harbor trial requires result.json: %s", trial.string());
		}
		auto name = trial.lexically_relative(root).generic_string();
		FindTrajectory(root, trial, name, name + ".lance", plan);
		if (fs::is_directory(trial / "steps")) {
			for (const auto &step : fs::directory_iterator(trial / "steps")) {
				if (step.is_directory()) {
					FindTrajectory(root, step.path(), name,
					               name + "/steps/" + step.path().filename().generic_string() + ".lance", plan);
				}
			}
		}
	}
	std::sort(plan.trajectories.begin(), plan.trajectories.end(),
	          [](const RunTrajectory &a, const RunTrajectory &b) { return a.relative_source < b.relative_source; });
	return plan;
}
static bool IsRemote(const string &path) {
	return path.find("://") != string::npos;
}
static string Normalize(const string &path) {
	if (!IsRemote(path)) {
		return fs::weakly_canonical(path).generic_string();
	}
	auto value = path;
	while (!value.empty() && value.back() == '/') {
		value.pop_back();
	}
	return value;
}
static bool Overlaps(const string &a, const string &b) {
	return a == b || a.compare(0, b.size() + 1, b + "/") == 0 || b.compare(0, a.size() + 1, a + "/") == 0;
}
void ValidateDestinations(const RunOptions &options, const RunPlan &plan) {
	auto source = Normalize(plan.source);
	auto job = Normalize(options.job_destination);
	if (Overlaps(source, job)) {
		throw InvalidInputException("job_destination and job_directory must not overlap");
	}
	if (!plan.trajectories.empty() && options.trajectory_destination.empty()) {
		throw InvalidInputException("trajectory_destination is required when the run contains trajectories");
	}
	if (!options.trajectory_destination.empty()) {
		auto trajectory = Normalize(options.trajectory_destination);
		if (Overlaps(source, trajectory) || Overlaps(job, trajectory)) {
			throw InvalidInputException("trajectory_destination must not overlap job_directory or job_destination");
		}
		if (IsRemote(trajectory) && trajectory.compare(0, 5, "s3://") != 0) {
			throw InvalidInputException("trajectory_destination supports only local paths and s3:// URIs");
		}
	}
}
} // namespace duckdb
