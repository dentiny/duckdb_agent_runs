#include "run_ingest.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/common/path.hpp"
namespace duckdb {
void UploadRun(ClientContext &context, const RunOptions &options, const RunPlan &plan) {
	auto &fs = FileSystem::GetFileSystem(context);
	// Preflight every object before creating anything. This is not a distributed lock.
	for (const auto &file : plan.files) {
		const auto target = fs.JoinPath(options.job_destination, file.relative_path);
		if (fs.FileExists(target)) {
			throw IOException("Destination already exists: %s", target);
		}
	}
	if (options.dry_run) {
		return;
	}
	vector<char> buffer(1024 * 1024);
	for (const auto &file : plan.files) {
		context.InterruptCheck();
		auto source = fs.OpenFile(fs.JoinPath(plan.source, file.relative_path), FileFlags::FILE_FLAGS_READ);
		if (source->GetFileSize() != file.size) {
			throw IOException("Source changed during ingestion: %s", file.relative_path);
		}
		const auto target = fs.JoinPath(options.job_destination, file.relative_path);
		if (target.find("://") == string::npos) {
			fs.CreateDirectoriesRecursive(Path::FromString(target).Parent().ToString());
		}
		auto destination = fs.OpenFile(target, FileFlags::FILE_FLAGS_WRITE | FileFlags::FILE_FLAGS_FILE_CREATE |
		                                           FileFlags::FILE_FLAGS_EXCLUSIVE_CREATE);
		try {
			idx_t offset = 0;
			while (offset < file.size) {
				context.InterruptCheck();
				auto count = MinValue<idx_t>(buffer.size(), file.size - offset);
				source->Read(buffer.data(), count, offset);
				idx_t written = 0;
				while (written < count) {
					auto size = destination->Write(buffer.data() + written, count - written);
					if (size <= 0) {
						throw IOException("Incomplete write: %s", target);
					}
					written += idx_t(size);
				}
				offset += count;
			}
			destination->Close();
		} catch (...) {
			destination->AbortWrite();
			throw;
		}
	}
}
} // namespace duckdb
