#pragma once
#include "duckdb.hpp"
namespace duckdb {
struct RunOptions {
	string source;
	string format = "harbor";
	string job_destination;
	bool dry_run = false;
};
struct RunFile {
	string relative_path;
	idx_t size;
};
struct RunPlan {
	string source;
	vector<RunFile> files;
	idx_t total_bytes = 0;
};
RunPlan PlanHarborRun(const RunOptions &options);
void ValidateDestinations(const RunOptions &options, const RunPlan &plan);
void UploadRun(ClientContext &context, const RunOptions &options, const RunPlan &plan);
void RegisterRunIngest(ExtensionLoader &loader);
} // namespace duckdb
