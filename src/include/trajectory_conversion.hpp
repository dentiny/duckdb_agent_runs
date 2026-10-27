#pragma once

namespace duckdb {

class ExtensionLoader;

//! Register the public ATIF-to-Lance conversion function; dependency SQL stays internal.
void RegisterTrajectoryConversion(ExtensionLoader &loader);

} // namespace duckdb
