cmake_minimum_required(VERSION 3.15)
project(agent_runs)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include_directories(src/include external/duckdb_lance_conversion/src/include)
# Embed ordered, immutable migrations; installed extensions need no SQL files.
file(GLOB migration_files CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/schema/migrations/*.sql")
list(SORT migration_files)
if(NOT migration_files)
  message(FATAL_ERROR "No agent_runs schema migrations found")
endif()
set(AGENT_RUNS_MIGRATIONS "")
foreach(migration ${migration_files})
  get_filename_component(version_name "${migration}" NAME_WE)
  if(NOT version_name MATCHES "^[0-9][0-9][0-9][0-9]$" OR version_name STREQUAL
                                                          "0000")
    message(
      FATAL_ERROR
        "Migration filenames must be positive four-digit versions: ${migration}"
    )
  endif()
  math(EXPR version "1${version_name} - 10000")
  set_property(
    DIRECTORY
    APPEND
    PROPERTY CMAKE_CONFIGURE_DEPENDS "${migration}")
  file(READ "${migration}" migration_sql)
  string(
    APPEND AGENT_RUNS_MIGRATIONS
    "\t{${version}, R\"agent_runs_sql(${migration_sql})agent_runs_sql\"},\n")
endforeach()
set(AGENT_RUNS_SCHEMA_VERSION ${version})
configure_file(src/schema_migrations.hpp.in agent_runs_schema_migrations.hpp
               @ONLY)

set(EXTENSION_SOURCES src/agent_runs_extension.cpp src/schema_migration.cpp
                      src/trajectory_conversion.cpp)
build_static_extension(agent_runs ${EXTENSION_SOURCES})
build_loadable_extension(agent_runs "" ${EXTENSION_SOURCES})
foreach(target agent_runs_extension agent_runs_loadable_extension)
  target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
  target_link_libraries(${target} lance_conversion_extension)
endforeach()
install(
  TARGETS agent_runs_extension
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
