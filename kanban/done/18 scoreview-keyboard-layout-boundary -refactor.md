# Separate ScoreView keyboard layout from NanoVG replay

**Type:** refactor  
**Priority:** P2  
**Raised by:** Codex  
**Depends on:** coordinate with pending `07`

## Boundary verification

- [x] Inventory geometry, palette, presentation values, draw callers, and tests.
- [x] Confirm that the overlay builder and NanoVG renderers share only palette APIs.
- [x] Retain the established palette bytes and representative key-position coverage.

## Implementation and migration

- [x] Add public core `keyboard_layout.h`.
- [x] Move MIDI bounds, geometry, and palette APIs into it.
- [x] Move `KeyboardLit` and draw declarations to a host-private header.
- [x] Update core/host includes.
- [x] Remove the public `keyboard_render_nvg.h` without forwarding duplication.
- [x] Extract keyboard geometry and palette tests from the runtime-classified composer suite.

## Unit tests

- [x] Add `scoreview_keyboard_layout_tests.cpp`.
- [x] Test 88-key bounds, black/white classification, centers, spelling, and palette selection.
- [x] Keep one runtime-level draw smoke if useful (not added: layout/palette behavior is fully exercised without constructing a NanoVG context).
- [x] Build/run `draxul-test-scoreview` and `draxul-test-scoreview-runtime`.

## Cross-platform validation

- [x] Verify NanoVG output and palette bytes on Windows.
- [x] Verify NanoVG output and palette bytes on macOS.
- [x] Confirm core keyboard tests link no runtime/NanoVG/SDL/ImGui dependency.

## Agent documentation/tooling

- [x] Update the Score module ownership reference.

## Acceptance criteria

- [x] Core publishes no `NVGcontext` keyboard API.
- [x] Geometry/palette tests use only `draxul-scoreview`.
- [x] Visual output is unchanged.
- [x] Score core/runtime tests pass.

## Windows validation checkpoint (2026-09-22)

- The post-edit `py do.py test debug --products` gate passed all 48 selected
  entries in the shared Windows Debug/Ninja cache (230.53 s). The ScoreView
  core and runtime entries passed, including the extracted keyboard
  geometry/palette tests, and the subsequent same-cache smoke passed.
- The registered ScoreView render initially showed a deterministic 358-pixel
  (0.058%) one-pixel scrollbar-thumb drift. Visual review confirmed the current
  output was intended; the Windows reference was refreshed and the rerun
  passed.
- A direct Windows rerun of `draxul-test-scoreview.exe
  "[scoreview][keyboard]"` passed 247 assertions in 4 test cases, including
  every exact spelling-palette byte and representative 88-key geometry case.
- Current macOS output/palette verification remains open; this card stays
  pending until that cross-platform criterion is satisfied.

## macOS validation checkpoint (2026-09-22)

- The full focused CTest selection passed all three entries after the closeout
  fix: `draxul-test-scoreview-shard-0` (61.90 s),
  `draxul-test-scoreview-shard-1` (34.28 s), and
  `draxul-test-scoreview-runtime-shard-0` (8.48 s). The added flow-readiness
  regression passed 4 assertions independently.
- A dedicated 1920x1080 Metal capture used `roll nocomposer locktempo`, a
  5-second settle, and the full Grieg fixture. The frame shows the complete
  88-key keyboard, white/black-key geometry, active waterfall colors, the
  spelling palette, and score-note colors. The runtime log confirms 3013
  conveyor draw operations and 287 onsets before capture.
- Flow render tests previously waited forever because runtime readiness was
  tied only to paged `pages_`. Readiness now uses `strip_` in Flow mode and
  `pages_` in Paged mode, with deterministic orchestration coverage.
- The same-cache `draxul --console --smoke-test` passed on Apple M5 Metal. The
  wrapper's first sandboxed attempt could not bind the local control socket;
  running the same built app outside that restriction passed.
- Evidence capture: `/tmp/draxul-scoreview-keyboard-evidence-macos.bmp` (PNG
  inspection copy beside it). The generic registered render remains
  Windows-only; no manifest or reference baseline was changed.
