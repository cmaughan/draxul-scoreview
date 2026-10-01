# Move ScoreView progress saves out of the playback pump

**Summary:** Save ScoreView practice progress in the background so slow disk writes at bar changes do not interrupt playback.

**Source:** `plugins/scoreview/product/draxul-scoreview/src/score_session_controller.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude, Codex. `score_runtime.cpp:1703–1713` flushes at dirty bar transitions; controller lines 105–124 serialize the complete model, and `progress_store.cpp:43–69` writes, flushes, closes, and renames synchronously. Bar gating limits frequency but not storage latency on the playback path.

- [ ] **Baseline:** Measure pump p95/p99 and written bytes on long progress with injected slow storage.
- [ ] **Implement:** Use one coalescing worker with bounded immutable latest versions and explicit final drain.
- [ ] **Functional safety:** Preserve latest-state persistence, failure reporting, shutdown, and completed final-save behavior.
- [ ] **Compare:** Require file I/O to leave the interactive pump, reporting before/after latency.
- [ ] **Platforms:** Check persistence and teardown on Windows/macOS, ScoreView aggregate and smoke.
- [ ] **Acceptance:** Dirty bar transitions cannot synchronously stall playback for disk writes.
