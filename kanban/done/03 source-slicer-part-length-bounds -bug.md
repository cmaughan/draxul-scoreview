# Reject inconsistent multi-part measure counts before slicing

**Type:** bug  
**Priority:** P1 / sequence 03  
**Raised by:** Claude  
**Consensus:** `plans/reviews/paged-roll-transition-consensus.md`

## Problem

`SourceSlicer::window_xml_for()` validates a source-bar index against the first
part's measure count, then indexes every part with that value. A malformed or
truncated later part can therefore cause out-of-bounds access while entering Roll.

## Implementation

- [x] Validate every referenced source bar against every emitted part before indexing measures or `state_before`.
- [x] Define a stable rejection category for inconsistent part lengths.
- [x] Keep valid multi-part verbatim streaming behavior unchanged.
- [x] Avoid partial output when validation fails.
- [x] Cross-reference the broader compressed-input corpus in `kanban/ice-box/18 hostile-mxl-inputs -test.md` without conflating ZIP validation with MusicXML structure validation.

## Tests

- [x] Add deterministic two-part fixtures with shorter and empty trailing parts.
- [x] Cover mismatched `state_before` and measure vectors (the loader constructs both in lockstep; malformed source lengths are rejected before either can be indexed).
- [x] Assert bounded rejection with no crash or undefined behavior.
- [x] Retain valid equal-length multi-part window equivalence coverage.
- [x] Run malformed fixtures under the normal Debug test runner; sanitizer builds were removed by project decision and are no longer a gate.

## Acceptance criteria

- [x] No source-bar index is used before per-part bounds validation.
- [x] Inconsistent MusicXML fails safely with an actionable error.
- [x] Valid multi-part sources still render correctly.
- [x] Source-slicer and ScoreView aggregate tests pass.

## Evidence

`SourceSlicer::load()` rejects unequal part measure counts with `source parts have inconsistent measure counts`, clears the partial model, and `window_xml_for()` retains defensive per-part measure/state checks before cloning. `window_xml()` also avoids signed overflow in range validation. Focused stream tests passed 8 cases / 225 assertions, including shorter, empty, longer, and equal-length two-part sources. This is MusicXML structure validation; the separate compressed ZIP corpus remains owned by `kanban/ice-box/18 hostile-mxl-inputs -test.md`. Final ScoreView-scoped aggregate passed 28/28 and same-cache Debug `run -- --smoke-test` passed; the standard 30-second wrapper timed out on the existing nine-pane Session.
