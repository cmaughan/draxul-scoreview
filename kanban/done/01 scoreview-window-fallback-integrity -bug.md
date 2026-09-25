# Keep ScoreView window failures and restart policy coherent

**Type:** bug  
**Priority:** P1 / sequence 01  
**Raised by:** Claude  
**Consensus:** `plans/reviews/paged-roll-transition-consensus.md`

## Problem

A synchronous rolling-window engrave can load slice XML into the main engine and
then fail before `ScoreStreamController::note_installed()` marks the window active.
The fallback therefore skips the full-source reload and can silently present the
slice as the whole score. Separately, `windowed_` is latched false by fallback while
restart paths can still install a window, producing contradictory active/windowed
state.

## Implementation

- [x] Track which document the main engine holds independently from successful window installation, or reload `source_bytes_` immediately after a failed synchronous window build.
- [x] Ensure monolithic fallback always engraves and paginates the complete source.
- [x] Define whether window fallback is permanent, retryable, or explicitly re-armed.
- [x] Make restart, clear-progress, restyle, and async-failure paths honor that policy.
- [x] Prevent `active() == true` while `windowed() == false`.
- [x] Preserve the last valid engraving and surface a bounded warning when recovery itself fails.

## Tests

- [x] Add a deterministic engine that loads slice XML successfully and then fails interpretation or transport construction.
- [x] Assert fallback reloads the complete source and retains the complete score in paged view.
- [x] Cover synchronous initial-window failure and asynchronous advance failure.
- [x] Cover restart and clear-progress after fallback.
- [x] Cover `.mxl`, `mono`, one-bar, and ordinary sliceable MusicXML policy.

## Acceptance criteria

- [x] No window failure can replace the complete score with a slice.
- [x] Windowed/active state remains internally consistent across every recovery path.
- [x] Recovery preserves the last valid visible content and produces an actionable diagnostic.
- [x] ScoreView aggregate tests and same-cache smoke pass.

## Evidence and policy

The main-engine document flag is separate from the installed-window flag. Failed synchronous/async windows mark a pending monolithic fallback without changing `windowed()` until a complete source reload and full transport build succeed. Restarts and restyles route through the same fallback; no further window advances occur while recovery is pending. Successful fallback disables windowing for the current source until reopen. Failed recovery leaves the last strip visible and adds a status warning; failed paged reload stops retrying every frame. The final direct regressions passed in the focused host run (33 cases / 333 assertions), including paged-after-failure, failed recovery, restart/clear, and source-policy variants. Final ScoreView-scoped aggregate passed 28/28; same-cache Debug `run -- --smoke-test` passed. The 30-second `smoke --skip-build` wrapper timed out on the existing nine-pane Session, so the explicit startup smoke was used for this gate.
