# Keep microphone workers alive with their module
**Summary:** Finish microphone-opening work before releasing its code so quitting during a permission prompt cannot crash.

**Priority:** 10  
**Severity:** CRITICAL  
**Source:** `plugins/scoreview/product/draxul-scoreview/src/mic_player_input.cpp`  
**Reported by:** Claude M8; consensus F18.

**Evidence and trigger:** The worker detaches at line 217; destruction marks abandonment without completion ordering. Process static teardown can release the module while its permission worker sleeps.

- [x] **Investigate:** Trace worker/code ownership through pending permission, device opening/resume, pane shutdown, and process module release.
- [x] **Fix:** Track outstanding openers and order completion before module release, using cancellable waits or retained module ownership.
- [x] **Fix:** Preserve asynchronous consent/device handling and avoid unbounded interface-thread joins.
- [x] **Acceptance:** Controlled shutdown during pending permission cannot resume into unloaded module code.
- [x] **Acceptance:** Abandoned opening/resuming operations clean up exactly once and ordinary input remains functional.
- [x] **Validation:** Run the ScoreView-scoped aggregate and same-cache smoke; inspect Windows lifetime behavior (code review plus the retained-module test).
- [ ] **Validation:** Manually quit during a pending microphone permission prompt and confirm no crash — on Windows, where the plugin library is actually unloaded (macOS keeps the Objective-C plugin loaded). Not yet run; the session was headless.

## Resolution

- **Root cause:** the microphone opener was a detached `std::thread` whose code lives in the
  ScoreView plugin module. `PluginManager` releases the module (`FreeLibrary`/`dlclose`,
  `libs/draxul-plugin/src/plugin_manager.cpp`) after the instance is destroyed. A worker still
  polling consent or blocked in a device call then resumed into unmapped code. Destruction only
  marked `Abandoned` and never ordered completion before the module release.
- **Fix:** new `start_module_retaining_thread()`
  (`product/draxul-scoreview/src/module_retaining_thread.{h,cpp}`), used by
  `MicPlayerInput` in `mic_player_input.cpp`. The worker takes its own reference on the module
  that contains it: `GetModuleHandleExW(FROM_ADDRESS)` on Windows, and `dladdr` plus
  `dlopen(RTLD_NOLOAD)` on POSIX. The reference is released only after no module frame remains
  on the thread:
  - Windows: a `CreateThread` trampoline ends in `FreeLibraryAndExitThread`.
  - POSIX: a pthread key whose destructor is `dlclose` itself. The C runtime runs it after the
    start routine has returned.

  Destruction still never joins, so asynchronous consent and device handling are unchanged and
  shutdown stays bounded. The existing exactly-once stream ownership protocol is unchanged.
- **Tests:** `tests/scoreview_microphone_tests.cpp`, "microphone opener code stays loaded until
  its worker leaves the module", loads a tiny real module
  (`tests/support/retaining_thread_module.cpp`, `draxul-scoreview-test-retaining-module` in
  `cmake/Tests.cmake`). It parks a retaining worker inside that module, unloads the module the
  way the host does, and checks the image stays mapped. It then releases the worker and checks
  the image is unmapped afterwards, so there is no permanent pin. With a plain detached thread,
  the same test reported the module unloaded and then crashed with SIGSEGV. All 11 microphone
  lifetime/failure/poll cases pass, repeated five times.
- **Platform notes:** on macOS the shipping plugin module contains Objective-C
  (`mic_permission.mm`), so dyld does not unmap it on `dlclose`. The fix matters most on
  Windows, and the Windows path compiled here only by inspection. CI must build it. The
  interactive macOS consent-prompt quit was not exercised by hand because this was a headless
  session.
- **Validation:** `python3 do.py test debug --scoreview` (core + scoreview, 59 CTest entries): every ScoreView suite passed; the only failures were the known pre-existing core `draxul-test-app-shard-0` (tests/app_dispatch_tests.cpp:453) and `draxul-do-py-tests`, which reads the uninitialized megacity submodule's AGENTS.md in this worktree. `python3 do.py smoke --skip-build` passed from the same cache.
