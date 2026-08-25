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

- [ ] Validate every referenced source bar against every emitted part before indexing measures or `state_before`.
- [ ] Define a stable rejection category for inconsistent part lengths.
- [ ] Keep valid multi-part verbatim streaming behavior unchanged.
- [ ] Avoid partial output when validation fails.
- [ ] Cross-reference the broader compressed-input corpus in `kanban/ice-box/18 hostile-mxl-inputs -test.md` without conflating ZIP validation with MusicXML structure validation.

## Tests

- [ ] Add deterministic two-part fixtures with shorter and empty trailing parts.
- [ ] Cover mismatched `state_before` and measure vectors.
- [ ] Assert bounded rejection with no crash or undefined behavior.
- [ ] Retain valid equal-length multi-part window equivalence coverage.
- [ ] Run available sanitizer coverage for the malformed fixtures.

## Acceptance criteria

- [ ] No source-bar index is used before per-part bounds validation.
- [ ] Inconsistent MusicXML fails safely with an actionable error.
- [ ] Valid multi-part sources still render correctly.
- [ ] Source-slicer and ScoreView aggregate tests pass.
