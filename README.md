# agent_runs

DuckDB extension for uploading complete agent runs and converting their trajectories to Lance.
Harbor is the first input adapter. Job/Trial metadata catalog support is separate future work.

The pinned `external/duckdb_lance_conversion` Git submodule supplies ATIF reading,
media handling, and Lance writing. Both extensions build against the same DuckDB
submodule. `LOAD agent_runs` loads the statically bundled conversion dependency;
no runtime download or `INSTALL` is needed.

## Build

```sh
git clone --recurse-submodules https://github.com/dentiny/duckdb_agent_runs.git
cd duckdb_agent_runs
# Requires CMake, a C++17 compiler, Cargo, and protoc (or vcpkg protobuf).
GEN=ninja make
make test
```

For an existing checkout, run `git submodule update --init --recursive` first.
The loadable binary is `build/release/extension/agent_runs/agent_runs.duckdb_extension`.
DuckDB extensions require the exact DuckDB version they were built against.

## Review sequence

1. Initialize agent_runs and bundle the pinned Lance conversion submodule.
2. Add the single ingestion function, Harbor adapter, and hierarchy-preserving uploads.
3. Convert discovered ATIF trajectories to Lance and add end-to-end coverage.
