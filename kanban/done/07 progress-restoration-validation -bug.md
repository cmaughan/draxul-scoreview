# Reject invalid saved practice values safely

**Summary:** Reject unusable saved practice data so opening a piece can start safely.

**Priority:** 07  
**Severity:** CRITICAL  
**Source:** `plugins/scoreview/product/draxul-score-learn/src/player_model.cpp`

**Evidence and trigger:** B06; valid syntax with wrong field types or invalid numeric keys throws through initialization after clearing model state.

- [x] **Investigate:** Inventory typed fields, numeric keys, bounds, and partial mutation during restoration.
- [x] **Fix:** Validate temporary state, contain conversion failures, and publish only complete valid state.
- [x] **Acceptance:** Wrong types and invalid keys return failure without exceptions or partial state; source attachment reaches the intended fresh-session behavior.
- [x] **Validation:** Preserve valid progress round trips; run the ScoreView-scoped aggregate and same-cache smoke.

## Resolution

- **Root cause:** `PlayerModel::deserialize` cleared the live maps first and then read fields
  with `json::value()`, `std::stoi`, and `std::stod`. A well-formed record with a wrong-typed
  field (`"total_notes": "five"`) or a malformed numeric key (`"pitch": {"sixty": ...}`) threw
  `nlohmann::json::type_error` / `std::invalid_argument` out of
  `ScoreSessionController::attach_source`, leaving the model half-cleared. Floats or
  out-of-range integers in count fields were also narrowed to `int` unchecked.
- **Fix:** `product/draxul-score-learn/src/player_model.cpp` restores into a scratch copy of the
  model. Typed readers (`read_count`, `read_double`, `read_string`, `read_passes`,
  `parse_int_key`, `parse_double_key`) check type, range, finiteness, and full key consumption,
  and throw a private `ProgressFormatError`. Both that error and `json::exception` are caught.
  The scratch copy replaces `*this` only after every field has validated. `quarters_per_bar`
  must be greater than zero.
- **Tests:** `tests/scoreview_player_model_tests.cpp`, "player model rejects well-formed
  progress with invalid fields atomically", runs 23 invalid records with no exception and a
  byte-identical model afterward, and valid progress still round-trips. Against the old code it
  failed with an unexpected exception. `tests/scoreview_host_orchestration_tests.cpp`, "the
  session controller survives a corrupt progress file", now also covers wrong-typed and
  invalid-key files through `attach_source`, which starts a fresh session.
- **Validation:** `python3 do.py test debug --scoreview` (core + scoreview, 59 CTest entries): every ScoreView suite passed; the only failures were the known pre-existing core `draxul-test-app-shard-0` (tests/app_dispatch_tests.cpp:453) and `draxul-do-py-tests`, which reads the uninitialized megacity submodule's AGENTS.md in this worktree. `python3 do.py smoke --skip-build` passed from the same cache.
