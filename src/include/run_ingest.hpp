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
	string owner_email;
	//! Exact per-job root of already converted datasets; used only to populate trials.dataset_uri.
	string trajectory_destination;
};

struct RunRecord {
	Value id;
	string name;
	Value config;
	Value result;
};

struct RunMetadata {
	RunRecord job;
	vector<RunRecord> trials;
};

//! Read only the job/trial config and result files, retaining nested values as VARIANT.
//! Trial discovery supports direct directories and the trials/ container; no artifacts are copied.
RunMetadata ReadHarborRun(ClientContext &context, const RunOptions &options);
//! Register ingestion into the existing typed jobs/trials tables in the caller's transaction.
void RegisterRunIngest(ExtensionLoader &loader);

} // namespace duckdb
