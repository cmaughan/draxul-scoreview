# Wake ScoreView when background engraving completes

**Source:** `plugins/scoreview/product/draxul-scoreview/src/score_runtime.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude. Lines 1748–1749 schedule a 16 ms deadline merely while the engraver is busy; `scoreview_plugin.cpp:318–320` converts that deadline into redraw. Paused views can render repeatedly just to poll for completion.

- [ ] **Baseline:** Count paused busy frames, tick CPU, and completion-to-display latency for a long engraving job.
- [ ] **Implement:** Publish one thread-safe completion wake and consume the latest result once; retain true playback deadlines.
- [ ] **Functional safety:** Preserve cancellation, latest-wins behavior, plugin lifetime, and hidden presentation policy.
- [ ] **Compare:** Require no repeated paused redraw solely for `engrave_busy()`.
- [ ] **Platforms:** Check ScoreView on Windows/macOS, aggregate and smoke.
- [ ] **Acceptance:** Background completion triggers presentation without polling frames.
