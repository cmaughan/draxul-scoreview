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
- [ ] Extract keyboard tests from the host-classified composer suite.

## Unit tests

- [ ] Add `scoreview_keyboard_layout_tests.cpp`.
- [ ] Test 88-key bounds, black/white classification, centers, spelling, and palette selection.
- [ ] Keep one host-level draw smoke if useful.
- [ ] Build/run `draxul-test-scoreview` and `draxul-test-scoreview-host`.

## Cross-platform validation

- [ ] Verify NanoVG output and palette bytes on Windows and macOS.
- [ ] Confirm core keyboard tests link no host/NanoVG/SDL/ImGui dependency.

## Agent documentation/tooling

- [x] Update the Score module ownership reference.

## Acceptance criteria

- [ ] Core publishes no `NVGcontext` keyboard API.
- [ ] Geometry/palette tests use only `draxul-scoreview`.
- [ ] Visual output is unchanged.
- [ ] Score core/host tests pass.
