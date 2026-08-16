# ScoreView

ScoreView is a native product plugin for [Draxul](https://github.com/cmaughan/Draxul),
a cross-platform GPU terminal / agentic shell host. It renders MusicXML piano scores
with an embedded, pinned Verovio 6.2.1 engraving engine, drives them with a transport
clock, judges live playing from a MIDI keyboard, the microphone, or a dev keyboard,
and adapts the practice program to the player. It is mounted into the Draxul build as
a git submodule at `plugins/scoreview` and loaded at runtime as a dynamic module
(`dev.draxul.scoreview`) over Draxul's versioned C plugin ABI, rendering through its
own private NanoVG backend (Vulkan on Windows, Metal on macOS). The end goal, set out
in [plans/scoreview-manifesto.md](plans/scoreview-manifesto.md), is an adaptive
piano-learning experience — an "endless runner" practice stream that teaches the
piece you chose without ever feeling like practice.

Launch it into a Draxul tab or pane:

```
draxul tab create --space <space-id> --name ScoreView --plugin dev.draxul.scoreview --plugin-config '{"source":"C:/scores/piece.musicxml","mode":"paged"}' --json
```

`source` accepts `.musicxml` or compressed `.mxl`; `mode` defaults to `paged`, with
`flow`, `flow-autoplay`, `roll`, and the runner tokens selecting conveyor/practice
behavior. Hidden panes pause the transport and release device leases by default;
`"background_playback": true` opts into hidden logic and audio (renders stay
suspended). The full product narrative lives in [docs/scoreview.md](docs/scoreview.md).

## Facilities

- **Notation** — MusicXML/`.mxl` import through a semantic model with exact rational
  onsets and stable note ids (`product/draxul-notation`), engraved by a pinned
  Verovio 6.2.1. Verovio's SVG dialect is interpreted into a resolution-independent
  draw list (SMuFL glyph outlines, exact paths for staff lines/stems/beams/slurs,
  styled text runs) and replayed each frame — crisp at any DPI, clipped to its pane.
- **Viewing modes** — `paged` reading view with scrolling, zoom, and a status bar;
  `flow` conveyor mode that re-engraves the piece as one endless system under a
  settling playhead; `roll`, the runner: the transport never waits, each note judges
  in a timing window, and tempo eases up or down from demonstrated accuracy. A
  `gate` (wait) mode survives as a dev/verification instrument.
- **Playback and transport** — position-locked metronome with accented downbeats and
  eighth subdivisions, tempo control clamped to the piece's marking, and optional
  audition of the score through either a built-in synth or any `.sf2` soundfont
  loaded via TinySoundFont (the FreePats YDP Grand Piano is staged by the build,
  with its CC-BY 3.0 attribution). Both voices mix into one leased output stream.
- **MIDI input** — live enumeration of MIDI input ports via RtMidi (CoreMIDI on
  macOS, WinMM on Windows), with arrival-time-accurate note events judged exactly
  like every other input.
- **Microphone input** — SDL3 records the default input and an offline-testable
  acoustic listener (KissFFT STFT, spectral-flux onsets, inharmonic partial-template
  scoring with per-piano tuning calibration) turns it into note events, so the real
  piano is the controller.
- **Practice and learning** — per-piece progress persistence (content-hash-keyed
  JSON under the Draxul config dir), a composer that programs the stream from the
  live player model (spaced review of weak bars, hands-separate simplification,
  chord drills, phrase-aware mastery gates, overnight re-tests), piece analysis
  (key estimation, chord inventory, motif mining, phrase/section structure) with an
  on-score overlay, spelling-based note coloring with a guidance keyboard and a
  piano-roll waterfall, and wrong-note marking on the sheet.
- **ImGui learning inspector** — a floating panel (self-hosted ImGui context) for
  transport, view toggles, audio, live performance stats, timing drift, the trouble
  tree, and the composer's upcoming program, drawn by the plugin's own
  Vulkan/Metal-backed renderer. Product preferences are pane-local.
- **Device discipline** — output, microphone, and named MIDI devices use
  process-local leases; passive panes never enumerate or open devices, and a lease
  conflict degrades to keyboard/visual operation with an actionable status.

## Building

Third-party dependencies (SDL3, Verovio, NanoVG, ImGui, RtMidi, KissFFT,
TinySoundFont, tinyxml2, GLM, nlohmann_json, and the staged soundfont) are fetched
automatically via CMake FetchContent (`cmake/Dependencies.cmake`). Requires CMake
3.25+, C++20, and the platform GPU toolchain (Vulkan SDK with `glslc` on Windows,
Xcode command line tools with the Metal compiler on macOS).

### As part of Draxul (the normal path)

This repository is mounted into the Draxul superproject as a submodule at
`plugins/scoreview`:

```
git clone --recurse-submodules https://github.com/cmaughan/Draxul.git
```

`DRAXUL_ENABLE_SCOREVIEW` gates the plugin and defaults to `ON` on Windows and
macOS (`OFF` elsewhere). The mount point can be overridden with the
`DRAXUL_SCOREVIEW_PLUGIN_DIR` cache path, so a working checkout of this repository
anywhere on disk can be built inside Draxul. An enabled but unmounted submodule is
a graceful configure-time skip locally; in CI, `DRAXUL_REQUIRE_ENABLED_PLUGINS`
turns the same skip into a hard failure.

### Standalone, against the installed Draxul Plugin SDK

When this repository is the top-level CMake source directory, the build sets
`DRAXUL_SCOREVIEW_STANDALONE` and resolves the host side with
`find_package(DraxulPluginSDK CONFIG REQUIRED)` — point `CMAKE_PREFIX_PATH` at a
prefix where Draxul's `draxul-plugin-sdk` install component has been installed.
Standalone builds also expect the copied `plugins/support/imgui` tree from Draxul
at `../support/imgui` relative to this checkout, plus Draxul's
`libs/draxul-imgui-core` copied to `../support/imgui-core` (the shared
scancode/IImGuiHost leaf the support ImGui target consumes; the extraction smoke
test stages both exactly this way).

The extraction smoke test, `tests/external_product_plugin_smoke.py`, proves this
path end to end: it installs the SDK component from a Draxul build, copies this
tree (plus the support ImGui tree) into a temporary layout, configures and builds
it standalone against only the installed SDK, and loads the resulting module into
a real `draxul` — optionally validating a rendered frame. In the Draxul build it
is exposed as the opt-in `draxul-scoreview-extraction-smoke` target (opt-in
because rebuilding Verovio cold is deliberately too expensive for the ordinary
smoke loop).

## Layout

| Path | Contents |
|------|----------|
| `CMakeLists.txt` | Plugin module target, standalone/bundled build wiring, bundling registration |
| `plugin.toml` | Plugin manifest: id `dev.draxul.scoreview`, ABI version, per-platform library names |
| `src/` | The plugin entry point (`scoreview_plugin.cpp`) — the only production ABI surface |
| `shaders/` | NanoVG Vulkan shaders (macOS uses the Metal-backed canvas) |
| `cmake/` | `Dependencies.cmake` (FetchContent graph) and `Tests.cmake` (ctest registration, extraction smoke) |
| `product/draxul-notation` | Semantic score model + MusicXML importer; pure symbolic, no renderer or Verovio dependency |
| `product/draxul-score-learn` | Learning core: player model, piece analysis, source slicer, composer seam + adaptive stream, progress-file IO; a leaf library with purity enforced by the linker |
| `product/draxul-score-input` | Player-input seam (`IPlayerInput`/`PlayerNoteEvent`), dev keyboard scaffold, hardware MIDI input; isolates RtMidi |
| `product/draxul-score-audio` | Metronome/tone synths, soundfont voice, acoustic note listener; isolates TinySoundFont and KissFFT, offline-testable |
| `product/draxul-score-canvas` | NanoVG canvas with the private Vulkan and Metal backends |
| `product/draxul-score-runtime-support` | Shared runtime support: logging, SDL/ImGui input bridging |
| `product/draxul-scoreview` | Layout/transport pipeline (Verovio wrapper, SVG interpreter, flow judge, bots) and the `draxul-scoreview-runtime` orchestration/presentation layer |
| `tests/` | Unit/contract test sources, fixtures, and the extraction smoke script |
| `docs/scoreview.md` | The owned feature page — the full product narrative |
| `plans/` | Design docs: the manifesto, phase plans, runner/stream/composer/ear designs, learning research |

## Testing

When built inside Draxul, the test sources register into Draxul's ctest as two
executables:

- `draxul-test-scoreview` — the core suite: notation import, layout, interpreter,
  flow/roll/gate judging, analysis, player model, stream program, metronome, MIDI,
  and listener DSP tests.
- `draxul-test-scoreview-runtime` — the runtime suite: composer, host
  orchestration, window rebuild, microphone, overlay, and worker stress tests
  (this executable hosts its own SDL).

Run them from the Draxul build with
`ctest --test-dir build -R draxul-test-scoreview --output-on-failure`. The
extraction smoke (`draxul-scoreview-extraction-smoke`, described above) is the
third leg: it proves the copied-tree standalone build against the installed SDK.

## Relationship to Draxul

The dependency is strictly one-way: ScoreView consumes Draxul's public C plugin
SDK (`Draxul::PluginSDK`) plus explicitly allowlisted `Draxul::PluginSupport::*`
targets (ImGui), and nothing else — the Draxul build enforces this at configure
time (`DEPENDENCY_MODE STRICT`); a product library reaching into core or another
product fails the configure. Only the C ABI is a runtime contract: no Draxul C++
object crosses the dylib/DLL boundary, and the module borrows raw Vulkan/Metal
frame objects through that ABI. The executable contains no ScoreView provider or
fallback.

This repository was split out of the Draxul monorepo; the deep pre-split history
remains there.
