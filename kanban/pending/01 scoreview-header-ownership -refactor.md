# Separate ScoreView pipeline/runtime headers and audio dependency

**Summary:** Give ScoreView's score-processing and live-display code explicit interfaces and audio dependencies so each can be built without accidentally depending on the other's internals.

**Priority:** P2 — shared include root and pass-through audio link obscure ownership.  
**Source:** `plugins/scoreview/product/draxul-scoreview/CMakeLists.txt`  
**Proposed by:** Claude 46, narrowed. **Owner:** one ScoreView agent.  
**Evidence:** pipeline/runtime publish one include root; pipeline exposes audio, while runtime public microphone header truly includes listener types.

**Boundary verification**
- [ ] Classify public/private headers and exact audio includes.
**Implementation and migration**
- [ ] Split include ownership; make runtime directly own audio with visibility matching its public headers. Avoid unproven extra controllers.
**Unit tests**
- [ ] Compile minimal pipeline/runtime consumers plus existing ScoreView suites and standalone extraction.
**Cross-platform validation**
- [ ] Check macOS SDL module linkage, Windows Verovio DLL staging, `--scoreview` aggregate and smoke.
**Agent documentation and tooling**
- [ ] Add a short root product `AGENTS.md` with targets and platform gates.
**Acceptance criteria**
- [ ] Each consumer sees only owned headers and required audio links.
