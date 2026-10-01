# Validate imported key-signature arithmetic
**Summary:** Validate extreme key signatures so imported scores cannot cause invalid musical calculations.

**Priority:** 11  
**Severity:** CRITICAL  
**Source:** `plugins/scoreview/product/draxul-score-learn/src/piece_analysis.cpp`  
**Reported by:** Claude M16; consensus F20.

**Evidence and trigger:** Source line 63 multiplies fifths by seven after the importer accepts unrestricted representable integers. `<fifths>2147483647</fifths>` overflows when analyzed.

- [ ] **Investigate:** Trace imported signatures through paged and conveyor analysis and define supported input policy.
- [ ] **Fix:** Reject unsupported imported signatures and independently use safe pitch-class normalization.
- [ ] **Acceptance:** Extreme positive/negative signatures cannot overflow; accepted ordinary signatures retain their expected analysis prior.
- [ ] **Acceptance:** Rejected import values produce controlled diagnostics rather than invalid model state.
- [ ] **Validation:** Run the ScoreView-scoped aggregate and same-cache smoke; use arithmetic instrumentation where available.
