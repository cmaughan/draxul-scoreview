I read `REVIEW_PROMPT.md` (one line: *"Review the code and tell me what else needs fixing"*), then reviewed the uncommitted change in `REPO_STATE.md` and the code around it. No files were modified.

# Review — `codex/fix-scoreview-paged-roll`

## The change itself

The diff is correct and minimal. `toggle_flow_mode` used to gate `start_in_gate_` on `stream_->windowed() && stream_->slicer().ready()`, but the slicer is only primed inside `relayout_flow` (`score_runtime.cpp:674`), which runs *after* the toggle — so a `paged`-launched score could never enter the runner on the first `f`. Dropping the pre-check and letting `relayout_flow` → `enter_gate_mode` (`score_runtime.cpp:1268`) make the windowed-vs-monolith decision is the right seam. The new test exercises exactly that ordering.

What follows is what still needs fixing, ranked.

---

## 1. A failed synchronous window build strands the window document in the main engine

`product/draxul-scoreview/src/score_runtime.cpp:842`

`engrave_window` loads the window XML into the engine *before* it can fail (`engraved_window.cpp:106`). When it fails at interpret/timemap/transport, `rebuild_window` sets `windowed_ = false` and `flow_dirty_ = true` — but never restores `source_bytes_`. Both recovery paths only reload the source when `stream_->active()` is true (`score_runtime.cpp:527`, `:625`), and `active_` is set only by a *successful* install (`note_installed`).

Failure scenario: first entry into the runner (launch, or now every `f` from the paged view — `initial_window_installed_` false ⇒ synchronous path). The window loads, the transport join fails. The "monolithic fallback" then engraves the **19-bar window** as the whole piece: `quarters_per_bar_`, `piece_marking_qpm_`, the piece analysis and `set_piece(...)` are all derived from the slice, and pressing `f` paginates the same slice as the reading view. Silent wrong content, no error surfaced.

Fix: reload `source_bytes_` in that failure branch (or track "engine holds a window document" separately from "install succeeded" — the two are conflated in `active_`). Note the async failure path (`:1084`) is fine, because the worker owns its own engine.

## 2. Leaving the runner for the paged view never ends the Roll session

`product/draxul-scoreview/src/score_runtime.cpp:1127`

```cpp
if (flow_.mode() == FlowController::TransportMode::Gate)
    exit_gate_mode();
```

Roll — the *default* game mode — is not covered, so `f` out of the runner leaves the session live: the mic/MIDI lease stays held (`release_input_device` never runs, OS recording indicator stays on), `end_progress_session` is never called so the practice clock keeps accruing while the user reads the sheet, and `flow_.mode()` stays `Roll` behind a paged view. Escape does the right thing for both modes (`handle_gate_key:1320` → `exit_gate_mode` for any non-Clock mode), so this is an inconsistency, not a deliberate policy.

It also matters more after this change: the return leg now re-arms Roll unconditionally, so the leaving leg should tear down unconditionally. Suggest `if (flow_.mode() != FlowController::TransportMode::Clock) exit_gate_mode();`.

## 3. `windowed_` is a one-way latch for the life of the process

`set_windowed(false)` is called from five places (`:220`, `:679`, `:816`, `:846`, `:1086`); nothing ever sets it back to true. One transient engrave failure permanently demotes the session to the monolithic strip — no restart, view toggle, or reload recovers the rolling window.

The converse also leaks: `clear_piece_progress` → `restart_stream` → `rebuild_window` (`:973`, `:807`) doesn't consult `windowed()`, so "Clear progress" on a `mono` launch (or after a fallback) silently re-installs a rolling window, leaving `active() == true` with `windowed() == false`. Either honour the latch there or give the fallback an explicit re-arm point.

## 4. `Gate` game mode can no longer be re-entered after a paged round-trip

`product/draxul-scoreview/src/score_runtime.cpp:1122`

The re-arm is `if (game_mode_ == Roll)`. Launched with the `gate` dev token: `f` → paged calls `exit_gate_mode()` (mode → Clock), `f` back leaves `start_in_gate_` false ⇒ the clock conveyor. The only way back is `g`, which forces `game_mode_ = Roll` (`:1943`). The gate verification instrument is unreachable for the rest of the run. Re-arm whatever `game_mode_` records rather than hard-coding Roll.

## 5. `start_in_gate_` can latch true and fire later

It is cleared only inside the `if (transport_ok)` block (`:736`). If `relayout_flow` returns early on `InterpretFailed` (`:645`, which forces the view back to Paged) or the transport join fails, the flag stays set; likewise if the user toggles back to Paged before the next pump (the Paged branch clears neither `start_in_gate_` nor `flow_dirty_`). A later, unrelated flow build then drops into the runner. Clear both in the Paged branch of `toggle_flow_mode`.

## 6. Every flow build re-runs the whole-piece analysis and rewrites `.analysis.json`

`score_runtime.cpp:731` calls `analyze_piece(...)` → `set_piece_profile`, which serializes and writes the dump to disk (`score_session_controller.cpp:65-73`) — unconditionally, on every `relayout_flow`. The analysis is a pure function of the timemap plus notated key, so it is identical every time. The paged path already guards against redoing it (`score_runtime.cpp:490`); the flow path doesn't.

`relayout_flow` runs on every `f` into Flow, every `exit_gate_mode`, and every monolith fallback — and after this change the paged→flow path is a *whole-piece* Verovio engrave plus a full re-analysis plus a file write, immediately thrown away by `enter_gate_mode`'s `rebuild_window(0, …)`. Reuse the cached profile when one already exists for the piece.

## 7. `SourceSlicer::window_xml_for` indexes every part with the first part's bar index

`product/draxul-score-learn/src/source_slicer.cpp:286` and `:310`

Validation is `bar.source_bar >= bar_count()` (`:256`), and `bar_count()` is the *first* part's measure count (`:79`). The emit loop then does `part.measures[item.source_bar]` and `part.state_before[state_index]` for every part. A score whose later parts have fewer measures — malformed or truncated MusicXML — is an out-of-bounds `vector::operator[]`, i.e. UB, not a rejection. Multi-part sources do reach this path: they only disable the *composer* (`stream_composer.h:88` requires `part_count() == 1`), not windowing. Bound the check by `std::min` over all parts, or skip parts that are short. (This is the class of gap ice-box card 18 "hostile-mxl-inputs" is holding.)

## 8. Test coverage gaps around the change

- The new test covers Paged → Roll, but nothing covers the reverse leg — `toggle_flow_mode` is used in exactly one test, and finding 2 would be caught by asserting `transport_mode == Clock` / input released after `f` out of the runner.
- The behaviour the change newly enables for non-sliceable sources (`.mxl`, `mono`) — Roll on the whole-piece strip via `enter_gate_mode`'s else branch — is untested.
- `"a layout failure degrades to the monolithic fallback"` (`tests/scoreview_host_orchestration_tests.cpp:209`) only exercises `load()`-fails and then asserts shutdown safety; it never asserts what the fallback actually renders, which is why finding 1 is invisible today. A `DeterministicLayoutEngine` that loads successfully but yields no timemap would cover it.

---

Findings 1 and 2 are the ones I'd fix before merging; 1 is a silent-wrong-content bug on a path this change makes more reachable, and 2 is a device/privacy leak with a one-line fix in the function already being edited.
