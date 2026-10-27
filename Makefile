PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Configuration of extension
EXT_NAME=agent_runs
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Use checkout SQL files for development; installed builds use their resource directory.
export DUCKDB_AGENT_RUNS_MIGRATIONS ?= $(PROJ_DIR)schema/migrations

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile

format-all: format-fix
	cmake-format -i CMakeLists.txt extension_config.cmake test/extension_config.cmake

.PHONY: format-all
