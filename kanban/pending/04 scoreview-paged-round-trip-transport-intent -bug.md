# Preserve ScoreView transport intent across paged round trips

**Type:** bug  
**Priority:** P2 / sequence 04  
**Raised by:** Claude and Codex  
**Consensus:** `plans/reviews/paged-roll-transition-consensus.md`

## Problem

Paged-to-Flow re-entry is hard-coded around `game_mode_ == Roll`. This prevents
`Gate` from returning after a round trip, while explicit `flow` and
`flow-autoplay` launches can unexpectedly become Roll because `game_mode_` retains
its default value. Pending `start_in_gate_` state can also survive a failed or
abandoned transition and fire later.

## Implementation

- [ ] Represent intended Flow transport explicitly as `Clock`, `Roll`, or `Gate`.
- [ ] Initialize intent correctly for `paged`, `flow`, `flow-autoplay`, `roll*`, and `gate*` launch modes.
- [ ] Preserve intent while tearing down an active performance session for Paged.
- [ ] Re-enter Roll or Gate using the recorded input configuration.
- [ ] Keep explicit clock-conveyor modes in Clock.
- [ ] Clear pending transition flags after failure, cancellation, or a return to Paged.
- [ ] Keep slicer/window capability responsible only for rolling-window versus monolithic rendering.

## Tests

- [ ] Preserve the paged-launch-to-Roll test before slicer priming.
- [ ] Add Roll-to-Paged-to-Roll and Gate-to-Paged-to-Gate coverage.
- [ ] Add `flow` and `flow-autoplay` round-trip tests asserting Clock.
- [ ] Cover failed flow interpretation, failed transport construction, rapid double-toggle, `.mxl`, `mono`, and one-bar sources.

## Acceptance criteria

- [ ] Every launch mode returns from Paged to its intended transport.
- [ ] Gate remains reachable after a paged round trip.
- [ ] Explicit flow modes never change to Roll implicitly.
- [ ] A failed or cancelled transition cannot trigger a later surprise mode change.
- [ ] Behavior is consistent regardless of windowing support.
