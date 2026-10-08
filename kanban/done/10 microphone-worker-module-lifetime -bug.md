# Keep microphone workers alive with their module
**Summary:** Finish microphone-opening work before releasing its code so quitting during a permission prompt cannot crash.

**Priority:** P0 — an outstanding microphone worker can execute unloaded plugin code and crash.
**Source:** `plugins/scoreview/product/draxul-scoreview/src/mic_player_input.cpp`  
**Reported by:** Claude M8; consensus F18.

**Evidence and trigger:** The worker detaches at line 217; destruction marks abandonment without completion ordering. Process static teardown can release the module while its permission worker sleeps.

- [x] **Investigate:** Trace worker/code ownership through pending permission, device opening/resume, pane shutdown, and process module release.
- [x] **Fix:** Track outstanding openers and order completion before module release, using cancellable waits or retained module ownership.
- [x] **Fix:** Preserve asynchronous consent/device handling and avoid unbounded interface-thread joins.
- [x] **Acceptance:** Controlled shutdown during pending permission cannot resume into unloaded module code.
- [x] **Acceptance:** Abandoned opening/resuming operations clean up exactly once and ordinary input remains functional.
- [x] **Validation:** Run the ScoreView-scoped aggregate and same-cache smoke; inspect Windows lifetime behavior (code review plus the retained-module test).
- [x] **Validation:** Windows runtime lifetime coverage and isolated-profile same-cache smoke passed. This replaces the unreachable Windows permission-prompt gate as explained below, not a manual microphone or app-quit observation.

## Windows completion-gate audit (2026-10-08)

- **Gate correction:** Windows selects `mic_permission_stub.cpp`, which returns
  `Granted`; only macOS implements the TCC prompt. The old Windows prompt gate
  was unreachable. Replace it with deterministic coverage of the reported
  abandoned-opener/code-unload defect, not a claim of manual verification.
- **Coverage:** `tests/scoreview_microphone_tests.cpp` injects pending permission
  and blocked open/resume into production `MicPlayerInput`, checking bounded
  destruction, abandonment and exactly-once cleanup. Its real DLL test compiles
  the production `module_retaining_thread.cpp`, drops the host's `FreeLibrary`
  reference while a worker executes DLL code, and verifies retention followed
  by eventual unload. CMake includes both in `draxul-test-scoreview-runtime`.
- **Code review:** production shutdown clears the input rig; the opener retains
  only shared state and destroys captures before `FreeLibraryAndExitThread`.
  This matches the tested boundaries. These tests do not exercise a real
  microphone, SDL driver, or whole-app quit; the loader fixture is a minimal DLL.
  No additional app-level failure was established. No snapshot is required.
- **Windows evidence:** parent build succeeded. The behavior log confirms all
  three ScoreView entries passed: runtime shard 0 in 13.62 s, main shard 0 in
  65.04 s, and main shard 1 in 45.06 s:
  `build-ninja-debug/validation-logs/20261008-150658-521060/ctest-behavior.log`.
  The full inventory passed 73/79, not a clean global pass; the parent owns
  remaining non-ScoreView failures. Default-profile smoke timed out at 30 s,
  tracked separately in root `kanban/pending/65 windows-validation-timing -test.md`.
  The parent then reported `py do.py smoke debug --skip-build` passing from the
  same cache with isolated profile `D:/dev/Draxul/build-windows-smoke-profile`.
  This validates isolated startup, not resolution of the default-profile timeout.
  No duplicate product tests or render checks were run for this card audit.
  Final Release startup also passed (parent-reported):
  `py do.py smoke release --skip-build`, exit 0, 33.866647 s including toolchain
  setup; log `windows-gates/final-release-smoke-retry.log`.
  All card gates are complete on the reconciled evidence.

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
  Windows. The original implementation session inspected but did not compile the Windows
  path; the Windows build/runtime evidence above now supersedes that limitation. The
  interactive macOS consent-prompt quit was not exercised by hand.
- **Original implementation validation (macOS):** `python3 do.py test debug --scoreview` (core + scoreview, 59 CTest entries): every ScoreView suite passed; the only failures were the known pre-existing core `draxul-test-app-shard-0` (tests/app_dispatch_tests.cpp:453) and `draxul-do-py-tests`, which reads the uninitialized megacity submodule's AGENTS.md in this worktree. `python3 do.py smoke --skip-build` passed from the same cache.
