# Use the pinned submodule, never a runtime INSTALL from the community
# repository.
file(
  STRINGS
  "${CMAKE_CURRENT_LIST_DIR}/external/duckdb_lance_conversion/rust/Cargo.toml"
  LANCE_VERSION_LINE
  REGEX "^version = \"[^\"]+\"$"
  LIMIT_COUNT 1)
string(REGEX REPLACE "^version = \"([^\"]+)\"$" "\\1" LANCE_VERSION
                     "${LANCE_VERSION_LINE}")
duckdb_extension_load(
  lance_conversion SOURCE_DIR
  ${CMAKE_CURRENT_LIST_DIR}/external/duckdb_lance_conversion EXTENSION_VERSION
  ${LANCE_VERSION})
duckdb_extension_load(agent_runs SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}
                      EXTENSION_VERSION "0.1.0")
duckdb_extension_load(parquet)

if(BUILD_EXTENSION_TEST_DEPS STREQUAL "default" OR BUILD_EXTENSION_TEST_DEPS
                                                   STREQUAL "full")
  include(${CMAKE_CURRENT_LIST_DIR}/test/extension_config.cmake)
endif()
