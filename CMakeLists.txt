cmake_minimum_required(VERSION 3.15)
project(agent_runs)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include_directories(src/include external/duckdb_lance_conversion/src/include)
set(EXTENSION_SOURCES src/agent_runs_extension.cpp src/schema_migration.cpp
                      src/trajectory_conversion.cpp)
build_static_extension(agent_runs ${EXTENSION_SOURCES})
build_loadable_extension(agent_runs "" ${EXTENSION_SOURCES})
foreach(target agent_runs_extension agent_runs_loadable_extension)
  target_compile_definitions(
    ${target}
    PRIVATE
      AGENT_RUNS_MIGRATION_DIRECTORY="${CMAKE_INSTALL_PREFIX}/share/agent_runs/migrations"
  )
  target_link_libraries(${target} lance_conversion_extension)
endforeach()
install(
  TARGETS agent_runs_extension
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")

install(
  DIRECTORY schema/migrations/
  DESTINATION share/agent_runs/migrations
  COMPONENT agent_runs_schema
  FILES_MATCHING
  PATTERN "*.sql")
