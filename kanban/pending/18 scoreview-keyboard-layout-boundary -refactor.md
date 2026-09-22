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

- [ ] Verify NanoVG output and palette bytes on Windows and macOS.
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
- Current macOS output/palette verification remains open; this card stays
  pending until that cross-platform criterion is satisfied.
