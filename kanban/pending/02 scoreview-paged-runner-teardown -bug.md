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

- [ ] Route both `Roll` and `Gate` through complete performance teardown when switching from Flow to Paged.
- [ ] End the progress session and stop practice-time accrual.
- [ ] Cancel pending stream work and release microphone/MIDI/audio leases.
- [ ] Leave the active transport paused in `Clock` before rendering pages.
- [ ] Restore the requested input, including the software keyboard, when a visible performance view resumes.
- [ ] Preserve the intended performance mode separately for a later return to Flow.

## Tests

- [ ] Add Roll-to-Paged and Gate-to-Paged orchestration tests.
- [ ] Assert transport, progress-session, input-rig, and fake device-lease state after teardown.
- [ ] Assert paged-view time is not recorded as practice.
- [ ] Add hide/show coverage for keyboard, microphone, and MIDI input selections.

## Acceptance criteria

- [ ] No input or audio device remains leased in paged view.
- [ ] No practice time accrues in paged view.
- [ ] Neither Roll nor Gate remains active behind paged rendering.
- [ ] The selected input works after a pane visibility round trip.
- [ ] ScoreView host orchestration tests pass.
