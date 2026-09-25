# Cache immutable ScoreView piece analysis across view changes

**Type:** refactor  
**Priority:** P3 / sequence 05  
**Raised by:** Claude  
**Consensus:** `plans/reviews/paged-roll-transition-consensus.md`

## Problem

Every `relayout_flow()` recomputes the same piece analysis and rewrites the same
`.analysis.json`, even though the analysis is a pure function of the source
timemap and notated key. View toggles and fallback rebuilds therefore repeat CPU
work and filesystem writes unnecessarily.

## Implementation

- [x] Track analysis validity by source identity and analysis inputs.
- [x] Reuse the cached `PieceProfile` across paged/flow toggles, restyles, and window rebuilds.
- [x] Invalidate only when source bytes or analysis-relevant options change.
- [x] Write `.analysis.json` only when the profile changes or the cache file is missing/corrupt.
- [x] Preserve composer configuration against the reused profile.

## Tests

- [x] Count analysis builds and persistence writes across repeated view toggles.
- [x] Assert one analysis for an unchanged source.
- [x] Assert source replacement invalidates and rebuilds the profile.
- [x] Assert missing/corrupt analysis dumps are repaired without changing the in-memory profile.

## Acceptance criteria

- [x] Repeated view transitions perform no redundant analysis or filesystem write.
- [x] Source changes still produce fresh, correct analysis.
- [x] Composer and inspector output remain unchanged.
- [x] ScoreView aggregate tests and same-cache smoke pass.

## Evidence

`ScoreRuntime` keys analysis on the exact source bytes, quarter length and notated key; `ScoreSessionController` rewrites the profile dump only when its bytes differ, so missing/corrupt dumps repair without recomputation. Composer configuration now sees the current cached profile. The deterministic host test counted one analysis/write over two unchanged Flow builds, independent repair writes, and a second build after source replacement; focused host suite passed 33 cases / 333 assertions. Final ScoreView-scoped aggregate passed 28/28 and same-cache Debug `run -- --smoke-test` passed; the standard 30-second wrapper timed out on the existing nine-pane Session.
