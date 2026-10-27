#pragma once

#include "duckdb/common/string.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckdb/common/vector.hpp"

namespace duckdb {

class ClientContext;
class ExtensionLoader;

enum class RunFormat : uint8_t { HARBOR };

struct RunOptions {
	//! Job directory or object-storage prefix containing Harbor config/result files.
	string source;
	RunFormat format = RunFormat::HARBOR;
	//! Per-job root where each trial's ATIF trajectory is converted to a Lance dataset.
	string trajectory_destination;
};

struct RunRecord {
	Value id;
	string name;
	Value config;
	Value result;
	//! Harbor trial trajectory path; empty for the job record.
	string trajectory_path;
};

struct RunMetadata {
	RunRecord job;
	vector<RunRecord> trials;
};

//! Read job/trial metadata and locate each trial's ATIF trajectory.
//! Trial discovery supports direct directories and the trials/ container.
RunMetadata ReadHarborRun(ClientContext &context, const RunOptions &options);
//! Register ingestion into the existing typed jobs/trials tables in the caller's transaction.
void RegisterRunIngest(ExtensionLoader &loader);

} // namespace duckdb
