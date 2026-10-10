# Avoid paged reengraving on height-only resize

**Summary:** Keep the existing musical notation layout when only pane height changes so vertical resizing does not rebuild every page.

**Source:** `plugins/scoreview/product/draxul-scoreview/src/score_runtime.cpp`  
**Priority:** P2; static, high confidence. **Reported by:** Codex; Claude found adjacent layout work. Lines 347–352 dirty layout for any size change, while engraving options at 574–580 depend on width, scale, and zoom. Lines 582–633 render and interpret each page after invalidation.

- [ ] **Baseline:** Count `RedoLayout`, SVG renders, and frame p95 during fixed-width vertical drag on a long score.
- [x] **Implement:** Invalidate engraving only for its actual inputs; clamp scroll and repaint on height change.
- [x] **Functional safety:** Preserve width/DPI/zoom, page bounds, scroll, and presentation changes.
- [x] **Compare:** Require zero reengraves after initial layout for height-only changes.
- [ ] **Platforms:** Check ScoreView snapshots and aggregate/smoke on Windows and macOS.
- [x] **Acceptance:** Vertical resizing does not rebuild paged notation.

## Implementation notes (2026-10-10)

- Paged engraving derives page width/height from viewport width and its scale from DPI × zoom. Height alone is no engraving input; `set_viewport` now dirties engraving only for width or pixel-scale changes.
- Height/position changes request a frame and immediately clamp vertical scroll to the current page extent. Zoom and presentation paths retain existing invalidation behavior.
- Added an integration regression using the real runtime + deterministic layout engine: initial paged layout followed by 60 height-only changes must retain the same pages and add zero options/ SVG render calls; width, DPI, and zoom each add one engraving. Counts protect the actual engine boundary; synthetic timing would not represent real Verovio frame p95.
- Root owns aggregate/smoke and local render checks. Fixed-width long-score drag frame p95 before/after and Windows snapshots remain outstanding; leave this card pending until every platform/manual gate is complete.

## Final Debug validation (2026-10-10)

Core + SatView + ScoreView aggregate passed all 62 entries in 45.57s, including
both ScoreView runtime shards. The executed viewport regression retained pages
through 60 height changes with zero added engine options/SVG calls; width, DPI
and zoom each reengraved once, scroll stayed bounded and position changes did
not reengrave. Same-cache native Metal startup smoke passed.

Long-score real drag frame p95 and product visual checks remain unchecked; this
card stays pending. Final Release startup confirmation passed.

## Completed validation

Final aggregate: 62/62 passed (45.57s). Two earlier 62-entry passes took
53.08s and 52.01s and exposed the documented client test synchronization races;
the second also observed a 262ms/250ms checkpoint timing failure that passed
both the first and final aggregate. Focused concurrency diagnosis: 5 epoch
repeats plus 15 paired epoch/pre-wait repeats, all passed after corrections
(20.44s total tests). These repeats overlap aggregate coverage intentionally
while diagnosing failures.

Debug startup smoke passed (~1.2s); basic native Metal comparison passed
(~1.8s) and registered `draxul-render-basic-view` CTest passed (1.66s).
The comparison was repeated once to confirm the registered CTest gate.
Final Release build/configure passed (63.95s total: configure/generate 10.3s,
compile/link ~53.6s); Release isolated startup smoke passed (~0.7s). The first
custom Release runtime exceeded the Unix socket path limit; the standard short
isolated runner succeeded, and no failed helper remains. Debug's initial
aggregate compile took 271.07s after the existing Release cache was reconfigured;
warm aggregate builds and the explicit app link reused that cache. One initial
sandbox build was blocked by compiler-cache access before tests; the authorized
run used the existing compiler cache. Windows/remote CI was not run locally.

Detailed session logs: `/tmp/draxul-easy-cards-aggregate-pass.log`,
`/tmp/draxul-easy-cards-render-ctest.log`, and
`/tmp/draxul-easy-cards-release-smoke.log`.
