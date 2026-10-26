PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Configuration of extension
EXT_NAME=agent_runs
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile

PYTHON ?= python3
CMAKE_FORMAT ?= cmake-format
BUILD_TYPE ?= release
DUCKDB_BIN ?= $(PROJ_DIR)build/$(BUILD_TYPE)/duckdb
AGENT_RUNS_EXTENSION ?= $(PROJ_DIR)build/$(BUILD_TYPE)/extension/agent_runs/agent_runs.duckdb_extension
INTEGRATION_TEST_ARGS ?= -v
HTTPFS_EXTENSION ?=

# The shared Makefile's "format" target checks formatting; format-fix applies it.
# The shared formatter covers C++, Python, and SQL tests in src/test.
# Format this extension only, leaving the pinned dependency submodules untouched.
format-all: format-fix
	$(CMAKE_FORMAT) -i CMakeLists.txt extension_config.cmake

format-check-all: format-check
	$(CMAKE_FORMAT) --check CMakeLists.txt extension_config.cmake

# These targets use an existing build, just like test_release/test_debug/test_reldebug.
define RUN_INGEST_TESTS
	$(PYTHON) "$(PROJ_DIR)test/integration/test_ingest.py" \
		--duckdb "$(DUCKDB_BIN)" --extension "$(AGENT_RUNS_EXTENSION)" $(1) $(INTEGRATION_TEST_ARGS)
endef

test_integration test_release_integration test_debug_integration test_reldebug_integration:
	$(call RUN_INGEST_TESTS,)

test_s3 test_release_s3 test_debug_s3 test_reldebug_s3:
	@test -n "$(HTTPFS_EXTENSION)" || { echo "Set HTTPFS_EXTENSION to the matching DuckDB httpfs extension binary." >&2; exit 1; }
	$(call RUN_INGEST_TESTS,--httpfs "$(HTTPFS_EXTENSION)")

test_release_integration test_release_s3: BUILD_TYPE = release
test_debug_integration test_debug_s3: BUILD_TYPE = debug
test_reldebug_integration test_reldebug_s3: BUILD_TYPE = reldebug

.PHONY: format-all format-check-all test_integration test_s3 \
	test_release_integration test_debug_integration test_reldebug_integration \
	test_release_s3 test_debug_s3 test_reldebug_s3
