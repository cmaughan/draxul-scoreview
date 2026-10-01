# Preserve keyboard input after hardware failure

**Summary:** Keep keyboard input available when a selected music device cannot open.

**Priority:** 08  
**Severity:** HIGH  
**Source:** `plugins/scoreview/product/draxul-scoreview/src/score_runtime.cpp`

**Evidence and trigger:** B22; hardware selection installs fallback input, then runtime cleanup deletes that fallback when opening fails.

- [ ] **Investigate:** Trace immediate device-open failure, fallback selection, lease release, and later recovery.
- [ ] **Fix:** Release the unsuccessful hardware lease without clearing the installed keyboard input.
- [ ] **Acceptance:** Injected device-open failure leaves keyboard practice usable and releases hardware ownership; successful selection still works.
- [ ] **Validation:** Run the ScoreView-scoped aggregate and same-cache smoke; check supported device paths on both platforms.
