#pragma once

namespace duckdb {

class ExtensionLoader;

//! Register the explicit, transactional schema migration entry point.
void RegisterSchemaMigration(ExtensionLoader &loader);

} // namespace duckdb
