// ScoreHost orchestration coverage (kanban 15): the real host's lifecycle —
// initialize/pump/status/viewport/shutdown — driven headlessly through the
// shared fixture. No GPU, window, microphone, MIDI keyboard, audio output,
// or user dialog is required by any test here; device layers are reached
// only through their fakes or not at all.

#include <catch2/catch_all.hpp>

#include "support/scoreview_host_fixture.h"
#include "support/temp_dir.h"

#include "score_audio_controller.h"
#include "score_session_controller.h"

#include <draxul/scoreview/progress_store.h>
#include <draxul/scoreview/score_device_lease.h>

#include <draxul/plugin_runtime.h>

#include <fstream>
#include <memory>
#include <string>

namespace
{

using draxul::PluginRuntimeContext;
using draxul::PluginRuntimeViewport;
using draxul::scoreview::CountingHostCallbacks;
using draxul::scoreview::DeterministicLayoutEngine;
using draxul::scoreview::FakeEngineState;
using draxul::scoreview::kScoreHostFixtureMinimalScore;
using draxul::scoreview::NoteOutcome;
using draxul::scoreview::NoteVerdict;
using draxul::scoreview::PlayerInputRig;
using draxul::scoreview::read_verovio_svg_fixture;
using draxul::scoreview::release_loads;
using draxul::scoreview::ScoreAudioController;
using draxul::scoreview::ScoreHost;
using draxul::scoreview::ScoreHostTestAccess;
using draxul::scoreview::ScoreSessionController;
using draxul::scoreview::wait_for_loads;
using draxul::scoreview::WindowEngraver;

PluginRuntimeViewport viewport(int w, int h)
{
    PluginRuntimeViewport view;
    view.pixel_size = { w, h };
    view.pixel_scale = 1.0f;
    return view;
}

// A primed rolling-window host (fake engine, no devices), shared by the
// input-selection and shutdown tests.
struct PrimedHost
{
    std::shared_ptr<FakeEngineState> engine_state = std::make_shared<FakeEngineState>();
    ScoreHost host;

    bool prime()
    {
        const std::string svg = read_verovio_svg_fixture();
        if (svg.empty())
            return false;
        std::string error;
        return ScoreHostTestAccess::prime_window(host,
            std::make_unique<DeterministicLayoutEngine>(engine_state, svg, false),
            kScoreHostFixtureMinimalScore, error);
    }
};

} // namespace

TEST_CASE("a sourceless host runs one full headless lifecycle",
    "[scoreview][host][orchestration]")
{
    auto callbacks = std::make_unique<CountingHostCallbacks>();
    auto host = std::make_unique<ScoreHost>();

    PluginRuntimeContext context;
    context.initial_viewport = viewport(800, 600);
    REQUIRE(host->initialize(context, *callbacks));
    CHECK(host->is_running());
    CHECK(host->init_error().empty());

    // The placeholder page: pump, interrogate, resize — no GPU touched.
    for (int frame = 0; frame < 3; ++frame)
        host->pump();
    CHECK_FALSE(host->status_text().empty());
    (void)host->runtime_state();
    (void)host->debug_state();
    (void)host->print_hint();
    (void)host->default_background();
    host->set_viewport(viewport(1024, 768));
    host->pump();

    host->shutdown();
    CHECK_FALSE(host->is_running());
    host->shutdown(); // idempotent
    CHECK_FALSE(host->is_running());

    // Callbacks must not be reached after shutdown: destroy the host while
    // the callbacks object is still alive, then release the callbacks.
    const int frames_at_shutdown = callbacks->frames;
    host.reset();
    CHECK(callbacks->frames == frames_at_shutdown);
}

TEST_CASE("a completed flow layout reports content ready",
    "[scoreview][host][orchestration][render-ready]")
{
    CountingHostCallbacks callbacks;
    ScoreHost host;

    PluginRuntimeContext context;
    context.initial_viewport = viewport(800, 600);
    REQUIRE(host.initialize(context, callbacks));

    const std::string svg = read_verovio_svg_fixture();
    REQUIRE_FALSE(svg.empty());
    auto engine_state = std::make_shared<FakeEngineState>();
    std::string error;
    REQUIRE(ScoreHostTestAccess::prime_window(host,
        std::make_unique<DeterministicLayoutEngine>(engine_state, svg, false),
        kScoreHostFixtureMinimalScore, error));

    CHECK(host.runtime_state().content_ready);
}

TEST_CASE("a missing source fails initialize cleanly and shutdown stays safe",
    "[scoreview][host][orchestration]")
{
    CountingHostCallbacks callbacks;
    ScoreHost host;

    PluginRuntimeContext context;
    context.initial_viewport = viewport(640, 480);
    context.launch_options.source_path = "/nonexistent/draxul-test-piece.musicxml";
    REQUIRE_FALSE(host.initialize(context, callbacks));
    CHECK_FALSE(host.is_running());
    CHECK(host.init_error().find("could not open score file") != std::string::npos);

    // Shutdown from the partially-initialized state, twice.
    host.shutdown();
    host.shutdown();
    CHECK_FALSE(host.is_running());
}

TEST_CASE("shutdown is safe from a never-initialized host", "[scoreview][host][orchestration]")
{
    ScoreHost host;
    host.shutdown();
    host.shutdown();
    CHECK_FALSE(host.is_running());
}

TEST_CASE("shutdown retires a completed but uninstalled window generation",
    "[scoreview][host][orchestration]")
{
    PrimedHost primed;
    REQUIRE(primed.prime());

    // A blockable worker completes an engrave the host never polls.
    auto worker_state = std::make_shared<FakeEngineState>();
    std::string error;
    auto engraver = WindowEngraver::create(
        std::make_unique<DeterministicLayoutEngine>(
            worker_state, read_verovio_svg_fixture(), true),
        error);
    REQUIRE(engraver);
    ScoreHostTestAccess::inject_engraver(primed.host, std::move(engraver));

    ScoreHostTestAccess::restyle_current_window(primed.host);
    REQUIRE(ScoreHostTestAccess::pending_request(primed.host) != 0);
    release_loads(worker_state, 1);
    REQUIRE(wait_for_loads(worker_state, 1));

    // No poll: the finished generation is still queued when the host goes
    // down. Shutdown must join the worker and drop the pending install
    // without hanging or installing into a dead presentation.
    primed.host.shutdown();
    CHECK_FALSE(primed.host.is_running());
}

namespace
{

// An engine whose load() always fails: the layout-failure path.
class FailingLayoutEngine final : public draxul::scoreview::ILayoutEngine
{
public:
    bool load(std::string_view, std::string& error) override
    {
        error = "deterministic layout failure";
        return false;
    }
    void set_options(const draxul::scoreview::LayoutOptions&) override {}
    bool is_loaded() const override
    {
        return false;
    }
    int page_count() override
    {
        return 0;
    }
    std::string render_page_svg(int) override
    {
        return {};
    }
    std::string render_timemap() override
    {
        return {};
    }
    int midi_pitch_for_element(const std::string&) override
    {
        return -1;
    }
    int note_letter_for_element(const std::string&) override
    {
        return -1;
    }
    std::vector<std::string> tie_end_ids() override
    {
        return {};
    }
};

class RejectingMicrophoneLeaseProvider final
    : public draxul::scoreview::IScoreDeviceLeaseProvider
{
public:
    draxul::scoreview::ScoreDeviceLeaseResult acquire(
        draxul::scoreview::ScoreDeviceKind kind,
        std::string_view, const void*) override
    {
        if (kind == draxul::scoreview::ScoreDeviceKind::Microphone)
            ++microphone_requests;
        return { {}, "deterministic device contention" };
    }

    int microphone_requests = 0;
};

} // namespace

TEST_CASE("a layout failure degrades to the monolithic fallback, shutdown stays safe",
    "[scoreview][host][orchestration]")
{
    ScoreHost host;
    std::string error;
    CHECK_FALSE(ScoreHostTestAccess::prime_window(
        host, std::make_unique<FailingLayoutEngine>(), kScoreHostFixtureMinimalScore, error));
    CHECK_FALSE(error.empty());
    host.shutdown();
    host.shutdown();
    CHECK_FALSE(host.is_running());
}

TEST_CASE("input selection swaps in place and reports requested-vs-engaged",
    "[scoreview][host][orchestration][input]")
{
    PrimedHost primed;
    REQUIRE(primed.prime());
    ScoreHostTestAccess::set_transport(primed.host, 1.5, 96.0, true);
    const double position = ScoreHostTestAccess::position_q(primed.host);
    const double tempo = ScoreHostTestAccess::tempo_qpm(primed.host);

    using GateInput = ScoreHostTestAccess::GateInput;
    CHECK(ScoreHostTestAccess::select_input(primed.host, GateInput::Keyboard));
    CHECK(ScoreHostTestAccess::input_kind(primed.host) == PlayerInputRig::Kind::Keyboard);

    // A MIDI request with no device engages the keyboard instead and says so.
    CHECK_FALSE(ScoreHostTestAccess::select_input(
        primed.host, GateInput::Midi, /*midi_port=*/-1));
    CHECK(ScoreHostTestAccess::input_kind(primed.host) == PlayerInputRig::Kind::Keyboard);

    // The bot always engages (deterministic verification player).
    CHECK(ScoreHostTestAccess::select_input(primed.host, GateInput::Bot));
    CHECK(ScoreHostTestAccess::input_kind(primed.host) == PlayerInputRig::Kind::Bot);

    // Swaps never touch the session: transport survives every switch.
    CHECK(ScoreHostTestAccess::position_q(primed.host) == Catch::Approx(position));
    CHECK(ScoreHostTestAccess::tempo_qpm(primed.host) == Catch::Approx(tempo));
    CHECK(ScoreHostTestAccess::playing(primed.host));
}

TEST_CASE("the host applies the earned tempo ladder cap unless tempo is locked",
    "[scoreview][host][orchestration][tempo-ladder]")
{
    PrimedHost primed;
    REQUIRE(primed.prime());

    // A new bar starts at the model's 60% ladder rung. The host glue must
    // translate that fraction into a cap against the piece marking.
    ScoreHostTestAccess::apply_tempo_ladder_at(
        primed.host, /*position_q=*/0.0, /*marking_qpm=*/120.0,
        /*tempo_qpm=*/120.0, /*lock_tempo=*/false);
    CHECK(ScoreHostTestAccess::tempo_qpm(primed.host) == Catch::Approx(72.0));

    // The user's explicit lock always wins over the adaptive model.
    ScoreHostTestAccess::apply_tempo_ladder_at(
        primed.host, /*position_q=*/0.0, /*marking_qpm=*/120.0,
        /*tempo_qpm=*/120.0, /*lock_tempo=*/true);
    CHECK(ScoreHostTestAccess::tempo_qpm(primed.host) == Catch::Approx(120.0));
}

TEST_CASE("process device leases reject contention and release deterministically",
    "[scoreview][host][orchestration][devices]")
{
    using draxul::scoreview::ScoreDeviceKind;
    auto provider = draxul::scoreview::create_score_device_lease_provider();
    int first_owner = 1;
    int second_owner = 2;

    auto first = provider->acquire(
        ScoreDeviceKind::AudioOutput, "default", &first_owner);
    REQUIRE(first.lease);
    CHECK(first.error.empty());

    auto conflict = provider->acquire(
        ScoreDeviceKind::AudioOutput, "default", &second_owner);
    CHECK_FALSE(conflict.lease);
    CHECK(conflict.error.find("another ScoreView pane") != std::string::npos);

    auto other_device = provider->acquire(
        ScoreDeviceKind::MidiInput, "Piano A", &second_owner);
    CHECK(other_device.lease);

    first.lease.reset();
    auto handed_off = provider->acquire(
        ScoreDeviceKind::AudioOutput, "default", &second_owner);
    CHECK(handed_off.lease);
}

TEST_CASE("hidden presentation pauses transport unless background playback is enabled",
    "[scoreview][host][orchestration][visibility]")
{
    PrimedHost paused;
    REQUIRE(paused.prime());
    ScoreHostTestAccess::set_transport(paused.host, 1.5, 96.0, true);
    paused.host.set_presentation_visible(false);
    CHECK_FALSE(ScoreHostTestAccess::playing(paused.host));
    CHECK_FALSE(paused.host.next_deadline().has_value());
    paused.host.set_presentation_visible(true);
    CHECK(ScoreHostTestAccess::playing(paused.host));
    CHECK(paused.host.next_deadline().has_value());

    PrimedHost background;
    REQUIRE(background.prime());
    REQUIRE(ScoreHostTestAccess::select_input(
        background.host, ScoreHostTestAccess::GateInput::Bot));
    ScoreHostTestAccess::set_transport(background.host, 1.5, 96.0, true);
    const int background_misses = ScoreHostTestAccess::miss_count(background.host);
    static_cast<draxul::scoreview::ScoreRuntime&>(background.host)
        .set_presentation_visible(false, /*allow_background_playback=*/true);
    CHECK(ScoreHostTestAccess::playing(background.host));
    CHECK(background.host.next_deadline().has_value());
    CHECK(ScoreHostTestAccess::input_kind(background.host)
        == PlayerInputRig::Kind::Bot);
    CHECK(ScoreHostTestAccess::miss_count(background.host) == background_misses);
}

TEST_CASE("showing a paused gate restores every device-free input path before transport",
    "[scoreview][host][orchestration][visibility][input]")
{
    using GateInput = ScoreHostTestAccess::GateInput;
    struct InputCase
    {
        const char* name;
        GateInput requested;
        int midi_port;
        bool request_engages;
        PlayerInputRig::Kind engaged;
    };
    const InputCase cases[] = {
        { "keyboard", GateInput::Keyboard, -1, true,
            PlayerInputRig::Kind::Keyboard },
        { "bot", GateInput::Bot, -1, true, PlayerInputRig::Kind::Bot },
        // An unavailable hardware port follows the production fallback while
        // retaining the MIDI request that visibility will retry on show.
        { "unavailable MIDI", GateInput::Midi, -1, false,
            PlayerInputRig::Kind::Keyboard },
    };

    for (const InputCase& input : cases)
    {
        DYNAMIC_SECTION(input.name)
        {
            PrimedHost primed;
            REQUIRE(primed.prime());
            CHECK(ScoreHostTestAccess::select_input(
                      primed.host, input.requested, input.midi_port)
                == input.request_engages);
            REQUIRE(ScoreHostTestAccess::input_kind(primed.host)
                == input.engaged);
            if (input.requested == GateInput::Bot)
            {
                CHECK(ScoreHostTestAccess::bot_pace_qpm(primed.host)
                    == Catch::Approx(60.0));
            }
            ScoreHostTestAccess::set_transport(primed.host, 1.5, 96.0, true);
            const int misses_before = ScoreHostTestAccess::miss_count(primed.host);

            primed.host.set_presentation_visible(false);
            CHECK(ScoreHostTestAccess::input_kind(primed.host)
                == PlayerInputRig::Kind::None);
            primed.host.set_presentation_visible(true);

            CHECK(ScoreHostTestAccess::input_kind(primed.host)
                == input.engaged);
            if (input.requested == GateInput::Bot)
            {
                CHECK(ScoreHostTestAccess::bot_pace_qpm(primed.host)
                    == Catch::Approx(60.0));
            }
            CHECK(ScoreHostTestAccess::playing(primed.host));
            CHECK(ScoreHostTestAccess::miss_count(primed.host) == misses_before);
        }
    }
}

TEST_CASE("visibility releases input leases only when background playback is disabled",
    "[scoreview][host][orchestration][visibility][devices]")
{
    using draxul::scoreview::ScoreDeviceKind;
    const auto verify_policy = [](bool allow_background_playback) {
        auto provider = draxul::scoreview::create_score_device_lease_provider();
        PrimedHost primed;
        REQUIRE(primed.prime());
        REQUIRE(ScoreHostTestAccess::acquire_input_lease(primed.host,
            provider, ScoreDeviceKind::Microphone, "default"));

        primed.host.set_presentation_visible(
            false, allow_background_playback);

        int competing_owner = 0;
        auto competing = provider->acquire(ScoreDeviceKind::Microphone,
            "default", &competing_owner);
        CHECK(static_cast<bool>(competing.lease)
            == !allow_background_playback);
    };

    DYNAMIC_SECTION("foreground-only")
    {
        verify_policy(false);
    }
    DYNAMIC_SECTION("background playback")
    {
        verify_policy(true);
    }
}

TEST_CASE("showing a paused microphone gate retries its lease without phantom misses",
    "[scoreview][host][orchestration][visibility][devices]")
{
    auto leases = std::make_shared<RejectingMicrophoneLeaseProvider>();
    PrimedHost primed;
    REQUIRE(primed.prime());
    ScoreHostTestAccess::set_device_lease_provider(primed.host, leases);
    CHECK_FALSE(ScoreHostTestAccess::select_input(
        primed.host, ScoreHostTestAccess::GateInput::Mic));
    REQUIRE(leases->microphone_requests == 1);
    REQUIRE(ScoreHostTestAccess::input_kind(primed.host)
        == PlayerInputRig::Kind::Keyboard);
    ScoreHostTestAccess::set_transport(primed.host, 1.5, 96.0, true);
    const int misses_before = ScoreHostTestAccess::miss_count(primed.host);

    primed.host.set_presentation_visible(false);
    CHECK(ScoreHostTestAccess::input_kind(primed.host)
        == PlayerInputRig::Kind::None);
    primed.host.set_presentation_visible(true);

    CHECK(leases->microphone_requests == 2);
    CHECK(ScoreHostTestAccess::input_kind(primed.host)
        == PlayerInputRig::Kind::Keyboard);
    CHECK(ScoreHostTestAccess::playing(primed.host));
    CHECK(ScoreHostTestAccess::miss_count(primed.host) == misses_before);
}

TEST_CASE("full-score note colors are on by default and inspector-toggleable",
    "[scoreview][host][orchestration][view]")
{
    PrimedHost primed;
    REQUIRE(primed.prime());

    CHECK(ScoreHostTestAccess::show_note_colors(primed.host));
    ScoreHostTestAccess::set_note_colors(primed.host, false);
    CHECK_FALSE(ScoreHostTestAccess::show_note_colors(primed.host));
    ScoreHostTestAccess::set_note_colors(primed.host, true);
    CHECK(ScoreHostTestAccess::show_note_colors(primed.host));
}

TEST_CASE("full-score note colors resolve after the paged timemap is available",
    "[scoreview][host][orchestration][view]")
{
    auto engine_state = std::make_shared<FakeEngineState>();
    ScoreHost host;
    const std::string svg = read_verovio_svg_fixture();
    REQUIRE_FALSE(svg.empty());

    std::string error;
    REQUIRE(ScoreHostTestAccess::prime_paged(host,
        std::make_unique<DeterministicLayoutEngine>(
            engine_state, svg, false, /*require_timemap_for_midi=*/true),
        kScoreHostFixtureMinimalScore, error));

    ScoreHostTestAccess::relayout_paged(host);
    CHECK(ScoreHostTestAccess::paged_guided_glyph_count(host) > 0);
}

TEST_CASE("switching a paged score enters the rolling play view before the slicer is primed",
    "[scoreview][host][orchestration][view]")
{
    auto engine_state = std::make_shared<FakeEngineState>();
    ScoreHost host;
    const std::string svg = read_verovio_svg_fixture();
    REQUIRE_FALSE(svg.empty());

    std::string source(kScoreHostFixtureMinimalScore);
    const size_t part_end = source.rfind("</part>");
    REQUIRE(part_end != std::string::npos);
    source.insert(part_end, R"xml(
    <measure number="2">
      <note><pitch><step>D</step><octave>4</octave></pitch><duration>4</duration><type>whole</type></note>
    </measure>
  )xml");

    std::string error;
    REQUIRE(ScoreHostTestAccess::prime_paged(host,
        std::make_unique<DeterministicLayoutEngine>(engine_state, svg, false),
        source, error));
    REQUIRE_FALSE(ScoreHostTestAccess::slicer_ready(host));

    ScoreHostTestAccess::toggle_flow_mode(host);
    ScoreHostTestAccess::relayout_flow(host);

    CHECK(ScoreHostTestAccess::transport_mode(host)
        == draxul::scoreview::FlowController::TransportMode::Roll);
    CHECK(ScoreHostTestAccess::stream_active(host));
    CHECK(ScoreHostTestAccess::waterfall_note_count(host) > 0);
}

TEST_CASE("paged round trips preserve the selected flow transport and end performance sessions",
    "[scoreview][host][orchestration][view]")
{
    using Mode = draxul::scoreview::FlowController::TransportMode;
    using draxul::scoreview::ScoreDeviceKind;
    const Mode modes[] = { Mode::Clock, Mode::Roll, Mode::Gate };
    for (const Mode intent : modes)
    {
        DYNAMIC_SECTION(static_cast<int>(intent))
        {
            PrimedHost primed;
            REQUIRE(primed.prime());
            ScoreHostTestAccess::set_flow_intent(primed.host, intent);
            auto leases = draxul::scoreview::create_score_device_lease_provider();
            REQUIRE(ScoreHostTestAccess::acquire_audio_lease(primed.host, leases));
            REQUIRE(ScoreHostTestAccess::acquire_input_lease(
                primed.host, leases, ScoreDeviceKind::Microphone, "default"));
            const draxul::tests::TempDir progress("scoreview-paged-roundtrip");
            REQUIRE(ScoreHostTestAccess::attach_progress_session(primed.host, progress.path));
            ScoreHostTestAccess::set_transport(primed.host, 0.0, 96.0, true);

            ScoreHostTestAccess::toggle_flow_mode(primed.host);
            CHECK(ScoreHostTestAccess::paged(primed.host));
            CHECK(ScoreHostTestAccess::transport_mode(primed.host) == Mode::Clock);
            CHECK_FALSE(ScoreHostTestAccess::playing(primed.host));
            CHECK_FALSE(ScoreHostTestAccess::session_active(primed.host));
            CHECK_FALSE(ScoreHostTestAccess::pending_performance_entry(primed.host));
            int competitor = 0;
            CHECK(leases->acquire(ScoreDeviceKind::AudioOutput, "default", &competitor).lease);
            CHECK(leases->acquire(ScoreDeviceKind::Microphone, "default", &competitor).lease);

            ScoreHostTestAccess::toggle_flow_mode(primed.host);
            CHECK(ScoreHostTestAccess::pending_performance_entry(primed.host)
                == (intent != Mode::Clock));
            ScoreHostTestAccess::relayout_flow(primed.host);
            CHECK(ScoreHostTestAccess::transport_mode(primed.host) == intent);
            CHECK_FALSE(ScoreHostTestAccess::pending_performance_entry(primed.host));
        }
    }
}

TEST_CASE("ScoreView launch modes keep transport intent separate from window capability",
    "[scoreview][host][orchestration][view]")
{
    using Mode = draxul::scoreview::FlowController::TransportMode;
    CHECK(ScoreHostTestAccess::launch_intent("paged") == Mode::Roll);
    CHECK(ScoreHostTestAccess::launch_intent("roll-notick") == Mode::Roll);
    CHECK(ScoreHostTestAccess::launch_intent("roll-mono") == Mode::Roll);
    CHECK(ScoreHostTestAccess::launch_intent("gate-bot") == Mode::Gate);
    CHECK(ScoreHostTestAccess::launch_intent("gate-mic") == Mode::Gate);
    CHECK(ScoreHostTestAccess::launch_intent("flow") == Mode::Clock);
    CHECK(ScoreHostTestAccess::launch_intent("flow-autoplay") == Mode::Clock);

    PrimedHost primed;
    REQUIRE(primed.prime());
    ScoreHostTestAccess::toggle_flow_mode(primed.host); // Flow -> Paged
    ScoreHostTestAccess::toggle_flow_mode(primed.host); // Paged -> Flow, build pending
    CHECK(ScoreHostTestAccess::pending_performance_entry(primed.host));
    ScoreHostTestAccess::toggle_flow_mode(primed.host); // cancelled before build
    CHECK(ScoreHostTestAccess::paged(primed.host));
    CHECK_FALSE(ScoreHostTestAccess::pending_performance_entry(primed.host));
    CHECK(ScoreHostTestAccess::transport_mode(primed.host) == Mode::Clock);
}

TEST_CASE("window capability never changes Roll transport intent",
    "[scoreview][host][orchestration][view]")
{
    const std::string svg = read_verovio_svg_fixture();
    REQUIRE_FALSE(svg.empty());
    const std::string ordinary = [] {
        std::string xml(kScoreHostFixtureMinimalScore);
        const size_t part_end = xml.rfind("</part>");
        xml.insert(part_end,
            "<measure number=\"2\"><note><rest/><duration>4</duration>"
            "<type>whole</type></note></measure>");
        return xml;
    }();
    struct SourceCase
    {
        const char* name;
        std::string source;
        bool force_mono;
        bool expect_window;
    };
    const SourceCase cases[] = {
        { "ordinary", ordinary, false, true },
        { "one bar", std::string(kScoreHostFixtureMinimalScore), false, false },
        { "compressed mxl", "PK compressed MusicXML fixture", false, false },
        { "mono", ordinary, true, false },
    };
    for (const SourceCase& source : cases)
    {
        DYNAMIC_SECTION(source.name)
        {
            auto state = std::make_shared<FakeEngineState>();
            ScoreHost host;
            std::string error;
            REQUIRE(ScoreHostTestAccess::prime_paged(host,
                std::make_unique<DeterministicLayoutEngine>(state, svg, false),
                source.source, error));
            if (source.force_mono)
                ScoreHostTestAccess::disable_windowing(host);
            ScoreHostTestAccess::toggle_flow_mode(host);
            ScoreHostTestAccess::relayout_flow(host);
            CHECK(ScoreHostTestAccess::transport_mode(host)
                == draxul::scoreview::FlowController::TransportMode::Roll);
            CHECK(ScoreHostTestAccess::stream_windowed(host) == source.expect_window);
            CHECK(ScoreHostTestAccess::stream_active(host) == source.expect_window);
        }
    }
}

TEST_CASE("failed flow builds clear pending intent before a later retry",
    "[scoreview][host][orchestration][view]")
{
    const std::string svg = read_verovio_svg_fixture();
    REQUIRE_FALSE(svg.empty());
    for (const bool fail_transport : { false, true })
    {
        DYNAMIC_SECTION("failure " << (fail_transport ? "transport" : "interpretation"))
        {
            auto state = std::make_shared<FakeEngineState>();
            ScoreHost host;
            std::string error;
            REQUIRE(ScoreHostTestAccess::prime_paged(host,
                std::make_unique<DeterministicLayoutEngine>(state, svg, false,
                    /*require_timemap_for_midi=*/false, /*fail_load=*/false,
                    /*fail_interpret_on_load_call=*/fail_transport ? 0 : 1,
                    /*fail_timemap_on_load_call=*/fail_transport ? 1 : 0),
                kScoreHostFixtureMinimalScore, error));
            ScoreHostTestAccess::toggle_flow_mode(host);
            REQUIRE(ScoreHostTestAccess::pending_performance_entry(host));
            ScoreHostTestAccess::relayout_flow(host);
            CHECK_FALSE(ScoreHostTestAccess::pending_performance_entry(host));
            CHECK(ScoreHostTestAccess::transport_mode(host)
                == draxul::scoreview::FlowController::TransportMode::Clock);

            if (!ScoreHostTestAccess::paged(host))
                ScoreHostTestAccess::toggle_flow_mode(host);
            REQUIRE(ScoreHostTestAccess::reload_source(host));
            ScoreHostTestAccess::toggle_flow_mode(host);
            ScoreHostTestAccess::relayout_flow(host);
            CHECK_FALSE(ScoreHostTestAccess::pending_performance_entry(host));
            CHECK(ScoreHostTestAccess::transport_mode(host)
                == draxul::scoreview::FlowController::TransportMode::Roll);
        }
    }
}

TEST_CASE("Roll judges queued keyboard events at arrival time before closing the window",
    "[scoreview][host][orchestration][input]")
{
    const auto run = [](double event_at_seconds) {
        PrimedHost primed;
        REQUIRE(primed.prime());
        ScoreHostTestAccess::pump_roll_event(primed.host, event_at_seconds,
            /*pump_at_seconds=*/0.46, /*elapsed_seconds=*/0.03, /*pitch=*/60);
        return std::pair{ ScoreHostTestAccess::miss_count(primed.host),
            ScoreHostTestAccess::wrong_count(primed.host) };
    };
    // At 60 QPM the late boundary for the onset at zero is 0.45s.
    // Delivery at 0.46s must not turn the 0.44s arrival into a miss.
    CHECK(run(0.44).first == 0);
    CHECK(run(0.46).second == 1); // genuinely late input is a stray
}

TEST_CASE("monolithic Roll rewind clears stale event-time history",
    "[scoreview][host][orchestration][input]")
{
    PrimedHost primed;
    REQUIRE(primed.prime());
    ScoreHostTestAccess::disable_windowing(primed.host);
    CHECK(ScoreHostTestAccess::rewind_clears_roll_history(primed.host));
    CHECK(ScoreHostTestAccess::position_q(primed.host) == Catch::Approx(0.0));
}

TEST_CASE("unchanged flow analysis is reused and missing dumps are repaired",
    "[scoreview][host][orchestration][analysis]")
{
    const std::string svg = read_verovio_svg_fixture();
    REQUIRE_FALSE(svg.empty());
    auto state = std::make_shared<FakeEngineState>();
    ScoreHost host;
    std::string error;
    REQUIRE(ScoreHostTestAccess::prime_paged(host,
        std::make_unique<DeterministicLayoutEngine>(state, svg, false),
        kScoreHostFixtureMinimalScore, error));
    const draxul::tests::TempDir progress("scoreview-analysis-cache");
    ScoreHostTestAccess::attach_analysis_source(host, progress.path);
    ScoreHostTestAccess::set_flow_intent(host,
        draxul::scoreview::FlowController::TransportMode::Clock);
    const auto rebuild = [&]() {
        if (!ScoreHostTestAccess::paged(host))
            ScoreHostTestAccess::toggle_flow_mode(host);
        ScoreHostTestAccess::toggle_flow_mode(host);
        ScoreHostTestAccess::relayout_flow(host);
    };
    rebuild();
    REQUIRE(ScoreHostTestAccess::analysis_build_count(host) == 1);
    REQUIRE(ScoreHostTestAccess::analysis_dump_write_count(host) == 1);
    rebuild();
    CHECK(ScoreHostTestAccess::analysis_build_count(host) == 1);
    CHECK(ScoreHostTestAccess::analysis_dump_write_count(host) == 1);

    auto dump = draxul::scoreview::progress_path(progress.path,
        std::string(kScoreHostFixtureMinimalScore));
    dump.replace_extension(".analysis.json");
    REQUIRE(std::filesystem::remove(dump));
    rebuild();
    CHECK(ScoreHostTestAccess::analysis_build_count(host) == 1);
    CHECK(ScoreHostTestAccess::analysis_dump_write_count(host) == 2);
    { std::ofstream corrupt(dump); corrupt << "{corrupt"; }
    rebuild();
    CHECK(ScoreHostTestAccess::analysis_build_count(host) == 1);
    CHECK(ScoreHostTestAccess::analysis_dump_write_count(host) == 3);

    const std::string replaced = std::string(kScoreHostFixtureMinimalScore)
        + "<!-- new source identity -->";
    REQUIRE(ScoreHostTestAccess::replace_analysis_source(host, replaced, progress.path));
    rebuild();
    CHECK(ScoreHostTestAccess::analysis_build_count(host) == 2);
    CHECK(ScoreHostTestAccess::analysis_dump_write_count(host) == 4);
}

TEST_CASE("score audio can prefer a staged piano lazily",
    "[scoreview][host][orchestration][audio]")
{
    const draxul::tests::TempDir dir("scoreview-soundfonts");
    std::ofstream(dir.path / "YDP-GrandPiano-20160804.sf2").put('\0');

    ScoreAudioController audio;
    audio.stage_soundfonts(dir.path);

    REQUIRE(audio.prefer_piano(0));
    CHECK(audio.voice() == ScoreAudioController::Voice::Piano);
    CHECK(audio.selected_soundfont_index() == 0);
    CHECK(audio.loaded_soundfont_index() == -1);
    CHECK_FALSE(audio.audition());
    CHECK(audio.tick_level() == ScoreAudioController::TickLevel::Off);
    CHECK_FALSE(audio.wants_pump());
}

TEST_CASE("score launch tick options are independent of tempo lock and each other",
    "[scoreview][host][orchestration][audio]")
{
    using TickLevel = ScoreAudioController::TickLevel;
    CHECK(ScoreAudioController::tick_level_from_mode("roll-notick") == TickLevel::Off);
    CHECK(ScoreAudioController::tick_level_from_mode("roll-tick") == TickLevel::Beats);
    CHECK(ScoreAudioController::tick_level_from_mode("roll-tick8") == TickLevel::Eighths);
    CHECK(ScoreAudioController::tick_level_from_mode("roll-notick-locktempo") == TickLevel::Off);
    CHECK(ScoreAudioController::tick_level_from_mode("roll-locktempo-tick8") == TickLevel::Eighths);
    CHECK_FALSE(ScoreAudioController::tick_level_from_mode("roll-locktempo"));
    CHECK_FALSE(ScoreAudioController::tick_level_from_mode("roll-antick"));
}

TEST_CASE("the session controller survives a corrupt progress file",
    "[scoreview][host][orchestration][session]")
{
    const draxul::tests::TempDir progress_dir("scoreview-progress");
    const std::string source_bytes = "fake-source-bytes-for-hashing";

    // A first session writes real progress.
    {
        ScoreSessionController session;
        session.attach_source(progress_dir.path, source_bytes);
        REQUIRE(session.attached());
        session.model().set_piece("Test Piece", 120.0, 4.0);
        REQUIRE(session.begin_session());
        NoteOutcome outcome;
        outcome.onset_q = 0.0;
        outcome.pitch = 60;
        outcome.verdict = NoteVerdict::Correct;
        outcome.quality = 1.0;
        session.model().apply(outcome);
        session.mark_dirty();
        session.end_session(0.75); // final flush
    }

    // A second controller loads it back.
    std::filesystem::path stored_file;
    {
        ScoreSessionController session;
        session.attach_source(progress_dir.path, source_bytes);
        CHECK(session.model().total_notes_judged() == 1);
        CHECK(session.model().sessions().size() == 1);
        stored_file = draxul::scoreview::progress_path(progress_dir.path, source_bytes);
        REQUIRE(std::filesystem::exists(stored_file));
    }

    // Corrupt the file: the third controller starts fresh instead of crashing
    // or half-loading, and can still begin a session and save.
    {
        std::ofstream corrupt(stored_file, std::ios::binary | std::ios::trunc);
        corrupt << "{ this is not json";
    }
    {
        ScoreSessionController session;
        session.attach_source(progress_dir.path, source_bytes);
        CHECK(session.model().total_notes_judged() == 0);
        CHECK(session.model().sessions().empty());
        REQUIRE(session.begin_session());
        session.mark_dirty();
        session.flush_at_bar(1);
    }
    {
        ScoreSessionController session;
        session.attach_source(progress_dir.path, source_bytes);
        CHECK(session.model().sessions().size() == 1); // the fresh record took over
    }
}
