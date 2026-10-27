#pragma once

#include <string_view>

namespace duckdb {

//! Compare borrowed strings case-insensitively without copying them.
bool CIEquals(std::string_view lhs, std::string_view rhs);

} // namespace duckdb
