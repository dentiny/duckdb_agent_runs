#include "duckdb/common/string_util.hpp"
#include "util.hpp"

namespace duckdb {

bool CIEquals(std::string_view lhs, std::string_view rhs) {
	return StringUtil::CIEquals(lhs.data(), lhs.size(), rhs.data(), rhs.size());
}

} // namespace duckdb
