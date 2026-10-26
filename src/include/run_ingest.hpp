#pragma once
#include "duckdb.hpp"
namespace duckdb {
enum class RunFormat : uint8_t { HARBOR };

struct RunOptions {
	//! Local job directory supplied by the job_directory SQL argument.
	string source;
	RunFormat format = RunFormat::HARBOR;
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
//! Inspect a local Harbor job and inventory its regular files in stable path order.
//! Require config.json/result.json and reject resolvable internal symlinks; write nothing.
//! Directory enumeration follows the calling context's DuckDB filesystem semantics.
RunPlan PlanHarborRun(ClientContext &context, const RunOptions &options);
//! Reject overlapping source/output roots before ingestion writes any files.
void ValidateDestinations(ClientContext &context, const RunOptions &options, const RunPlan &plan);
//! Preflight targets, then stream planned files to job_destination while preserving relative paths.
//! Dry runs only preflight; a failure can leave files uploaded earlier in the run.
void UploadRun(ClientContext &context, const RunOptions &options, const RunPlan &plan);
//! Register ingest_agent_run and its named SQL parameters with the extension loader.
//! Binding validates arguments; file I/O is deferred until execution.
void RegisterRunIngest(ExtensionLoader &loader);
} // namespace duckdb
