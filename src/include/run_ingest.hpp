#pragma once
#include "duckdb.hpp"
namespace duckdb {
struct RunOptions {
	string source;
	string format = "harbor";
	string job_destination;
	string trajectory_destination;
	string text_compression = "lz4";
	bool dry_run = false;
};
struct RunFile {
	string relative_path;
	idx_t size;
};
struct RunTrajectory {
	string trial_name;
	string relative_source;
	string relative_dataset;
};
struct RunPlan {
	string source;
	vector<RunFile> files;
	vector<RunTrajectory> trajectories;
	idx_t total_bytes = 0;
};
RunPlan PlanHarborRun(const RunOptions &options);
void ValidateDestinations(const RunOptions &options, const RunPlan &plan);
void PreflightTrajectories(ClientContext &context, const RunOptions &options, const RunPlan &plan);
void ConvertTrajectories(ClientContext &context, const RunOptions &options, const RunPlan &plan);
void UploadRun(ClientContext &context, const RunOptions &options, const RunPlan &plan);
void RegisterRunIngest(ExtensionLoader &loader);
} // namespace duckdb
