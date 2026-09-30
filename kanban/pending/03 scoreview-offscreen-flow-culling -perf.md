# Cull offscreen monolithic Flow and Clock notation

**Source:** `plugins/scoreview/product/draxul-scoreview/src/score_render_nvg.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude, Codex. `score_presentation.cpp:327–333` scissors output, but this file’s lines 157–180 and 264–269 still replay complete path and glyph outlines. Active playback uses roughly 16 ms frames. Normal windowed Roll and paged views already bound more work.

- [ ] **Baseline:** Count submitted paths/glyphs and frame p95 as monolithic score length grows with a fixed viewport.
- [ ] **Implement:** Store conservative interpreted-object bounds and reject offscreen NanoVG submissions.
- [ ] **Functional safety:** Preserve paint order, transformed glyphs, strokes/accidentals at clip edges, and replacement invalidation.
- [ ] **Compare:** Require submissions to follow visible notation, with identical pixels at boundaries.
- [ ] **Platforms:** Check Vulkan and Metal ScoreView output, product aggregate and smoke.
- [ ] **Acceptance:** Distant Flow/Clock notation is not replayed each frame.
