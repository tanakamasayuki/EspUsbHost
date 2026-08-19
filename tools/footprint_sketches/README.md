# Footprint measurement sketches

> 日本語版: [README.ja.md](README.ja.md)

These sketches are fixed probes used by `tools/footprint_matrix.py`. They are not
user-facing examples. Keep them small and stable: changing a probe changes the
measurement baseline for every library version.

`Base` measures common USB Host startup cost. The other probes reference one
representative feature so their Flash and static RAM deltas can be compared with
`Base`. Unused library code is removed by the linker, so every probe must call the
feature API it intends to measure.
