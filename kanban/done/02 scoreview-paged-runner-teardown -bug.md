# Tear down ScoreView performance sessions before showing the paged view

**Type:** bug  
**Priority:** P1 / sequence 02  
**Raised by:** Claude and Codex  
**Consensus:** `plans/reviews/paged-roll-transition-consensus.md`

## Problem

`ScoreRuntime::toggle_flow_mode()` calls `exit_gate_mode()` only for `Gate`.
Leaving `Roll` therefore keeps the progress session and microphone/MIDI lease
active while the paged reading view is displayed. Visibility changes also clear
the software keyboard without restoring it when the pane becomes visible again.

## Implementation

- [x] Route both `Roll` and `Gate` through complete performance teardown when switching from Flow to Paged.
- [x] End the progress session and stop practice-time accrual.
- [x] Cancel pending stream work and release microphone/MIDI/audio leases.
- [x] Leave the active transport paused in `Clock` before rendering pages.
- [x] Restore the requested input, including the software keyboard, when a visible performance view resumes.
- [x] Preserve the intended performance mode separately for a later return to Flow.

## Tests

- [x] Add Roll-to-Paged and Gate-to-Paged orchestration tests.
- [x] Assert transport, progress-session, input-rig, and fake device-lease state after teardown.
- [x] Assert paged-view time is not recorded as practice.
- [x] Add hide/show coverage for keyboard, microphone, and MIDI input selections.

## Acceptance criteria

- [x] No input or audio device remains leased in paged view.
- [x] No practice time accrues in paged view.
- [x] Neither Roll nor Gate remains active behind paged rendering.
- [x] The selected input works after a pane visibility round trip.
- [x] ScoreView host orchestration tests pass.

## Evidence

`ScoreRuntime::toggle_flow_mode()` now tears down both performance transports, pauses Clock, and releases audio; the intent remains in `game_mode_`. The existing keyboard/bot/MIDI and microphone visibility tests plus the new round-trip session/lease host test passed in the final focused 33-case host run (333 assertions). The session is inactive before any paged pumping, so the paged interval cannot enter the practice clock. Final ScoreView-scoped aggregate passed 28/28 and same-cache Debug `run -- --smoke-test` passed; the standard 30-second wrapper timed out on the existing nine-pane Session.
