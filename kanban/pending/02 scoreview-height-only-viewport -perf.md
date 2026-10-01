# Avoid paged reengraving on height-only resize

**Summary:** Keep the existing musical notation layout when only pane height changes so vertical resizing does not rebuild every page.

**Source:** `plugins/scoreview/product/draxul-scoreview/src/score_runtime.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Codex; Claude found adjacent layout work. Lines 347–352 dirty layout for any size change, while engraving options at 574–580 depend on width, scale, and zoom. Lines 582–633 render and interpret each page after invalidation.

- [ ] **Baseline:** Count `RedoLayout`, SVG renders, and frame p95 during fixed-width vertical drag on a long score.
- [ ] **Implement:** Invalidate engraving only for its actual inputs; clamp scroll and repaint on height change.
- [ ] **Functional safety:** Preserve width/DPI/zoom, page bounds, scroll, and presentation changes.
- [ ] **Compare:** Require zero reengraves after initial layout for height-only changes.
- [ ] **Platforms:** Check ScoreView snapshots and aggregate/smoke on Windows and macOS.
- [ ] **Acceptance:** Vertical resizing does not rebuild paged notation.
