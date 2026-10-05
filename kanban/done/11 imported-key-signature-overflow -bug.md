# Validate imported key-signature arithmetic
**Summary:** Validate extreme key signatures so imported scores cannot cause invalid musical calculations.

**Priority:** 11  
**Severity:** CRITICAL  
**Source:** `plugins/scoreview/product/draxul-score-learn/src/piece_analysis.cpp`  
**Reported by:** Claude M16; consensus F20.

**Evidence and trigger:** Source line 63 multiplies fifths by seven after the importer accepts unrestricted representable integers. `<fifths>2147483647</fifths>` overflows when analyzed.

- [x] **Investigate:** Trace imported signatures through paged and conveyor analysis and define supported input policy.
- [x] **Fix:** Reject unsupported imported signatures and independently use safe pitch-class normalization.
- [x] **Acceptance:** Extreme positive/negative signatures cannot overflow; accepted ordinary signatures retain their expected analysis prior.
- [x] **Acceptance:** Rejected import values produce controlled diagnostics rather than invalid model state.
- [x] **Validation:** Run the ScoreView-scoped aggregate and same-cache smoke; use arithmetic instrumentation where available.

## Resolution

- **Root cause:** the MusicXML importer accepted any `std::stoi` value for `<fifths>`.
  Unrepresentable values silently became 0, and trailing junk was accepted. `estimate_key`
  (`piece_analysis.cpp`) then computed `fifths * 7` before reducing, which is signed overflow
  for large values. Both the paged and the conveyor analysis paths in `score_runtime.cpp` pass
  the imported value straight through.
- **Fix:**
  - Policy: `KeySignature::kMinFifths/kMaxFifths` = -7..7
    (`product/draxul-notation/include/draxul/notation/score_document.h`).
  - `MusicXmlImporter::parse_key` (`product/draxul-notation/src/musicxml_importer.cpp`) parses
    `<fifths>` strictly. Out-of-range, unrepresentable, or malformed values add a
    measure-located warning and leave the measure without a key change. A `<key>` with no
    `<fifths>` keeps the C major default.
  - `estimate_key` reduces `fifths % 12` before multiplying, so it is total for every `int`.
- **Tests:** `tests/notation_importer_tests.cpp`, "imported key signatures outside the
  supported range are rejected with a warning", checks that the -7..7 range and a missing
  `<fifths>` are accepted. It checks that INT_MAX/INT_MIN, 20-digit values, ±8, `3x`, `sharp`,
  `+-3`, and `1.5` are rejected with one warning. `tests/scoreview_analysis_tests.cpp`, "analysis
  signature prior is safe for any notated fifths value", checks the ordinary priors and that
  2^30, INT_MAX, INT_MIN, and -2^30 match their mod-12 equivalents.
- **Arithmetic instrumentation:** a standalone `-fsanitize=signed-integer-overflow` build of the
  old and new expressions flagged `2147483647 * 7` in the old one and was clean for the new one.
- **Validation:** `python3 do.py test debug --scoreview` (core + scoreview, 59 CTest entries): every ScoreView suite passed; the only failures were the known pre-existing core `draxul-test-app-shard-0` (tests/app_dispatch_tests.cpp:453) and `draxul-do-py-tests`, which reads the uninitialized megacity submodule's AGENTS.md in this worktree. `python3 do.py smoke --skip-build` passed from the same cache.
