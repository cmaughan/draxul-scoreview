# Judge ScoreView MIDI events by their arrival time
**Severity:** HIGH  
**Source:** Codex #19; `plugins/scoreview/product/draxul-scoreview/src/flow_controller.cpp:323`.

Roll judgment uses processing position and expires windows before queued input is read, turning valid delayed-delivery events into misses.

**Investigation**

- [ ] Trace MIDI timestamps through input polling, transport advancement, judgment, and expiration.

**Fix strategy**

- [ ] Map event times onto the transport timeline and judge queued events before expiring their corresponding windows.
- [ ] Preserve correct mapping through tempo changes, pauses, and delayed pumps.

**Acceptance criteria**

- [ ] At 60 QPM, the onset-1 event arriving at 1.44 remains valid when processed at 1.46.
- [ ] Truly late events remain misses; run ScoreView aggregate tests and same-cache smoke.
