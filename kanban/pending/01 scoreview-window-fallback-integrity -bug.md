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

- [ ] Track which document the main engine holds independently from successful window installation, or reload `source_bytes_` immediately after a failed synchronous window build.
- [ ] Ensure monolithic fallback always engraves and paginates the complete source.
- [ ] Define whether window fallback is permanent, retryable, or explicitly re-armed.
- [ ] Make restart, clear-progress, restyle, and async-failure paths honor that policy.
- [ ] Prevent `active() == true` while `windowed() == false`.
- [ ] Preserve the last valid engraving and surface a bounded warning when recovery itself fails.

## Tests

- [ ] Add a deterministic engine that loads slice XML successfully and then fails interpretation or transport construction.
- [ ] Assert fallback reloads the complete source and retains the complete score in paged view.
- [ ] Cover synchronous initial-window failure and asynchronous advance failure.
- [ ] Cover restart and clear-progress after fallback.
- [ ] Cover `.mxl`, `mono`, one-bar, and ordinary sliceable MusicXML policy.

## Acceptance criteria

- [ ] No window failure can replace the complete score with a slice.
- [ ] Windowed/active state remains internally consistent across every recovery path.
- [ ] Recovery preserves the last valid visible content and produces an actionable diagnostic.
- [ ] ScoreView aggregate tests and same-cache smoke pass.
