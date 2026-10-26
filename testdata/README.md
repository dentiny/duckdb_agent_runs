# ATIF fixtures

`atif/text/` contains an ATIF v1.8 text document and its deterministic gzip form.
`atif/multimodal/` contains Unicode text, PNG/WAV user messages, a tool call with
image/audio observations, and a final response. Media paths are relative.

The JSON and media were copied from synthetic conformance fixtures in
`duckdb_lance_conversion` at commit
`0c27b7cfa5746c8a3b99cf2712b83cec4588a3ae`; these are not production captures.
The image is a 1024 × 768 RGB pattern. The audio is a 0.1-second 440 Hz mono
16-bit PCM tone sampled at 8 kHz. Tests check the exact media bytes in written
Lance fragments, for both message and observation references.
