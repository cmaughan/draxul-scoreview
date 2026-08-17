# Hostile compressed MusicXML inputs

**Type:** test

## Gap

ScoreView covers a valid `.mxl`, generic garbage, and pure-XML rejection, but the
Verovio ZIP path lacks a bounded negative corpus for truncated, malformed, encrypted,
overlapping, traversal, and decompression-bomb archives.

## Work

- [ ] Define compressed/uncompressed byte, entry-count, per-entry, nesting, and load
      deadline limits before untrusted data reaches Verovio.
- [ ] Reject traversal, duplicate roots, encryption, invalid central-directory data,
      unsupported compression, and excessive expansion with stable error categories.
- [ ] Generate tiny deterministic negative fixtures and run them repeatedly under the
      available sanitizers.
- [ ] Preserve valid `.mxl` loading and ordinary MusicXML layout.

Complete this before file-drop routing or a piece library makes `.mxl` opening more
discoverable.
