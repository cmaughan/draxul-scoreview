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

- [ ] Track analysis validity by source identity and analysis inputs.
- [ ] Reuse the cached `PieceProfile` across paged/flow toggles, restyles, and window rebuilds.
- [ ] Invalidate only when source bytes or analysis-relevant options change.
- [ ] Write `.analysis.json` only when the profile changes or the cache file is missing.
- [ ] Preserve composer configuration against the reused profile.

## Tests

- [ ] Count analysis builds and persistence writes across repeated view toggles.
- [ ] Assert one analysis for an unchanged source.
- [ ] Assert source replacement invalidates and rebuilds the profile.
- [ ] Assert missing/corrupt analysis dumps are repaired without changing the in-memory profile.

## Acceptance criteria

- [ ] Repeated view transitions perform no redundant analysis or filesystem write.
- [ ] Source changes still produce fresh, correct analysis.
- [ ] Composer and inspector output remain unchanged.
- [ ] ScoreView aggregate tests and same-cache smoke pass.
