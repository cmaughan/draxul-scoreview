# Judge ScoreView MIDI events by their arrival time
**Severity:** HIGH  
**Source:** Codex #19; `plugins/scoreview/product/draxul-scoreview/src/flow_controller.cpp:323`.

Roll judgment uses processing position and expires windows before queued input is read, turning valid delayed-delivery events into misses.

**Investigation**

- [x] Trace MIDI timestamps through input polling, transport advancement, judgment, and expiration.

**Fix strategy**

- [x] Map event times onto the transport timeline and judge queued events before expiring their corresponding windows.
- [x] Preserve correct mapping through tempo changes, pauses, and delayed pumps.

**Acceptance criteria**

- [x] At 60 QPM, the onset-1 event arriving at 1.44 remains valid when processed at 1.46.
- [x] Truly late events remain misses; run ScoreView aggregate tests and same-cache smoke.

## Evidence and bounds

MIDI callback timestamps are already steady-clock-based and translated to host seconds at poll. ScoreRuntime now records recent transport segments, maps each queued event to its arrival-time quarter position, judges before expiring Roll windows, and clears the history on pause, hide, keyboard rewind/restart, and view/document replacement. A 150 ms bounded segment history and 100 ms expiry grace cover callback delivery jitter without accepting genuinely late event timestamps as hits; stale events outside history are dropped. The 60 QPM onset-1 late-pump case and genuinely late stray/miss cases passed in the focused 8-case Roll suite (59 assertions), and the host pump arrival/rewind cases passed in the focused 33-case host suite (333 assertions). Final ScoreView-scoped aggregate passed 28/28 and same-cache Debug `run -- --smoke-test` passed; the standard 30-second wrapper timed out on the existing nine-pane Session.
