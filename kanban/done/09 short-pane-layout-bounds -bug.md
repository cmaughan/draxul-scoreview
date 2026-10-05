# Keep score layout bounds valid in short panes
**Summary:** Fit the score within short panes so resizing cannot trigger an invalid layout operation.

**Priority:** 09  
**Severity:** CRITICAL  
**Source:** `plugins/scoreview/product/draxul-scoreview/src/score_runtime.cpp`  
**Reported by:** Claude M10; consensus F19.

**Evidence and trigger:** Line 402 and `score_presentation.cpp:152` reverse clamp bounds below approximately 107 pixels times display scale. A short loaded Flow/Roll pane reaches both paths.

- [x] **Investigate:** Trace band layout across short/zero viewports, scale, zoom, and supported score modes.
- [x] **Fix:** Derive ordered bounds from available space or explicitly handle insufficient space in both calculations.
- [x] **Acceptance:** Short panes produce defined geometry without assertions, reversed clamps, or drawing outside intended bounds.
- [x] **Acceptance:** Normal pane sizing, score bands, and keyboard/waterfall layout remain correct.
- [x] **Validation:** Run the ScoreView-scoped aggregate, affected render checks, and same-cache smoke.

## Resolution

- **Root cause:** `ScoreRuntime::flow_band()` (`score_runtime.cpp`) and
  `ScorePresentation::record_flow` (`score_presentation.cpp`) both called
  `std::clamp(x, 96 * scale, vh * 0.9)`. Below about 107 px times the display scale, the low
  bound exceeds the high one. That is an MSVC debug assertion and undefined behaviour
  elsewhere. In practice it made the band taller than the pane.
- **Fix:** `fit_score_band_height()` (`product/draxul-scoreview/src/score_presentation.h`)
  derives ordered bounds. The 90%-of-pane ceiling wins over the 96 px floor. A zero, negative,
  or non-finite pane gives an empty band, and the helper is used at both sites.
  `record_flow` records nothing for a collapsed (0-size) pane instead of dividing by a zero
  sheet scale.
- **Tests:** `tests/scoreview_host_rebuild_tests.cpp`. "ScoreHost flow band stays inside short
  and collapsed panes" drives a loaded Flow host across heights 0 to 1400, scales 1/2/3, and
  zooms 0.4/1/4. It checks that the band and `print_hint()` stay inside the pane and that normal
  panes keep their geometry. "score band fitting orders its bounds for every pane height" covers
  the shared helper, including the Roll score-region inputs.
- **Manual check:** headless `--plugin dev.draxul.scoreview --screenshot-size` runs in flow and
  roll modes at 640x60, 640x100, and 640x600 all exited 0 with correctly sized BMPs. The
  640x600 roll layout (score band, waterfall, keyboard) looked unchanged.
- **Render note:** the `scoreview-plugin` render scenario excludes macOS, so no render snapshot
  ran locally. Windows CI covers it.
- **Validation:** `python3 do.py test debug --scoreview` (core + scoreview, 59 CTest entries): every ScoreView suite passed; the only failures were the known pre-existing core `draxul-test-app-shard-0` (tests/app_dispatch_tests.cpp:453) and `draxul-do-py-tests`, which reads the uninitialized megacity submodule's AGENTS.md in this worktree. `python3 do.py smoke --skip-build` passed from the same cache.
