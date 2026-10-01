# Reject invalid saved practice values safely

**Summary:** Reject unusable saved practice data so opening a piece can start safely.

**Priority:** 07  
**Severity:** CRITICAL  
**Source:** `plugins/scoreview/product/draxul-score-learn/src/player_model.cpp`

**Evidence and trigger:** B06; valid syntax with wrong field types or invalid numeric keys throws through initialization after clearing model state.

- [ ] **Investigate:** Inventory typed fields, numeric keys, bounds, and partial mutation during restoration.
- [ ] **Fix:** Validate temporary state, contain conversion failures, and publish only complete valid state.
- [ ] **Acceptance:** Wrong types and invalid keys return failure without exceptions or partial state; source attachment reaches the intended fresh-session behavior.
- [ ] **Validation:** Preserve valid progress round trips; run the ScoreView-scoped aggregate and same-cache smoke.
