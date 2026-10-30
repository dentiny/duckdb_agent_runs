cmake_minimum_required(VERSION 3.15)
project(agent_runs)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include_directories(
  ${CMAKE_CURRENT_BINARY_DIR} src/include
  external/duckdb_lance_conversion/src/include duckdb/extension/json/include
  duckdb/third_party/mbedtls/include)

# Community installs distribute one extension binary, so compile the SQL files
# into it while keeping the checked-in SQL as the migration source of truth.
set(EMBEDDED_MIGRATIONS_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/agent_runs_embedded_migrations.hpp")
file(
  WRITE "${EMBEDDED_MIGRATIONS_HEADER}"
  "#pragma once\n\n#include <string_view>\n\nnamespace duckdb {\n"
  "struct EmbeddedMigration { std::string_view filename; std::string_view sql; };\n"
)
file(GLOB MIGRATION_SQL_FILES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/schema/migrations/*.sql")
list(SORT MIGRATION_SQL_FILES)
set_property(
  DIRECTORY
  APPEND
  PROPERTY CMAKE_CONFIGURE_DEPENDS ${MIGRATION_SQL_FILES})
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/schema/migrations/schema.sql")
  message(FATAL_ERROR "Missing schema/migrations/schema.sql")
endif()
foreach(sql_file IN LISTS MIGRATION_SQL_FILES)
  get_filename_component(filename "${sql_file}" NAME)
  file(READ "${sql_file}" sql)
  if(sql MATCHES "\\)agent_runs_sql\"")
    message(
      FATAL_ERROR
        "Migration SQL contains the C++ raw-string delimiter: ${filename}")
  endif()
  if(filename STREQUAL "schema.sql")
    file(
      APPEND "${EMBEDDED_MIGRATIONS_HEADER}"
      "inline constexpr std::string_view AGENT_RUNS_SCHEMA_SQL = R\"agent_runs_sql(${sql})agent_runs_sql\";\n"
    )
  elseif(filename MATCHES "^[0-9][0-9][0-9][0-9]\\.sql$")
    string(
      APPEND migration_entries
      "    {\"${filename}\", R\"agent_runs_sql(${sql})agent_runs_sql\"},\n")
  else()
    message(FATAL_ERROR "Unexpected migration SQL filename: ${filename}")
  endif()
endforeach()
if(NOT migration_entries)
  message(FATAL_ERROR "No numbered schema migrations found")
endif()
file(
  APPEND "${EMBEDDED_MIGRATIONS_HEADER}"
  "inline constexpr EmbeddedMigration AGENT_RUNS_MIGRATIONS[] = {\n${migration_entries}};\n} // namespace duckdb\n"
)

set(EXTENSION_SOURCES src/agent_runs_extension.cpp src/harbor_metadata.cpp
                      src/run_ingest.cpp src/schema_migration.cpp)
build_static_extension(agent_runs ${EXTENSION_SOURCES})
build_loadable_extension(agent_runs "" ${EXTENSION_SOURCES})
foreach(target agent_runs_extension agent_runs_loadable_extension)
  target_link_libraries(${target} lance_conversion_extension json_extension
                        duckdb_mbedtls)
endforeach()
install(
  TARGETS agent_runs_extension
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
