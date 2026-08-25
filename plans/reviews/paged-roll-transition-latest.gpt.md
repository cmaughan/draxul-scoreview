## Findings

- **P1 — Roll is not torn down when returning to paged view.** [score_runtime.cpp:1127](/C:/Users/cmaughan/AppData/Local/Temp/draxul-review-codex-16rmj5g4/workspace/product/draxul-scoreview/src/score_runtime.cpp:1127) only exits `Gate`, not `Roll`. Consequently, switching back from the newly enabled paged→Roll path leaves the progress session and microphone/MIDI lease active, potentially blocking other panes and counting reading time as practice. Roll needs the same session/input teardown.

- **P2 — Explicit `flow` mode can become Roll after a view round-trip.** [score_runtime.cpp:1122](/C:/Users/cmaughan/AppData/Local/Temp/draxul-review-codex-16rmj5g4/workspace/product/draxul-scoreview/src/score_runtime.cpp:1122) treats `game_mode_ == Roll` as proof the user was in the runner, but initialization leaves `game_mode_` as Roll even for `flow`/`flow-autoplay`. Thus flow→paged→flow can unexpectedly auto-start Roll, newly affecting `.mxl` and one-bar sources after removal of the window checks. Track the previous transport mode or set `game_mode_` to `Clock` for explicit flow mode.

Tests were not run, per the review-only constraint.
