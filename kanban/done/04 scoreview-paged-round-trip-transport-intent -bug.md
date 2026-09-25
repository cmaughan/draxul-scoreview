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

- [x] Represent intended Flow transport explicitly as `Clock`, `Roll`, or `Gate`.
- [x] Initialize intent correctly for `paged`, `flow`, `flow-autoplay`, `roll*`, and `gate*` launch modes.
- [x] Preserve intent while tearing down an active performance session for Paged.
- [x] Re-enter Roll or Gate using the recorded input configuration.
- [x] Keep explicit clock-conveyor modes in Clock.
- [x] Clear pending transition flags after failure, cancellation, or a return to Paged.
- [x] Keep slicer/window capability responsible only for rolling-window versus monolithic rendering.

## Tests

- [x] Preserve the paged-launch-to-Roll test before slicer priming.
- [x] Add Roll-to-Paged-to-Roll and Gate-to-Paged-to-Gate coverage.
- [x] Add `flow` and `flow-autoplay` launch-intent and Clock round-trip coverage.
- [x] Cover failed flow interpretation, failed transport construction, rapid double-toggle, `.mxl`, `mono`, and one-bar sources.

## Acceptance criteria

- [x] Every launch mode returns from Paged to its intended transport.
- [x] Gate remains reachable after a paged round trip.
- [x] Explicit flow modes never change to Roll implicitly.
- [x] A failed or cancelled transition cannot trigger a later surprise mode change.
- [x] Behavior is consistent regardless of windowing support.

## Evidence

`game_mode_` is the persistent Flow intent and `start_in_gate_` is only a pending transition flag. The intent parser distinguishes Clock launches, Gate launches, and Roll/paged launches; view teardown never mutates it. The focused 33-case/333-assertion host run covers round trips, failed interpretation/transport then retry, rapid cancellation, and the four source-window policies. Final ScoreView-scoped aggregate passed 28/28 and same-cache Debug `run -- --smoke-test` passed; the standard 30-second wrapper timed out on the existing nine-pane Session.
