# Preserve valid imported musical fractions
**Summary:** Reject unrepresentable timing values so imported scores retain valid musical durations.

**Priority:** 12  
**Severity:** MEDIUM  
**Source:** `plugins/scoreview/product/draxul-notation/include/draxul/notation/score_document.h`  
**Reported by:** Claude M16; consensus F52.

**Evidence and trigger:** Line 42 narrows normalized wide integers without checks. Divisions `1073741824` and duration `1` produce a denominator that narrows to zero.

- [ ] **Investigate:** Trace duration/division parsing, fraction normalization, arithmetic, and import error propagation.
- [ ] **Fix:** Check normalized numerator/denominator ranges before narrowing and prevent publication of invalid fractions.
- [ ] **Fix:** Validate duration conversion inputs before rounding and report unusable imported timing coherently.
- [ ] **Acceptance:** The reported input cannot create a zero/negative denominator or infinite semantic duration; valid representable timing remains exact.
- [ ] **Validation:** Run the ScoreView-scoped aggregate and same-cache smoke.
