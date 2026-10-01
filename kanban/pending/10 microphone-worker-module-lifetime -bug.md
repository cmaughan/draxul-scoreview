# Keep microphone workers alive with their module
**Summary:** Finish microphone-opening work before releasing its code so quitting during a permission prompt cannot crash.

**Priority:** 10  
**Severity:** CRITICAL  
**Source:** `plugins/scoreview/product/draxul-scoreview/src/mic_player_input.cpp`  
**Reported by:** Claude M8; consensus F18.

**Evidence and trigger:** The worker detaches at line 217; destruction marks abandonment without completion ordering. Process static teardown can release the module while its permission worker sleeps.

- [ ] **Investigate:** Trace worker/code ownership through pending permission, device opening/resume, pane shutdown, and process module release.
- [ ] **Fix:** Track outstanding openers and order completion before module release, using cancellable waits or retained module ownership.
- [ ] **Fix:** Preserve asynchronous consent/device handling and avoid unbounded interface-thread joins.
- [ ] **Acceptance:** Controlled shutdown during pending permission cannot resume into unloaded module code.
- [ ] **Acceptance:** Abandoned opening/resuming operations clean up exactly once and ordinary input remains functional.
- [ ] **Validation:** Run the ScoreView-scoped aggregate and same-cache smoke; verify the macOS permission/shutdown path and inspect Windows lifetime behavior.
