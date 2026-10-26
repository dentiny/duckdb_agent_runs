#pragma once

#include "duckdb/common/string.hpp"
#include "duckdb/common/typedefs.hpp"
#include "duckdb/common/vector.hpp"

namespace duckdb {

class ClientContext;
class ExtensionLoader;

enum class RunFormat : uint8_t { HARBOR };

struct RunOptions {
	//! Job directory or object-storage prefix supplied by the job_directory SQL argument.
	string source;
	RunFormat format = RunFormat::HARBOR;
	string job_destination;
	bool dry_run = false;
};

struct RunFile {
	//! Path relative to the job root; append to source/destination roots to preserve the job hierarchy.
	string relative_path;
	idx_t size;
};

struct RunPlan {
	string source;
	vector<RunFile> files;
	idx_t total_bytes = 0;
};

//! Inspect a Harbor job directory or object-storage prefix and inventory its regular files in stable path order.
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
