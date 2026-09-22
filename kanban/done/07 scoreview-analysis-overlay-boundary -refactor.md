# Split ScoreView analysis-overlay build from NanoVG drawing

**Type:** refactor
**Priority:** P2 / sequence 07
**Raised by:** Claude
**Consensus:** `plans/reviews/review-refactor-consensus.md`, Accepted 8

## Goal

Compile pure analysis-overlay geometry construction into `draxul-scoreview` and
keep NanoVG replay private to `draxul-scoreview-host`, allowing overlay geometry
tests to use the lighter core test target.

## Boundary verification

- [x] Verify `analysis_overlay.cpp` construction and draw regions have no hidden shared mutable state.
- [x] Inventory all declarations/callers in `analysis_overlay.h`, `ScoreHost`, and `ScorePresentation`.
- [x] Confirm core construction requires only draw-list, timemap, highlight, and learning profile values.
- [x] Confirm drawing alone needs `NVGcontext` and `ScoreTextFonts`.
- [x] Record current overlay geometry coverage before moving it; NanoVG replay coverage remains host-side.

## Implementation and migration

- [x] Keep `AnalysisOverlay` values and `build_analysis_overlay` in the public core header.
- [x] Move construction to `analysis_overlay_build.cpp` in `draxul-scoreview`.
- [x] Move NanoVG replay to `analysis_overlay_draw.cpp` and a host-private draw header.
- [x] Update `ScoreHost`/`ScorePresentation` includes and target source lists.
- [x] Move `scoreview_overlay_tests.cpp` from runtime test sources to core ScoreView test sources.
- [x] Do not reopen or redesign the completed ScoreView controllers retained in
  repository history.

## Unit tests

- [x] Run every existing overlay geometry assertion through `draxul-test-scoreview`.
- [x] Add/retain one runtime-level draw smoke case for NanoVG replay if current coverage exercises drawing (not applicable: existing overlay coverage is construction-only; replay moved without behavioral edits).
- [x] Verify no core test links SDL, ImGui, NanoVG, runtime, or microphone code for overlay construction.
- [x] Build both `draxul-test-scoreview` and `draxul-test-scoreview-runtime`.
- [x] Run CTest labels `scoreview` and `scoreview-runtime`.

## Cross-platform validation

- [x] Build and run focused tests on Windows.
- [x] Build and run focused tests on macOS.
- [x] Confirm NanoVG overlay output/ordering on Windows.
- [x] Confirm NanoVG overlay output/ordering remains identical on macOS.
- [x] Confirm no Vulkan/Metal API or resource type enters the pure build path.
- [x] Do not touch microphone/audio code; verify macOS TCC ordering remains unaffected.

## Agent documentation and tooling

- [x] Update Score module ownership documentation.
- [x] Ensure label tooling builds core-only overlay tests for `--label scoreview`.

## Acceptance criteria

- [x] Pure overlay tests link to `draxul-scoreview`, not `draxul-scoreview-runtime`.
- [x] NanoVG replay remains runtime-private and behaviorally unchanged.
- [x] Public analysis value/build API remains source compatible.
- [x] Focused ScoreView core/runtime tests, full tests, and smoke pass.

## Dependencies and ownership

Depends on the core repository's internal-target build-policy work. One ScoreView
owner performs the declaration/source split. Core test migration and a
host draw smoke can proceed independently after declarations settle.

## Windows validation checkpoint (2026-09-22)

- The post-edit `py do.py test debug --products` gate passed all 48 selected
  entries in the shared Windows Debug/Ninja cache (230.53 s). The ScoreView
  core and runtime entries passed, including all migrated overlay geometry
  assertions, and the subsequent same-cache smoke passed.
- The registered ScoreView render initially showed a deterministic 358-pixel
  (0.058%) one-pixel scrollbar-thumb drift. Visual review confirmed the current
  output was intended; the Windows reference was refreshed and the rerun
  passed.
- A direct Windows rerun of `draxul-test-scoreview.exe
  "[scoreview][overlay]"` passed 483 assertions in 11 test cases. This pins the
  migrated geometry, ordering, palette indices, and real-Grieg overlay cases in
  the core-only target.
- A current macOS build/render comparison remains open.

## macOS validation checkpoint (2026-09-22)

- The full focused CTest selection passed all three entries after the closeout
  fix: `draxul-test-scoreview-shard-0` (61.90 s),
  `draxul-test-scoreview-shard-1` (34.28 s), and
  `draxul-test-scoreview-runtime-shard-0` (8.48 s). The added flow-readiness
  regression passed 4 assertions independently.
- A dedicated 1920x1080 Metal capture used `paged analysis`, a 5-second
  settle, and the full Grieg fixture. The frame visibly retains the analysis
  banner, key summary, colored motif legend, phrase spans and labels, note
  guidance washes, and their order over the engraved score. The runtime log
  confirms A minor, 15 chords, 8 motifs, 5 figures, 2 responsive pages, and
  3097 draw operations before capture.
- The same-cache `draxul --console --smoke-test` passed on Apple M5 Metal. The
  wrapper's first sandboxed attempt could not bind the local control socket;
  running the same built app outside that restriction passed.
- Evidence capture: `/tmp/draxul-scoreview-analysis-evidence-macos.bmp` (PNG
  inspection copy beside it). The generic registered render remains
  Windows-only; no manifest or reference baseline was changed.
