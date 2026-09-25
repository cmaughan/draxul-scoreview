# Honor ScoreView’s `notick` launch mode
**Severity:** MEDIUM  
**Source:** Codex #26; `plugins/scoreview/product/draxul-scoreview/src/score_runtime.cpp:269`.

`roll-notick` selects Off and then matches the later `"tick"` substring check, enabling beats.

**Investigation**

- [ ] Trace plugin mode strings and interactions among `notick`, `tick`, `tick8`, and `locktempo`.

**Fix strategy**

- [ ] Parse exact tokens or make tick choices mutually exclusive, keeping unrelated options independent.

**Acceptance criteria**

- [ ] `roll-notick` disables metronome ticks; `tick` and `tick8` retain their intended levels.
- [ ] Verify combined mode options; run ScoreView aggregate tests and same-cache smoke.
