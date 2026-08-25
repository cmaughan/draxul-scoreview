### kanban/pending/scoreview-paged-runner-teardown -bug.md

# Tear down ScoreView performance sessions before showing the paged view

**Type:** bug  
**Priority:** P1  
**Raised by:** Claude and Codex  
**Consensus:** Both input reviews

## Problem

`ScoreRuntime::toggle_flow_mode()` calls `exit_gate_mode()` only for `Gate`. Leaving `Roll` therefore keeps the progress session and microphone/MIDI lease active while the paged reading view is displayed.

## Implementation

- [ ] Route both `Roll` and `Gate` through `exit_gate_mode()` when switching from Flow to Paged.
- [ ] End the progress session and stop practice-time accrual.
- [ ] Cancel pending stream work and release the active input device lease.
- [ ] Leave the transport paused in `Clock` mode before rendering pages.
- [ ] Preserve the intended performance mode separately for a later return to Flow.

## Tests

- [ ] Add a Roll → Paged orchestration test.
- [ ] Assert the transport becomes `Clock` and is paused.
- [ ] Assert the input rig and fake microphone/MIDI lease are released.
- [ ] Assert the progress session ends and paged-view time is not recorded as practice.
- [ ] Retain Gate → Paged teardown coverage.

## Acceptance criteria

- [ ] No input device remains leased in the paged view.
- [ ] No practice time accrues in the paged view.
- [ ] Neither `Roll` nor `Gate` remains active behind paged rendering.
- [ ] Existing ScoreView host orchestration tests pass.

### kanban/pending/scoreview-paged-round-trip-transport-intent -bug.md

# Preserve ScoreView transport intent across paged round trips

**Type:** bug  
**Priority:** P2  
**Raised by:** Claude and Codex  
**Consensus:** Both input reviews

## Problem

Paged → Flow re-entry is hard-coded around `game_mode_ == Roll`. This prevents `Gate` from returning after a round trip, while explicit `flow` and `flow-autoplay` launches can unexpectedly become Roll because `game_mode_` retains its default value.

## Implementation

- [ ] Represent the intended Flow transport mode explicitly as `Clock`, `Roll`, or `Gate`.
- [ ] Initialize that intent correctly for `paged`, `flow`, `flow-autoplay`, `roll*`, and `gate*` launch modes.
- [ ] Preserve the intent when tearing down an active performance session for Paged.
- [ ] Re-enter `Roll` or `Gate` from Paged using the recorded intent and input configuration.
- [ ] Keep explicit clock-conveyor modes in `Clock`; do not auto-start Roll.
- [ ] Keep window/slicer capability limited to choosing rolling-window versus monolithic rendering, not transport mode.

## Tests

- [ ] Preserve the new paged-launch → Roll test before slicer priming.
- [ ] Add Roll → Paged → Roll coverage.
- [ ] Add Gate → Paged → Gate coverage.
- [ ] Add `flow` and `flow-autoplay` round-trip tests asserting `Clock`.
- [ ] Cover sliceable, `mono`, and non-sliceable sources such as `.mxl` or one-bar scores.

## Acceptance criteria

- [ ] Every launch mode returns from Paged to its intended transport mode.
- [ ] Gate remains reachable after a paged round trip.
- [ ] Explicit flow modes never change to Roll implicitly.
- [ ] Transport selection behaves consistently regardless of windowing support.
- [ ] Existing ScoreView host orchestration tests pass.
