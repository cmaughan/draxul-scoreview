# Keep required full-page engraving off the interactive pump

**Source:** `plugins/scoreview/product/draxul-scoreview/src/score_runtime.cpp`  
**Priority/evidence:** P2; static, medium confidence. **Reported by:** Claude. Legitimate width, zoom, and view changes still reach synchronous relayout at line 1609, render/interpret every page at 582–633, and repeat timemap-related work. Height-only invalidation should be removed first.

- [ ] **Baseline:** Measure pump p95, page SVG/timemap calls, and allocations for long-piece zoom/view bursts.
- [ ] **Implement:** Use a single latest-wins owned engine worker or adapt the existing engraver; retain prior pages until a current result publishes.
- [ ] **Functional safety:** Avoid concurrent access to mutable Verovio state; preserve full-source versus rolling-slice behavior and cancellation.
- [ ] **Compare:** Report interactive stalls and redundant jobs before/after; parse timemap once per accepted result.
- [ ] **Platforms:** Check ScoreView behavior on Windows/macOS, aggregate and smoke.
- [ ] **Acceptance:** Required reengraving does not monopolize the GUI pump.
