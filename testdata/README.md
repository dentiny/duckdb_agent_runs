# ATIF fixtures

These checked-in fixtures belong to this extension. SQLLogicTests read them directly or use the checked-in Harbor job layouts in
`harbor/`; they do not load fixtures from submodules or construct ATIF documents
at runtime.

- `atif/text/trajectory.json`: a minimal ATIF v1.8 text conversation.
- `atif/text/trajectory.json.gz`: the same document, compressed with a fixed gzip
  timestamp. Each Harbor trial contains only its intended representation.
- `atif/multimodal/`: an ATIF v1.8 conversation with Unicode text, image/audio
  messages, a tool call, image/audio observations, and a final response. Relative
  media paths resolve to the adjacent PNG and WAV files.
- `atif/missing_media/trajectory.json`: intentionally references an absent image.
- `atif/invalid/trajectory.json`: intentionally malformed input.

The valid JSON and media fixtures were copied from the generated conformance
fixtures in `duckdb_lance_conversion` at commit
`0c27b7cfa5746c8a3b99cf2712b83cec4588a3ae`. They are synthetic data, not production
agent captures. The PNG is a 1024 × 768 RGB pattern; the WAV is a 0.1-second,
440 Hz mono 16-bit PCM tone sampled at 8 kHz.

`harbor/` contains complete job/trial metadata and copies of the ATIF fixtures
for upload-only, multimodal, multi-step/gzip, and intentionally invalid jobs.
The `symlink` and `symlink_loop` jobs exercise POSIX-only link rejection; their
SQLLogicTest skips Windows.

Both local and S3 SQLLogicTests verify that the original image and audio bytes
are embedded in Lance for both messages and tool observations. The checks read
only the output Lance fragments when searching for embedded media.
