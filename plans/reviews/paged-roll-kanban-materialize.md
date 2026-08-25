### kanban/pending/01 scoreview-window-fallback-integrity -bug.md

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

### kanban/pending/02 scoreview-paged-runner-teardown -bug.md

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

### kanban/pending/03 source-slicer-part-length-bounds -bug.md

# Reject inconsistent multi-part measure counts before slicing

**Type:** bug  
**Priority:** P1 / sequence 03  
**Raised by:** Claude  
**Consensus:** `plans/reviews/paged-roll-transition-consensus.md`

## Problem

`SourceSlicer::window_xml_for()` validates a source-bar index against the first
part's measure count, then indexes every part with that value. A malformed or
truncated later part can therefore cause out-of-bounds access while entering Roll.

## Implementation

- [ ] Validate every referenced source bar against every emitted part before indexing measures or `state_before`.
- [ ] Define a stable rejection category for inconsistent part lengths.
- [ ] Keep valid multi-part verbatim streaming behavior unchanged.
- [ ] Avoid partial output when validation fails.
- [ ] Cross-reference the broader compressed-input corpus in `kanban/ice-box/18 hostile-mxl-inputs -test.md` without conflating ZIP validation with MusicXML structure validation.

## Tests

- [ ] Add deterministic two-part fixtures with shorter and empty trailing parts.
- [ ] Cover mismatched `state_before` and measure vectors.
- [ ] Assert bounded rejection with no crash or undefined behavior.
- [ ] Retain valid equal-length multi-part window equivalence coverage.
- [ ] Run available sanitizer coverage for the malformed fixtures.

## Acceptance criteria

- [ ] No source-bar index is used before per-part bounds validation.
- [ ] Inconsistent MusicXML fails safely with an actionable error.
- [ ] Valid multi-part sources still render correctly.
- [ ] Source-slicer and ScoreView aggregate tests pass.

### kanban/pending/04 scoreview-paged-round-trip-transport-intent -bug.md

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

### kanban/pending/05 scoreview-flow-analysis-cache -refactor.md

# Cache immutable ScoreView piece analysis across view changes

**Type:** refactor  
**Priority:** P3 / sequence 05  
**Raised by:** Claude  
**Consensus:** `plans/reviews/paged-roll-transition-consensus.md`

## Problem

Every `relayout_flow()` recomputes the same piece analysis and rewrites the same
`.analysis.json`, even though the analysis is a pure function of the source
timemap and notated key. View toggles and fallback rebuilds therefore repeat CPU
work and filesystem writes unnecessarily.

## Implementation

- [ ] Track analysis validity by source identity and analysis inputs.
- [ ] Reuse the cached `PieceProfile` across paged/flow toggles, restyles, and window rebuilds.
- [ ] Invalidate only when source bytes or analysis-relevant options change.
- [ ] Write `.analysis.json` only when the profile changes or the cache file is missing.
- [ ] Preserve composer configuration against the reused profile.

## Tests

- [ ] Count analysis builds and persistence writes across repeated view toggles.
- [ ] Assert one analysis for an unchanged source.
- [ ] Assert source replacement invalidates and rebuilds the profile.
- [ ] Assert missing/corrupt analysis dumps are repaired without changing the in-memory profile.

## Acceptance criteria

- [ ] Repeated view transitions perform no redundant analysis or filesystem write.
- [ ] Source changes still produce fresh, correct analysis.
- [ ] Composer and inspector output remain unchanged.
- [ ] ScoreView aggregate tests and same-cache smoke pass.
