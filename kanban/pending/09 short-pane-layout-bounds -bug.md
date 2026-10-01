# Keep score layout bounds valid in short panes
**Summary:** Fit the score within short panes so resizing cannot trigger an invalid layout operation.

**Priority:** 09  
**Severity:** CRITICAL  
**Source:** `plugins/scoreview/product/draxul-scoreview/src/score_runtime.cpp`  
**Reported by:** Claude M10; consensus F19.

**Evidence and trigger:** Line 402 and `score_presentation.cpp:152` reverse clamp bounds below approximately 107 pixels times display scale. A short loaded Flow/Roll pane reaches both paths.

- [ ] **Investigate:** Trace band layout across short/zero viewports, scale, zoom, and supported score modes.
- [ ] **Fix:** Derive ordered bounds from available space or explicitly handle insufficient space in both calculations.
- [ ] **Acceptance:** Short panes produce defined geometry without assertions, reversed clamps, or drawing outside intended bounds.
- [ ] **Acceptance:** Normal pane sizing, score bands, and keyboard/waterfall layout remain correct.
- [ ] **Validation:** Run the ScoreView-scoped aggregate, affected render checks, and same-cache smoke.
