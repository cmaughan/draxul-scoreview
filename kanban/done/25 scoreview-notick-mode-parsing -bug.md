# Honor ScoreView’s `notick` launch mode
**Severity:** MEDIUM  
**Source:** Codex #26; `plugins/scoreview/product/draxul-scoreview/src/score_runtime.cpp:269`.

`roll-notick` selects Off and then matches the later `"tick"` substring check, enabling beats.

**Investigation**

- [x] Trace plugin mode strings and interactions among `notick`, `tick`, `tick8`, and `locktempo`.

**Fix strategy**

- [x] Parse exact tokens or make tick choices mutually exclusive, keeping unrelated options independent.

**Acceptance criteria**

- [x] `roll-notick` disables metronome ticks; `tick` and `tick8` retain their intended levels.
- [x] Verify combined mode options; run ScoreView aggregate tests and same-cache smoke.

## Evidence

Tick options now use complete hyphen-separated launch tokens, so `notick` cannot match `tick`, and `locktempo` is independent. Focused ScoreView host tests passed 33 cases / 333 assertions including all tick combinations. Final ScoreView-scoped aggregate passed 28/28 and same-cache Debug `run -- --smoke-test` passed; the standard 30-second wrapper timed out on the existing nine-pane Session.
