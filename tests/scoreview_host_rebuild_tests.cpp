#include <catch2/catch_all.hpp>

#include "support/scoreview_host_fixture.h"
#include "support/temp_dir.h"

#include "score_presentation.h" // fit_score_band_height

#include <algorithm>
#include <chrono>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

namespace
{

using draxul::scoreview::DeterministicLayoutEngine;
using draxul::scoreview::FakeEngineState;
using draxul::scoreview::kScoreHostFixtureMinimalScore;
using draxul::scoreview::read_verovio_svg_fixture;
using draxul::scoreview::release_loads;
using draxul::scoreview::ScoreHost;
using draxul::scoreview::ScoreHostTestAccess;
using draxul::scoreview::wait_for_host_install;
using draxul::scoreview::wait_for_loads;
using draxul::scoreview::WindowEngraver;

constexpr std::string_view kMinimalScore = kScoreHostFixtureMinimalScore;

} // namespace

TEST_CASE("ScoreHost restart and restyle remain nonblocking behind an active engrave",
    "[scoreview][host][engraver][lifetime]")
{
    const std::string svg = read_verovio_svg_fixture();
    REQUIRE_FALSE(svg.empty());

    auto main_state = std::make_shared<FakeEngineState>();
    ScoreHost host;
    std::string error;
    REQUIRE(ScoreHostTestAccess::prime_window(host,
        std::make_unique<DeterministicLayoutEngine>(main_state, svg, false),
        kMinimalScore, error));
    INFO(error);

    auto worker_state = std::make_shared<FakeEngineState>();
    auto worker_engine = std::make_unique<DeterministicLayoutEngine>(worker_state, svg, true);
    auto engraver = WindowEngraver::create(std::move(worker_engine), error);
    INFO(error);
    REQUIRE(engraver);
    ScoreHostTestAccess::inject_engraver(host, std::move(engraver));

    ScoreHostTestAccess::set_transport(host, 1.0, 96.0, true);
    const auto old_strip = ScoreHostTestAccess::strip(host);
    REQUIRE(old_strip);
    const double old_position = ScoreHostTestAccess::position_q(host);
    const double old_tempo = ScoreHostTestAccess::tempo_qpm(host);

    ScoreHostTestAccess::restyle_current_window(host);
    const auto first_request = ScoreHostTestAccess::pending_request(host);
    REQUIRE(first_request != 0);
    REQUIRE(wait_for_loads(worker_state, 1));

    const auto restart_start = std::chrono::steady_clock::now();
    ScoreHostTestAccess::restart(host);
    const auto restart_elapsed = std::chrono::steady_clock::now() - restart_start;
    const auto restart_request = ScoreHostTestAccess::pending_request(host);

    const auto restyle_start = std::chrono::steady_clock::now();
    ScoreHostTestAccess::restyle_current_window(host);
    const auto restyle_elapsed = std::chrono::steady_clock::now() - restyle_start;
    const auto latest_request = ScoreHostTestAccess::pending_request(host);

    CHECK(restart_elapsed < std::chrono::milliseconds(50));
    CHECK(restyle_elapsed < std::chrono::milliseconds(50));
    CHECK(first_request < restart_request);
    CHECK(restart_request < latest_request);
    CHECK(ScoreHostTestAccess::strip(host).get() == old_strip.get());
    CHECK(ScoreHostTestAccess::position_q(host) == Catch::Approx(old_position));
    CHECK(ScoreHostTestAccess::tempo_qpm(host) == Catch::Approx(old_tempo));
    CHECK(ScoreHostTestAccess::playing(host));

    // Exercise the host's generation check directly: a delayed completion
    // from the first request cannot clear or replace the latest pending job.
    ScoreHostTestAccess::deliver_stale_completion(host, first_request);
    CHECK(ScoreHostTestAccess::async_pending(host));
    CHECK(ScoreHostTestAccess::pending_request(host) == latest_request);
    CHECK(ScoreHostTestAccess::strip(host).get() == old_strip.get());

    // Completing the first generation must not expose it to ScoreHost: the
    // newest restyle starts, while the old engraving and transport stay live.
    release_loads(worker_state, 1);
    REQUIRE(wait_for_loads(worker_state, 2));
    ScoreHostTestAccess::poll(host);
    CHECK(ScoreHostTestAccess::async_pending(host));
    CHECK(ScoreHostTestAccess::pending_request(host) == latest_request);
    CHECK(ScoreHostTestAccess::strip(host).get() == old_strip.get());
    {
        std::lock_guard lock(worker_state->mutex);
        CHECK(worker_state->load_calls == 2);
        CHECK(worker_state->payloads.size() == 2);
    }

    release_loads(worker_state, 2);
    REQUIRE(wait_for_host_install(host));
    CHECK_FALSE(ScoreHostTestAccess::async_pending(host));
    CHECK(ScoreHostTestAccess::pending_request(host) == 0);
    CHECK(ScoreHostTestAccess::strip(host).get() != old_strip.get());
    CHECK(ScoreHostTestAccess::position_q(host) == Catch::Approx(old_position));
    CHECK(ScoreHostTestAccess::tempo_qpm(host) == Catch::Approx(old_tempo));
    CHECK(ScoreHostTestAccess::playing(host));
}

TEST_CASE("ScoreHost uses synchronous restart and restyle when its worker is unavailable",
    "[scoreview][host][engraver][fallback]")
{
    const std::string svg = read_verovio_svg_fixture();
    REQUIRE_FALSE(svg.empty());

    auto main_state = std::make_shared<FakeEngineState>();
    ScoreHost host;
    std::string error;
    REQUIRE(ScoreHostTestAccess::prime_window(host,
        std::make_unique<DeterministicLayoutEngine>(main_state, svg, false),
        kMinimalScore, error));
    INFO(error);
    REQUIRE(main_state->load_calls == 1);

    ScoreHostTestAccess::set_transport(host, 1.0, 102.0, true);
    const double tempo = ScoreHostTestAccess::tempo_qpm(host);
    const auto initial_strip = ScoreHostTestAccess::strip(host);
    ScoreHostTestAccess::restart(host);

    CHECK(main_state->load_calls == 2);
    CHECK_FALSE(ScoreHostTestAccess::async_pending(host));
    CHECK(ScoreHostTestAccess::pending_request(host) == 0);
    CHECK(ScoreHostTestAccess::strip(host).get() != initial_strip.get());
    CHECK(ScoreHostTestAccess::position_q(host) == Catch::Approx(0.0));
    CHECK(ScoreHostTestAccess::tempo_qpm(host) == Catch::Approx(tempo));
    CHECK(ScoreHostTestAccess::playing(host));

    ScoreHostTestAccess::set_transport(host, 1.0, tempo, true);
    const auto restarted_strip = ScoreHostTestAccess::strip(host);
    ScoreHostTestAccess::restyle_current_window(host);
    CHECK(main_state->load_calls == 3);
    CHECK_FALSE(ScoreHostTestAccess::async_pending(host));
    CHECK(ScoreHostTestAccess::strip(host).get() != restarted_strip.get());
    CHECK(ScoreHostTestAccess::position_q(host) == Catch::Approx(1.0));
    CHECK(ScoreHostTestAccess::tempo_qpm(host) == Catch::Approx(tempo));
    CHECK(ScoreHostTestAccess::playing(host));
}

TEST_CASE("failed initial slice interpretation reloads the complete source before flow fallback",
    "[scoreview][host][engraver][fallback]")
{
    const std::string svg = read_verovio_svg_fixture();
    REQUIRE_FALSE(svg.empty());
    auto state = std::make_shared<FakeEngineState>();
    ScoreHost host;
    std::string error;
    CHECK_FALSE(ScoreHostTestAccess::prime_window(host,
        std::make_unique<DeterministicLayoutEngine>(state, svg, false,
            /*require_timemap_for_midi=*/false, /*fail_load=*/false,
            /*fail_interpret_on_load_call=*/1),
        kMinimalScore, error));
    REQUIRE(ScoreHostTestAccess::fallback_pending(host));
    CHECK(ScoreHostTestAccess::stream_windowed(host));
    ScoreHostTestAccess::relayout_flow(host);
    CHECK_FALSE(ScoreHostTestAccess::fallback_pending(host));
    CHECK_FALSE(ScoreHostTestAccess::stream_windowed(host));
    CHECK_FALSE(ScoreHostTestAccess::stream_active(host));
    REQUIRE(ScoreHostTestAccess::strip(host));
    REQUIRE(state->payloads.size() == 2);
    CHECK(state->payloads.back() == kMinimalScore);
}

TEST_CASE("failed async advance keeps the old engraving until whole-source fallback succeeds",
    "[scoreview][host][engraver][fallback]")
{
    const std::string svg = read_verovio_svg_fixture();
    REQUIRE_FALSE(svg.empty());
    auto state = std::make_shared<FakeEngineState>();
    ScoreHost host;
    std::string error;
    REQUIRE(ScoreHostTestAccess::prime_window(host,
        std::make_unique<DeterministicLayoutEngine>(state, svg, false),
        kMinimalScore, error));
    const auto valid_strip = ScoreHostTestAccess::strip(host);
    REQUIRE(valid_strip);
    ScoreHostTestAccess::fail_async_advance(host);
    CHECK(ScoreHostTestAccess::fallback_pending(host));
    CHECK(ScoreHostTestAccess::stream_active(host));
    CHECK(ScoreHostTestAccess::stream_windowed(host));
    CHECK(ScoreHostTestAccess::strip(host).get() == valid_strip.get());
    ScoreHostTestAccess::relayout_flow(host);
    CHECK_FALSE(ScoreHostTestAccess::fallback_pending(host));
    CHECK_FALSE(ScoreHostTestAccess::stream_active(host));
    CHECK_FALSE(ScoreHostTestAccess::stream_windowed(host));
    REQUIRE(state->payloads.size() == 2);
    CHECK(state->payloads.back() == kMinimalScore);
}

TEST_CASE("failed full-source recovery retains the last valid engraving",
    "[scoreview][host][engraver][fallback]")
{
    const std::string svg = read_verovio_svg_fixture();
    REQUIRE_FALSE(svg.empty());
    auto state = std::make_shared<FakeEngineState>();
    ScoreHost host;
    std::string error;
    REQUIRE(ScoreHostTestAccess::prime_window(host,
        std::make_unique<DeterministicLayoutEngine>(state, svg, false,
            /*require_timemap_for_midi=*/false, /*fail_load=*/false,
            /*fail_interpret_on_load_call=*/2),
        kMinimalScore, error));
    const auto valid_strip = ScoreHostTestAccess::strip(host);
    REQUIRE(valid_strip);
    ScoreHostTestAccess::fail_async_advance(host);
    ScoreHostTestAccess::relayout_flow(host);
    CHECK(ScoreHostTestAccess::fallback_pending(host));
    CHECK(ScoreHostTestAccess::stream_windowed(host));
    CHECK(ScoreHostTestAccess::stream_active(host));
    CHECK(ScoreHostTestAccess::strip(host).get() == valid_strip.get());
}

TEST_CASE("a failed initial slice cannot become the complete paged score",
    "[scoreview][host][engraver][fallback]")
{
    const std::string svg = read_verovio_svg_fixture();
    REQUIRE_FALSE(svg.empty());
    auto state = std::make_shared<FakeEngineState>();
    ScoreHost host;
    std::string error;
    CHECK_FALSE(ScoreHostTestAccess::prime_window(host,
        std::make_unique<DeterministicLayoutEngine>(state, svg, false,
            /*require_timemap_for_midi=*/false, /*fail_load=*/false,
            /*fail_interpret_on_load_call=*/1),
        kMinimalScore, error));
    REQUIRE(ScoreHostTestAccess::fallback_pending(host));
    ScoreHostTestAccess::relayout_paged(host);
    REQUIRE(state->payloads.size() == 2);
    CHECK(state->payloads.back() == kMinimalScore);
    CHECK(ScoreHostTestAccess::paged_page_count(host) == 1);
    CHECK_FALSE(ScoreHostTestAccess::stream_active(host));
}

TEST_CASE("restart and clear-progress stay monolithic after window fallback",
    "[scoreview][host][engraver][fallback]")
{
    const std::string svg = read_verovio_svg_fixture();
    REQUIRE_FALSE(svg.empty());
    auto state = std::make_shared<FakeEngineState>();
    ScoreHost host;
    std::string error;
    CHECK_FALSE(ScoreHostTestAccess::prime_window(host,
        std::make_unique<DeterministicLayoutEngine>(state, svg, false,
            /*require_timemap_for_midi=*/false, /*fail_load=*/false,
            /*fail_interpret_on_load_call=*/1),
        kMinimalScore, error));
    const draxul::tests::TempDir progress("scoreview-fallback-progress");
    ScoreHostTestAccess::attach_analysis_source(host, progress.path);
    ScoreHostTestAccess::relayout_flow(host);
    REQUIRE_FALSE(ScoreHostTestAccess::stream_windowed(host));
    REQUIRE_FALSE(ScoreHostTestAccess::stream_active(host));

    ScoreHostTestAccess::restart(host);
    ScoreHostTestAccess::relayout_flow(host);
    CHECK_FALSE(ScoreHostTestAccess::stream_windowed(host));
    CHECK_FALSE(ScoreHostTestAccess::stream_active(host));
    CHECK(ScoreHostTestAccess::strip(host));
    ScoreHostTestAccess::clear_progress(host);
    ScoreHostTestAccess::relayout_flow(host);
    CHECK_FALSE(ScoreHostTestAccess::stream_windowed(host));
    CHECK_FALSE(ScoreHostTestAccess::stream_active(host));
    CHECK(ScoreHostTestAccess::strip(host));
    for (const std::string& payload : state->payloads)
    {
        if (payload != state->payloads.front())
            CHECK(payload == kMinimalScore);
    }
}

TEST_CASE("ScoreHost flow band stays inside short and collapsed panes",
    "[scoreview][host][layout][short-pane]")
{
    // Below ~107 px times the display scale the band's 96 px floor used to
    // exceed its 90%-of-pane ceiling, handing std::clamp reversed bounds (an
    // assertion on MSVC debug, a band taller than the pane elsewhere).
    const std::string svg = read_verovio_svg_fixture();
    REQUIRE_FALSE(svg.empty());
    auto state = std::make_shared<FakeEngineState>();
    ScoreHost host;
    std::string error;
    REQUIRE(ScoreHostTestAccess::prime_window(host,
        std::make_unique<DeterministicLayoutEngine>(state, svg, false), kMinimalScore, error));
    INFO(error);
    REQUIRE(ScoreHostTestAccess::strip(host));

    for (const float scale : { 1.0f, 2.0f, 3.0f })
    {
        for (const float zoom : { 0.4f, 1.0f, 4.0f })
        {
            for (const int height : { 0, 1, 12, 40, 100, 106, 107, 150, 213, 320, 600, 1400 })
            {
                CAPTURE(scale, zoom, height);
                draxul::PluginRuntimeViewport viewport;
                viewport.pixel_size = { 640, height };
                viewport.pixel_scale = scale;
                host.set_viewport(viewport);
                ScoreHostTestAccess::set_zoom(host, zoom);

                const auto band = ScoreHostTestAccess::flow_band(host);
                const float vh = static_cast<float>(height);
                CHECK(band.target_h >= 0.0f);
                CHECK(band.target_h <= vh * 0.9f + 1e-3f);
                CHECK(band.strip_y >= 0.0f);
                CHECK(band.strip_y + band.target_h <= vh + 1e-3f);
                // Room permitting, the floor and the zoomed share still apply.
                const float ceiling = vh * 0.9f;
                const float expected
                    = std::clamp(vh * 0.35f * zoom, std::min(96.0f * scale, ceiling), ceiling);
                CHECK(band.target_h == Catch::Approx(expected).margin(1e-3));

                const auto hint = host.print_hint();
                CHECK(hint.content_pos.y >= 0);
                CHECK(hint.content_size.y >= 0);
                CHECK(hint.content_pos.y + hint.content_size.y <= height);
            }
        }
    }

    // A normal pane keeps its familiar geometry.
    draxul::PluginRuntimeViewport normal;
    normal.pixel_size = { 1280, 800 };
    normal.pixel_scale = 1.0f;
    host.set_viewport(normal);
    ScoreHostTestAccess::set_zoom(host, 1.0f);
    CHECK(ScoreHostTestAccess::flow_band(host).target_h == Catch::Approx(280.0f));
}

TEST_CASE("score band fitting orders its bounds for every pane height",
    "[scoreview][layout][short-pane]")
{
    using draxul::scoreview::fit_score_band_height;
    // The Roll presentation's score region (score_height_frac 0.2..0.6 of the
    // pane, 96 px floor) shares this fit with the flow band.
    CHECK(fit_score_band_height(0.4f * 800.0f, 96.0f, 800.0f) == Catch::Approx(320.0f));
    CHECK(fit_score_band_height(0.2f * 300.0f, 96.0f, 300.0f) == Catch::Approx(96.0f));
    CHECK(fit_score_band_height(0.6f * 2000.0f, 96.0f, 2000.0f) == Catch::Approx(1200.0f));
    // Short panes: the 90% ceiling wins over the floor.
    CHECK(fit_score_band_height(0.4f * 100.0f, 96.0f, 100.0f) == Catch::Approx(90.0f));
    CHECK(fit_score_band_height(0.4f * 50.0f, 192.0f, 50.0f) == Catch::Approx(45.0f));
    // Collapsed or invalid panes produce an empty band.
    CHECK(fit_score_band_height(0.0f, 96.0f, 0.0f) == 0.0f);
    CHECK(fit_score_band_height(10.0f, 96.0f, -5.0f) == 0.0f);
    CHECK(fit_score_band_height(10.0f, 96.0f, std::numeric_limits<float>::quiet_NaN()) == 0.0f);
    CHECK(fit_score_band_height(std::numeric_limits<float>::infinity(), 96.0f, 100.0f)
        == Catch::Approx(90.0f));
}

TEST_CASE("ScoreHost height-only viewport changes preserve paged engraving", "[scoreview][host][viewport]")
{
    const std::string svg = read_verovio_svg_fixture();
    REQUIRE_FALSE(svg.empty());
    auto state = std::make_shared<FakeEngineState>();
    ScoreHost host;
    std::string error;
    REQUIRE(ScoreHostTestAccess::prime_paged(host,
        std::make_unique<DeterministicLayoutEngine>(state, svg, false), kMinimalScore, error));
    draxul::PluginRuntimeViewport viewport;
    viewport.pixel_size = { 800, 300 };
    viewport.pixel_scale = 1.0f;
    host.set_viewport(viewport);
    host.pump();
    const auto pages = ScoreHostTestAccess::pages(host);
    REQUIRE(pages);
    const int initial_options = state->options_calls;
    const int initial_svgs = state->svg_calls;
    REQUIRE(initial_options == 1);
    REQUIRE(initial_svgs == 1);
    ScoreHostTestAccess::set_scroll(host, ScoreHostTestAccess::max_scroll(host));
    for (int height = 310; height <= 900; height += 10)
    {
        viewport.pixel_size.y = height;
        host.set_viewport(viewport);
        host.pump();
        CHECK(ScoreHostTestAccess::pages(host).get() == pages.get());
        CHECK(ScoreHostTestAccess::scroll(host) <= ScoreHostTestAccess::max_scroll(host));
    }
    CHECK(state->options_calls == initial_options);
    CHECK(state->svg_calls == initial_svgs);
    ++viewport.pixel_pos.y;
    host.set_viewport(viewport);
    host.pump();
    CHECK(state->options_calls == initial_options);
    ++viewport.pixel_size.x;
    host.set_viewport(viewport);
    host.pump();
    CHECK(state->options_calls == initial_options + 1);
    viewport.pixel_scale = 2.0f;
    host.set_viewport(viewport);
    host.pump();
    CHECK(state->options_calls == initial_options + 2);
    ScoreHostTestAccess::set_zoom(host, 1.2f);
    host.pump();
    CHECK(state->options_calls == initial_options + 3);
}
