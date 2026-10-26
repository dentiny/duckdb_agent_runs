# ATIF fixtures

`atif/text/` contains an ATIF v1.8 text document and its deterministic gzip form.
`atif/multimodal/` contains Unicode text, PNG/WAV user messages, a tool call with
image/audio observations, and a final response. Media paths are relative.

The JSON and media were copied from synthetic conformance fixtures in
`duckdb_lance_conversion` at commit
`0c27b7cfa5746c8a3b99cf2712b83cec4588a3ae`; these are not production captures.
The image is a 1024 × 768 RGB pattern. The audio is a 0.1-second 440 Hz mono
16-bit PCM tone sampled at 8 kHz. Public API tests convert these multimodal
fixtures and compressed text inputs through `import_agent_trajectory`, then
read the datasets with Lance to compare metadata, text, tool calls, and embedded
media bytes against the original local files.

`harbor/` contains static native Harbor config/result pairs adapted from
`ml-infra`'s catalog API fixtures at commit
`d7af6649c51790c78afc1ed4d5af990a1ec634b7`. Root/trial result files include native
UUIDs and trial names. Config/result payloads exercise typed nested structures
and VARIANT values larger than 2^53. The deliberately invalid trajectory files
must remain unread by metadata ingestion. The invalid-trial fixture exercises
transaction rollback after the job append; the empty job has no trials.
